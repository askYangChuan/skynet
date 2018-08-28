#define LUA_LIB

#include <stdio.h>
#include <lua.h>
#include <lauxlib.h>

#include <time.h>

#if defined(__APPLE__)
#include <mach/task.h>
#include <mach/mach.h>
#endif

#define NANOSEC 1000000000
#define MICROSEC 1000000

// #define DEBUG_LOG

static double
get_time() {
#if  !defined(__APPLE__)
	struct timespec ti;
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ti);

	int sec = ti.tv_sec & 0xffff;
	int nsec = ti.tv_nsec;

	return (double)sec + (double)nsec / NANOSEC;	
#else
	struct task_thread_times_info aTaskInfo;
	mach_msg_type_number_t aTaskInfoCount = TASK_THREAD_TIMES_INFO_COUNT;
	if (KERN_SUCCESS != task_info(mach_task_self(), TASK_THREAD_TIMES_INFO, (task_info_t )&aTaskInfo, &aTaskInfoCount)) {
		return 0;
	}

	int sec = aTaskInfo.user_time.seconds & 0xffff;
	int msec = aTaskInfo.user_time.microseconds;

	return (double)sec + (double)msec / MICROSEC;
#endif
}

static inline double 
diff_time(double start) {
	double now = get_time();
	if (now < start) {
		return now + 0x10000 - start;
	} else {
		return now - start;
	}
}

static int
lstart(lua_State *L) {
	if (lua_gettop(L) != 0) {
		lua_settop(L,1);
		luaL_checktype(L, 1, LUA_TTHREAD);
	} else {
		lua_pushthread(L);
	}
	lua_pushvalue(L, 1);	// push coroutine
	lua_rawget(L, lua_upvalueindex(2));
	if (!lua_isnil(L, -1)) {
		return luaL_error(L, "Thread %p start profile more than once", lua_topointer(L, 1));
	}
	lua_pushvalue(L, 1);	// push coroutine
	lua_pushnumber(L, 0);
	lua_rawset(L, lua_upvalueindex(2));

	lua_pushvalue(L, 1);	// push coroutine
	double ti = get_time();
#ifdef DEBUG_LOG
	fprintf(stderr, "PROFILE [%p] start\n", L);
#endif
	lua_pushnumber(L, ti);
	lua_rawset(L, lua_upvalueindex(1));

	return 0;
}

static int
lstop(lua_State *L) {
	if (lua_gettop(L) != 0) {
		lua_settop(L,1);
		luaL_checktype(L, 1, LUA_TTHREAD);
	} else {
		lua_pushthread(L);
	}
	lua_pushvalue(L, 1);	// push coroutine
	lua_rawget(L, lua_upvalueindex(1));
	if (lua_type(L, -1) != LUA_TNUMBER) {
		return luaL_error(L, "Call profile.start() before profile.stop()");
	} 
	double ti = diff_time(lua_tonumber(L, -1));
	lua_pushvalue(L, 1);	// push coroutine
	lua_rawget(L, lua_upvalueindex(2));
	double total_time = lua_tonumber(L, -1);

	lua_pushvalue(L, 1);	// push coroutine
	lua_pushnil(L);
	lua_rawset(L, lua_upvalueindex(1));

	lua_pushvalue(L, 1);	// push coroutine
	lua_pushnil(L);
	lua_rawset(L, lua_upvalueindex(2));

	total_time += ti;
	lua_pushnumber(L, total_time);
#ifdef DEBUG_LOG
	fprintf(stderr, "PROFILE [%p] stop (%lf/%lf)\n", lua_tothread(L,1), ti, total_time);
#endif

	return 1;
}

static int
timing_resume(lua_State *L) {
	lua_pushvalue(L, -1);
	lua_rawget(L, lua_upvalueindex(2));		/* 将第二个上值total time表的 total[co]入栈  */
	if (lua_isnil(L, -1)) {		// check total time
		lua_pop(L,2);	// pop from coroutine
	} else {
		lua_pop(L,1);
		double ti = get_time();
#ifdef DEBUG_LOG
		fprintf(stderr, "PROFILE [%p] resume %lf\n", lua_tothread(L, -1), ti);
#endif
		lua_pushnumber(L, ti);
		lua_rawset(L, lua_upvalueindex(1));	// set start time	/* start time[co] = ti */
	}

	lua_CFunction co_resume = lua_tocfunction(L, lua_upvalueindex(3));
	
	return co_resume(L);	/* 3rd/lua 下面的resume函数，具体实现就不关心了 */
}

static int
lresume(lua_State *L) {
	lua_pushvalue(L,1);	/* traceback是虚拟机L的栈第一个索引，这里是lua调用c，所以他的栈只能访问到传给他的参数，所以索引1是co，co被压入栈顶 */
	
	return timing_resume(L);
}

static int
lresume_co(lua_State *L) {
	luaL_checktype(L, 2, LUA_TTHREAD);
	lua_rotate(L, 2, -1);	// 'from' coroutine rotate to the top(index -1)

	return timing_resume(L);
}

static int
timing_yield(lua_State *L) {
#ifdef DEBUG_LOG
	lua_State *from = lua_tothread(L, -1);		//取出一个线程
#endif
	lua_pushvalue(L, -1);	//将栈顶的线程拷贝一份压栈
	lua_rawget(L, lua_upvalueindex(2));	// check total time
	if (lua_isnil(L, -1)) {
		lua_pop(L,2);
	} else {
		double ti = lua_tonumber(L, -1);
		lua_pop(L,1);

		lua_pushvalue(L, -1);	// push coroutine
		lua_rawget(L, lua_upvalueindex(1));
		double starttime = lua_tonumber(L, -1);
		lua_pop(L,1);

		double diff = diff_time(starttime);
		ti += diff;
#ifdef DEBUG_LOG
		fprintf(stderr, "PROFILE [%p] yield (%lf/%lf)\n", from, diff, ti);
#endif

		lua_pushvalue(L, -1);	// push coroutine
		lua_pushnumber(L, ti);
		lua_rawset(L, lua_upvalueindex(2));
		lua_pop(L, 1);	// pop coroutine
	}

	lua_CFunction co_yield = lua_tocfunction(L, lua_upvalueindex(3));

	return co_yield(L);
}

static int
lyield(lua_State *L) {
	lua_pushthread(L);

	return timing_yield(L);
}

static int
lyield_co(lua_State *L) {
	luaL_checktype(L, 1, LUA_TTHREAD);
	lua_rotate(L, 1, -1);
	
	return timing_yield(L);
}

LUAMOD_API int
luaopen_skynet_profile(lua_State *L) {
	luaL_checkversion(L);
	luaL_Reg l[] = {
		{ "start", lstart },
		{ "stop", lstop },
		{ "resume", lresume },	/* 这个函数的第三个上值不是nil，被赋值为coroutine.resume */
		{ "yield", lyield },	/* 这个函数的第三个上值不是nil，被赋值为coroutine.yield */
		{ "resume_co", lresume_co },	/* 这个函数的第三个上值不是nil，被赋值为coroutine.resume */
		{ "yield_co", lyield_co },		/* 这个函数的第三个上值不是nil，被赋值为coroutine.yield */
		{ NULL, NULL },
	};
	luaL_newlibtable(L,l);		/* 开辟l大小的数据栈空间 */
	lua_newtable(L);	// table thread->start time		/* 创建2个空表 */
	lua_newtable(L);	// table thread->total time

	lua_newtable(L);	// weak table	/* 第三个空表 */
	lua_pushliteral(L, "kv");		/* kv 入栈 */
	lua_setfield(L, -2, "__mode");	/* 设置__mode = {"kv"}，第三个表就是弱引用表.kv出栈 */

	lua_pushvalue(L, -1);		/* 复制week table并入栈 */
	lua_setmetatable(L, -3); 	/* 设置栈顶的2个week table为前面2个空表的原表，并出栈 */
	lua_setmetatable(L, -3);

	lua_pushnil(L);	// cfunction (coroutine.resume or coroutine.yield)
	luaL_setfuncs(L,l,3);	/* 上面设置了一个nil值，这里就将前面2个空表+nil值设置为所有函数的上值了,并且3个值都弹出栈 */

	int libtable = lua_gettop(L);	/* 获取libtable的索引 */

	lua_getglobal(L, "coroutine");	/* 全局变量table coroutine入栈 */
	lua_getfield(L, -1, "resume");	/* 将coroutine.resume 入栈 */

	lua_CFunction co_resume = lua_tocfunction(L, -1);
	if (co_resume == NULL)
		return luaL_error(L, "Can't get coroutine.resume");
	lua_pop(L,1);	/* coroutine.resume 出栈 */

	lua_getfield(L, libtable, "resume");	/* 将上面luaL_Reg l 的resume对应的lresume入栈 */
	lua_pushcfunction(L, co_resume);
	lua_setupvalue(L, -2, 3);			/* 给resume(lresume)的第三个上值设置为coroutine.resume */
	lua_pop(L,1);

	lua_getfield(L, libtable, "resume_co");
	lua_pushcfunction(L, co_resume);
	lua_setupvalue(L, -2, 3);			/* 给resume_co(lresume_co)的第三个上值设置为coroutine.resume */
	lua_pop(L,1);		

	lua_getfield(L, -1, "yield");

	lua_CFunction co_yield = lua_tocfunction(L, -1);
	if (co_yield == NULL)
		return luaL_error(L, "Can't get coroutine.yield");
	lua_pop(L,1);

	lua_getfield(L, libtable, "yield");
	lua_pushcfunction(L, co_yield);
	lua_setupvalue(L, -2, 3);		/* 给yield(lyield)的第三个上值设置为coroutine.yield */
	lua_pop(L,1);

	lua_getfield(L, libtable, "yield_co");
	lua_pushcfunction(L, co_yield);
	lua_setupvalue(L, -2, 3);		/* 给yield_co(lyield_co)的第三个上值设置为coroutine.yield */
	lua_pop(L,1);

	lua_settop(L, libtable);		/* 清除libtable上面的栈空间，将libtable设置为栈顶 */

	return 1;
}

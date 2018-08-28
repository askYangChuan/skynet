#ifndef SKYNET_IMP_H
#define SKYNET_IMP_H

struct skynet_config {
	int thread;
	int harbor;
	int profile;				/* default 1，用来统计每个服务使用了多少 cpu 时间 */
	const char * daemon;
	const char * module_path;	/* c库路径, cpath ./cservice/?.so */
	const char * bootstrap;		/* snlua bootstrap */
	const char * logger;
	const char * logservice;	/* "logservice", "logger" */
};

#define THREAD_WORKER 0
#define THREAD_MAIN 1
#define THREAD_SOCKET 2
#define THREAD_TIMER 3
#define THREAD_MONITOR 4

void skynet_start(struct skynet_config * config);

#endif

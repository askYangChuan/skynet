local skynet = require "skynet"
local core = require "skynet.core"
require "skynet.manager"	-- import manager apis
local string = string

local services = {}         --创建的handle为key，一个名字为value， 例如 "snlua cdummy"
local command = {}
local instance = {} -- for confirm (function command.LAUNCH / command.ERROR / command.LAUNCHOK) handle为key, 应答函数response为value

local function handle_to_address(handle)
	return tonumber("0x" .. string.sub(handle , 2))
end

local NORET = {}

function command.LIST()
	local list = {}
	for k,v in pairs(services) do
		list[skynet.address(k)] = v
	end
	return list
end

function command.STAT()
	local list = {}
	for k,v in pairs(services) do
		local ok, stat = pcall(skynet.call,k,"debug","STAT")
		if not ok then
			stat = string.format("ERROR (%s)",v)
		end
		list[skynet.address(k)] = stat
	end
	return list
end

function command.KILL(_, handle)
	handle = handle_to_address(handle)
	skynet.kill(handle)
	local ret = { [skynet.address(handle)] = tostring(services[handle]) }
	services[handle] = nil
	return ret
end

function command.MEM()
	local list = {}
	for k,v in pairs(services) do
		local ok, kb, bytes = pcall(skynet.call,k,"debug","MEM")
		if not ok then
			list[skynet.address(k)] = string.format("ERROR (%s)",v)
		else
			list[skynet.address(k)] = string.format("%.2f Kb (%s)",kb,v)
		end
	end
	return list
end

function command.GC()
	for k,v in pairs(services) do
		skynet.send(k,"debug","GC")
	end
	return command.MEM()
end

function command.REMOVE(_, handle, kill)
	services[handle] = nil
	local response = instance[handle]
	if response then
		-- instance is dead
		response(not kill)	-- return nil to caller of newservice, when kill == false
		instance[handle] = nil
	end

	-- don't return (skynet.ret) because the handle may exit
	return NORET
end

local function launch_service(service, ...)
	local param = table.concat({...}, " ")
	local inst = skynet.launch(service, param)
	local response = skynet.response()      --这个地方会调用coroutine_yield，让出cpu，实际上也只是去获取一个response函数，里面包装了应答的handle和session
	if inst then
		services[inst] = service .. " " .. param
		instance[inst] = response
	else
		response(false)
		return
	end
	return inst
end

function command.LAUNCH(_, service, ...)            --一般传递到这儿的参数是(source, "snlua", name) service 就是snlua
	launch_service(service, ...)
	return NORET
end

function command.LOGLAUNCH(_, service, ...)
	local inst = launch_service(service, ...)
	if inst then
		core.command("LOGON", skynet.address(inst))
	end
	return NORET
end

function command.ERROR(address)
	-- see serivce-src/service_lua.c
	-- init failed
	local response = instance[address]
	if response then
		response(false)
		instance[address] = nil
	end
	services[address] = nil
	return NORET
end

function command.LAUNCHOK(address)
	-- init notice
	local response = instance[address]
	if response then
		response(true, address)
		instance[address] = nil
	end

	return NORET
end

-- for historical reasons, launcher support text command (for C service)

skynet.register_protocol {              --lua的函数在调用时候参数可以不用加()的，所以这里就是给skynet.register_protocol 传递了一个class(本质还是table)
	name = "text",
	id = skynet.PTYPE_TEXT,
	unpack = skynet.tostring,
	dispatch = function(session, address , cmd)
		if cmd == "" then
			command.LAUNCHOK(address)
		elseif cmd == "ERROR" then
			command.ERROR(address)
		else
			error ("Invalid text command " .. cmd)
		end
	end,
}

--session是请求方的request id，address是请求方的handle
skynet.dispatch("lua", function(session, address, cmd , ...)            --lua 这个在skynet.lua里面已经注册了，这里是给他设置一个分配函数
	cmd = string.upper(cmd)
	local f = command[cmd]
	if f then
		local ret = f(address, ...)
		if ret ~= NORET then
			skynet.ret(skynet.pack(ret))
		end
	else
		skynet.ret(skynet.pack {"Unknown command"} )
	end
end)

skynet.start(function() end)

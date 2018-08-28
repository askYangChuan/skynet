local skynet = require "skynet"
local harbor = require "skynet.harbor"
require "skynet.manager"	-- import skynet.launch, ...
local memory = require "skynet.memory"


--[[
每一个skynet.start都是启动一个协程来处理数据
根据standalone来确定是否为master节点。
根据harbor_id是否为0确定是否为单节点模式
每个节点包括master节点都要启动cslave来保持与master通讯
多节点模式启动:
1,C服务 snlua, 取名为.launcher
2，cmaster 服务(standalone存在)
3，clave服务(节点间的消息转发，以及同步全局名字),取名为.cslave
4, datacenterd 服务(standalone存在)，取名为DATACENTER
5，service_mgr服务(启动用于 UniqueService 管理的)

单节点模式启动
1,C服务 snlua, 取名为.launcher
2, cdummy服务，它负责拦截对外广播的全局名字变更，取名为.cslave
3，service_mgr服务(启动用于 UniqueService 管理的)
]]

skynet.start(function()
	local sharestring = tonumber(skynet.getenv "sharestring" or 4096)
	memory.ssexpand(sharestring)

	local standalone = skynet.getenv "standalone"

	local launcher = assert(skynet.launch("snlua","launcher"))     --launch参数返回的是handle,launch就是利用snlua.so再创建一个ctx，并命名为launcher,这里在执行时，可能launcher都还没有被其他线程给启动
	skynet.name(".launcher", launcher)  --注册本地服务名字 叫launcher
    
	local harbor_id = tonumber(skynet.getenv "harbor" or 0)
	if harbor_id == 0 then
		assert(standalone ==  nil)
		standalone = true
		skynet.setenv("standalone", "true")

		local ok, slave = pcall(skynet.newservice, "cdummy")    --这里调用默认的proto['lua'] 和skynet.pack打包参数，并给launcher发送一个指令
		if not ok then
			skynet.abort()
		end
		skynet.name(".cslave", slave)

	else 
		if standalone then
			if not pcall(skynet.newservice,"cmaster") then
				skynet.abort()
			end
		end
        
		local ok, slave = pcall(skynet.newservice, "cslave")
		if not ok then
			skynet.abort()
		end
		skynet.name(".cslave", slave)
	end

	if standalone then
		local datacenter = skynet.newservice "datacenterd"
		skynet.name("DATACENTER", datacenter)
	end
	skynet.newservice "service_mgr"                             -- 用于UniqueService 管理
	pcall(skynet.newservice,skynet.getenv "start" or "main")
	skynet.exit()
end)

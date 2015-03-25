-- server
-- nc -l 8888

-- client
-- lua -i test_data.lua
local socketevent = require("socketevent")

sock = socketevent.tcp()

sock:on("connect", function(event)
	print("connect")
end)

sock:on("data", function(event)
	print("data: " .. event.data)

	print("send: hello server")
	sock:send("hello server\n")

	if event.data == "close\n" then
		sock:close()
	end
end)

sock:on("close", function(event)
	print("close!")
end)

sock:on("error", function(event)
	print(string.format("error: %s. message: %s.", event.error, event.message))
end)

local option = {
	keepalive = 1,
	keepidle = 60,
	keepintvl = 10,
	keepcnt = 3,
}

sock:setOption(option)

if sock:connect("192.168.1.99", 8888) ~= 1 then
	os.exit(1)
end

print("send: hello server")
sock:send("hello server\n")
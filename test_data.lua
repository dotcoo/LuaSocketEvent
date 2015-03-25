-- server
-- nc -l 8888

-- client
-- lua -i test_data.lua
local socketevent = require("socketevent")

sock = socketevent.tcp()

sock:on("connect", function(event)
	print("connect: ok!")

	print("send: hello server")
	sock:send("hello server\n")
end)

sock:on("data", function(event)
	print("data: " .. event.data)

	if event.data == "close\n" then
		sock:close()
	end

	print("send: hello server")
	sock:send("hello server\n")
end)

sock:on("close", function(event)
	print("close: bye!")
end)

sock:on("error", function(event)
	print(string.format("c line: %s. error: %s. message: %s.", event.line, event.error, event.message))
end)

local option = {
	keepalive = 1,
	keepidle = 60,
	keepintvl = 10,
	keepcnt = 3,
}
sock:setopt(option)

sock:connect("127.0.0.1", 8888)

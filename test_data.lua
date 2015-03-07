-- server
-- nc -l 8888

local socketevent = require("socketevent")

local sock = socketevent.tcp()

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
	print("error: " .. event.error .. ", " .. event.message)
end)

sock:connect("127.0.0.1", 8888)

print("send: hello server")
sock:send("hello server\n")

sock:wait()

print("lua ok")
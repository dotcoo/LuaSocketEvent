-- server
-- printf "\x0c\x00\x00\x00hello client\x0c\x00\x00\x00hello client" | nc -l 8888 | xxd

-- client
-- lua -i test_message.lua
local socketevent = require("socketevent")

sock = socketevent.tcp()

sock:on("connect", function(event)
	print("connect")
end)

sock:on("message", function(event)
	print("message: " .. event.data)
end)

sock:on("close", function(event)
	print("close")
end)

sock:on("error", function(event)
	print("error: " .. event.error .. ", " .. event.message)
end)

sock:connect("127.0.0.1", 8888)

print("send: hello server")
sock:sendmessage("hello server")
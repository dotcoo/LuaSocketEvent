-- server
-- printf "\x0c\x00\x00\x00hello client\x0c\x00\x00\x00hello client" | nc -l 8888 | xxd

-- client
-- lua -i test_message.lua
local socketevent = require("socketevent")

sock = socketevent.tcp()

sock:on("connect", function(event)
	print("connect: ok!")

	print("send: hello server")
	sock:sendmessage("hello server\n")
end)

sock:on("message", function(event)
	print("message: " .. event.data)
end)

sock:on("close", function(event)
	print("close: bye!")
end)

sock:on("error", function(event)
	print(string.format("c line: %s. error: %s. message: %s.", event.line, event.error, event.message))
end)

sock:connect("127.0.0.1", 8888)

# Lua Socket Event

`LuaSocketEvent` the Lua Socket Event, Asynchronous Socket. support platform for Linux, MacOSX, Windows, IOS, Android, Cocos2d-x.

`LuaSocketEvent` supports data and message in two formats, data are the original data, the message is 4 bytes unsigned int little endian and message content

## Instructions

### Server

	nc -l 8888

### Client

	local socketevent = require("socketevent")

	sock = socketevent.tcp()

	sock:on("connect", function(event)
		print("connect")
	end)

	sock:on("data", function(event)
		print("data: " .. event.data)
	end)

	sock:on("close", function(event)
		print("close!")
	end)

	sock:on("error", function(event)
		print("error: " .. event.error .. ", " .. event.message)
	end)

	sock:connect("127.0.0.1", 8888)

	sock:send("hello server\n")

### Run Data Client

	lua -i test_data.lua

### Client Send Data

	> sock:send("hello server\n")

### Message Server

	printf "\x0c\x00\x00\x00hello client\x0c\x00\x00\x00hello client" | nc -l 8888 | xxd

### Message Client

	......

	sock:on("message", function(event)
		print("data: " .. event.data)
	end)

	......

	sock:sendmessage("hello server")

## Build LuaSocketEvent Library

### *nix binary

	gcc -o socketevent socketevent.c -I/usr/local/include -L/usr/local/lib -llua -lm -ldl -lpthread

### linux

	gcc -fPIC --shared -o socketevent.so socketevent.c -lpthread

### macosx

	gcc -o socketevent.o -c socketevent.c
	gcc -bundle -undefined dynamic_lookup -o socketevent.so socketevent.o

### windows

1. create `lua-5.1.4\build.bat` file

		cd src
		cl /O2 /W3 /c /DLUA_BUILD_AS_DLL l*.c
		del lua.obj luac.obj
		link /DLL /out:lua53.dll l*.obj
		cl /O2 /W3 /c /DLUA_BUILD_AS_DLL lua.c luac.c
		link /out:lua.exe lua.obj lua53.lib
		del lua.obj
		link /out:luac.exe l*.obj
		cd ..

2. open VS2013 developers tools, run `build.bat`

3. copy `socketevent.c` to `lua-5.1.4\src`

4. create socketevent.dll

		cl /O2 /W3 /c /DLUA_BUILD_AS_DLL socketevent.c
		link /DLL /out:socketevent.dll socketevent.obj lua53.lib

### android

### ios

### Cococs2d-x

#### windows cocos-simulator-bin

1. build source to `socketevent.dll`

2. copy `socketevent.dll` to `C:\Program Files (x86)\Cocos\cocos-simulator-bin\win32`

3. main.lua join

		require("socketevent")

4. using socketevent

#### mac simulator

1. Xcode open `frameworks/runtime-src/proj.ios_mac/LuaTest.xcodeproj`

2. Classes dir create `socketevent.h` `socketevent.c` file

3. lua_module_register.h join

		extern "C" {
			#include "socketevent.h"
		}

4. lua_module_register function join

		luaopen_socketevent(L);

5. Xcode build for Mac 
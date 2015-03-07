//
//  socketevent.c
//  LuaSocketEvent
//
//  Created by dotcoo on 15/3/1.
//  Copyright (c) 2015 dotcoo. All rights reserved.
//

#define lsocketeventlib_c
#define LUA_LIB

//#include "lprefix.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef __APPLE__
#include <sys/malloc.h>
#else
#include <malloc.h>
#endif
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <process.h>
#else
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <pthread.h>
#endif
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#if defined(LUA_BUILD_AS_DLL)	/* { */
#if defined(LUA_CORE) || defined(LUA_LIB)	/* { */
#define LUA_API __declspec(dllexport)
#else						/* }{ */
#define LUA_API __declspec(dllimport)
#endif						/* } */
#else				/* }{ */
#define LUA_API		extern
#endif				/* } */
#define LUALIB_API	LUA_API
#define LUAMOD_API	LUALIB_API

#define LUA_SOCKETEVENT_TCP_HANDLE					"SOCKETEVENT_TCP*"
#define LUA_SOCKETEVENT_TCP_BUFFER_SIZE				0x4000
#define LUA_SOCKETEVENT_TCP_MESSAGE_HEAD_SIZE		4
#define LUA_SOCKETEVENT_TCP_MESSAGE_MAX_SIZE		0x100000
#define LUA_SOCKETEVENT_TCP_STATE_CONNECT			0x1
#define LUA_SOCKETEVENT_TCP_STATE_THREAD			0x2
#define LUA_SOCKETEVENT_TCP_STATE_CLOSE				0x4

#ifdef _WIN32
#define pthread_t int
#endif

typedef struct lua_SocketEventTCP {
	// lua State
	lua_State *L;

	// state
	int state;

	// pthread
	pthread_t thread;

	// socket
	int socket;
	const char *ip;
	lua_Integer port;

	// data buffer
	int data_buffer_size;
	int data_buffer_use;
	char *data_buffer;

	// message buffer
	int message_buffer_size;
	int message_buffer_use;
	char *message_buffer;
	int message_len;

	// event
	int event_connect;
	int event_data;
	int event_close;
	int event_error;
	int event_message;

} LSocketEventTCP;

static int socketevent_tcp(lua_State *L);

void socketevent_tcp_trigger_connect(LSocketEventTCP *sock, lua_State *L);

void socketevent_tcp_trigger_data(LSocketEventTCP *sock, lua_State *L);

void socketevent_tcp_trigger_message(LSocketEventTCP *sock, lua_State *L);

void socketevent_tcp_trigger_close(LSocketEventTCP *sock, lua_State *L);

void socketevent_tcp_trigger_error(LSocketEventTCP *sock, lua_State *L, int err, const char *message);

void *socketevent_tcp_data(void *psock);

static int socketevent_tcp_connect(lua_State *L);

static int socketevent_tcp_on(lua_State *L);

static int socketevent_tcp_send(lua_State *L);

static int socketevent_tcp_send_message(lua_State *L);

static int socketevent_tcp_close(lua_State *L);

static int socketevent_tcp_wait(lua_State *L);

static int socketevent_tcp_gc(lua_State *L);

static int socketevent_tcp_tostring(lua_State *L);

LUAMOD_API int luaopen_socketevent(lua_State *L);

#ifdef _WIN32

void socketevent_tcp_data_win(void *psock);

#pragma comment(lib,"ws2_32.lib")

#endif

// ==========

static int socketevent_tcp(lua_State *L) {
	// create tcp sock handle
	LSocketEventTCP *sock = (LSocketEventTCP *)lua_newuserdata(L, sizeof(LSocketEventTCP));
#if LUA_VERSION_NUM == 501
	luaL_getmetatable(L, LUA_SOCKETEVENT_TCP_HANDLE);
	lua_setmetatable(L, -2);
#else
	luaL_setmetatable(L, LUA_SOCKETEVENT_TCP_HANDLE);
#endif

	// lua State
	sock->L = L;

	// state
	sock->state = 0x0;

	// pthread
	// sock->thread = -1;

	// socket
	sock->socket = -1;
	sock->ip = NULL;
	sock->port = -1;

	// data buffer
	sock->data_buffer_size = LUA_SOCKETEVENT_TCP_BUFFER_SIZE;
	sock->data_buffer_use = 0;
	sock->data_buffer = (char *)malloc(sock->data_buffer_size);
	memset(sock->data_buffer, 0, sock->data_buffer_size);

	// message buffer
	sock->message_buffer_size = LUA_SOCKETEVENT_TCP_BUFFER_SIZE;
	sock->message_buffer_use = 0;
	sock->message_buffer = (char *)malloc(sock->message_buffer_size);
	memset(sock->message_buffer, 0, sock->message_buffer_size);
	sock->message_len = 0;

	// event
	sock->event_connect = -1;
	sock->event_data = -1;
	sock->event_close = -1;
	sock->event_error = -1;
	sock->event_message = -1;

	return 1;
}

void socketevent_tcp_trigger_connect(LSocketEventTCP *sock, lua_State *L) {
	if (sock->event_connect < 0) {
		return;
	}

	// trigger lua connect handle
	lua_rawgeti(L, LUA_REGISTRYINDEX, sock->event_connect);
	lua_newtable(L);

	int result = lua_pcall(L, 1, 0, 0);
	if (0 != result) {
		luaL_error(L, "connect event call error: %d", result);
	}
}

void socketevent_tcp_trigger_data(LSocketEventTCP *sock, lua_State *L) {
	if (sock->event_data < 0) {
		return;
	}

	// trigger lua data handle
	lua_rawgeti(L, LUA_REGISTRYINDEX, sock->event_data);
	lua_newtable(L);
	lua_pushliteral(L, "data");
	lua_pushlstring(L, sock->data_buffer, sock->data_buffer_use);
	lua_settable(L, -3);

	int result = lua_pcall(L, 1, 0, 0);
	if (0 != result) {
		luaL_error(L, "data event call error: %d", result);
	}
}

void socketevent_tcp_trigger_message(LSocketEventTCP *sock, lua_State *L) {
	if (sock->event_message < 0) {
		return;
	}

	// trigger lua message handle
	lua_rawgeti(L, LUA_REGISTRYINDEX, sock->event_message);
	lua_newtable(L);
	lua_pushliteral(L, "data");
	lua_pushlstring(L, sock->message_buffer + LUA_SOCKETEVENT_TCP_MESSAGE_HEAD_SIZE, sock->message_len);
	lua_settable(L, -3);

	int result = lua_pcall(L, 1, 0, 0);
	if (0 != result) {
		luaL_error(L, "message event call error: %d", result);
	}
}

void socketevent_tcp_trigger_close(LSocketEventTCP *sock, lua_State *L) {
	if (sock->event_close < 0) {
		return;
	}

	// check close state
	if ((sock->state & LUA_SOCKETEVENT_TCP_STATE_CLOSE) == LUA_SOCKETEVENT_TCP_STATE_CLOSE) {
		return;
	}
	sock->state |= LUA_SOCKETEVENT_TCP_STATE_CLOSE;

	// trigger lua close handle
	lua_rawgeti(L, LUA_REGISTRYINDEX, sock->event_close);
	lua_newtable(L);

	int result = lua_pcall(L, 1, 0, 0);
	if (0 != result) {
		luaL_error(L, "close event call error: %d", result);
	}
}

void socketevent_tcp_trigger_error(LSocketEventTCP *sock, lua_State *L, int err, const char *message) {
	if (sock->event_error < 0) {
		return;
	}

	// trigger lua error handle
	lua_rawgeti(L, LUA_REGISTRYINDEX, sock->event_error);
	lua_newtable(L);
	lua_pushliteral(L, "error");
	lua_pushinteger(L, err);
	lua_settable(L, -3);
	lua_pushliteral(L, "message");
	lua_pushstring(L, message);
	lua_settable(L, -3);

	int result = lua_pcall(L, 1, 0, 0);
	if (0 != result) {
		luaL_error(L, "error event call error: %d", result);
	}
}

void *socketevent_tcp_data(void *psock) {
	// tcp sock struct
	LSocketEventTCP *sock = (LSocketEventTCP *)psock;

	// create new thread State
	sock->L = lua_newthread(sock->L);

	// recv data
	while (1) {
		sock->data_buffer_use = recv(sock->socket, sock->data_buffer, sock->data_buffer_size, 0);

		// check error
		if (sock->data_buffer_use == 0) {
			printf("001: %d\n", errno);
			break;
		}
		if (sock->data_buffer_use < 0) {
			printf("002: %d\n", errno);
			socketevent_tcp_trigger_error(sock, sock->L, sock->data_buffer_use, "recv data error!");
			break;
		}

		// print data
		/*
		printf("c recv len: %d\n", sock->data_buffer_use);
		int i = 0;
		for (i = 0; i < sock->data_buffer_use; i++) {
		printf("%02x ", *(sock->data_buffer + i));
		if ((i+1) % 10 == 0) {
		printf("\n");
		}
		}
		printf("\n");
		*/

		// trigger data event
		socketevent_tcp_trigger_data(sock, sock->L);

		// check message handle
		if (sock->event_message >= 0) {
			// check message buffer size
			if (sock->message_buffer_size - sock->message_buffer_use < sock->data_buffer_use) {
				while (sock->message_buffer_size - sock->message_buffer_use < sock->data_buffer_use) {
					sock->message_buffer_size <<= 1;
				}
				if (sock->message_buffer_size > LUA_SOCKETEVENT_TCP_MESSAGE_HEAD_SIZE + LUA_SOCKETEVENT_TCP_MESSAGE_MAX_SIZE) {
					sock->message_buffer_size = LUA_SOCKETEVENT_TCP_MESSAGE_HEAD_SIZE + LUA_SOCKETEVENT_TCP_MESSAGE_MAX_SIZE;
				}
				sock->message_buffer = (char *)realloc((void *)sock->message_buffer, sock->message_buffer_size);
			}

			// append message
			memcpy(sock->message_buffer + sock->message_buffer_use, sock->data_buffer, sock->data_buffer_use);
			sock->message_buffer_use += sock->data_buffer_use;

			// packet splicing
			int message_raw_len = 0;
			int break_while = 0;
			while (1) {
				// check message head
				if (sock->message_buffer_use < LUA_SOCKETEVENT_TCP_MESSAGE_HEAD_SIZE) {
					break;
				}

				// message len
				sock->message_len = *((unsigned int *)sock->message_buffer);

				// check message len
				if (sock->message_len > LUA_SOCKETEVENT_TCP_MESSAGE_MAX_SIZE) {
					break_while = 1;
					socketevent_tcp_trigger_error(sock, sock->L, 1, "message too long!");
					break;
				}

				// message raw len
				message_raw_len = LUA_SOCKETEVENT_TCP_MESSAGE_HEAD_SIZE + sock->message_len;

				// check message len
				if (sock->message_buffer_use < message_raw_len) {
					break;
				}

				// trigger message event
				socketevent_tcp_trigger_message(sock, sock->L);

				// move data
				if (sock->message_buffer_use - message_raw_len > 0) {
					memmove(sock->message_buffer, sock->message_buffer + message_raw_len, sock->message_buffer_use - message_raw_len);
				}
				sock->message_buffer_use -= message_raw_len;
			}
			if (break_while) {
				break;
			}
		}
	}

	// trigger close handle
	socketevent_tcp_trigger_close(sock, sock->L);

	return NULL;
}

void socketevent_tcp_data_win(void *psock) {
	socketevent_tcp_data(psock);
}

#ifdef _WIN32
static int socketevent_tcp_connect(lua_State *L) {
	// sock struct
	LSocketEventTCP *sock = (LSocketEventTCP *)luaL_checkudata(L, 1, LUA_SOCKETEVENT_TCP_HANDLE);

	// check connect state
	if ((sock->state & LUA_SOCKETEVENT_TCP_STATE_CONNECT) == LUA_SOCKETEVENT_TCP_STATE_CONNECT) {
		return 1;
	}

	// get params
	const char *ip = luaL_checkstring(L, 2);
	lua_Integer port = luaL_checkinteger(L, 3);

	sock->ip = ip;
	sock->port = port;

	WSADATA wsa;
	//初始化套接字DLL 
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		socketevent_tcp_trigger_error(sock, sock->L, 1, "c WSAStartup function error!");
		return 0;
	}

	// create socket
	if ((sock->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
		socketevent_tcp_trigger_error(sock, sock->L, 1, "c socket function error!");
		return 0;
	}

	// server address
	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(struct sockaddr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = inet_addr(sock->ip);
	server_addr.sin_port = htons((u_short)sock->port);

	// connect to server
	if (connect(sock->socket, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1) {
		printf("003: %d\n", errno);
		sock->state |= LUA_SOCKETEVENT_TCP_STATE_CLOSE;
		socketevent_tcp_trigger_error(sock, sock->L, 1, "remote host does not exist!");
		return 0;
	}

	// set connect state
	sock->state |= LUA_SOCKETEVENT_TCP_STATE_CONNECT;

	// trigger connect handle
	socketevent_tcp_trigger_connect(sock, sock->L);

	// start thread
	_beginthread(socketevent_tcp_data_win, 0, sock);

	// set thread state
	sock->state |= LUA_SOCKETEVENT_TCP_STATE_THREAD;

	// set close state
	sock->state ^= LUA_SOCKETEVENT_TCP_STATE_CLOSE;

	return 1;
}
#else
static int socketevent_tcp_connect(lua_State *L) {
	// sock struct
	LSocketEventTCP *sock = luaL_checkudata(L, 1, LUA_SOCKETEVENT_TCP_HANDLE);

	// check connect state
	if ((sock->state & LUA_SOCKETEVENT_TCP_STATE_CONNECT) == LUA_SOCKETEVENT_TCP_STATE_CONNECT) {
		return 1;
	}

	// get params
	const char *ip = luaL_checkstring(L, 2);
	lua_Integer port = luaL_checkinteger(L, 3);

	sock->ip = ip;
	sock->port = port;

	// create socket
	if ((sock->socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		socketevent_tcp_trigger_error(sock, sock->L, 1, "c socket function error!");
		return 0;
	}

#if defined(__linux__) || defined(__ANDROID__)
	// tcp option set
	int keepalive = 1;
	int keepidle = 60;
	int keepintvl = 10;
	int keepcnt = 3;
	if (setsockopt(sock->socket, SOL_SOCKET, SO_KEEPALIVE, (void *)&keepalive, sizeof(keepalive)) < 0) {
		socketevent_tcp_trigger_error(sock, sock->L, 1, "c setsockopt function SO_KEEPALIVE error!");
		return 0;
	}
	if (setsockopt(sock->socket, SOL_TCP, TCP_KEEPIDLE, (void *)&keepidle, sizeof(keepidle)) < 0) {
		socketevent_tcp_trigger_error(sock, sock->L, 1, "c setsockopt function TCP_KEEPIDLE error!");
		return 0;
	}
	if (setsockopt(sock->socket, SOL_TCP, TCP_KEEPINTVL, (void *)&keepintvl, sizeof(keepintvl)) < 0) {
		socketevent_tcp_trigger_error(sock, sock->L, 1, "c setsockopt function TCP_KEEPINTVL error!");
		return 0;
	}
	if (setsockopt(sock->socket, SOL_TCP, TCP_KEEPCNT, (void *)&keepcnt, sizeof(keepcnt)) < 0) {
		socketevent_tcp_trigger_error(sock, sock->L, 1, "c setsockopt function TCP_KEEPCNT error!");
		return 0;
	}
#endif

	// server address
	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(struct sockaddr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = inet_addr(sock->ip);
	server_addr.sin_port = htons(sock->port);

	// connect to server
	if (connect(sock->socket, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1) {
		printf("003: %d\n", errno);
		// sock->state |= LUA_SOCKETEVENT_TCP_STATE_CLOSE;
		socketevent_tcp_trigger_error(sock, sock->L, 1, "remote host does not exist!");
		return 0;
	}

	// set connect state
	sock->state |= LUA_SOCKETEVENT_TCP_STATE_CONNECT;

	// trigger connect handle
	socketevent_tcp_trigger_connect(sock, sock->L);

	// start thread
	int retval = pthread_create(&sock->thread, NULL, socketevent_tcp_data, sock);
	if (retval != 0) {
		socketevent_tcp_trigger_error(sock, sock->L, retval, "create new thread failure!");
		return 0;
	}

	// set thread state
	sock->state |= LUA_SOCKETEVENT_TCP_STATE_THREAD;

	// // set close state
	// sock->state ^= LUA_SOCKETEVENT_TCP_STATE_CLOSE;

	return 1;
}
#endif

static int socketevent_tcp_on(lua_State *L) {
	// sock struct
	LSocketEventTCP *sock = (LSocketEventTCP *)luaL_checkudata(L, 1, LUA_SOCKETEVENT_TCP_HANDLE);

	// get params
	const char *name = luaL_checkstring(L, 2);
	int handler = luaL_ref(L, LUA_REGISTRYINDEX);

	// save event handle
	if (strcmp("connect", name) == 0) {
		sock->event_connect = handler;
	}
	else if (strcmp("data", name) == 0) {
		sock->event_data = handler;
	}
	else if (strcmp("close", name) == 0) {
		sock->event_close = handler;
	}
	else if (strcmp("error", name) == 0) {
		sock->event_error = handler;
	}
	else if (strcmp("message", name) == 0) {
		sock->event_message = handler;
	}
	else {
		luaL_error(L, "event %s not support!", name);
	}

	return 1;
}

static int socketevent_tcp_send(lua_State *L) {
	// sock struct
	LSocketEventTCP *sock = (LSocketEventTCP *)luaL_checkudata(L, 1, LUA_SOCKETEVENT_TCP_HANDLE);

	// get params
	size_t data_size = 0;
	const char *data = luaL_checklstring(L, 2, &data_size);

	// send data
	int retval = send(sock->socket, data, data_size, 0);
	if (retval == -1) {
		socketevent_tcp_trigger_error(sock, sock->L, retval, "send data failure!");
		return 0;
	}

	return 1;
}

static int socketevent_tcp_send_message(lua_State *L) {
	// sock struct
	LSocketEventTCP *sock = (LSocketEventTCP *)luaL_checkudata(L, 1, LUA_SOCKETEVENT_TCP_HANDLE);

	// get params
	size_t data_size = 0;
	const char *data = luaL_checklstring(L, 2, &data_size);

	// message buffer
	size_t message_raw_len = LUA_SOCKETEVENT_TCP_MESSAGE_HEAD_SIZE + data_size;
	char *message_buffer = (char *)malloc(message_raw_len);
	memcpy(message_buffer, (void *)(&data_size), LUA_SOCKETEVENT_TCP_MESSAGE_HEAD_SIZE);
	memcpy(message_buffer + LUA_SOCKETEVENT_TCP_MESSAGE_HEAD_SIZE, (void *)data, data_size);

	// send message
	int retval = send(sock->socket, message_buffer, message_raw_len, 0);
	if (retval == -1) {
		free(message_buffer);
		socketevent_tcp_trigger_error(sock, sock->L, retval, "send message failure!");
		return 0;
	}

	// free message_buffer
	free(message_buffer);

	return 1;
}

static int socketevent_tcp_close(lua_State *L) {
	// sock struct
	LSocketEventTCP *sock = (LSocketEventTCP *)luaL_checkudata(L, 1, LUA_SOCKETEVENT_TCP_HANDLE);

	// close socket
#ifdef _WIN32
	closesocket(sock->socket);
#else
	close(sock->socket);
#endif

	// exit thread
#ifndef _WIN32
	pthread_cancel(sock->thread);
#endif

	// trigger close handle
	socketevent_tcp_trigger_close(sock, L);

	return 1;
}

static int socketevent_tcp_wait(lua_State *L) {
	// sock struct
	LSocketEventTCP *sock = (LSocketEventTCP *)luaL_checkudata(L, 1, LUA_SOCKETEVENT_TCP_HANDLE);

	// wait thread exit
#ifdef _WIN32
	while (1) {
		// check close state
		if ((sock->state & LUA_SOCKETEVENT_TCP_STATE_CLOSE) == LUA_SOCKETEVENT_TCP_STATE_CLOSE) {
			return 1;
		}
		Sleep(1000);
	}
#else
	void *retval = NULL;
	pthread_join(sock->thread, &retval);
#endif

	return 1;
}

static int socketevent_tcp_gc(lua_State *L) {
	// sock struct
	LSocketEventTCP *sock = (LSocketEventTCP *)luaL_checkudata(L, 1, LUA_SOCKETEVENT_TCP_HANDLE);

	// close socket
#ifdef _WIN32
	closesocket(sock->socket);
#else
	close(sock->socket);
	// shutdown(sock->socket, SHUT_RD);
#endif

	// exit thread
#ifndef _WIN32
	pthread_cancel(sock->thread);
#endif

	// free buffer
	free((void *)sock->data_buffer);
	free((void *)sock->message_buffer);

	return 0;
}

static int socketevent_tcp_tostring(lua_State *L) {
	// sock struct
	LSocketEventTCP *sock = (LSocketEventTCP *)luaL_checkudata(L, 1, LUA_SOCKETEVENT_TCP_HANDLE);

	// return string
	lua_pushfstring(L, "tcp sock %s:%d", sock->ip, sock->port);

	return 1;
}

static const luaL_Reg socketeventlib[] = {
	{ "tcp", socketevent_tcp },
	{ NULL, NULL }
};

static const luaL_Reg tcplib[] = {
	{ "connect", socketevent_tcp_connect },
	{ "on", socketevent_tcp_on },
	{ "send", socketevent_tcp_send },
	{ "sendmessage", socketevent_tcp_send_message },
	{ "close", socketevent_tcp_close },
	{ "wait", socketevent_tcp_wait },
	{ "__gc", socketevent_tcp_gc },
	{ "__tostring", socketevent_tcp_tostring },
	{ NULL, NULL }
};

LUAMOD_API int luaopen_socketevent(lua_State *L) {

#if LUA_VERSION_NUM == 501
	// create tcp metatable
	luaL_newmetatable(L, LUA_SOCKETEVENT_TCP_HANDLE);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	luaL_register(L, NULL, tcplib);

	// create lib
	luaL_register(L, "socketevent", socketeventlib);
#else
	// create lib
	luaL_newlib(L, socketeventlib);

	// create tcp metatable
	luaL_newmetatable(L, LUA_SOCKETEVENT_TCP_HANDLE);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	luaL_setfuncs(L, tcplib, 0);
	lua_pop(L, 1);
#endif

	return 1;
}

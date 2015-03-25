//
//  socketevent.c
//  LuaSocketEvent
//
//  Created by dotcoo on 2015/03/01.
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
#include <ws2tcpip.h>
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

#define LUA_SOCKETEVENT_TCP_HANDLE			"SOCKETEVENT_TCP*"
#define LUA_SOCKETEVENT_TCP_BUFFER_SIZE			0x4000
#define LUA_SOCKETEVENT_TCP_MESSAGE_HEAD_SIZE		4
#define LUA_SOCKETEVENT_TCP_MESSAGE_MAX_SIZE		0x100000
#define LUA_SOCKETEVENT_TCP_STATE_CONNECT		0x1
#define LUA_SOCKETEVENT_TCP_STATE_THREAD		0x2
#define LUA_SOCKETEVENT_TCP_STATE_CLOSE			0x4

#if defined(__APPLE__)
#define SOL_TCP IPPROTO_TCP
#endif

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
	const char *host;
	const char *ip;
	lua_Integer port;

	// tcp option
	int keepalive;
	int keepidle;
	int keepintvl;
	int keepcnt;

	// action
	int connect_sync;
	int close_type;

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

void socketevent_tcp_trigger_error(LSocketEventTCP *sock, lua_State *L, int line, int err, const char *message);

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
	sock->state = 0;

	// pthread
	// sock->thread = -1;

	// socket
	sock->socket = -1;
	sock->host = NULL;
	sock->ip = NULL;
	sock->port = -1;

	// tcp option
	sock->keepalive = 1;
	sock->keepidle = 120;
	sock->keepintvl = 20;
	sock->keepcnt = 3;

	// action
	sock->connect_sync = 0;
	sock->close_type = 2;

	// data buffer
	sock->data_buffer_size = LUA_SOCKETEVENT_TCP_BUFFER_SIZE;
	sock->data_buffer_use = 0;
	sock->data_buffer = (char *)malloc(sock->data_buffer_size + 1);
	memset(sock->data_buffer, 0, sock->data_buffer_size + 1);

	// message buffer
	sock->message_buffer_size = LUA_SOCKETEVENT_TCP_BUFFER_SIZE;
	sock->message_buffer_use = 0;
	sock->message_buffer = (char *)malloc(sock->message_buffer_size + 1);
	memset(sock->message_buffer, 0, sock->message_buffer_size + 1);
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
	if (sock->state != 1) {
		return;
	}

	// trigger lua close handle
	lua_rawgeti(L, LUA_REGISTRYINDEX, sock->event_close);
	lua_newtable(L);

	int result = lua_pcall(L, 1, 0, 0);
	if (0 != result) {
		luaL_error(L, "close event call error: %d", result);
	}
}

void socketevent_tcp_trigger_error(LSocketEventTCP *sock, lua_State *L, int line, int err, const char *message) {
	if (sock->event_error < 0) {
		return;
	}

	// trigger lua error handle
	lua_rawgeti(L, LUA_REGISTRYINDEX, sock->event_error);
	lua_newtable(L);
	lua_pushliteral(L, "line");
	lua_pushinteger(L, line);
	lua_settable(L, -3);
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

	// server address
	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	if (inet_pton(AF_INET, (const char *)sock->ip, &server_addr.sin_addr) <= 0) {
		socketevent_tcp_trigger_error(sock, sock->L, __LINE__, 12, "domain error");
		return 0;
	}
	server_addr.sin_port = htons((u_short)sock->port);

	// connect to server
	if (connect(sock->socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
		socketevent_tcp_trigger_error(sock, sock->L, __LINE__, errno, strerror(errno));
		return NULL;
	}

	// trigger connect handle
	socketevent_tcp_trigger_connect(sock, sock->L);

	// recv data
	while (1) {
		sock->data_buffer_use = recv(sock->socket, sock->data_buffer, sock->data_buffer_size, 0);
		// check error
		if (sock->data_buffer_use == 0) {
			break;
		}
		if (sock->data_buffer_use < 0) {
			socketevent_tcp_trigger_error(sock, sock->L, __LINE__, errno, strerror(errno));
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
				sock->message_buffer = (char *)realloc((void *)sock->message_buffer, sock->message_buffer_size + 1);
				sock->message_buffer[sock->message_buffer_size] = 0;
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
					socketevent_tcp_trigger_error(sock, sock->L, __LINE__, 1, "message too long!");
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

	// socket state
	sock->state = 2;

	return NULL;
}

#if defined(_WIN32)
void socketevent_tcp_data_win(void *psock) {
	socketevent_tcp_data(psock);
}
#endif

static int socketevent_tcp_setopt(lua_State *L) {
	// sock struct
	LSocketEventTCP *sock = (LSocketEventTCP *)luaL_checkudata(L, 1, LUA_SOCKETEVENT_TCP_HANDLE);

	// get params
	luaL_checktype(L, 2, LUA_TTABLE);

	// set option
	lua_pushnil(L);
	while (lua_next(L, 2)) {
		const char *key = luaL_checkstring(L, -2);
		lua_Integer val = luaL_checkinteger(L, -1);

		if (strcmp(key, "keepalive") == 0){
			sock->keepalive = val;
		}
		if (strcmp(key, "keepidle") == 0){
			sock->keepidle = val;
		}
		if (strcmp(key, "keepintvl") == 0){
			sock->keepintvl = val;
		}
		if (strcmp(key, "keepcnt") == 0){
			sock->keepcnt = val;
		}
		
		if (strcmp(key, "connect_sync") == 0){
			sock->connect_sync = val;
		}
		if (strcmp(key, "close_type") == 0){
			sock->close_type = val;
		}

		lua_pop(L, 1);
	}
	lua_pop(L, 1);

	return 1;
}

static int socketevent_tcp_connect(lua_State *L) {
	// sock struct
	LSocketEventTCP *sock = luaL_checkudata(L, 1, LUA_SOCKETEVENT_TCP_HANDLE);

	// check connect state
	if (sock->state != 0) {
		socketevent_tcp_trigger_error(sock, sock->L, __LINE__, 6, "socket has connect");
		return 0;
	}
	sock->state++;

	// get params
	const char *host = luaL_checkstring(L, 2);
	lua_Integer port = luaL_checkinteger(L, 3);

#if defined(_WIN32)
	WSADATA wsa;
	// WinSock Startup
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		socketevent_tcp_trigger_error(sock, sock->L, __LINE__, 2, "c WSAStartup function error!");
		return 0;
	}
#endif

	sock->host = host;
	if (-1 == inet_addr(host)) {
		struct hostent *hostinfo;
		if ((hostinfo = (struct hostent*)gethostbyname(host)) == NULL) {
#if defined(_WIN32)
			socketevent_tcp_trigger_error(sock, sock->L, __LINE__, h_errno, hstrerror(h_errno));
#else
			socketevent_tcp_trigger_error(sock, sock->L, __LINE__, 18, "domain not found!");
#endif
			return 0;
		}
		if (hostinfo->h_addrtype == AF_INET && hostinfo->h_addr_list != NULL) {
#if defined(_WIN32)
			char ipstr[16];
			char * ipbyte = *(hostinfo->h_addr_list);
			sprintf(ipstr, "%d.%d.%d.%d", *ipbyte, *(ipbyte++), *(ipbyte+2), *(ipbyte+3));
			sock->ip = ipstr;
#else
			char ipstr[16];
			inet_ntop(hostinfo->h_addrtype, *(hostinfo->h_addr_list), ipstr, sizeof(ipstr));
			sock->ip = ipstr;
#endif
		} else {
			socketevent_tcp_trigger_error(sock, sock->L, __LINE__, 3, "not support ipv6!");
			return 0;
		}
	} else {
		sock->ip = host;
	}
	sock->port = port;

	// create socket
	if ((sock->socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		socketevent_tcp_trigger_error(sock, sock->L, __LINE__, errno, strerror(errno));
		return 0;
	}

#if defined(__linux__) || defined(__ANDROID__)
	// tcp option set
	if (sock->keepalive == 1) {
		if (setsockopt(sock->socket, SOL_SOCKET, SO_KEEPALIVE, (void *)&(sock->keepalive), sizeof(sock->keepalive)) < 0) {
			socketevent_tcp_trigger_error(sock, sock->L, __LINE__, errno, strerror(errno));
			return 0;
		}
		if (setsockopt(sock->socket, SOL_TCP, TCP_KEEPIDLE, (void *)&(sock->keepidle), sizeof(sock->keepidle)) < 0) {
			socketevent_tcp_trigger_error(sock, sock->L, __LINE__, errno, strerror(errno));
			return 0;
		}
		if (setsockopt(sock->socket, SOL_TCP, TCP_KEEPINTVL, (void *)&(sock->keepintvl), sizeof(sock->keepintvl)) < 0) {
			socketevent_tcp_trigger_error(sock, sock->L, __LINE__, errno, strerror(errno));
			return 0;
		}
		if (setsockopt(sock->socket, SOL_TCP, TCP_KEEPCNT, (void *)&(sock->keepcnt), sizeof(sock->keepcnt)) < 0) {
			socketevent_tcp_trigger_error(sock, sock->L, __LINE__, errno, strerror(errno));
			return 0;
		}
	}
#endif

	// start thread
#if defined(_WIN32)
	_beginthread(socketevent_tcp_data_win, 0, sock);
#else
	int retval = pthread_create(&sock->thread, NULL, socketevent_tcp_data, sock);
	if (retval != 0) {
		socketevent_tcp_trigger_error(sock, sock->L, __LINE__, retval, strerror(retval));
		return 0;
	}
#endif

	return 1;
}

static int socketevent_tcp_on(lua_State *L) {
	// sock struct
	LSocketEventTCP *sock = (LSocketEventTCP *)luaL_checkudata(L, 1, LUA_SOCKETEVENT_TCP_HANDLE);

	// get params
	const char *name = luaL_checkstring(L, 2);
	int handler = luaL_ref(L, LUA_REGISTRYINDEX);

	// save event handle
	if (strcmp("connect", name) == 0) {
		sock->event_connect = handler;
	} else if (strcmp("data", name) == 0) {
		sock->event_data = handler;
	} else if (strcmp("close", name) == 0) {
		sock->event_close = handler;
	} else if (strcmp("error", name) == 0) {
		sock->event_error = handler;
	} else if (strcmp("message", name) == 0) {
		sock->event_message = handler;
	} else {
		luaL_error(L, "event %s not support!", name);
	}

	return 1;
}

static int socketevent_tcp_send(lua_State *L) {
	// sock struct
	LSocketEventTCP *sock = (LSocketEventTCP *)luaL_checkudata(L, 1, LUA_SOCKETEVENT_TCP_HANDLE);

	// check connect state
	if (sock->state != 1) {
		socketevent_tcp_trigger_error(sock, sock->L, __LINE__, 5, "socket not connect!");
		lua_pushinteger(L, 0);
		return 0;
	}

	// get params
	size_t data_size = 0;
	const char *data = luaL_checklstring(L, 2, &data_size);

	// send data
	int retval = send(sock->socket, data, data_size, 0);
	if (retval == -1) {
		socketevent_tcp_trigger_error(sock, sock->L, __LINE__, errno, strerror(errno));
		return 0;
	}

	return 1;
}

static int socketevent_tcp_send_message(lua_State *L) {
	// sock struct
	LSocketEventTCP *sock = (LSocketEventTCP *)luaL_checkudata(L, 1, LUA_SOCKETEVENT_TCP_HANDLE);

	// check connect state
	if (sock->state != 1) {
		socketevent_tcp_trigger_error(sock, sock->L, __LINE__, 5, "socket not connect!");
		return 0;
	}

	// get params
	size_t data_size = 0;
	const char *data = luaL_checklstring(L, 2, &data_size);

	// message buffer
	size_t message_raw_len = LUA_SOCKETEVENT_TCP_MESSAGE_HEAD_SIZE + data_size;
	char *message_buffer = (char *)malloc(message_raw_len + 1);
	message_buffer[message_raw_len] = 0;
	memcpy(message_buffer, (void *)(&data_size), LUA_SOCKETEVENT_TCP_MESSAGE_HEAD_SIZE);
	memcpy(message_buffer + LUA_SOCKETEVENT_TCP_MESSAGE_HEAD_SIZE, (void *)data, data_size);

	// send message
	int retval = send(sock->socket, message_buffer, message_raw_len, 0);
	if (retval == -1) {
		free(message_buffer);
		socketevent_tcp_trigger_error(sock, sock->L, __LINE__, errno, strerror(errno));
		return 0;
	}

	// free message_buffer
	free(message_buffer);

	return 1;
}

static int socketevent_tcp_close(lua_State *L) {
	// sock struct
	LSocketEventTCP *sock = (LSocketEventTCP *)luaL_checkudata(L, 1, LUA_SOCKETEVENT_TCP_HANDLE);

	// check connect state
	if (sock->state != 1) {
		socketevent_tcp_trigger_error(sock, sock->L, __LINE__, 5, "socket not connect!");
		lua_pushinteger(L, 0);
		return 0;
	}

	// close socket
	switch (sock->close_type) {
#ifdef _WIN32
		case 1:
			shutdown(sock->socket, SD_RECEIVE);
			break;
		case 2:
			shutdown(sock->socket, SD_SEND);
			break;
		case 3:
			shutdown(sock->socket, SD_BOTH);
			break;
		default :
			closesocket(sock->socket);
			break;
#else
		case 1:
			shutdown(sock->socket, SHUT_RD);
			break;
		case 2:
			shutdown(sock->socket, SHUT_WR);
			break;
		case 3:
			shutdown(sock->socket, SHUT_RDWR);
			break;
		default :
			close(sock->socket);
			break;
#endif
	}

	return 1;
}

static int socketevent_tcp_wait(lua_State *L) {
	// sock struct
	LSocketEventTCP *sock = (LSocketEventTCP *)luaL_checkudata(L, 1, LUA_SOCKETEVENT_TCP_HANDLE);

	// wait thread exit
#ifdef _WIN32
	while (1) {
		// check close state
		if (sock->state == 2) {
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
	{ "setopt", socketevent_tcp_setopt },
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

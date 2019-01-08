/*********************************************************************/
/*                                                                   */
/* Copyright (C) 2010,  AVIQ Systems AG                              */
/*                                                                   */
/* Project:       lrun                                               */
/* Filename:      luavlcwrp.c                                        */
/*                                                                   */
/*********************************************************************/ 

#ifdef _WIN32
  #include <windows.h>
  #ifdef LUAVLCWRP_BUILD
    #ifdef LUAVLCWRP_DLL
      #define LUAVLCWRP_API __declspec(dllexport)
    #endif
  #else
    #ifdef LUAVLCWRP_DLL
      #define LUAVLCWRP_API __declspec(dllimport)
    #endif
  #endif
#else
  #ifdef LUAVLCWRP_BUILD
    #define LUAVLCWRP_API extern
  #endif
#endif

#ifndef LUAVLCWRP_API
#define LUAVLCWRP_API
#endif

#include <stdlib.h>
#include <stdarg.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <vlcwrp.h>
#include <ctype.h>
char *strdup(const char *);

#define LIBVLC_MT "LIBVLC_MT"
LUAVLCWRP_API int luaopen_vlcwrp(lua_State *L);

static int fail_error_exit(lua_State* L, const char* fmt, ...)
{
	va_list ap;
	lua_pushnil(L);
	va_start(ap, fmt);
	lua_pushvfstring(L, fmt, ap);
	va_end(ap);
	return 2;
}

static int fail_allocate_exit(lua_State* L, int line)
{
	return fail_error_exit(L, "allocation error (luavlc.c:%d)", line);
}

static int catch_vlc_error(lua_State* L)
{
	const char* errmsg = vlcwrp_error();
	if (errmsg)
		return fail_error_exit(L, "vlc error: %s", errmsg);

	lua_pushboolean(L, 1);
	return 1; /* no errors */
}

static void createmeta (lua_State *L, const char* mt)
{
	luaL_newmetatable(L, mt);
	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -2);
	lua_rawset(L, -3);
}

static const char* vlc_gettablestr(lua_State* L, int index, const char* key)
{
	const char* res;
	luaL_argcheck(L, lua_istable(L, index), index, "table argument expected");
	lua_getfield(L, index, key);
	res = luaL_checkstring(L, -1);
	lua_pop(L, 1);
	return res;
}

static int vlc_gettableint(lua_State* L, int index, const char* key)
{
	int res;
	luaL_argcheck(L, lua_istable(L, index), index, "table argument expected");
	lua_getfield(L, index, key);
	res = luaL_checkinteger(L, -1);
	lua_pop(L, 1);
	return res;
}

static int vlc_new(lua_State* L)
{
	int i;
	int width, height;
	int vlc_argc;
	char **vlc_argv;
	struct vlcwrp_ctx_t** pctx;

	luaL_checktype(L, 1, LUA_TTABLE);
	pctx = (struct vlcwrp_ctx_t**)lua_newuserdata(L, sizeof(struct vlcwrp_ctx_t*));
	if (!pctx) return fail_allocate_exit(L, __LINE__);
	luaL_getmetatable(L, LIBVLC_MT);
	lua_setmetatable(L, -2);

	vlc_argc = luaL_getn(L, 1);
	vlc_argv = (char**)malloc(vlc_argc * sizeof(char *));
	if (!vlc_argv) return fail_allocate_exit(L, __LINE__);

	for (i=1; i<=vlc_argc; i++)
	{
		lua_rawgeti(L, 1, i);
		vlc_argv[i-1] = strdup(lua_tostring(L, -1));
		lua_pop(L, 1);
		if (!vlc_argv[i-1])
		{
			int j;
			for (j=0; j<i-2; j++) free(vlc_argv[j]);
			free(vlc_argv);
			return fail_allocate_exit(L, __LINE__);
		}
	}

	width  = vlc_gettableint(L, 1, "vmem_width");
	height = vlc_gettableint(L, 1, "vmem_height");
	*pctx = vlcwrp_create(vlc_argc, (const char * const*)vlc_argv, width, height);
	for (i=0; i<vlc_argc; i++) free(vlc_argv[i]);
	free(vlc_argv);
	if (*pctx)
	{
		return 1;
	}
	return fail_error_exit(L, "VLC init error");
}

static int vlc_destroy(lua_State* L)
{
	struct vlcwrp_ctx_t** pctx = (struct vlcwrp_ctx_t**)luaL_checkudata(L, 1, LIBVLC_MT);
	if (pctx && *pctx)
	{
		vlcwrp_destroy(*pctx);
		*pctx = 0;
	}
	return 0;
}

static int vlc_play(lua_State* L)
{
	struct vlcwrp_ctx_t** pctx = (struct vlcwrp_ctx_t**)luaL_checkudata(L, 1, LIBVLC_MT);
	const char* url = luaL_optstring(L, 2, NULL);
	if (pctx)
	{
		vlcwrp_play(*pctx, url);
		return catch_vlc_error(L);
	}
	return 0;
}

static int vlc_stop(lua_State* L)
{
	struct vlcwrp_ctx_t** pctx = (struct vlcwrp_ctx_t**)luaL_checkudata(L, 1, LIBVLC_MT);
	if (pctx)
	{
		vlcwrp_stop(*pctx);
		return catch_vlc_error(L);
	}
	return 0;
}

static int vlc_pause(lua_State* L)
{
	struct vlcwrp_ctx_t** pctx = (struct vlcwrp_ctx_t**)luaL_checkudata(L, 1, LIBVLC_MT);
	if (pctx)
	{
		vlcwrp_pause(*pctx);
		return catch_vlc_error(L);
	}
	return 0;
}

static int vlc_get_state(lua_State* L)
{
	struct vlcwrp_ctx_t** pctx = (struct vlcwrp_ctx_t**)luaL_checkudata(L, 1, LIBVLC_MT);
	if (pctx)
	{
		switch (vlcwrp_get_state(*pctx))
		{
		case VLC_OPENING: lua_pushliteral(L, "Opening"); break;
		case VLC_BUFFERING: lua_pushliteral(L, "Buffering"); break;
		case VLC_PLAYING: lua_pushliteral(L, "Playing"); break;
		case VLC_PAUSED: lua_pushliteral(L, "Paused"); break;
		case VLC_STOPPED: lua_pushliteral(L, "Stopped"); break;
		case VLC_ENDED: lua_pushliteral(L, "Ended"); break;
		case VLC_ERROR: lua_pushliteral(L, "Error"); break;
		}
		return 1;
	}
	return 0;
}

static int vlc_frame_acquire(lua_State* L)
{
	struct vlcwrp_ctx_t** pctx = (struct vlcwrp_ctx_t**)luaL_checkudata(L, 1, LIBVLC_MT);
	if (pctx)
	{
		void* pframe = vlcwrp_frame_acquire(*pctx);
		if (pframe)
		{
			lua_pushlightuserdata(L, pframe);
			return 1;
		}
	}
	return 0;
}

static int vlc_frame_release(lua_State* L)
{
	struct vlcwrp_ctx_t** pctx = (struct vlcwrp_ctx_t**)luaL_checkudata(L, 1, LIBVLC_MT);
	if (pctx)
	{
		vlcwrp_frame_release(*pctx);
	}
	return 0;
}

static const luaL_reg vlc_funcs[] =
{
	{"new", vlc_new},
	{NULL, NULL},
};

static const luaL_reg vlc_meths[] =
{
	{"__gc", vlc_destroy},
	{"play", vlc_play},
	{"stop", vlc_stop},
	{"pause", vlc_pause},
	{"get_state", vlc_get_state},
	{"frame_acquire", vlc_frame_acquire},
	{"frame_release", vlc_frame_release},
	{NULL, NULL},
};

LUAVLCWRP_API int luaopen_vlcwrp(lua_State *L)
{
	createmeta(L, LIBVLC_MT);
	luaL_openlib(L, 0, vlc_meths, 0);
	luaL_openlib(L, "vlc", vlc_funcs, 0);
	return 1;
}

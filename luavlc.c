/*********************************************************************/
/*                                                                   */
/* Copyright (C) 2010,  AVIQ Systems AG                              */
/*                                                                   */
/* Project:       lrun                                               */
/* Filename:      luavlc.c                                           */
/*                                                                   */
/*********************************************************************/ 

#ifdef _WIN32
  #include <windows.h>
  #ifdef LUAVLC_BUILD
    #ifdef LUAVLC_DLL
      #define LUAVLC_API __declspec(dllexport)
    #endif
  #else
    #ifdef LUAVLC_DLL
      #define LUAVLC_API __declspec(dllimport)
    #endif
  #endif
#else
  #ifdef LUAVLC_BUILD
    #define LUAVLC_API extern
  #endif
#endif

#ifndef LUAVLC_API
#define LUAVLC_API
#endif

#include <assert.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <GL/gl.h>
#include <vlc/vlc.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>

char *strdup(const char *);
char *strncpy(char *, const char *, size_t );
int strcmp(const char *, const char *);
int stricmp(const char *, const char *);

int stricmp(const char *s1, const char *s2)
{
	unsigned char c1, c2;
	for (;;)
	{
		c1 = *s1++;
		c2 = *s2++;
		if (!c1 || !c2) break;
		if (c1 == c2) continue;
		if ((c1 = tolower(c1)) != (c2 = tolower(c2))) break;
	}
	return (int)c1 - (int)c2;
}

#define VMEM_WIDTH_DEFAULT 1024
#define VMEM_HEIGHT_DEFAULT 768
#define VMEM_PITCH_DEFAULT (4*VMEM_WIDTH_DEFAULT)
#define VMEM_CHROMA_DEFAULT "RGBA"
#define VERBOSITY_DEFAULT 0

#define LIBVLC_MT "LIBVLC_MT"
#define LIBVLC_MP_MT "LIBVLC_MP_MT"

#define QUEUE_SIZE 100

LUAVLC_API int luaopen_luavlc (lua_State *L);

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

static int fail_pthread_exit(lua_State* L, int rc, int line)
{
	assert(rc);
	switch (rc)
	{
		case ENOMEM:    return fail_allocate_exit(L, line);
		case EINVAL:    return fail_error_exit(L, "invalid argument (luavlc.c:%d)", line);
		case EBUSY:     return fail_error_exit(L, "thread busy during operation (luavlc.c:%d)", line);
		case EAGAIN:    return fail_error_exit(L, "out of resources (luavlc.c:%d)", line);
		case ETIMEDOUT: return fail_error_exit(L, "timeout error (luavlc.c:%d)", line);
		case EDEADLK:   return fail_error_exit(L, "dead lock (luavlc.c:%d)", line);
		case ESRCH:     return fail_error_exit(L, "no such process (luavlc.c:%d)", line);
		case EPERM:     return fail_error_exit(L, "permission denied (luavlc.c:%d)", line);
	}
	return fail_error_exit(L, "unknown pthread error (luavlc.c:%d)", line);
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

typedef struct
{
	libvlc_instance_t *vlc;
	int verbose;
	char* pix_buffer[QUEUE_SIZE];
	int nfullbuffers;
	int ridx, widx;
	pthread_cond_t cond_not_full, cond_not_empty;
	pthread_mutex_t mutex;
	int width, height, pitch;
	char chroma[5];
} vlc_ctx_t;

typedef struct
{
	libvlc_media_t *vlc_m;
	libvlc_media_player_t *vlc_mp;
	int vlc_ref;
} vlc_mp_ctx_t;

static int catch_error(lua_State* L)
{
	if (libvlc_errmsg())
	{
		lua_pushnil(L);
		lua_pushfstring(L, "vlc error: %s", libvlc_errmsg());
		libvlc_clearerr();
		return 2; /* error status */
	}
	return 0; /* no errors */
}

static void *lock(void* opaque, void **plane)
{
	vlc_ctx_t *vlc_ctx = (vlc_ctx_t *)opaque;
	/* VLC wants decoding video frame */
	pthread_mutex_lock(&(vlc_ctx->mutex));
	while (vlc_ctx->nfullbuffers == QUEUE_SIZE)
	{
		pthread_cond_wait(&(vlc_ctx->cond_not_full), &(vlc_ctx->mutex));
	}
	pthread_mutex_unlock(&(vlc_ctx->mutex));

	if (vlc_ctx->verbose) printf("lock buffer %d (%d)\n", vlc_ctx->widx, vlc_ctx->ridx);
	*plane = vlc_ctx->pix_buffer[vlc_ctx->widx];
	return NULL;
}

static void unlock(void* opaque, void *picture, void *const *plane)
{
	vlc_ctx_t *vlc_ctx = (vlc_ctx_t *)opaque;
	if (vlc_ctx->verbose) printf("unlock buffer %d (%d)\n", vlc_ctx->widx, vlc_ctx->ridx);
	/* VLC just decoded video frame */
	vlc_ctx->widx = (vlc_ctx->widx + 1) % QUEUE_SIZE;
	pthread_mutex_lock(&(vlc_ctx->mutex));
	if (vlc_ctx->nfullbuffers < QUEUE_SIZE)
		vlc_ctx->nfullbuffers++;
	pthread_cond_signal(&(vlc_ctx->cond_not_empty));
	pthread_mutex_unlock(&(vlc_ctx->mutex));
}

static void display(void* opaque, void *picture)
{
	/* VLC wants displaying video frame */
}

static int vlc_new(lua_State* L)
{
	int r, rc;
	int vlc_argc;
	char **vlc_argv;
	vlc_ctx_t* vlc_ctx;
	int i, vlc_argv_alloc = 0;

	luaL_checktype(L, 1, LUA_TTABLE);
	vlc_ctx = (vlc_ctx_t*)lua_newuserdata(L, sizeof(vlc_ctx_t));
	if (!vlc_ctx) return fail_allocate_exit(L, __LINE__);
	vlc_ctx->verbose = VERBOSITY_DEFAULT;
	vlc_ctx->pix_buffer[0] = vlc_ctx->pix_buffer[1] = NULL;
	vlc_ctx->ridx = vlc_ctx->widx = vlc_ctx->nfullbuffers = 0;
	vlc_ctx->width = vlc_ctx->height = vlc_ctx->pitch = 0;
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

	vlc_ctx->width  = vlc_gettableint(L, 1, "vmem_width");
	vlc_ctx->height = vlc_gettableint(L, 1, "vmem_height");
	vlc_ctx->pitch  = vlc_gettableint(L, 1, "vmem_pitch");
	strncpy(vlc_ctx->chroma, vlc_gettablestr(L, 1, "vmem_chroma"), 4);
	vlc_ctx->chroma[4] = '\0';

	for (i=0; i<QUEUE_SIZE; i++)
	{
		vlc_ctx->pix_buffer[i] = (char*)malloc(vlc_ctx->pitch * vlc_ctx->height);
		if (!vlc_ctx->pix_buffer[i])
		{
			for (i=0; i<vlc_argc; i++) free(vlc_argv[i]);
			free(vlc_argv);
			if (i==1) { free(vlc_ctx->pix_buffer[0]); vlc_ctx->pix_buffer[0] = NULL; }
			return fail_allocate_exit(L, __LINE__);
		}
	}

	if (0 != (rc = pthread_mutex_init(&(vlc_ctx->mutex), 0)))
	{
		for (i=0; i<vlc_argc; i++) free(vlc_argv[i]);
		free(vlc_argv);
		return fail_pthread_exit(L, rc, __LINE__);
	}

	if (0 != (rc = pthread_cond_init(&(vlc_ctx->cond_not_full), 0)))
	{
		for (i=0; i<vlc_argc; i++) free(vlc_argv[i]);
		free(vlc_argv);
		return fail_pthread_exit(L, rc, __LINE__);
	}

	if (0 != (rc = pthread_cond_init(&(vlc_ctx->cond_not_empty), 0)))
	{
		for (i=0; i<vlc_argc; i++) free(vlc_argv[i]);
		free(vlc_argv);
		return fail_pthread_exit(L, rc, __LINE__);
	}

	if (vlc_ctx->verbose)
	{
		printf("vlc:new width=%d, height=%d, pitch=%d, chroma=%s, ",
			vlc_ctx->width, vlc_ctx->height, vlc_ctx->pitch, vlc_ctx->chroma);

		for (i=0; i<vlc_argc; i++)
		{
			if (i>0) printf(" ,");
			printf("%s", vlc_argv[i]);
		}
		printf("\n");
	}
	vlc_ctx->vlc = libvlc_new(vlc_argc, (const char * const*)vlc_argv);
	if (vlc_argv_alloc)
	{
		for (i=0; i<vlc_argc; i++) free(vlc_argv[i]);
		free(vlc_argv);
	}
	if ((r = catch_error(L))) return r;
	return 1;
}

static int vlc_destroy(lua_State* L)
{
	vlc_ctx_t* vlc_ctx = (vlc_ctx_t*)luaL_checkudata(L, 1, LIBVLC_MT);
	if (vlc_ctx)
	{
		int i;
		if (vlc_ctx->verbose)
			printf("vlc:destroy()\n");
		if (vlc_ctx->vlc)
			libvlc_release(vlc_ctx->vlc);
		for (i=0; i<QUEUE_SIZE; i++)
			if (vlc_ctx->pix_buffer[i])
			{
				free(vlc_ctx->pix_buffer[i]);
				vlc_ctx->pix_buffer[i] = NULL;
			}
		pthread_mutex_destroy(&(vlc_ctx->mutex));
		pthread_cond_destroy(&(vlc_ctx->cond_not_full));
		pthread_cond_destroy(&(vlc_ctx->cond_not_empty));
	}
	return 0;
}

static int vlc_open(lua_State* L)
{
	libvlc_media_t *m;
	libvlc_media_player_t *vlc_mp;
	vlc_mp_ctx_t* vlc_mp_ctx;
	int r;
	vlc_ctx_t* vlc_ctx = (vlc_ctx_t*)luaL_checkudata(L, 1, LIBVLC_MT);
	m = libvlc_media_new_path(vlc_ctx->vlc, luaL_checkstring(L, 2));
	if ((r = catch_error(L))) return r;

	vlc_mp = libvlc_media_player_new_from_media(m);
	if ((r = catch_error(L)))
	{
		libvlc_media_release(m);
		return r;
	}

	vlc_mp_ctx = (vlc_mp_ctx_t*)lua_newuserdata(L, sizeof(vlc_mp_ctx_t));
	if (!vlc_mp_ctx)
	{
		libvlc_media_player_release(vlc_mp);
		libvlc_media_release(m);
		return fail_allocate_exit(L, __LINE__);
	}

	libvlc_video_set_callbacks(vlc_mp, lock, unlock, display, vlc_ctx);
	libvlc_video_set_format(vlc_mp, vlc_ctx->chroma, vlc_ctx->width, vlc_ctx->height, vlc_ctx->pitch);

	luaL_getmetatable(L, LIBVLC_MP_MT);
	lua_setmetatable(L, -2);
	lua_pushvalue(L, 1); 
	vlc_mp_ctx->vlc_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	vlc_mp_ctx->vlc_m = m;
	vlc_mp_ctx->vlc_mp = vlc_mp;
	return 1;
}

static int vlc_wait(lua_State* L)
{
	vlc_ctx_t* vlc_ctx = (vlc_ctx_t*)luaL_checkudata(L, 1, LIBVLC_MT);
	libvlc_wait(vlc_ctx->vlc);
	return 0;
}

static int vlc_get_version(lua_State* L)
{
	lua_pushstring(L, libvlc_get_version());
	return 1;
}

static int vlc_get_compiler(lua_State* L)
{
	lua_pushstring(L, libvlc_get_compiler());
	return 1;
}

static int vlc_get_video_frame_size(lua_State* L)
{
	vlc_ctx_t* vlc_ctx = (vlc_ctx_t*)luaL_checkudata(L, 1, LIBVLC_MT);
	lua_pushinteger(L, vlc_ctx->width);
	lua_pushinteger(L, vlc_ctx->height);
	lua_pushinteger(L, vlc_ctx->pitch);
	return 3;
}

static int vlc_has_video_frame(lua_State* L)
{
	int has;
	vlc_ctx_t* vlc_ctx = (vlc_ctx_t*)luaL_checkudata(L, 1, LIBVLC_MT);
	pthread_mutex_lock(&(vlc_ctx->mutex));
	has = vlc_ctx->nfullbuffers > 0;
	pthread_mutex_unlock(&(vlc_ctx->mutex));
	lua_pushboolean(L, has);
	return 1;
}

static int vlc_get_video_frame(lua_State* L)
{
	vlc_ctx_t* vlc_ctx = (vlc_ctx_t*)luaL_checkudata(L, 1, LIBVLC_MT);
	pthread_mutex_lock(&(vlc_ctx->mutex));
	if (vlc_ctx->verbose) printf("vlc_get_video_frame buffer %d (%d)\n", vlc_ctx->ridx, vlc_ctx->widx);
	lua_pushlstring(L, vlc_ctx->pix_buffer[vlc_ctx->ridx], vlc_ctx->pitch * vlc_ctx->height);
	pthread_mutex_unlock(&(vlc_ctx->mutex));
	return 1;
}

static int vlc_next_video_frame(lua_State* L)
{
	vlc_ctx_t* vlc_ctx = (vlc_ctx_t*)luaL_checkudata(L, 1, LIBVLC_MT);
	pthread_mutex_lock(&(vlc_ctx->mutex));
	if (vlc_ctx->nfullbuffers > 0)
	{
		vlc_ctx->ridx = (vlc_ctx->ridx + 1) % QUEUE_SIZE;
		vlc_ctx->nfullbuffers--;
	}
	if (vlc_ctx->verbose) printf("vlc_next_video_frame buffer %d (%d) nfullbuffers = %d\n", vlc_ctx->ridx, vlc_ctx->widx, vlc_ctx->nfullbuffers);
	pthread_mutex_unlock(&(vlc_ctx->mutex));

	return 0;
}

static int vlc_wait_video_frame(lua_State* L)
{
	vlc_ctx_t* vlc_ctx = (vlc_ctx_t*)luaL_checkudata(L, 1, LIBVLC_MT);
	pthread_mutex_lock(&(vlc_ctx->mutex));
	while (vlc_ctx->nfullbuffers == 0)
	{
		if (vlc_ctx->verbose) printf("vlc_wait_video_frame waits\n");
		pthread_cond_wait(&(vlc_ctx->cond_not_empty), &(vlc_ctx->mutex));
	}
	pthread_cond_signal(&(vlc_ctx->cond_not_full));
	pthread_mutex_unlock(&(vlc_ctx->mutex));
	return 0;
}

static int vlc_mp_destroy(lua_State* L)
{
	vlc_mp_ctx_t* vlc_mp_ctx = (vlc_mp_ctx_t*)luaL_checkudata(L, 1, LIBVLC_MP_MT);
	if (vlc_mp_ctx)
	{
		vlc_ctx_t* vlc_ctx;
		lua_rawgeti(L, LUA_REGISTRYINDEX, vlc_mp_ctx->vlc_ref);
		vlc_ctx = (vlc_ctx_t*)lua_touserdata(L, -1);
		assert(vlc_ctx);
		lua_pop(L, 1);
		if (vlc_ctx->verbose)
			printf("vlc_mp:destroy()\n");
		luaL_unref(L, LUA_REGISTRYINDEX, vlc_mp_ctx->vlc_ref);
		
		if (vlc_mp_ctx->vlc_m)
			libvlc_media_release(vlc_mp_ctx->vlc_m);
		if (vlc_mp_ctx->vlc_mp)
			libvlc_media_player_release(vlc_mp_ctx->vlc_mp);
	}
	return 0;
}

static int vlc_mp_play(lua_State* L)
{
	int r;
	vlc_mp_ctx_t* vlc_mp_ctx = (vlc_mp_ctx_t*)luaL_checkudata(L, 1, LIBVLC_MP_MT);
	libvlc_media_player_play(vlc_mp_ctx->vlc_mp);
	if ((r = catch_error(L))) return r;
	lua_pushboolean(L, 1);
	return 1;
}

static int vlc_mp_is_playing(lua_State* L)
{
	int r, status;
	vlc_mp_ctx_t* vlc_mp_ctx = (vlc_mp_ctx_t*)luaL_checkudata(L, 1, LIBVLC_MP_MT);
	status = libvlc_media_player_is_playing(vlc_mp_ctx->vlc_mp);
	if ((r = catch_error(L))) return r;
	lua_pushboolean(L, status);
	return 1;
}

static int vlc_mp_pause(lua_State* L)
{
	int r;
	vlc_mp_ctx_t* vlc_mp_ctx = (vlc_mp_ctx_t*)luaL_checkudata(L, 1, LIBVLC_MP_MT);
	libvlc_media_player_pause(vlc_mp_ctx->vlc_mp);
	if ((r = catch_error(L))) return r;
	lua_pushboolean(L, 1);
	return 1;
}

static int vlc_mp_stop(lua_State* L)
{
	int r;
	vlc_mp_ctx_t* vlc_mp_ctx = (vlc_mp_ctx_t*)luaL_checkudata(L, 1, LIBVLC_MP_MT);
	libvlc_media_player_stop(vlc_mp_ctx->vlc_mp);
	if ((r = catch_error(L))) return r;
	lua_pushboolean(L, 1);
	return 1;
}

static int vlc_mp_get_state(lua_State* L)
{
	int r;
	libvlc_state_t state;
	vlc_mp_ctx_t* vlc_mp_ctx = (vlc_mp_ctx_t*)luaL_checkudata(L, 1, LIBVLC_MP_MT);
	state = libvlc_media_get_state(vlc_mp_ctx->vlc_m);
	if ((r = catch_error(L))) return r;
	switch (state)
	{
	case libvlc_NothingSpecial: lua_pushliteral(L, "NothingSpecial"); break;
	case libvlc_Opening: lua_pushliteral(L, "Opening"); break;
	case libvlc_Buffering: lua_pushliteral(L, "Buffering"); break;
	case libvlc_Playing: lua_pushliteral(L, "Playing"); break;
	case libvlc_Paused: lua_pushliteral(L, "Paused"); break;
	case libvlc_Stopped: lua_pushliteral(L, "Stopped"); break;
	case libvlc_Ended: lua_pushliteral(L, "Ended"); break;
	case libvlc_Error: lua_pushliteral(L, "Error"); break;
	default: lua_pushliteral(L, "Unknown"); break;
	}
	return 1;
}

static int vlc_mp_get_duration(lua_State* L)
{
	int r;
	libvlc_time_t duration;
	vlc_mp_ctx_t* vlc_mp_ctx = (vlc_mp_ctx_t*)luaL_checkudata(L, 1, LIBVLC_MP_MT);
	duration = libvlc_media_get_duration(vlc_mp_ctx->vlc_m);
	if ((r = catch_error(L))) return r;
	lua_pushinteger(L, (lua_Integer)duration);
	return 1;
}

static int vlc_mp_get_length(lua_State* L)
{
	int r;
	libvlc_time_t len;
	vlc_mp_ctx_t* vlc_mp_ctx = (vlc_mp_ctx_t*)luaL_checkudata(L, 1, LIBVLC_MP_MT);
	len = libvlc_media_player_get_length(vlc_mp_ctx->vlc_mp);
	if ((r = catch_error(L))) return r;
	lua_pushinteger(L, (lua_Integer)len);
	return 1;
}

static int vlc_mp_get_time(lua_State* L)
{
	int r;
	libvlc_time_t atime;
	vlc_mp_ctx_t* vlc_mp_ctx = (vlc_mp_ctx_t*)luaL_checkudata(L, 1, LIBVLC_MP_MT);
	atime = libvlc_media_player_get_time(vlc_mp_ctx->vlc_mp);
	if ((r = catch_error(L))) return r;
	lua_pushinteger(L, (lua_Integer)atime);
	return 1;
}

static int vlc_mp_set_time(lua_State* L)
{
	int r;
	libvlc_time_t atime;
	vlc_mp_ctx_t* vlc_mp_ctx = (vlc_mp_ctx_t*)luaL_checkudata(L, 1, LIBVLC_MP_MT);
	atime = luaL_checkinteger(L, 2);
	libvlc_media_player_set_time(vlc_mp_ctx->vlc_mp, atime);
	if ((r = catch_error(L))) return r;
	lua_pushboolean(L, 1);
	return 1;
}

static int vlc_mp_get_position(lua_State* L)
{
	int r;
	float position;
	vlc_mp_ctx_t* vlc_mp_ctx = (vlc_mp_ctx_t*)luaL_checkudata(L, 1, LIBVLC_MP_MT);
	position = libvlc_media_player_get_position(vlc_mp_ctx->vlc_mp);
	if ((r = catch_error(L))) return r;
	lua_pushnumber(L, (lua_Number)position);
	return 1;
}

static int vlc_mp_set_position(lua_State* L)
{
	int r;
	float position;
	vlc_mp_ctx_t* vlc_mp_ctx = (vlc_mp_ctx_t*)luaL_checkudata(L, 1, LIBVLC_MP_MT);
	position = (float)luaL_checknumber(L, 2);
	libvlc_media_player_set_position(vlc_mp_ctx->vlc_mp, position);
	if ((r = catch_error(L))) return r;
	lua_pushboolean(L, 1);
	return 1;
}

static int vlc_mp_is_parsed(lua_State* L)
{
	int r;
	int preparsed;
	vlc_mp_ctx_t* vlc_mp_ctx = (vlc_mp_ctx_t*)luaL_checkudata(L, 1, LIBVLC_MP_MT);
	preparsed = libvlc_media_is_parsed(vlc_mp_ctx->vlc_m);
	if ((r = catch_error(L))) return r;
	lua_pushboolean(L, preparsed);
	return 1;
}

static int vlc_mp_get_meta(lua_State* L)
{
	struct meta_str_t
	{
		libvlc_meta_t meta;
		const char* str;
	} meta_str [] =
	{
		{ libvlc_meta_Title, "Title"},
		{ libvlc_meta_Artist, "Artist"},
		{ libvlc_meta_Genre, "Genre"},
		{ libvlc_meta_Copyright, "Copyright"},
		{ libvlc_meta_Album, "Album"},
		{ libvlc_meta_TrackNumber, "TrackNumber"},
		{ libvlc_meta_Description, "Description"},
		{ libvlc_meta_Rating, "Rating"},
		{ libvlc_meta_Date, "Date"},
		{ libvlc_meta_Setting, "Setting"},
		{ libvlc_meta_URL, "URL"},
		{ libvlc_meta_Language, "Language"},
		{ libvlc_meta_NowPlaying, "NowPlaying"},
		{ libvlc_meta_Publisher, "Publisher"},
		{ libvlc_meta_EncodedBy, "EncodedBy"},
		{ libvlc_meta_ArtworkURL, "ArtworkURL"},
		{ libvlc_meta_TrackID, "TrackID"},
		{ 0, NULL}
	};
	struct meta_str_t* pmeta_str = meta_str;
	vlc_mp_ctx_t* vlc_mp_ctx = (vlc_mp_ctx_t*)luaL_checkudata(L, 1, LIBVLC_MP_MT);
	const char* meta_desc = luaL_checkstring(L, 2);
	while (pmeta_str->str)
	{
		if (0 == stricmp(pmeta_str->str, meta_desc))
		{
			int r;
			const char* res;
			res = libvlc_media_get_meta(vlc_mp_ctx->vlc_m, pmeta_str->meta);
			if ((r = catch_error(L))) return r;
			lua_pushstring(L, res?res:"");
			return 1;
		}
		pmeta_str++;
	}

	lua_pushnil(L);
	lua_pushfstring(L, "Invalid meta description `%s'", meta_desc);
	return 2;
}

static int vlc_mp_get_fps(lua_State* L)
{
	int r;
	float fps;
	vlc_mp_ctx_t* vlc_mp_ctx = (vlc_mp_ctx_t*)luaL_checkudata(L, 1, LIBVLC_MP_MT);
	fps = libvlc_media_player_get_fps(vlc_mp_ctx->vlc_mp);
	if ((r = catch_error(L))) return r;
	lua_pushnumber(L, (lua_Number)fps);
	return 1;
}

static int vlc_mp_is_seekable(lua_State* L)
{
	int r;
	int seekable;
	vlc_mp_ctx_t* vlc_mp_ctx = (vlc_mp_ctx_t*)luaL_checkudata(L, 1, LIBVLC_MP_MT);
	seekable = libvlc_media_player_is_seekable(vlc_mp_ctx->vlc_mp);
	if ((r = catch_error(L))) return r;
	lua_pushboolean(L, seekable);
	return 1;
}

static int vlc_mp_can_pause(lua_State* L)
{
	int r;
	int can_pause;
	vlc_mp_ctx_t* vlc_mp_ctx = (vlc_mp_ctx_t*)luaL_checkudata(L, 1, LIBVLC_MP_MT);
	can_pause = libvlc_media_player_can_pause(vlc_mp_ctx->vlc_mp);
	if ((r = catch_error(L))) return r;
	lua_pushboolean(L, can_pause);
	return 1;
}

static int vlc_mp_video_get_scale(lua_State* L)
{
	int r;
	float scale;
	vlc_mp_ctx_t* vlc_mp_ctx = (vlc_mp_ctx_t*)luaL_checkudata(L, 1, LIBVLC_MP_MT);
	scale = libvlc_video_get_scale(vlc_mp_ctx->vlc_mp);
	if ((r = catch_error(L))) return r;
	lua_pushnumber(L, scale);
	return 1;
}

static int vlc_mp_video_set_scale(lua_State* L)
{
	int r;
	float scale;
	vlc_mp_ctx_t* vlc_mp_ctx = (vlc_mp_ctx_t*)luaL_checkudata(L, 1, LIBVLC_MP_MT);
	scale = (float)luaL_checknumber(L, 2);
	libvlc_video_set_scale(vlc_mp_ctx->vlc_mp, scale);
	if ((r = catch_error(L))) return r;
	lua_pushboolean(L, 1);
	return 1;
}

static int vlc_mp_video_get_aspect_ratio(lua_State* L)
{
	int r;
	const char* aspect_ratio;
	vlc_mp_ctx_t* vlc_mp_ctx = (vlc_mp_ctx_t*)luaL_checkudata(L, 1, LIBVLC_MP_MT);
	aspect_ratio = libvlc_video_get_aspect_ratio(vlc_mp_ctx->vlc_mp);
	if ((r = catch_error(L))) return r;
	lua_pushstring(L, aspect_ratio);
	return 1;
}

static int vlc_mp_video_set_aspect_ratio(lua_State* L)
{
	int r;
	const char* aspect_ratio;
	vlc_mp_ctx_t* vlc_mp_ctx = (vlc_mp_ctx_t*)luaL_checkudata(L, 1, LIBVLC_MP_MT);
	aspect_ratio = luaL_checkstring(L, 2);
	libvlc_video_set_aspect_ratio(vlc_mp_ctx->vlc_mp, (char*)aspect_ratio);
	if ((r = catch_error(L))) return r;
	lua_pushboolean(L, 1);
	return 1;
}

static int vlc_display_opengl(lua_State* L)
{
	vlc_ctx_t* vlc_ctx = (vlc_ctx_t*)luaL_checkudata(L, 1, LIBVLC_MT);
	pthread_mutex_lock(&(vlc_ctx->mutex));
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vlc_ctx->width, vlc_ctx->height, GL_RGBA, GL_UNSIGNED_BYTE, vlc_ctx->pix_buffer[vlc_ctx->ridx]);
	pthread_mutex_unlock(&(vlc_ctx->mutex));

	glBegin(GL_QUADS);
		glTexCoord2f(0.0, 0.0); glVertex2f(-1.0, 1.0);
		glTexCoord2f(1.0, 0.0); glVertex2f(1.0, 1.0);
		glTexCoord2f(1.0, 1.0); glVertex2f(1.0, -1.0);
		glTexCoord2f(0.0, 1.0); glVertex2f(-1.0, -1.0);
	glEnd();
	return 0;
}

static const luaL_reg vlc_funcs[] = {
	{"new", vlc_new},
	{"get_version", vlc_get_version},
	{"get_compiler", vlc_get_compiler},
	{NULL, NULL},
};

static const luaL_reg vlc_meths[] = {
	{"__gc", vlc_destroy},
	{"open", vlc_open},
	{"wait", vlc_wait},
	{"get_video_frame_size", vlc_get_video_frame_size},
	{"has_video_frame", vlc_has_video_frame},
	{"get_video_frame", vlc_get_video_frame},
	{"next_video_frame", vlc_next_video_frame},
	{"wait_video_frame", vlc_wait_video_frame},
	{"display_opengl", vlc_display_opengl},
	{NULL, NULL},
};

static const luaL_reg vlc_mp_meths[] = {
	{"__gc", vlc_mp_destroy},
	{"play", vlc_mp_play},
	{"is_playing", vlc_mp_is_playing},
	{"pause", vlc_mp_pause},
	{"stop", vlc_mp_stop},
	{"get_state", vlc_mp_get_state},
	{"get_duration", vlc_mp_get_duration},
	{"get_length", vlc_mp_get_length},
	{"get_time", vlc_mp_get_time},
	{"set_time", vlc_mp_set_time},
	{"get_position", vlc_mp_get_position},
	{"set_position", vlc_mp_set_position},
	{"is_parsed", vlc_mp_is_parsed},
	{"get_meta", vlc_mp_get_meta},
	{"get_fps", vlc_mp_get_fps},
	{"is_seekable", vlc_mp_is_seekable},
	{"can_pause", vlc_mp_can_pause},
	{"get_scale", vlc_mp_video_get_scale},
	{"set_scale", vlc_mp_video_set_scale},
	{"get_aspect_ratio", vlc_mp_video_get_aspect_ratio},
	{"set_aspect_ratio", vlc_mp_video_set_aspect_ratio},
	{NULL, NULL},
};

LUAVLC_API int luaopen_luavlc (lua_State *L) {
	createmeta(L, LIBVLC_MP_MT);
	luaL_openlib(L, 0, vlc_mp_meths, 0);

	createmeta(L, LIBVLC_MT);
	luaL_openlib(L, 0, vlc_meths, 0);

	luaL_openlib(L, "vlc", vlc_funcs, 0);
	return 1;
}

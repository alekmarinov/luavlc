/*********************************************************************/
/*                                                                   */
/* Copyright (C) 2010,  AVIQ Bulgaria Ltd                            */
/*                                                                   */
/* Project:       LRun                                               */
/* Filename:      vlcwrp.h                                           */
/* Description:   VLC wrapper library                                */
/*                                                                   */
/*********************************************************************/ 

#ifndef __VLCWRP_H
#define __VLCWRP_H

#ifdef _WIN32
  #include <windows.h>
  #ifdef VLCWRP_BUILD
    #ifdef VLCWRP_DLL
      #define VLCWRP_API __declspec(dllexport)
    #endif
  #else
    #ifdef VLCWRP_DLL
      #define VLCWRP_API __declspec(dllimport)
    #endif
  #endif
#else
  #ifdef VLCWRP_BUILD
    #define VLCWRP_API extern
  #endif
#endif

#ifndef VLCWRP_API
#define VLCWRP_API
#endif

/**
 * VLC wrapper context type
 */
struct vlcwrp_ctx_t;

/**
 * VLC status enumeration
 */
typedef enum {
	VLC_OPENING,
	VLC_BUFFERING,
	VLC_PLAYING,
	VLC_PAUSED,
	VLC_STOPPED,
	VLC_ENDED,
	VLC_ERROR
} vlc_state_t;

/* get last VLC error message and clear the message, returns NULL if no error */
VLCWRP_API const char* vlcwrp_error(void);

/**
 * create VLC player instance
 * argc number of string arguments to be passed to the VLC player instance
 * argv string array of size argc to be passed to the VLC player instance
 * width defines the frame width to be used to decode frames, must be 4-aligned
 * height defines the frame height
 *
 * the pixel format is always 4 bytes - RGBA
 */
VLCWRP_API struct vlcwrp_ctx_t *vlcwrp_create(int argc, const char * const* argv, int width, int height);

/** destroy VLC player instance */
VLCWRP_API void vlcwrp_destroy(struct vlcwrp_ctx_t* ctx);

/** get playback status */
VLCWRP_API vlc_state_t vlcwrp_get_state(struct vlcwrp_ctx_t* ctx);

/** play media url. The url can be NULL if last played URL has not changed */
VLCWRP_API void vlcwrp_play(struct vlcwrp_ctx_t* ctx, const char* url);

/** stop playing */
VLCWRP_API void vlcwrp_stop(struct vlcwrp_ctx_t* ctx);

/** toggle pause/play */
VLCWRP_API void vlcwrp_pause(struct vlcwrp_ctx_t* ctx);

/**
 * acquire new decoded frame
 * returns NULL if there is no new frame since the last call
 */
VLCWRP_API void* vlcwrp_frame_acquire(struct vlcwrp_ctx_t* ctx);

/** release acquired frame
 * the function must be called if vlcwrp_frame_acquire returned non NULL frame
 * and the caller finished processing the returned frame
 * if the previous call to vlcwrp_frame_acquire returned NULL
 * calling this function has no effect
 */
VLCWRP_API void vlcwrp_frame_release(struct vlcwrp_ctx_t* ctx);

#endif

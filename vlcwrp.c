/*********************************************************************/
/*                                                                   */
/* Copyright (C) 2010,  AVIQ Bulgaria Ltd                            */
/*                                                                   */
/* Project:       LRun                                               */
/* Filename:      vlcwrp.c                                           */
/* Description:   VLC wrapper library                                */
/*                                                                   */
/*********************************************************************/ 

#include <stdlib.h>
#include <stdio.h>
#include <GL/gl.h>
#include <vlc/vlc.h>
#include <pthread.h>
#include <errno.h>

#include "vlcwrp.h"

#define BPP 4
#define CHROMA "RV32"

/* must be >= 3 */
#define QUEUE_SIZE 3

/* forward references to VLC media player callbacks */
static void *lockcb(void *, void **);
static void unlockcb(void *, void *, void *const *);
static void displaycb(void *, void *);

#define log
//printf

/**
 * VLC wrapper context
 */
struct vlcwrp_ctx_t
{
	/* VLC instance */
    libvlc_instance_t *libvlc;

	/* VLC media player instance */
    libvlc_media_player_t *mp;

	/* queue of frames */
	unsigned char* frame_queue[QUEUE_SIZE];

	/* frame width */
	int width;

	/* frame height */
	int height;

	/* number of decoded frames queued */
	int nqueuedframes;

	/* read and write queue indecies */
	int ridx, widx;

	/* condition the queue is empty */
	pthread_cond_t* cond_empty;

	/* condition the queue is full */
	pthread_cond_t* cond_full;

	/* synchronizing mutex */
	pthread_mutex_t* mutex;

	/* flag indicating request stopping */
	int requested_stop;
};

/* get last VLC error message and clear the message, returns NULL if no error */
const char* vlcwrp_error()
{
	const char* errmsg = libvlc_errmsg();
	libvlc_clearerr();
	return errmsg;
}

/* create VLC player instance */
struct vlcwrp_ctx_t *vlcwrp_create(int argc, const char * const* argv, int width, int height)
{
	int i;

	/* allocates context */
	struct vlcwrp_ctx_t* ctx = (struct vlcwrp_ctx_t*)malloc(sizeof(struct vlcwrp_ctx_t));

	if (!ctx)
	{
		return NULL;
	}
	
	/* creates new VLC instance */
	ctx->libvlc = libvlc_new(argc, argv);
	if (!ctx->libvlc)
	{
		vlcwrp_destroy(ctx);
		return NULL;
	}

	/* creates new VLC media player instance */
	ctx->mp = libvlc_media_player_new(ctx->libvlc);
	if (!ctx->mp)
	{
		vlcwrp_destroy(ctx);
		return NULL;
	}

	/* sets media player callbacks and desired frame format */
	libvlc_video_set_callbacks(ctx->mp, lockcb, unlockcb, displaycb, ctx);
	libvlc_video_set_format(ctx->mp, CHROMA, width, height, width*BPP);

	/* creates mutex */
	ctx->mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
	if (!ctx->mutex)
	{
		vlcwrp_destroy(ctx);
		return NULL;
	}
	pthread_mutex_init(ctx->mutex, 0);

	/* creates empty condition */
	ctx->cond_empty = (pthread_cond_t*)malloc(sizeof(pthread_cond_t));
	if (!ctx->cond_empty)
	{
		vlcwrp_destroy(ctx);
		return NULL;
	}
	pthread_cond_init(ctx->cond_empty, 0);

	/* creates full condition */
	ctx->cond_full = (pthread_cond_t*)malloc(sizeof(pthread_cond_t));
	if (!ctx->cond_full)
	{
		vlcwrp_destroy(ctx);
		return NULL;
	}
	pthread_cond_init(ctx->cond_full, 0);

	/* initialize queue data */
	ctx->width = width;
	ctx->height = height;
	for (i=0; i<QUEUE_SIZE; i++)
	{
		ctx->frame_queue[i] = (unsigned char*)malloc(width * height * BPP);
		if (!ctx->frame_queue[i])
		{
			vlcwrp_destroy(ctx);
			return NULL;
		}
	}
	ctx->nqueuedframes = ctx->ridx = ctx->widx = 0;
	return ctx;
}

/* destroy VLC player instance */
void vlcwrp_destroy(struct vlcwrp_ctx_t* ctx)
{
	int i;

	/* discard frames queue */
	for (i=0; i<QUEUE_SIZE; i++)
		if (ctx->frame_queue[i])
			free(ctx->frame_queue[i]);
	
	/* discard conditions and mutex  */
	if (ctx->cond_full)
	  pthread_cond_destroy(ctx->cond_full);
	if (ctx->cond_empty)
	  pthread_cond_destroy(ctx->cond_empty);
	if (ctx->mutex)
	  pthread_mutex_destroy(ctx->mutex);

	/* discard media player object */
	if (ctx->mp)
		libvlc_media_player_release(ctx->mp);

	/* discard VLC instance */
	if (ctx->libvlc)
		libvlc_release(ctx->libvlc);

	/* discard context */
	free(ctx);
}

/* get playback state */
vlc_state_t vlcwrp_get_state(struct vlcwrp_ctx_t* ctx)
{
	switch (libvlc_media_player_get_state(ctx->mp))
	{
	case libvlc_Opening: return VLC_OPENING;
	case libvlc_Buffering: return VLC_BUFFERING;
	case libvlc_Playing: return VLC_PLAYING;
	case libvlc_Paused: return VLC_PAUSED;
	case libvlc_Stopped: return VLC_STOPPED;
	case libvlc_Error: return VLC_ERROR;
	default: return VLC_ENDED; /* libvlc_NothingSpecial, libvlc_Ended */
	}
}

/* play media url */
void vlcwrp_play(struct vlcwrp_ctx_t* ctx, const char* url)
{
	/* stop before start playing */
	vlcwrp_stop(ctx);

	if (url)
	{
		libvlc_media_t *m = libvlc_media_new_path(ctx->libvlc, url);
		if (m)
		{
			libvlc_media_player_set_media(ctx->mp, m);
		}
		else
		{
			/* error occurred */
			return ;
		}
	}
	log("vlcwrp_play\n");
	pthread_mutex_lock(ctx->mutex);
	ctx->nqueuedframes = ctx->ridx = ctx->widx = ctx->requested_stop = 0;
	pthread_mutex_unlock(ctx->mutex);
	libvlc_media_player_play(ctx->mp);
}

/* stop playing */
void vlcwrp_stop(struct vlcwrp_ctx_t* ctx)
{
	log("vlcwrp_stop\n");
	/* enable flag we want stop */
	int err_lock = pthread_mutex_trylock(ctx->mutex);

	/* signal on both conditions */
	log("signal both conditions err_lock=%d\n", err_lock);
	ctx->requested_stop = 1;
	pthread_cond_signal(ctx->cond_empty);
	pthread_cond_signal(ctx->cond_full);

	if (err_lock == 0)
	{
		pthread_mutex_unlock(ctx->mutex);
	}

	log("do real stop\n");
	libvlc_media_player_stop(ctx->mp);
}

/* toggle pause/play */
void vlcwrp_pause(struct vlcwrp_ctx_t* ctx)
{
	libvlc_media_player_pause(ctx->mp);
}

/*
 * acquire new decoded frame,
 * return NULL if there is no new frame since the last call
 */
void* vlcwrp_frame_acquire(struct vlcwrp_ctx_t* ctx)
{
	void *pframe = NULL;
	
	log("vlcwrp_frame_acquire lock\n");
	pthread_mutex_lock(ctx->mutex);
	if (ctx->nqueuedframes > 0)
	{
		/* get frame from read index */
		log("vlcwrp_frame_acquire ridx=%d\n", ctx->ridx);
		pframe = ctx->frame_queue[ctx->ridx];
	}
	else
	{
		pthread_mutex_unlock(ctx->mutex);
	}
	return pframe;
}

/* release acquired frame
 * the function must be called if vlcwrp_frame_acquire returned non NULL frame
 * and the caller finished processing the returned frame
 * if the previous call to vlcwrp_frame_acquire returned NULL
 * calling this function has no effect
 */
void vlcwrp_frame_release(struct vlcwrp_ctx_t* ctx)
{
	log("vlcwrp_frame_release qf=%d\n", ctx->nqueuedframes);
	if (ctx->nqueuedframes > 0)
	{
		/* advance the read index */
		ctx->ridx = (ctx->ridx + 1) % QUEUE_SIZE;

		/* decrement queued frames number */
		ctx->nqueuedframes--;

		/* signal the queue is not full */
		pthread_cond_signal(ctx->cond_empty);
		pthread_mutex_unlock(ctx->mutex);
	}
}

/* VLC media player callbacks */

/* lockcb called when VLC wants buffer to decode new video frame */
static void *lockcb(void *opaque, void **p_pixels)
{
	struct vlcwrp_ctx_t *ctx = (struct vlcwrp_ctx_t *)opaque;
	log("lockcb lock stop=%d, qf=%d\n", ctx->requested_stop, ctx->nqueuedframes);
	pthread_mutex_lock(ctx->mutex);
	while (!ctx->requested_stop && ctx->nqueuedframes == QUEUE_SIZE)
	{
		printf("waiting on cond_empty\n");
		pthread_cond_wait(ctx->cond_empty, ctx->mutex);
		printf("cond_empty signaled stop=%d qf=%d\n", ctx->requested_stop, ctx->nqueuedframes);
	}
	/* get buffer from queue at the current write index */
	*p_pixels = ctx->frame_queue[ctx->widx];
	log("lockcb lock\n");
	pthread_mutex_unlock(ctx->mutex);
	return NULL;
}

/* unlockcb called when VLC finished decoding new video frame */
static void unlockcb(void *opaque, void *id, void *const *p_pixels)
{
	struct vlcwrp_ctx_t *ctx = (struct vlcwrp_ctx_t *)opaque;
	log("unlockcb lock\n");
	pthread_mutex_lock(ctx->mutex);

	/* advance queue write index */
	ctx->widx = (ctx->widx + 1) % QUEUE_SIZE;

	/* increment queued frames number */
	if (ctx->nqueuedframes < QUEUE_SIZE)
		ctx->nqueuedframes++;

	/* signal the queue is not empty */
	pthread_cond_signal(ctx->cond_full);
	log("unlockcb signal cond_full qf=%d widx=%d\n", ctx->nqueuedframes, ctx->widx);
	pthread_mutex_unlock(ctx->mutex);
}

/* displaycb called by VLC at the time ready to display a frame */
static void displaycb(void *opaque, void *id)
{
}

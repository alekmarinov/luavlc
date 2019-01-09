# lib/player/vlcwrp/vlcwrp.mak

TARGET=vlcwrp
VERSION=1.0
OBJS=vlcwrp.o
EXTRA_DEFS=-DVLCWRP_BUILD -Wno-long-long -std=c99
EXTRA_INCS=$(shell pkg-config libvlc --cflags)
EXTRA_LIBS=$(shell pkg-config libvlc --libs) -lpthread-2
INCLUDES=vlcwrp.h

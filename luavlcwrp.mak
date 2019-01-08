# modules/c/media/player/luavlc/luavlcwrp.mak

TARGET=vlcwrp
VERSION=1.0
OBJS=luavlcwrp.o
EXTRA_INCS="-I$(LRUN)/lib/player/vlcwrp"
EXTRA_LIBS="-L$(LRUN)/lib/player/vlcwrp" -lvlcwrp-1.0

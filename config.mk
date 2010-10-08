# dvtm version
VERSION = 0.6

# Customize below to fit your system

PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man

INCS = -I. -I/usr/include -I/usr/local/include 
LIBS = -lc -lutil -lncursesw
#LIBS = -lc -lutil -lncurses

CFLAGS += -std=c99 -Os ${INCS} -DVERSION=\"${VERSION}\" -DNDEBUG
LDFLAGS += -L/usr/lib -L/usr/local/lib ${LIBS}

# Mouse handling
CFLAGS += -DCONFIG_MOUSE
#CFLAGS += -DCONFIG_CMDFIFO
CFLAGS += -DCONFIG_STATUSBAR

DEBUG_CFLAGS = ${CFLAGS} -UNDEBUG -O0 -g -ggdb -Wall

CC = cc

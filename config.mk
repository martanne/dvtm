# dvtm version
VERSION = 0.3

# Customize below to fit your system

PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man

INCS = -I. -I/usr/include -I/usr/local/include 
LIBS = -lc -lutil -lncurses
LIBS_UTF8 = -lc -lutil -lncursesw

CFLAGS = -std=c99 -Os ${INCS} -DVERSION=\"${VERSION}\" -DNDEBUG
LDFLAGS = -L/usr/lib -L/usr/local/lib ${LIBS}

DEBUG_CFLAGS = -std=c99 -O0 -g -ggdb ${INCS} -Wall -DVERSION=\"${VERSION}\" -DDEBUG 

CC = cc

# dvtm version
VERSION = 0.1

# Customize below to fit your system

PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man

INCS = -I. -I/usr/include -I/usr/local/include 
LIBS = -lc -lcurses -lrote
LIBS_UTF8 = -lc -lncursesw -lrotew

CFLAGS = -Os ${INCS} -DVERSION=\"${VERSION}\"
LDFLAGS = -L/usr/lib -L/usr/local/lib ${LIBS}

DEBUG_CFLAGS = -O0 -g -ggdb ${INCS} -Wall -DVERSION=\"${VERSION}\" -DDEBUG

CC = cc

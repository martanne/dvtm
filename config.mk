# dvtm version
VERSION = 0.01

# Customize below to fit your system

PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man

INCS = -I. -I/usr/include -I/usr/local/include 
LIBS = -L/usr/lib -L/usr/local/lib -lc -lcurses -lrote

CFLAGS = -O0 -g -ggdb ${INCS} -Wall -DVERSION=\"${VERSION}\" -DNDEBUG
LDFLAGS = ${LIBS}

CC = cc

# dvtm version
VERSION = 0.11

# Customize below to fit your system

PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man

INCS = -I. -I/usr/include -I/usr/local/include
LIBS = -lc -lutil -lncursesw
# NetBSD
#LIBS = -lc -lutil -lcurses
# AIX
#LIBS = -lc -lncursesw
# Cygwin
#INCS += -I/usr/include/ncurses

CFLAGS += -std=c99 -Os ${INCS} -DVERSION=\"${VERSION}\" -DNDEBUG
LDFLAGS += -L/usr/lib -L/usr/local/lib ${LIBS}

DEBUG_CFLAGS = ${CFLAGS} -UNDEBUG -O0 -g -ggdb -Wall -Wextra -Wno-missing-field-initializers -Wno-unused-parameter

CC = cc

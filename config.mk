# dvtm version
VERSION = 0.12

# Customize below to fit your system

PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man
# specify your systems terminfo directory
# leave empty to install into your home folder
TERMINFO := ${DESTDIR}${PREFIX}/share/terminfo

INCS = -I.
LIBS = -lc -lutil -lncursesw
# NetBSD
#LIBS = -lc -lutil -lcurses
# AIX
#LIBS = -lc -lncursesw
# Solaris
#INCS = -I/usr/include/ncurses
#LIBS = -lc -lncursesw
# Cygwin
#INCS = -I/usr/include/ncurses

CFLAGS += -std=c99 -Os ${INCS} -DVERSION=\"${VERSION}\" -DNDEBUG -D_GNU_SOURCE
LDFLAGS += ${LIBS}

DEBUG_CFLAGS = ${CFLAGS} -UNDEBUG -O0 -g -ggdb -Wall -Wextra -Wno-missing-field-initializers -Wno-unused-parameter

CC = cc
STRIP = strip

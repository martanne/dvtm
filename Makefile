include config.mk

SRC = dvtm.c vt.c
OBJ = ${SRC:.c=.o}

all: clean options dvtm

options:
	@echo dvtm build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

config.h:
	cp config.def.h config.h

.c.o:
	@echo CC $<
	@${CC} -c ${CFLAGS} $<

${OBJ}: config.h config.mk

dvtm: ${OBJ}
	@echo CC -o $@
	@${CC} -o $@ ${OBJ} ${LDFLAGS}

debug: clean
	@make CFLAGS='${DEBUG_CFLAGS}'

clean:
	@echo cleaning
	@rm -f dvtm ${OBJ} dvtm-${VERSION}.tar.gz

dist: clean
	@echo creating dist tarball
	@mkdir -p dvtm-${VERSION}
	@cp -R LICENSE Makefile README.md testsuite.sh config.def.h config.mk \
		${SRC} vt.h forkpty-aix.c forkpty-sunos.c tile.c bstack.c \
		tstack.c vstack.c grid.c fullscreen.c fibonacci.c \
		dvtm-status dvtm.info dvtm.1 dvtm-${VERSION}
	@tar -cf dvtm-${VERSION}.tar dvtm-${VERSION}
	@gzip dvtm-${VERSION}.tar
	@rm -rf dvtm-${VERSION}

install: dvtm
	@echo stripping executable
	@${STRIP} dvtm
	@echo installing executable file to ${DESTDIR}${PREFIX}/bin
	@mkdir -p ${DESTDIR}${PREFIX}/bin
	@cp -f dvtm ${DESTDIR}${PREFIX}/bin
	@chmod 755 ${DESTDIR}${PREFIX}/bin/dvtm
	@cp -f dvtm-status ${DESTDIR}${PREFIX}/bin
	@chmod 755 ${DESTDIR}${PREFIX}/bin/dvtm-status
	@echo installing manual page to ${DESTDIR}${MANPREFIX}/man1
	@mkdir -p ${DESTDIR}${MANPREFIX}/man1
	@sed "s/VERSION/${VERSION}/g" < dvtm.1 > ${DESTDIR}${MANPREFIX}/man1/dvtm.1
	@chmod 644 ${DESTDIR}${MANPREFIX}/man1/dvtm.1
	@echo installing terminfo description
	@TERMINFO=${TERMINFO} tic -s dvtm.info

uninstall:
	@echo removing executable file from ${DESTDIR}${PREFIX}/bin
	@rm -f ${DESTDIR}${PREFIX}/bin/dvtm
	@rm -f ${DESTDIR}${PREFIX}/bin/dvtm-status
	@echo removing manual page from ${DESTDIR}${MANPREFIX}/man1
	@rm -f ${DESTDIR}${MANPREFIX}/man1/dvtm.1

.PHONY: all options clean dist install uninstall debug

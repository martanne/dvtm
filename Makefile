include config.mk

SRC += dvtm.c madtty.c
OBJ = ${SRC:.c=.o}

all: clean options dvtm

options:
	@echo dvtm build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

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
	@cp -R LICENSE Makefile README config.h config.mk \
		${SRC} tile.c bstack.c tstack.c grid.c fullscreen.c \
		madtty.h statusbar.c mouse.c cmdfifo.c \
		dvtm-status dvtm.1 dvtm-${VERSION}
	@tar -cf dvtm-${VERSION}.tar dvtm-${VERSION}
	@gzip dvtm-${VERSION}.tar
	@rm -rf dvtm-${VERSION}

install: dvtm
	@echo stripping executable
	@strip -s dvtm
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

uninstall:
	@echo removing executable file from ${DESTDIR}${PREFIX}/bin
	@rm -f ${DESTDIR}${PREFIX}/bin/dvtm
	@echo removing manual page from ${DESTDIR}${MANPREFIX}/man1
	@rm -f ${DESTDIR}${MANPREFIX}/man1/dvtm.1

.PHONY: all options clean dist install uninstall debug

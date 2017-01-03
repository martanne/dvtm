include config.mk

SRC = dvtm.c vt.c
BIN = dvtm dvtm-status dvtm-editor dvtm-pager

VERSION = $(shell git describe --always --dirty 2>/dev/null || echo "0.15-git")
CFLAGS += -DVERSION=\"${VERSION}\"
DEBUG_CFLAGS = ${CFLAGS} -UNDEBUG -O0 -g -ggdb -Wall -Wextra -Wno-unused-parameter

all: dvtm dvtm-editor

config.h:
	cp config.def.h config.h

dvtm: config.h config.mk *.c *.h
	${CC} ${CFLAGS} ${SRC} ${LDFLAGS} ${LIBS} -o $@

dvtm-editor: dvtm-editor.c
	${CC} ${CFLAGS} $^ ${LDFLAGS} -o $@

debug: clean
	@$(MAKE) CFLAGS='${DEBUG_CFLAGS}'

clean:
	@echo cleaning
	@rm -f dvtm
	@rm -f dvtm-editor

dist: clean
	@echo creating dist tarball
	@git archive --prefix=dvtm-${VERSION}/ -o dvtm-${VERSION}.tar.gz HEAD

install: all
	@mkdir -p ${DESTDIR}${PREFIX}/bin
	@for b in ${BIN}; do \
		echo "installing ${DESTDIR}${PREFIX}/bin/$$b"; \
		cp -f "$$b" "${DESTDIR}${PREFIX}/bin" && \
		chmod 755 "${DESTDIR}${PREFIX}/bin/$$b"; \
	done
	@echo installing manual page to ${DESTDIR}${MANPREFIX}/man1
	@mkdir -p ${DESTDIR}${MANPREFIX}/man1
	@sed "s/VERSION/${VERSION}/g" < dvtm.1 > ${DESTDIR}${MANPREFIX}/man1/dvtm.1
	@chmod 644 ${DESTDIR}${MANPREFIX}/man1/dvtm.1
	@echo installing terminfo description
	@TERMINFO=${TERMINFO} tic -s dvtm.info

uninstall:
	@for b in ${BIN}; do \
		echo "removing ${DESTDIR}${PREFIX}/bin/$$b"; \
		rm -f "${DESTDIR}${PREFIX}/bin/$$b"; \
	done
	@echo removing manual page from ${DESTDIR}${MANPREFIX}/man1
	@rm -f ${DESTDIR}${MANPREFIX}/man1/dvtm.1

.PHONY: all clean dist install uninstall debug

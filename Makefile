# openbar - an OpenBSD status bar for cwm(1) and other X11 window managers.
#
# Requires the OpenBSD comp set and Xenocara xbase (libX11, libXft,
# fontconfig, freetype) plus libtls from base.

.SUFFIXES: .c .o

CC ?= clang
CFLAGS ?= -O2 -pipe
CFLAGS += -std=c17 -Wall -Wextra -Wpedantic
CPPFLAGS += -I/usr/X11R6/include -I/usr/X11R6/include/freetype2
LDLIBS += -L/usr/X11R6/lib -lX11 -lXft -lXrender -lfontconfig -lfreetype
LDLIBS += -ltls

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
MANDIR ?= $(PREFIX)/man

PROG = openbar
SRCS = openbar.c config.c fmt.c ipc.c widgets.c net.c
OBJS = ${SRCS:.c=.o}

# The portable units (config.c, fmt.c, ipc.c) have host tests; they
# depend on nothing outside libc.  The feature-test macros and the
# strtonum shim exist only for non-OpenBSD test hosts; OpenBSD libc
# declares everything by default.
TEST_CPPFLAGS = -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
TEST_SRCS = tests/test_config.c tests/test_fmt.c tests/test_ipc.c
TEST_BINS = ${TEST_SRCS:.c=}
TEST_OBJS = tests/config.o tests/fmt.o tests/ipc.o tests/test_support.o

all: ${PROG}

${PROG}: ${OBJS}
	${CC} ${LDFLAGS} -o $@ ${OBJS} ${LDLIBS}

.c.o: openbar.h
	${CC} ${CFLAGS} ${CPPFLAGS} -c -o $@ $<

install: ${PROG}
	install -d ${DESTDIR}${BINDIR} ${DESTDIR}${MANDIR}/man1 \
	    ${DESTDIR}${MANDIR}/man5
	install -m 755 ${PROG} ${DESTDIR}${BINDIR}/${PROG}
	install -m 644 openbar.1 ${DESTDIR}${MANDIR}/man1/openbar.1
	install -m 644 openbarrc.5 ${DESTDIR}${MANDIR}/man5/openbarrc.5

# Install the example configuration as /etc/openbarrc, never clobbering
# an existing file.
install-conf:
	test ! -e ${DESTDIR}/etc/openbarrc || \
	    { echo "/etc/openbarrc exists; not overwritten" >&2; exit 1; }
	install -d ${DESTDIR}/etc
	install -m 644 openbar.conf ${DESTDIR}/etc/openbarrc

uninstall:
	rm -f ${DESTDIR}${BINDIR}/${PROG}
	rm -f ${DESTDIR}${MANDIR}/man1/openbar.1 \
	    ${DESTDIR}${MANDIR}/man5/openbarrc.5

tests/config.o: config.c openbar.h
	${CC} ${CFLAGS} ${CPPFLAGS} ${TEST_CPPFLAGS} \
	    -include tests/strtonum.h -c -o $@ config.c

tests/fmt.o: fmt.c openbar.h
	${CC} ${CFLAGS} ${CPPFLAGS} ${TEST_CPPFLAGS} \
	    -include tests/strtonum.h -c -o $@ fmt.c

tests/ipc.o: ipc.c openbar.h
	${CC} ${CFLAGS} ${CPPFLAGS} ${TEST_CPPFLAGS} \
	    -include tests/strtonum.h -c -o $@ ipc.c

tests/test_support.o: tests/test_support.c
	${CC} ${CFLAGS} ${CPPFLAGS} ${TEST_CPPFLAGS} -c -o $@ tests/test_support.c

${TEST_BINS}: ${TEST_OBJS} openbar.h tests/test.h
	${CC} ${CFLAGS} ${CPPFLAGS} ${TEST_CPPFLAGS} -o $@ $@.c ${TEST_OBJS}

test: ${TEST_BINS}
	@for t in ${TEST_BINS}; do echo "==> $$t"; ./$$t || exit 1; done

clean:
	rm -f ${PROG} ${OBJS} ${TEST_BINS} ${TEST_OBJS}

.PHONY: all install install-conf uninstall test clean
.POSIX:

# Compiler and flags
CC ?= cc
LIBS = -lX11
OPTFLAGS = -O3
DBGFLAGS = -O0 -g
CFLAGS = -pipe -Wall -Werror -march=native
INCLUDEDIR = -I.

# Targets
TARGET = openbar
CONFIG = openbar.conf
BINDIR = /usr/local/bin
CONFIGDIR = ${HOME}
MANDIR = /usr/local/man
INSTALLTARGET = ${BINDIR}/${TARGET}
INSTALLCONFIG = ${CONFIGDIR}/${CONFIG}
MAN1 = ${MANDIR}/man1
MAN5 = ${MANDIR}/man5

# Default target
all: build

# Build target with debugging flags
build: clean
	${CC} ${DBGFLAGS} ${CFLAGS} ${INCLUDEDIR} -o ${TARGET} ${LIBS} openbar.c

# Build target with optimization flags
opt: clean
	${CC} ${OPTFLAGS} ${CFLAGS} ${INCLUDEDIR} -o ${TARGET} ${LIBS} openbar.c

# Install target
install: ${TARGET}
	mkdir -p ${BINDIR}
	install -s ${TARGET} ${INSTALLTARGET}
	install -m 644 ${CONFIG} ${INSTALLCONFIG}
	mkdir -p ${MAN1}
	mkdir -p ${MAN5}
	install -m 644 openbar.1 ${MAN1}/openbar.1
	install -m 644 openbar.conf.5 ${MAN5}/openbar.conf.5

# Clean target
clean:
	rm -f ${TARGET}

# Uninstall target
uninstall:
	rm -f ${INSTALLTARGET}
	rm -f ${INSTALLCONFIG}
	rm -f ${MAN1}/openbar.1
	rm -f ${MAN5}/openbar.conf.5

# Debug target
debug: build
	egdb -q ./${TARGET} -ex "break main" -ex "run"

# Compiler and flags
CC ?= cc
LIBS = -L/usr/X11R6/lib -lX11
OPTFLAGS = -O3
DBGFLAGS = -O0 -g
CFLAGS = -pipe -Wall -Werror -march=native
INCLUDEDIR = -I/usr/X11R6/include -I.

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

# Default target to build the project
all: build

# Build target with debugging flags
.PHONY: build
build: clean
	@echo "Building ${TARGET} with debugging flags..."
	@${CC} ${DBGFLAGS} ${CFLAGS} ${INCLUDEDIR} -o ${TARGET} openbar.c ${LIBS}

# Build target with optimization flags
.PHONY: opt
opt: clean
	@echo "Building ${TARGET} with optimization flags..."
	@${CC} ${OPTFLAGS} ${CFLAGS} ${INCLUDEDIR} -o ${TARGET} openbar.c ${LIBS}

# Install target to copy the executable, config, and man pages to appropriate directories
.PHONY: install
install: ${TARGET}
	@echo "Installing ${TARGET} to ${INSTALLTARGET} and ${CONFIG} to ${INSTALLCONFIG}..."
	@mkdir -p ${BINDIR}
	@install -s ${TARGET} ${INSTALLTARGET}
	@install -m 644 ${CONFIG} ${INSTALLCONFIG}
	@echo "Installing man pages to ${MAN1} and ${MAN5}..."
	@mkdir -p ${MAN1}
	@mkdir -p ${MAN5}
	@install -m 644 openbar.1 ${MAN1}/openbar.1
	@install -m 644 openbar.conf.5 ${MAN5}/openbar.conf.5

# Clean target to remove build artifacts
.PHONY: clean
clean:
	@echo "Cleaning up..."
	@rm -f ${TARGET}

# Uninstall target to remove the installed files
.PHONY: uninstall
uninstall:
	@echo "Uninstalling ${INSTALLTARGET} and ${INSTALLCONFIG}..."
	@rm -f ${INSTALLTARGET}
	@rm -f ${INSTALLCONFIG}
	@echo "Removing man pages from ${MAN1} and ${MAN5}..."
	@rm -f ${MAN1}/openbar.1
	@rm -f ${MAN5}/openbar.conf.5

# Debug target to run the program in a debugger
.PHONY: debug
debug: build
	@echo "Starting debugger for ${TARGET}..."
	@egdb -q ./${TARGET} -ex "break main" -ex "run"
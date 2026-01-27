# Compiler and flags
CC ?= cc
LIBS = -L/usr/X11R6/lib -lX11
OPTFLAGS = -O3
DBGFLAGS = -O0 -g
CFLAGS = -pipe -Wall -Werror -march=native -std=c11
INCLUDEDIR = -I/usr/X11R6/include -I.
INFO = ==>

# Targets
TARGET = openbar
CONFIG = openbar.conf
BINDIR = /usr/local/bin
CONFIGDIR = /etc
MANDIR = /usr/local/man
INSTALLTARGET = ${BINDIR}/${TARGET}
INSTALLCONFIG = ${CONFIGDIR}/${CONFIG}
MAN1 = ${MANDIR}/man1
MAN5 = ${MANDIR}/man5

# Default target to build the project
.PHONY: all
all: build

# Build target with debugging flags
.PHONY: build
build: clean
	@echo "${INFO} Building ${TARGET} (debug)"
	@${CC} ${DBGFLAGS} ${CFLAGS} ${INCLUDEDIR} -o ${TARGET} openbar.c ${LIBS}

# Build target with optimization flags
.PHONY: opt
opt: clean
	@echo "${INFO} Building ${TARGET} (opt)"
	@${CC} ${OPTFLAGS} ${CFLAGS} ${INCLUDEDIR} -o ${TARGET} openbar.c ${LIBS}

# Install target to copy the executable, config, and man pages to appropriate directories
.PHONY: install
install: ${TARGET}
	@echo "${INFO} Installing ${TARGET} -> ${INSTALLTARGET}" && mkdir -p ${BINDIR} && install -s ${TARGET} ${INSTALLTARGET}
	@echo "${INFO} Installing ${CONFIG} -> ${INSTALLCONFIG}" && mkdir -p ${CONFIGDIR} && install -m 644 ${CONFIG} ${INSTALLCONFIG}
	@echo "${INFO} Installing man pages -> ${MAN1}/openbar.1 and ${MAN5}/openbar.conf.5" && mkdir -p ${MAN1} ${MAN5} && install -m 644 openbar.1 ${MAN1}/openbar.1 && install -m 644 openbar.conf.5 ${MAN5}/openbar.conf.5 && echo "${INFO} Install complete"

# Clean target to remove build artifacts
.PHONY: clean
clean:
	@echo "${INFO} Cleaning up build artifacts"
	@rm -f ${TARGET}
	@echo "${INFO} Clean complete"

# Uninstall target to remove the installed files
.PHONY: uninstall
uninstall:
	@echo "${INFO} Removing ${INSTALLTARGET}" && rm -f ${INSTALLTARGET}
	@echo "${INFO} Removing ${INSTALLCONFIG}" && rm -f ${INSTALLCONFIG}
	@echo "${INFO} Removing man pages" && rm -f ${MAN1}/openbar.1 ${MAN5}/openbar.conf.5 && echo "${INFO} Uninstall complete"

# Debug target to run the program in a debugger
.PHONY: debug
debug: build
	@echo "${INFO} Starting debugger for ${TARGET}"
	@egdb -q ./${TARGET} -ex "break main" -ex "run"

# Help target to display available commands
.PHONY: help
help:
	@printf "Available targets:\n  all        - Build the project with debugging flags\n  build      - Build the project with debugging flags\n  opt        - Build the project with optimization flags\n  install    - Install the executable, config, and man pages\n  clean      - Remove build artifacts\n  uninstall  - Remove the installed files\n  debug      - Run the program in a debugger\n  test       - Placeholder for tests\n"

.PHONY: test
test:
	@echo "${INFO} No automated tests defined"
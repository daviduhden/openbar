# Compiler and flags
CC = clang
CFLAGS = -O2 -pipe
CFLAGS += -Wall -Wextra -std=c99
CPPFLAGS += -I/usr/X11R6/include -I${SOURCE_DIR}
LDLIBS += -L/usr/X11R6/lib -lX11
OPTFLAGS = -O3
DBGFLAGS = -O0 -g
INFO = ==>

# Targets
TARGET = openbar
CONFIG = openbar.conf
SOURCE_DIR := ${.PARSEDIR}
SOURCE = ${SOURCE_DIR}/openbar.c
BUILD_TARGET = ${SOURCE_DIR}/${TARGET}
CONFIG_SOURCE = ${SOURCE_DIR}/${CONFIG}
MAN1_SOURCE = ${SOURCE_DIR}/openbar.1
MAN5_SOURCE = ${SOURCE_DIR}/openbar.conf.5
BINDIR = /usr/local/bin
CONFIGDIR = /etc
MANDIR = /usr/local/man
INSTALLTARGET = ${BINDIR}/${TARGET}
INSTALLCONFIG = ${CONFIGDIR}/${CONFIG}
MAN1 = ${MANDIR}/man1
MAN5 = ${MANDIR}/man5

# Default target to build the project
.PHONY: all
all: ${BUILD_TARGET}

# Normal build target.
.PHONY: build
build: ${BUILD_TARGET}

${BUILD_TARGET}: ${SOURCE}
	@echo "${INFO} Building ${TARGET}"
	@${CC} ${CFLAGS} ${CPPFLAGS} -o ${BUILD_TARGET} ${SOURCE} ${LDFLAGS} ${LDLIBS}

# Build target with optimization flags
.PHONY: opt
opt: clean
	@echo "${INFO} Building ${TARGET} (opt)"
	@${CC} ${CFLAGS} ${OPTFLAGS} ${CPPFLAGS} -o ${BUILD_TARGET} ${SOURCE} ${LDFLAGS} ${LDLIBS}

.PHONY: debug-build
debug-build: clean
	@echo "${INFO} Building ${TARGET} (debug)"
	@${CC} ${CFLAGS} ${DBGFLAGS} ${CPPFLAGS} -o ${BUILD_TARGET} ${SOURCE} ${LDFLAGS} ${LDLIBS}

# Install target to copy the executable, config, and man pages to appropriate directories
.PHONY: install
install: ${BUILD_TARGET}
	@echo "${INFO} Installing ${TARGET} -> ${INSTALLTARGET}" && mkdir -p ${BINDIR} && install -s ${BUILD_TARGET} ${INSTALLTARGET}
	@echo "${INFO} Installing ${CONFIG} -> ${INSTALLCONFIG}" && mkdir -p ${CONFIGDIR} && install -b -m 644 ${CONFIG_SOURCE} ${INSTALLCONFIG}
	@echo "${INFO} Installing man pages -> ${MAN1}/openbar.1 and ${MAN5}/openbar.conf.5" && mkdir -p ${MAN1} ${MAN5} && install -m 644 ${MAN1_SOURCE} ${MAN1}/openbar.1 && install -m 644 ${MAN5_SOURCE} ${MAN5}/openbar.conf.5 && echo "${INFO} Install complete"

# Clean target to remove build artifacts
.PHONY: clean
clean:
	@echo "${INFO} Cleaning up build artifacts"
	@rm -f ${BUILD_TARGET}
	@echo "${INFO} Clean complete"

# Uninstall target to remove the installed files
.PHONY: uninstall
uninstall:
	@echo "${INFO} Removing ${INSTALLTARGET}" && rm -f ${INSTALLTARGET}
	@echo "${INFO} Removing ${INSTALLCONFIG}" && rm -f ${INSTALLCONFIG}
	@echo "${INFO} Removing man pages" && rm -f ${MAN1}/openbar.1 ${MAN5}/openbar.conf.5 && echo "${INFO} Uninstall complete"

# Debug target to run the program in a debugger
.PHONY: debug
debug: debug-build
	@echo "${INFO} Starting debugger for ${TARGET}"
	@egdb -q ${BUILD_TARGET} -ex "break main" -ex "run"

# Help target to display available commands
.PHONY: help
help:
	@printf "Available targets:\n  all        - Build the project\n  build      - Build the project\n  opt        - Build with -O3\n  install    - Install the executable, config, and man pages\n  clean      - Remove build artifacts\n  uninstall  - Remove the installed files\n  debug      - Build with debug symbols and start egdb\n  test       - Report test availability\n"

.PHONY: test
test:
	@echo "${INFO} No automated tests defined"
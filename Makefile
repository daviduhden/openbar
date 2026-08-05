# Compiler and flags
CC = clang
CFLAGS = -O2 -pipe
CFLAGS += -Wall -Wextra -std=c99
CPPFLAGS += -I/usr/X11R6/include -I${SOURCE_DIR}
LDLIBS += -L/usr/X11R6/lib -lX11 -lXft
OPTFLAGS = -O3
DBGFLAGS = -O0 -g
INFO = ==>

# Targets
TARGET = openbar
SOURCE_DIR := ${.PARSEDIR}
SOURCE = ${SOURCE_DIR}/openbar.c
BUILD_TARGET = ${SOURCE_DIR}/${TARGET}
BINDIR = /usr/local/bin
MANDIR = /usr/local/man
INSTALLTARGET = ${BINDIR}/${TARGET}
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

# Install target to copy the executable and man pages
.PHONY: install
install: ${BUILD_TARGET}
	@echo "${INFO} Installing ${TARGET} -> ${INSTALLTARGET}" && mkdir -p ${BINDIR} && install -s ${BUILD_TARGET} ${INSTALLTARGET}
	@echo "${INFO} Installing man pages -> ${MAN1}/openbar.1 and ${MAN5}/openbarrc.5" && mkdir -p ${MAN1} ${MAN5} && install -m 644 ${SOURCE_DIR}/openbar.1 ${MAN1}/openbar.1 && install -m 644 ${SOURCE_DIR}/openbarrc.5 ${MAN5}/openbarrc.5 && echo "${INFO} Install complete"

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
	@echo "${INFO} Removing man pages" && rm -f ${MAN1}/openbar.1 ${MAN5}/openbarrc.5 && echo "${INFO} Uninstall complete"

# Debug target to run the program in a debugger
.PHONY: debug
debug: debug-build
	@echo "${INFO} Starting debugger for ${TARGET}"
	@egdb -q ${BUILD_TARGET} -ex "break main" -ex "run"

# Help target to display available commands
.PHONY: help
help:
	@printf "Available targets:\n  all        - Build the project\n  build      - Build the project\n  opt        - Build with -O3\n  install    - Install the executable and man pages\n  clean      - Remove build artifacts\n  uninstall  - Remove the installed files\n  debug      - Build with debug symbols and start egdb\n  test       - Report test availability\n"

.PHONY: test
test:
	@echo "${INFO} No automated tests defined"

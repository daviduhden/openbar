#!/bin/sh
# Self-test for the <stdckdint.h> fallback in compat/stdckdint.h.
#
# The fallback is forced with OPENBAR_STDCKDINT_FORCE_FALLBACK so that
# it is compiled and executed even when the host already provides a
# real <stdckdint.h>; otherwise the fallback would never be covered on
# a development machine.  The test binary is link-only against libc.

set -u

TESTS_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
COMPAT_DIR=$(CDPATH='' cd -- "$TESTS_DIR/.." && pwd)

CC=${CC:-cc}
CFLAGS=${CFLAGS:--O2 -pipe}
WARNINGS=${WARNINGS:--Wall -Wextra -Wpedantic}
CSTD=${CSTD:--std=c23}

bin="${TMPDIR:-/tmp}/openbar-stdckdint-test.$$"
trap 'rm -f "$bin"' EXIT HUP INT TERM

# shellcheck disable=SC2086  # the options are intentionally split
"$CC" $CFLAGS $CSTD $WARNINGS -I"$COMPAT_DIR" \
	-DOPENBAR_STDCKDINT_FORCE_FALLBACK \
	-o "$bin" "$TESTS_DIR/stdckdint.c" || exit 1

"$bin"

#!/bin/sh
# Apply repository .clang-format to all supported sources
#
# Copyright (c) 2025 David Uhden Collado <david@uhden.dev>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

set -eu

root_dir=${1:-.}

if ! command -v clang-format >/dev/null 2>&1; then
    printf '%s\n' "clang-format not found in PATH" >&2
    exit 1
fi

find "$root_dir" -type f \
    \( -name '*.[ch]' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' \
       -name '*.hh' -o -name '*.hpp' -o -name '*.hxx' \) -print0 \
    | xargs -0 -r clang-format -i -style=file -fallback-style=none

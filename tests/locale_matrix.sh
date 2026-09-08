#!/bin/sh
#
# Locale environment matrix for the openbar host tests.
#
# openbar pins the C locale at startup and never reads LANG or LC_*,
# so every environment in this matrix must produce byte-identical
# stdout, stderr and exit status compared to a clean environment.

set -u

bins=${*:-}

[ -n "$bins" ] || { echo "usage: $0 test-bin ..." >&2; exit 1; }

fail() {
	echo "FAIL ($env): $*" >&2
	exit 1
}

tmp=$(mktemp -d "${TMPDIR:-/tmp}/openbar-locale.XXXXXX") || exit 1
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

run_env() {
	env=$1
	shift
	n=1
	for b in $bins; do
		if ! env -i "$@" TZ=UTC0 PATH="$PATH" "$b" \
		    >"$tmp/$env.$n.out" 2>"$tmp/$env.$n.err"; then
			fail "$b exited non-zero"
		fi
		n=$((n + 1))
	done
}

run_env baseline

envnames=""
for spec in \
	"en_US.UTF-8 LANG=en_US.UTF-8" \
	"en_US_LC_ALL LC_ALL=en_US.UTF-8" \
	"de_DE LANG=de_DE.UTF-8" \
	"es_ES LANG=es_ES.UTF-8" \
	"fr_FR_messages LC_MESSAGES=fr_FR.UTF-8" \
	"mixed LC_ALL=es_ES.UTF-8 LANGUAGE=de_DE:fr_FR" \
	"c_locale LANG=C" \
	"c_posix LC_ALL=POSIX" \
	"gbk LANG=zh_CN.GBK" \
	"latin1 LC_ALL=en_US.ISO8859-1" \
	"numeric LC_NUMERIC=de_DE.UTF-8" \
	"koi8 LANG=ru_RU.KOI8-R"
do
	name=${spec%% *}
	set -- ${spec#* }
	envnames="$envnames $name"
	run_env "$name" "$@"
done

n=1
for b in $bins; do
	for name in $envnames; do
		cmp -s "$tmp/baseline.$n.out" "$tmp/$name.$n.out" || \
		    fail "$b stdout changed under $name"
		cmp -s "$tmp/baseline.$n.err" "$tmp/$name.$n.err" || \
		    fail "$b stderr changed under $name"
	done
	n=$((n + 1))
done

echo "locale matrix: ok"

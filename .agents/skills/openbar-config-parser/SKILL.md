---
name: openbar-config-parser
description: Change or review OpenBar's cwm-style configuration discovery, grammar, defaults, validation, and corresponding documentation. Use when editing config.c, openbar.conf, openbarrc.5, or configuration behavior.
---

# OpenBar configuration parser

## When to use this skill

Use this skill for any change to `config.c`, a configuration directive, a
default value, configuration discovery, parser diagnostics, `openbar.conf`, or
the configuration sections of `README.md` and `openbarrc.5`. Parser changes
almost always require manual-page changes.

## Required context

Read `config.c`, `struct conf` and its limits in `openbar.h`, `openbar.conf`,
`openbarrc.5`, the configuration section of `README.md`, and
`tests/test_config.c`.

## Current grammar

The parser reads one line at a time. It strips a trailing newline, trims spaces
and tabs, skips empty lines and lines whose first non-space character is `#`,
and splits the first whitespace-delimited token as the keyword. There are no
inline comments: `#` is special only at the start of the trimmed line.

| Directive | Grammar and limits |
| --- | --- |
| `barheight N` | Integer from 12 through 60; default 24 |
| `gap T B L R` | Exactly four integers, each 0 through `INT_MAX`; default `0 0 0 0` |
| `color SLOT VALUE` | `SLOT` is exactly `barbg`, `barfg`, or `urgent`; value is nonempty and at most 63 bytes; defaults `#CCCCCC`, `#000000`, `#FC8814` |
| `fontname FONT` | Nonempty Xft/fontconfig pattern; default `sans-serif:pixelsize=14:bold` |
| `logo TEXT` | Required, nonempty, at most 127 bytes; no default |
| `interface NAME` | 1 through 15 bytes; first byte `[A-Za-z_]`, remaining bytes `[A-Za-z0-9_.-]`; optional (fallback interface is selected at collection time) |
| `show WIDGET` | Enable one widget by exact lowercase name |
| `hide WIDGET` | Disable one widget by exact lowercase name |

Widget names are `hostname`, `date`, `cpu`, `mem`, `load`, `bat`, `vpn`, and
`net`. Widgets render in that order. All widgets start disabled. A repeated
directive overwrites the earlier value (last occurrence wins), including
`show`/`hide`.

`logo` and `fontname` accept a single matching pair of double or single quotes
around the whole argument. `color` accepts quotes around its value. There is no
escape syntax. A quoted argument must end at the closing quote; trailing
non-whitespace is an error. `interface`, `show`, and `hide` do not strip
quotes, so quoting an interface or widget name makes it invalid.

Malformed numeric values, missing arguments, unknown widget names, unknown
color slots, invalid interface names, overlong input lines (more than 4096
bytes including the newline; at most 4095 content bytes for a
newline-terminated line), and a missing `logo` are fatal. An unknown keyword
with an argument is diagnosed and ignored; an unknown keyword without an
argument hits the missing-argument check and is fatal. Color names and font
patterns are not syntax-checked at parse time; unusable values warn and fall
back to defaults when X11 initializes. Parse diagnostics include the file name
and line number; I/O errors and the missing-`logo` error include the file name
only.

Discovery order:

1. The path passed with `-c`; it must exist and be readable.
2. `$HOME/.openbarrc` when `$HOME` is nonempty, shorter than 1024 bytes, and
   the file is readable.
3. `/etc/openbarrc`.

`conf_defaults()` allocates the default `fontname` and all `colors[]`; `logo`
and `interface` stay null until a directive sets them. It must be called on a
zeroed or uninitialized structure; it does not free previous contents.
`conf_load()` applies defaults and then directives; on failure it calls
`conf_free()`, leaving the caller no owned state to release. `conf_free()` is
idempotent. The path returned by `conf_resolve_path()` is owned by the caller.

## Procedure

1. Decide whether the directive is new, already parsed, or only documented.
   Never document a keyword that `conf_parse_line()` does not handle.
2. Add or change the parser branch, validation, storage, and default in the
   same change. Use the ASCII predicates for machine-readable syntax; do not
   use locale-sensitive `<ctype.h>` classification.
3. Add valid, invalid, boundary, quoting, and last-wins cases to
   `tests/test_config.c` when the behavior is portable.
4. Update `openbarrc.5` (authoritative grammar and defaults), `README.md` (the
   user-facing example), and `openbar.conf` (the installed example) together.
5. Check ownership: every new `char *` in `struct conf` must be freed by
   `conf_free()` and replaced safely by `setstr()`.
6. Keep parser diagnostics in U.S. English with path and line number;
   file-level errors (I/O and missing `logo`) include the path only.

## Validation

Static checks:

- Compare every directive in `conf_parse_line()` with `openbarrc.5` and
  `openbar.conf`.
- Check byte limits against `LOGO_MAX` and `IFNAME_MAX` in `openbar.h`.
- Confirm the parser never reads `LANG` or `LC_*` and never uses locale-aware
  character classification.

When execution is allowed on a host:

```sh
make test
```

`make test` builds and runs all four host test binaries, including
`tests/test_config`. To isolate the configuration binary, build
`make tests/test_config` and run `./tests/test_config`; that target is defined
by the `Makefile` multi-target rule. The current suite does not test
`conf_resolve_path()`; do not claim discovery coverage. Do not run
`make install-conf`; it writes `/etc/openbarrc`.

## Common problems

- Updating `openbarrc.5` but not `openbar.conf` or the README example.
- Using line comments after a value: `logo x # note` makes the whole
  remainder part of the logo.
- Documenting a maximum in characters when the code counts bytes (UTF-8
  logos and other byte-limited fields).
- Forgetting the first-byte rule for interface names, or accepting a quoted
  interface name that the parser rejects.
- Treating an unknown keyword with an argument as fatal; it is warned and
  ignored, while an unknown keyword without an argument is fatal.
- Assuming color names or font patterns are validated by the parser; they
  are checked only when X11 initializes.
- Forgetting that a failed `conf_load()` already frees the configuration.
- Adding a directive without a host test because "it is trivial"; boundary and
  quoting errors are common regressions.

## Maintenance & Continuous Learning

After using this skill to modify the repository, review this skill and the
applicable `AGENTS.md` instructions as part of the task's final synchronization
pass. Update this skill when procedures, tools, configuration, behavior, or
assumptions in its scope change; add reusable implementation knowledge and
correct guidance that proved inaccurate. Keep examples and validation commands
current, and remove obsolete or conflicting guidance. Add only knowledge that
will help future work; do not record one-off execution details or cosmetic
changes. Check referenced paths and commands, then run
`make check-agent-sync` (or pass `BASE_REF=<git-ref>` for a CI change range).
The checker verifies structure, changed-file coverage, and local references; a
human semantic review must still confirm relevance. Report synchronization
results and any unresolved limitation in the final summary.

## References

- `config.c`
- `openbar.h`
- `openbar.conf`
- `openbarrc.5`
- `README.md`
- `tests/test_config.c`
- `tests/test.h`

---
name: openbar-regression-and-manpages
description: Run or review OpenBar's host regression suite, locale matrix, compat shim tests, and mdoc manual pages, and keep README/DEVELOPMENT claims synchronized with the code. Use for test changes, documentation updates, and release checks.
---

# OpenBar regression and manual pages

## When to use this skill

Use this skill when adding or changing tests, updating `openbar.1`,
`openbarrc.5`, `README.md`, or `DEVELOPMENT.md`, touching the `Makefile`
test targets, or auditing the locale and copyright policy. Use the
module-specific skills for behavior changes before coming here to validate and
document them.

## Test inventory

| Artifact | Coverage | Runs without OpenBSD/X11 |
| --- | --- | --- |
| `tests/test_config.c` | Defaults, directives, quoting, limits, errors, last-wins, widget lookup | Yes |
| `tests/test_fmt.c` | CPU/memory/load/battery/VPN/network/date/hostname formatting, composition, UTF-8 truncation | Yes |
| `tests/test_ipc.c` | IP validation, frame encode/decode, corrupt frames, descriptor helpers | Yes |
| `tests/test_locale.c` | Locale variables ignored, decimal point, English date names | Yes |
| `tests/locale_matrix.sh` | Byte-identical stdout and stderr across locale environments; every run must exit zero | Yes |
| `compat/tests/run.sh` | Forced `<stdckdint.h>` fallback against an `__int128` oracle | Yes |
| `compat/tests/stdckdint-compile-fail.sh` | Accepted and rejected checked-arithmetic uses | Yes |

`make test` builds the four test binaries, runs the locale matrix (the
`test-locale` prerequisite), then runs both compat scripts and finally the
binaries. `make test-locale` builds the binaries first and then runs only the
environment matrix. The host test build uses `_POSIX_C_SOURCE`,
`_DEFAULT_SOURCE`, and a `strtonum(3)` shim in `tests/`; none of those belong
in the production build.

The locale matrix covers `LANG`/`LC_ALL` English and translated UTF-8 values,
`LC_MESSAGES`, mixed variables, `C`, `POSIX`, GBK, ISO-8859-1, `LC_NUMERIC`,
KOI8-R, and a clean environment. Stdout and stderr must be byte-identical
across environments, and every run must exit zero; the script does not compare
exit statuses against each other.

## Manual pages

Both manuals are mdoc:

- Keep the `.Dd $Mdocdate: ... $`, `.Dt`, `.Os`, `.Sh`, and `.Xr` conventions
  already present. Do not replace mdoc with Markdown or roff requests that the
  file does not use. Update `.Dd` when the manual content changes; the date is
  currently a literal value, not an automatically expanded CVS keyword.
- `openbar.1` owns user-visible behavior: options, widgets, process
  architecture, locale, files, security, and limitations.
- `openbarrc.5` owns configuration grammar, defaults, widget names, and
  examples.
- Cross-reference the other manual by name: `openbar(1)` and `openbarrc(5)`,
  plus the relevant OpenBSD pages (`cwm(1)`, `pledge(2)`, `unveil(2)`,
  `tls_init(3)`, `apm(4)`, `wg(4)`, `Xft(3)`, `X(7)`).
- Keep `README.md` as the short overview and `DEVELOPMENT.md` as the internal
  design note. Do not duplicate the entire manual in either file.
- Preserve existing author and copyright sections, the license text, and the
  `$Mdocdate` style; do not add notices to files that never had them.

## Procedure

1. Identify what changed: portable logic, OpenBSD-only logic, IPC, rendering,
   or documentation only.
2. Add or update the narrowest host test that can observe the change. Use
   `CHECK`/`CHECK_STR` from `tests/test.h`; keep the test deterministic and
   independent of the host locale.
3. Update the authoritative manual section and every summary that repeats the
   claim (`README.md`, `DEVELOPMENT.md`, `openbar.conf`).
4. For a locale-sensitive change, add a case to `tests/test_locale.c` or the
   matrix only if the behavior must remain identical across environments.
5. Check the whole diff for whitespace errors, accidental reflow, and lost
   copyright notices.
6. Distinguish what was actually run: static review, compilation, host tests,
   or an OpenBSD/X11 runtime check. Do not describe host tests as validating
   X11, sysctl, APM, or the live network worker.

## Validation

When execution is allowed:

```sh
make test          # compat tests, locale matrix, four host binaries
make test-locale   # locale matrix only
```

`make test` does not need an X server and does not run the OpenBSD-only
collectors or network worker. A full build needs OpenBSD with the `comp` set,
Xenocara `xbase`, and libtls. Manual pages can be linted on OpenBSD with:

```sh
mandoc -Tlint -Werror openbar.1 openbarrc.5
```

`mandoc` is part of the OpenBSD base system; it is not a repository script.
The test targets depend on the test sources, and the test objects depend on
`tests/strtonum.h`; if a stale binary still surprises you, use `make clean`
before rebuilding. Check the final diff with `git diff --check` and inspect
`git status` for unintended files. Do not commit or push unless asked.

## Documentation drift checklist

- Every keyword in `conf_parse_line()` appears in `openbarrc.5` and
  `openbar.conf`.
- Every widget name and failure string in `fmt.c` appears in `openbar.1`.
  The manual documents `N/A`, CPU `x`, and `No VPN` for each widget.
- Every refresh period in `widget_intervals[]` and the date widget's special
  minute-boundary schedule match the table in `DEVELOPMENT.md` and the prose
  in `openbar.1`, including hostname's 60-second period.
- Pledge and unveil sets in `README.md`, `openbar.1`, and `DEVELOPMENT.md`
  match `build_pledge()`, `unveil_parent()`, and `net.c`.
- The IPC frame size and offsets in prose match the `static_assert` checks.
- Build and test commands in `README.md`, `DEVELOPMENT.md`, and `AGENTS.md`
  exist in the `Makefile`.
- The interface remains U.S. English and UTF-8 with no gettext/NLS claims.

## Common problems

- Assuming every test needs X11 or OpenBSD; only the real binary does.
- Running `make test` on a host without the C23 compiler and reporting a
  product failure instead of a host limitation.
- Letting `LANG`/`LC_*` influence a test or adding a locale-dependent format.
- Updating the code but not `openbarrc.5`, `openbar.conf`, `README.md`, or
  `DEVELOPMENT.md`.
- Reflowing mdoc text or changing `$Mdocdate` without reason.
- Adding a test that needs a real `/dev/apm`, a live interface, or
  ifconfig.me.
- Claiming a runtime result from static analysis or a host-only test run.

## Maintenance & Continuous Learning

After using this skill to modify the repository, review this skill and the
applicable `AGENTS.md` instructions as part of the task's final synchronization
pass. Update this skill when procedures, tools, configuration, behavior, or
assumptions in its scope change; add reusable implementation knowledge and
correct guidance that proved inaccurate. Keep examples and validation commands
current, and remove obsolete or conflicting guidance. Add only knowledge that
will help future work; do not record one-off execution details or cosmetic
changes. Check referenced paths and commands, then run
`make check-agent-knowledge` (or pass `BASE_REF=<git-ref>` for a CI change range).
The checker verifies structure, changed-file coverage, and local references; a
human semantic review must still confirm relevance. Report synchronization
results and any unresolved limitation in the final summary.

## References

- `Makefile`
- `tests/test.h`
- `tests/locale_matrix.sh`
- `compat/README.md`
- `compat/tests/run.sh`
- `compat/tests/stdckdint-compile-fail.sh`
- `openbar.1`
- `openbarrc.5`
- `README.md`
- `DEVELOPMENT.md`
- `LICENSE`

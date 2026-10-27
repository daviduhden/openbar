# AGENTS.md

OpenBar is a one- or two-process status bar for
[cwm(1)](https://man.openbsd.org/cwm.1) and other X11 window managers, written
in ISO C23 and targeting OpenBSD only.
Read `README.md` for behavior and `DEVELOPMENT.md` for internals before
changing code.

## Platform and language rules

- OpenBSD is the only target. Do not add Linux, FreeBSD, or portability
  fallbacks for `pledge(2)`, `unveil(2)`, `sysctl(2)`, `/dev/apm`, `libtls`,
  X11, or Xft. The `compat/` directory exists only for the missing C23
  `<stdckdint.h>` header; it is not a portability layer.
- Build with `clang` and `-std=c23` (clang 18 or newer). Do not lower the
  language mode or replace C23 facilities with pre-C23 spellings.
- Follow OpenBSD KNF (`style(9)`): tabs for indentation, 80 columns, BSD
  function declarations, one declaration per line where the file does so, and
  no comments that merely restate the code. Match the surrounding file.
- The interface is U.S. English and UTF-8 only. There is no gettext/NLS or
  locale-dependent formatting; `locale_init()` pins the C locale. Do not read
  `LANG` or `LC_*`, and do not introduce translated strings.
- Preserve existing copyright and license notices in source and manual
  files; never remove or relicense them. `LICENSE` is 2-clause BSD.

## Architecture map

| Path | Responsibility | Portability |
| --- | --- | --- |
| `openbar.c` | Display process: X11/Xft window, rendering, event loop, refresh scheduling, worker supervision, sandbox setup | OpenBSD + Xenocara |
| `config.c` | Configuration discovery and cwm-style parser | Portable C23 with BSD `<err.h>`/`strtonum(3)` (test shim) |
| `fmt.c` | Locale pinning, widget text formatting, bar composition, UTF-8 truncation | Portable C23/POSIX |
| `ipc.c` | Worker protocol codec and descriptor helpers | Portable C23/POSIX |
| `widgets.c` | CPU, memory, load, battery, hostname, date/time, interface, and VPN metrics | OpenBSD |
| `net.c` | Forked HTTPS worker for public IPv4/IPv6, worker sandbox | OpenBSD + libtls |
| `openbar.h` | Shared structs, limits, widget IDs, IPC frame, prototypes | ISO C23 |
| `compat/` | Embedded `<stdckdint.h>` shim and its self-tests | Portable C23 |
| `tests/` | Host tests for `config.c`, `fmt.c`, `ipc.c`, and locale policy | Portable C23/POSIX |
| `openbar.1`, `openbarrc.5` | User and configuration manuals (mdoc) | Documentation |

The display process collects metrics through collectors in `widgets.c`,
formats them in `fmt.c`, and renders only the resulting segments in
`openbar.c`. Keep that separation: the renderer must not query the kernel, and
formatters must not perform I/O. The network worker is forked before the
sandbox and is the only process with `inet` and `dns`.

## Commands that actually exist

`make` uses BSD make; the targets are defined in `Makefile`.

| Command | Effect |
| --- | --- |
| `make` | Build `openbar` (requires OpenBSD, Xenocara headers/libs, `libtls`) |
| `make rebuild` | `make clean` followed by a full build |
| `make debug` | Build `openbar_debug` with `-O0 -g` and launch it under `lldb` |
| `make test` | Build and run the host tests, compat tests, and locale matrix |
| `make test-locale` | Run only the locale-environment matrix |
| `make clean` | Remove binaries and test objects |
| `make install` | Install binary and man pages (needs privilege, usually `doas`) |
| `make install-conf` | Install the example configuration without overwriting |
| `make uninstall` | Remove installed binary and man pages (needs privilege, usually `doas`) |

Do not run `make install`, `make install-conf`, `make uninstall`, or
`make debug` unless the user explicitly asks; they modify the system or start
an interactive debugger. `make test` compiles and runs host code but does not
need an X server and does not exercise the OpenBSD-only collectors.

Static analysis is not a Make target. `DEVELOPMENT.md` suggests:

```sh
clang -std=c23 -Icompat -Wall -Wextra -Wpedantic -Wconversion \
      -Wsign-conversion -Wshadow -Wformat=2 -Wundef -Wpointer-arith \
      -Wstrict-prototypes -Wmissing-prototypes -Werror -fsyntax-only \
      config.c fmt.c ipc.c
clang --analyze -std=c23 -Icompat -D_POSIX_C_SOURCE=200809L \
      -D_DEFAULT_SOURCE -include tests/strtonum.h config.c fmt.c ipc.c
```

Add the Makefile's X11 include paths and list `openbar.c widgets.c net.c` when
checking the OpenBSD-only units. On a non-OpenBSD host, add the test
feature-test macros and `-include tests/strtonum.h` (as the test build does)
for `config.c`, `fmt.c`, and `ipc.c`. Use those as suggestions only when
execution is allowed; never claim a compile or test result that was not
actually observed.

## Change and verification policy

- Inspect `git status` and `git diff` before editing. Treat uncommitted changes
  as the user's work: do not revert, reformat, or overwrite them, and keep your
  edits scoped to the request.
- Do not commit, amend, push, or rewrite history unless the user asks.
- Do not add dependencies, run external commands from production code, weaken
  `pledge(2)`/`unveil(2)`, or add promises for convenience.
- Never perform live network fetches as a test, and never test against a real
  `/dev/apm`, X server, or user configuration without explicit permission.
- Keep the configuration grammar, `openbarrc.5`, `openbar.conf`, and the README
  in sync. Keep widget text, refresh periods, and failure states in sync with
  `openbar.1` and `DEVELOPMENT.md`.
- Portable logic changes need host tests in `tests/`. OpenBSD-only collection
  logic is validated by static review and manual testing on the target; do not
  present it as host-tested.
- Prefer static inspection and compilation checks first. State clearly whether
  a result came from static analysis, a build, host tests, or a real
  OpenBSD/X11 run.

## Skills

Load the relevant skill before working in these areas:

- [`openbar-x11-layout`](.agents/skills/openbar-x11-layout/SKILL.md): X11/Xft
  window geometry, rendering, colors, fonts, and redraw behavior.
- [`openbar-config-parser`](.agents/skills/openbar-config-parser/SKILL.md):
  configuration discovery, grammar, defaults, validation, and docs.
- [`openbar-widgets-and-system-data`](.agents/skills/openbar-widgets-and-system-data/SKILL.md):
  collectors, formatters, refresh periods, sysctl, APM, and interface data.
- [`openbar-ipc-and-networking`](.agents/skills/openbar-ipc-and-networking/SKILL.md):
  IPC frame, worker lifecycle, HTTPS fetch, and sandbox boundaries.
- [`openbar-regression-and-manpages`](.agents/skills/openbar-regression-and-manpages/SKILL.md):
  test inventory, locale matrix, manual pages, and documentation drift.

## Continuous Agent & Skills Maintenance

Agent instructions and skills are living repository artifacts.

Every repository modification MUST include an agent/skills synchronization
step.

Before completing any task, inspect the applicable AGENTS.md and SKILL.md
files, update the affected instructions, and validate their consistency with
the implementation.

A code change without the corresponding agent knowledge maintenance is
considered incomplete.

Never claim completion when the synchronization step has been skipped or has
failed.

> Before completing any task, inspect the applicable AGENTS.md and SKILL.md
> files, update the affected instructions, and validate their consistency with
> the implementation.

This review is required after every task that changes any tracked or
untracked repository artifact, including code, configuration, dependencies,
tests, workflows, documentation, architecture, agent instructions, and skills.

1. Inventory instructions with `find . -name AGENTS.md -o -name '*.agent.md'`
   and `find . -name SKILL.md`; also inspect tool-specific instruction trees
   such as `.github/`, `.claude/`, and `.cursor/` when present.
2. Map each changed area to its owning skill using the `When to use this skill`
   sections. The five `.agents/skills/openbar-*` skills own configuration,
   X11 layout, widgets/system data, IPC/networking, and regression/manual
   documentation. There are currently no specialized agent definition files;
   the root `AGENTS.md` governs all agents.
3. Re-read every applicable skill and instruction file after implementation.
   Update an existing skill when behavior, tools, validation, assumptions, or
   reusable implementation knowledge in its scope changed. Create a skill
   only when a durable responsibility has no suitable owner; update the root
   inventory and cross-references at the same time.
4. Update specialized agent instructions when their scope or workflow changes.
   Keep their mandatory post-change synchronization requirements aligned with
   this policy and the skills they use. Do not create duplicate agents or
   skills for the same responsibility.
5. Check documented paths and local references, command names, examples, and
   validation procedures against the repository. Run
   `make check-agent-sync [BASE_REF=<git-ref>]` after edits. This target invokes
   the Perl validator at `tools/check-agent-sync.pl`; the checker
   verifies the Git change set, a substantive instruction/skill diff, basic
   Markdown/frontmatter structure, and local Markdown links. Run any
   additional validation required by the affected skill.
6. Automatic checks establish structural conditions only. A reviewer must
   still confirm that the instruction change is semantically relevant and
   captures reusable knowledge; cosmetic edits, timestamps, and empty changes
   do not satisfy this policy.

The synchronization step is one complete review at the end of each task. If
that review finds and fixes an error, rerun the checker once. A task is blocked
from completion if no substantive, relevant instruction or skill update is
included, applicable files were not inspected, references/checks fail, or a
limitation is left undocumented. If a required update cannot be made, document
the reason and report the task as incomplete.

### Mandatory Definition of Done

- [ ] Repository changes implemented.
- [ ] Relevant agent instructions reviewed.
- [ ] Relevant skills reviewed.
- [ ] Affected AGENTS.md / SKILL.md files updated.
- [ ] Obsolete knowledge removed.
- [ ] New reusable knowledge incorporated.
- [ ] Agent and skill references validated.
- [ ] Automated maintenance checks passed.
- [ ] Final synchronization status reported.

Every final task report must include **Agent & Skills Synchronization** and
list agents reviewed, skills reviewed, instruction files updated, knowledge
added or corrected, validations actually run, and a final `PASS` or `FAIL`.

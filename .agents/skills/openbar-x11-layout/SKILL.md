---
name: openbar-x11-layout
description: Change or review OpenBar's X11/Xft window geometry, EWMH properties, font and color handling, bar composition, and redraw behavior. Use when editing openbar.c display code, gap or barheight behavior, rendering, or visual output.
---

# OpenBar X11 layout

## When to use this skill

Use this skill for changes to `openbar.c` involving `struct xstate`,
`xstate_open()`, `window_create()`, `draw_line()`, `redraw()`,
`handle_x_events()`, Xft colors or fonts, EWMH atoms, window placement, or
the relationship between `compose_bar()` in `fmt.c` and the rendered chunks.
Use `openbar-widgets-and-system-data` instead when the change is about metric
collection or refresh periods.

## Required context

Read:

- `openbar.c`: `struct xstate`, `build_pledge()`, `window_create()`,
  `xstate_open()`, `xstate_close()`, `draw_line()`, `redraw()`, and `main()`.
- `openbar.h`: `BAR_TEXT_MAX`, `WITEM_TEXT_MAX`, `struct witem`,
  `struct conf.gap`, `struct conf.barheight`, and color slots.
- `fmt.c`: `compose_bar()` and `bappend()`; the renderer must agree with the
  line format composed there.
- `openbar.1` and `openbarrc.5` for documented geometry and visual behavior.
- `DEVELOPMENT.md` for the lifecycle and sandbox ordering.

## Current layout invariants

- The bar is a top-edge window. `window_create()` places it at
  `x = gap[2]` (left) and `y = gap[0]` (top). Width is
  `DisplayWidth - gap[2] - gap[3]`; height is `barheight`, clamped to the
  screen height.
- `gap[1]` (bottom) is parsed and documented as a four-edge gap, but the
  current top-edge bar does not use it. If you change this, update the
  configuration manual and tests together; do not silently give the field a
  new meaning.
- The window is override-redirect, so the window manager must not place or
  move it. cwm's own `gap` only reserves space for maximized windows.
- EWMH properties mark the window as a dock, above, sticky, skipped in the
  taskbar and pager, and bypassing the compositor. Preserve that intent when
  adding or removing atoms.
- The line is composed as `| logo | widget | widget |` (or
  `| widget | widget |` without a logo). `compose_bar()` builds `bar_text`
  for change detection, and `draw_line()` independently rebuilds the same
  punctuation from the logo and `seg[]` for rendering; change both together
  and keep the tests in `tests/test_fmt.c` aligned.
- Widgets are drawn as chunks so an urgent segment can use the urgent color.
  The line is centered; negative horizontal positions clamp to zero.
- All text drawing uses Xft's `*Utf8` APIs. Do not introduce byte-oriented
  text drawing or truncate a multibyte sequence.
- Fonts and colors are opened before the `unveil(2)` lock because fontconfig
  reads configuration and caches during initialization. Lazy glyph fallback
  still needs the standard font directories and `rpath`; do not move font
  setup after the sandbox.
- An unknown color or font falls back to `default_colors[]` or
  `default_font`; a total failure is fatal. Keep the fallback diagnostics.
- Expose events repaint from cached segments. Redraws happen only when the
  composed text changes (`dirty`) or the server asks for an expose.

## Procedure

1. Read the call sites and confirm which value is authoritative: the parsed
   `struct conf`, the composed `bar_text`/`seg[]`, or the X11 geometry.
2. Make the smallest change in `openbar.c`; adjust `fmt.c` only if the line
   format itself changes.
3. If a configuration field or documented behavior changes, update
   `openbarrc.5`, `openbar.conf`, `README.md`, and `tests/test_config.c` or
   `tests/test_fmt.c` as applicable.
4. Check error paths: `xstate_open()` must leave `struct xstate` safe for
   `xstate_close()` after partial initialization, and color/font allocation
   counts must remain balanced.
5. Review the sandbox ordering in `main()`: X11, font, and `/dev/apm` setup
   happen before `unveil(2)` and `pledge(2)`.
6. If the change affects geometry, reason through tiny screens, gaps that
   exceed the screen width, and `barheight` larger than the screen.

## Validation

Static review is the first gate. Re-read `window_create()`, `draw_line()`,
`compose_bar()`, and their tests after editing.

When execution is allowed:

```sh
make            # only on OpenBSD with the Xenocara/libTLS dependencies
make test       # host tests; does not exercise an X server
```

A real X11 check requires OpenBSD plus a running X server and is a manual
runtime test. Do not claim it was performed from a compile or from the host
test suite. `make debug` starts `lldb`; do not run it unless asked.

## Common problems

- Editing `compose_bar()` but not `draw_line()`, producing different
  separators or spacing in the cached text and the rendered window.
- Placing the window through the window manager instead of keeping it
  override-redirect, which lets cwm or another WM reposition it.
- Using `XDrawString` or byte-length truncation for UTF-8 text.
- Moving `XftFontOpenName()` or color allocation after `unveil(2)`, which
  breaks fontconfig initialization or fallback loading.
- Forgetting that the bottom gap is currently unused and documenting it as if
  it positioned the bar.
- Assuming host tests cover X11 rendering; they do not.

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

- `openbar.c`
- `fmt.c`
- `openbar.h`
- `openbar.1`
- `openbarrc.5`
- `DEVELOPMENT.md`
- Xft(3), cwm(1)

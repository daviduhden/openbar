# Development

`openbar` is deliberately small: a two-process OpenBSD status bar in ISO C17.
This document describes the internal architecture and the invariants that
matter when changing it.

## Layout

```
openbar.h     shared ISO C17 declarations (struct conf, struct openbar,
              widget ids, IPC frame); includes only standard headers
config.c      configuration discovery + cwm-style parser (portable)
fmt.c         widget formatting + bar line composition (portable)
ipc.c         worker IPC codec + descriptor helpers (portable)
widgets.c     kernel metric collection: sysctl(2), getifaddrs(3), /dev/apm
net.c         network worker: HTTPS via libtls, worker sandbox, fork setup
openbar.c     display process: X11/Xft, event loop, refresh scheduling,
              worker supervision, display-process sandbox
tests/        host tests for the three portable units
```

The portable units (`config.c`, `fmt.c`, `ipc.c`) contain only ISO C17 and
POSIX interfaces available on any test host; they are what `make test`
compiles and runs.  The OpenBSD units are verified against the real OpenBSD
and Xenocara headers on the target, but their logic is exercised indirectly
through the portable units' tests where possible.

## State

`struct openbar` (in `openbar.h`) holds all mutable application state:

- `conf` – the parsed configuration (owns its strings);
- worker state – IPC descriptor, pid, receive staging buffer, deadlines;
- per-widget refresh deadlines (`due[]`, monotonic clock);
- collected metrics (hostname, CPU speed/temperature, memory, load,
  battery, addresses, VPN state);
- the rendered bar line (`bar_text`), per-widget segments (`seg[]`) and the
  `dirty` flag.

There is no other mutable global state; the only file-scope variables are
two `volatile sig_atomic_t` signal flags in `openbar.c`, written exclusively
by async-signal-safe handlers and read in the main loop.

## Lifecycle

```
parse arguments (getopt)
-> resolve configuration path ($HOME-aware, then defaults)
-> load configuration (fatal on malformed values)
-> tzset(3)                cache zoneinfo before the unveil lock
-> fork network worker     only if "net" is enabled; the child unveils and
                           pledges itself ("stdio inet dns")
-> XOpenDisplay, allocate Xft colours, open the font, create the window
-> open /dev/apm           only if "bat" is enabled
-> sample hw.cpuspeed      once; not pledge-readable later
-> unveil(2) + lock        /tmp/.X11-unix, Xauthority, /dev/apm, font dirs
-> pledge(2)               "stdio rpath" + vminfo + route as needed
-> event loop (poll(2))
-> cleanup
```

Fonts are opened **before** the unveil lock: fontconfig needs its
configuration and cache files during initialization.  Lazy fallback loading
for glyphs missing from the configured font may still open font files at
draw time, which is why the standard font directories remain visible and
`rpath` stays in the pledge set.

## Refresh model

Each enabled widget has a monotonic deadline (`due[]`) and a period:

| widget   | period                 |
|----------|------------------------|
| date     | next minute boundary   |
| cpu      | 5 s (temperature)      |
| mem      | 2 s                    |
| load     | 2 s                    |
| bat      | 30 s                   |
| vpn/net  | 10 s (interface state) |
| hostname | 60 s                   |
| public IP| 300 s, rate limited    |

The loop calls `poll(2)` over the X connection fd, the worker IPC fd and a
timeout computed from the nearest deadline (capped at one second).  When a
deadline passes, only that widget's collector runs; `compose_bar()` rebuilds
the bar line and sets `dirty` only when the text actually changed, so
redraws are skipped when nothing moved.  Expose events repaint from the
cached line without re-collecting metrics.

Collectors (`widgets.c`) and formatters (`fmt.c`) are separate: a collector
fills metrics, a formatter turns metrics into display text.  The renderer
(`draw_line` in `openbar.c`) only consumes the formatted segments and never
queries the kernel.

## Worker IPC

The protocol is described in `openbar.h`:

- request: one byte, `IPC_CMD_FETCH`;
- response: a fixed 65-byte frame (magic byte, v4/v6 status codes, two
  address strings), layout checked with `_Static_assert`.

Both peers run the same program image (`fork(2)`, no `exec`), so padding is
irrelevant, but the decoder (`ipc_decode`) still validates the magic byte,
the status codes and both addresses with `inet_pton(3)` – the display
process never trusts the worker blindly.

The parent uses a small state machine: `IPC_IDLE` → send request →
`IPC_FETCHING` → collect bytes with `recv(MSG_DONTWAIT)` until the frame is
complete.  A stalled worker (no complete frame within 30 s), EOF or a hard
error transitions to `IPC_BROKEN`: the descriptor is closed, the worker is
terminated and public addresses show `N/A` until restart.  There is no
respawn: `fork(2)` after `pledge(2)` would require the `proc` promise,
which is deliberately not retained.  Reading is strictly non-blocking, so a
hung network never stalls the X11 event loop.

## Network worker

The worker is the only process with `inet` and `dns`.  Per fetch (IPv4 then
IPv6, sequentially):

1. `getaddrinfo("ifconfig.me", "443", ...)` – the numeric service avoids
   `/etc/services`;
2. non-blocking `connect(2)` bounded by `poll(2)` (5 s), one address from
   the resolved chain at a time;
3. `tls_connect_socket(3)` with SNI/hostname verification against
   `/etc/ssl/cert.pem`;
4. a bounded HTTP/1.1 `GET /ip` exchange under 5 s socket timeouts;
5. response parsing: status line must be `200`, body must be exactly one
   address of the requested family, total response capped at 1024 bytes.

`libtls` and `libcrypto` are userland code; `stdio inet dns` is sufficient
for them.

## Sandbox notes

- `hw.sensors` (CPU temperature), `gethostname(3)` and `getloadavg(3)` are
  permitted by pledge under any promise set.
- `hw.cpuspeed` is **not** pledge-readable under any promise set; the CPU
  frequency is sampled once before pledging.
- `APM_IOC_GETPOWER` is rejected under every promise set; with the battery
  widget enabled the display process stays unpledged (unveil locked).
- The worker resets its inherited signal handlers before serving so the
  parent can terminate it normally.
- No external programs are executed; all metrics come from libc/kernel
  interfaces.

## Testing

```
make test
```

runs the three host test binaries (configuration parsing, IPC codec,
formatting).  The test build uses `-D_POSIX_C_SOURCE`/`-D_DEFAULT_SOURCE`
and a `strtonum(3)` shim solely to compile on non-OpenBSD hosts; neither
appears in the production build.  Suggested hygiene before committing:

```sh
make test
clang -std=c17 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion \
     -Wshadow -Wformat=2 -Wundef -Wpointer-arith -Wstrict-prototypes \
     -Wmissing-prototypes -Werror -fsyntax-only config.c fmt.c ipc.c ...
clang --analyze ...
```

## Style

OpenBSD KNF (`style(9)`), tabs for indentation, 80 columns, BSD function
declarations, no comments that merely restate code.  A matching
`.clang-format` is included.

---
name: openbar-widgets-and-system-data
description: Change or review OpenBar's CPU, memory, load, battery, hostname, interface, and WireGuard data collection, formatting, refresh periods, and failure states. Use when editing widgets.c, fmt.c metrics, or the refresh loop.
---

# OpenBar widgets and system data

## When to use this skill

Use this skill for changes to `widgets.c`, metric fields in `struct openbar`,
widget formatting in `fmt.c`, or refresh scheduling in `openbar.c`. Use
`openbar-ipc-and-networking` for the public-IP worker and its protocol.

## Required context

Read `widgets.c`, `fmt.c`, `openbar.h` (`struct openbar`, widget IDs, buffer
limits), the refresh constants and `collect_due()`/`schedule()` in
`openbar.c`, the WIDGETS section of `openbar.1`, and the widget descriptions in
`openbarrc.5` and `README.md`.

## Data sources and display behavior

| Widget | Source | Refresh | Failure text |
| --- | --- | --- | --- |
| `hostname` | `gethostname(3)` into `HOSTNAME_MAX + 1` bytes | 60 s | `N/A` |
| `date` | `time(3)` then `localtime_r(3)` with hard-coded English names | next wall-clock minute boundary | `N/A` |
| `cpu` | `hw.cpuspeed` once before pledge; `hw.sensors.*.temp0` | temperature every 5 s | frequency `N/A`; temperature `x` |
| `mem` | `sysctl(CTL_VM, VM_UVMEXP)` free pages × page size | 2 s | `N/A` |
| `load` | `getloadavg(3)`, 1-minute value | 2 s | `N/A` |
| `bat` | `APM_IOC_GETPOWER` on an already-open `/dev/apm` | 30 s | `N/A`; urgent at ≤15% |
| `net` | interface IPv4 via `getifaddrs(3)`; public addresses via worker | 10 s for interface state; 300 s for public IP | `N/A` |
| `vpn` | `getifaddrs(3)` WireGuard interface state | 10 s | `No VPN` |

The exact display strings are:

- CPU: `CPU: %4uMHz (%d C)`, `CPU: %4uMHz (x)`,
  `CPU: N/A (%d C)`, or `CPU: N/A (x)`.
- Memory: `Mem: %llu MiB`.
- Load: `Load: %.2f`.
- Battery: `Bat: %d%%`, urgent at 0 through 15.
- Network: `IPs: <public v4> | <public v6> ~ <interface v4>`.
- VPN: `VPN: VPN` or `VPN: No VPN`.

## Collector details

- CPU frequency is sampled by `collect_cpu_init()` after `unveil(2)` and before
  `pledge(2)` because `hw.cpuspeed` is not readable under any promise set. It
  is displayed statically. Do not move this read into the regular collector.
- CPU temperature searches sensor devices 0 through 63 for names beginning
  with `cpu` or `km` that advertise a `SENSOR_TEMP` channel, caches the device
  index in `cpu_sensor` (`0` unsearched, `-1` none, otherwise index + 1), reads
  `SENSOR_TEMP` index 0, rejects `SENSOR_FINVALID` and `SENSOR_FUNKNOWN`, and
  converts microkelvin with `(value - 273150000) / 1000000`. Any unavailable
  temperature produces `x`, not `N/A`, for the temperature field.
- Memory multiplies `uv.free` by `uv.pagesize` with `ckd_mul()` before dividing
  by 1 MiB. Keep the checked arithmetic and fail to `N/A` on overflow.
- Battery treats unknown state or charge, a charge above 100, a missing
  descriptor, or an ioctl failure as unavailable (`bat_pct = -1`). The urgent
  color is a formatting decision in `fmt_widget()`.
- The interface IPv4 address prefers the configured `interface`; if it is
  absent, down, or has no IPv4 address, the first up, non-loopback IPv4
  address is used. The selection is by interface, not by address range, so it
  can be a public address. `net` and `vpn` share this collector; enabling
  either walks the interface list.
- A WireGuard tunnel counts as up only for a `wg*` interface that is `IFF_UP`
  and `IFF_RUNNING` and has an IPv4 or IPv6 address. Do not infer VPN state
  from a process name, a socket, or an external command.
- No production code executes external programs. Metrics come from libc and
  kernel interfaces.
- Collectors fill metrics; formatters turn them into text. The renderer in
  `openbar.c` must not read the kernel.

## Refresh model

Each enabled widget has a `CLOCK_MONOTONIC` deadline in `due[]`. The event
loop polls X11, the IPC descriptor, and a timeout capped at one second. Only
expired widgets are collected, then `compose_bar()` runs and sets `dirty` only
if the text changed. The date widget is rescheduled to the next wall-clock
minute boundary using the realtime clock for phase and the monotonic
deadline for timing. Public IP fetches are rate-limited separately to one per
300 seconds, with a 30-second worker deadline.

## Procedure

1. Add or change the metric field in `struct openbar` and initialize its
   invalid state in `main()` or the collector.
2. Implement collection in `widgets.c` with bounded buffers and checked size
   arithmetic; degrade to the documented failure value instead of exiting.
3. Format in `fmt.c`, never in the renderer. Keep the exact labels and units
   consistent with `openbar.1`.
4. Wire the widget into `collect_widget()` and `widget_intervals[]`; the date
   widget is scheduled specially in `schedule()` and has no interval entry.
   Add the pledge promise if the syscall requires one (`vminfo`, `route`) and
   update the sandbox docs only after verifying the requirement.
5. Add a formatting test in `tests/test_fmt.c` for each new state. Add a
   locale test if formatting could become locale-dependent.
6. Update `openbar.1`, `openbarrc.5`, `README.md`, and the refresh table in
   `DEVELOPMENT.md` when the user-visible behavior changes.

## Validation

Static checks:

- Confirm `ckd_*` use on every size or page-count arithmetic.
- Confirm no format string can overflow `struct witem.text` (160 bytes).
- Confirm the pledge set still covers every enabled widget and that conditional
  promises are derived from the configuration, not from a global default.
- Confirm the refresh table in `DEVELOPMENT.md` matches
  `widget_intervals[]`.

When execution is allowed on a host:

```sh
make test
```

This covers `fmt.c` and locale policy in `tests/test_fmt.c` and
`tests/test_locale.c`. The OpenBSD collectors in `widgets.c` are not part of
the host build; they require an OpenBSD target and, for battery, a real
`/dev/apm`. Do not treat a passing host suite as validation of `sysctl(2)`,
`getifaddrs(3)`, or APM behavior.

## Common problems

- Reading `hw.cpuspeed` after `pledge(2)` and killing the process.
- Forgetting `vminfo` for `VM_UVMEXP` or `route` for `getifaddrs(3)`.
- Forgetting that the process remains unpledged only when the battery widget
  is enabled *and* `/dev/apm` opens successfully; if the open fails, it
  pledges normally and the battery shows `N/A`.
- Using `N/A` and `x` interchangeably for CPU temperature.
- Formatting in `widgets.c` or querying the kernel from `draw_line()`.
- Breaking the unsigned memory arithmetic or dropping the overflow check.
- Detecting WireGuard through an external command or by interface name alone.
- Assuming the hostname always fits `HOSTNAME_MAX`; a longer name makes
  `gethostname(3)` fail and the widget shows `N/A`.
- Updating the widget string but not `openbar.1`, `openbarrc.5`, or the
  `DEVELOPMENT.md` refresh table.

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

- `widgets.c`
- `fmt.c`
- `openbar.c`
- `openbar.h`
- `openbar.1`
- `openbarrc.5`
- `DEVELOPMENT.md`
- `tests/test_fmt.c`
- `tests/test_locale.c`

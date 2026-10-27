---
name: openbar-ipc-and-networking
description: Change or review OpenBar's worker IPC frame, parent/child lifecycle, HTTPS public-address fetch, and pledge/unveil boundaries. Use when editing ipc.c, net.c, or worker supervision in openbar.c.
---

# OpenBar IPC and networking

## When to use this skill

Use this skill for `ipc.c`, `net.c`, the IPC fields and state machine in
`openbar.c`/`openbar.h`, worker sandboxing, public-address fetching, or the
security claims in `README.md` and `openbar.1`. Use
`openbar-widgets-and-system-data` for private-interface and VPN collection.

## Required context

Read `openbar.h` (`struct net_response`, `enum net_status`, `enum ipc_state`,
`IPC_CMD_FETCH`, `IPC_MAGIC`, limits), `ipc.c`, `net.c`, the worker functions
in `openbar.c` (`ipc_tick()`, `ipc_read()`, `apply_response()`,
`worker_lost()`), `tests/test_ipc.c`, and the security sections of `README.md`,
`openbar.1`, and `DEVELOPMENT.md`. `net_worker_start()` is defined in `net.c`.

## Wire protocol

The parent sends one byte, `IPC_CMD_FETCH` (`0x01`), over a
`socketpair(2)` stream. The worker answers with a fixed 65-byte
`struct net_response`:

| Offset | Field | Size |
| --- | --- | --- |
| 0 | `magic` (`IPC_MAGIC`, `0x4f`, `'O'`) | 1 |
| 1 | `status_v4` | 1 |
| 2 | `status_v6` | 1 |
| 3 | `addr_v4` | 16 |
| 19 | `addr_v6` | 46 |

Status values are `NET_OK`, `NET_ERR_DNS`, `NET_ERR_CONNECT`,
`NET_ERR_RECV`, `NET_ERR_PARSE`, and `NET_ERR_SYS`. `ipc.c` enforces the size,
offsets, alignment, enum width, and status-count bound with `static_assert`.
Addresses are stored NUL-terminated and validated with `inet_pton(3)` before
display. The decoder never trusts the worker: it checks the magic byte, both
status codes, and each address whose status is `NET_OK`.

## Worker lifecycle and sandbox

`net_worker_start()` creates the socket pair and forks before the display
process opens X11 or applies `unveil(2)`. The child:

1. resets the inherited `SIGTERM` and `SIGINT` handlers to `SIG_DFL`;
2. closes the parent's socket end and standard input/output (stderr remains;
   a descriptor that the socket itself occupies is not closed);
3. unveils `/etc/resolv.conf`, `/etc/hosts`, and `/etc/ssl/cert.pem` read-only,
   then locks the filesystem view;
4. pledges `stdio rpath inet dns` and runs `net_worker()`.

`rpath` is required because libtls opens the CA bundle; `dns` does not cover
that read. The service port is numeric (`443`) and `getaddrinfo()` uses
`AI_NUMERICSERV`, so `/etc/services` is not needed. Do not add promises or
filesystem paths without a concrete need and a documentation update.

The parent sends a fetch when idle and the rate limit has elapsed, then reads
non-blockingly with `recv(MSG_DONTWAIT)` until the 65-byte frame is complete.
Reads are accepted only in `IPC_FETCHING`; unsolicited data marks the channel
broken. `EINTR` is retried. `FETCH_TIMEOUT` is 30 seconds; `FETCH_INTERVAL` is
300 seconds. A timeout, EOF, or hard error calls `worker_lost()`: it closes
the descriptor, sets the public addresses to `N/A`, and marks `IPC_BROKEN`.

There is no respawn. Forking after `pledge(2)` would require the `proc`
promise, which the display process deliberately does not keep. For the same
reason, `worker_lost()` must not call `kill(2)` or `waitpid(2)`; the child sees
EOF when it next reads the command or writes a frame and exits, and an ignored
`SIGCHLD` reaps it. Do not reintroduce either call.

## HTTPS fetch

Per request, the worker fetches IPv4 and then IPv6:

1. `getaddrinfo("ifconfig.me", "443", ...)` with the requested family;
2. one non-blocking `connect(2)` at a time, bounded by `poll(2)` for 5 seconds,
   then `getsockopt(SO_ERROR)`;
3. `tls_client()`/`tls_configure()`/`tls_connect_socket()` with hostname
   verification against the system CA bundle;
4. a bounded HTTP/1.1 `GET /ip` with 5-second receive/send socket timeouts per
   operation; DNS and the whole fetch have no separate total deadline, so the
   parent's 30-second deadline can expire first;
5. `parse_response()` requires an exact `200` status code, a body that is
   exactly one address of the requested family, and a complete response below
   1024 bytes (1023 accumulated bytes already fails). The response length is
   explicit, so an embedded NUL cannot hide trailing bytes.

Any failure is reported as the corresponding status; the parent displays `N/A`
for that family. The bar keeps running.

## Procedure

1. Change the protocol in `openbar.h` first, then `ipc.c`, then both callers.
   Update the `static_assert` layout checks if the frame changes.
2. Keep the worker and parent state machines consistent: every transition that
   loses the descriptor must produce `N/A` and `IPC_BROKEN`, and no path may
   block the X11 event loop.
3. Keep all reads and writes bounded and handle partial reads, `EINTR`, EOF,
   and timeouts. Validate remote data before it reaches the bar.
4. If the sandbox changes, verify the exact promise or unveil need against the
   OpenBSD API being used; update `README.md`, `openbar.1`, and
   `DEVELOPMENT.md` in the same change.
5. Add codec and helper cases to `tests/test_ipc.c`; do not test the live
   endpoint.

## Validation

Static checks:

- Recompute the frame size and offsets from the field sizes.
- Confirm every `NET_OK` address is validated by family on decode.
- Confirm the request loop cannot spin forever on `TLS_WANT_POLLIN` or
  `TLS_WANT_POLLOUT` with a blocking descriptor.
- Confirm the response bound and per-operation timeouts are still enforced.
- Confirm the display process does not use `kill(2)`, `waitpid(2)`, or
  `fork(2)` after pledging.
- Confirm the documented pledge sets match `net.c` and `build_pledge()`.

When execution is allowed on a host:

```sh
make test
```

`tests/test_ipc.c` covers IPv4/IPv6 validation, encode/decode round trips,
error statuses, truncated and corrupted frames, `read_full()`/`write_full()`,
and the fetch byte. It does not perform network I/O. Live fetching requires
OpenBSD, a network, and explicit permission; never use it as a unit test.

## Common problems

- Documenting or using `stdio inet dns` and omitting `rpath`, which makes the
  worker die when libtls reads the CA bundle.
- Adding a respawn or `waitpid(2)` after `pledge(2)` without the `proc`
  promise.
- Blocking the display process on `recv(2)` or waiting for the worker during
  `-1`/shutdown.
- Accepting extra bytes, a redirect, a non-200 response, a body with more
  than one address, or a body with an embedded NUL.
- Assuming the 30-second parent deadline bounds DNS or the whole worker
  fetch; there is no total fetch timeout, only per-socket-operation limits.
- Treating `EINTR` as a fatal `recv(2)` error, or accepting a frame while not
  in `IPC_FETCHING`.
- Forgetting that 1023 accumulated response bytes already fail; the parser
  never sees a full 1024-byte buffer.
- Trusting a frame because it came from the same binary; the decoder must
  still validate it.
- Changing the frame layout without updating the compile-time assertions.
- Adding a network test that depends on ifconfig.me, DNS, or connectivity.

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

- `ipc.c`
- `net.c`
- `openbar.c`
- `openbar.h`
- `tests/test_ipc.c`
- `README.md`
- `openbar.1`
- `DEVELOPMENT.md`
- pledge(2), unveil(2), tls_init(3), getaddrinfo(3)

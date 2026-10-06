# compat

Embedded portability shims.  `compat/` is placed before the system
include directories (`-Icompat`) so that a file here shadows the
corresponding system header.

On a system whose libc already provides the header, the shim forwards
to it with `include_next` and the native definition is used unchanged.
On a system whose C23 compiler is paired with an older libc (OpenBSD at
the time of writing, which lacks `<stdckdint.h>`), the shim supplies the
interface itself.

| File | Purpose |
| --- | --- |
| [stdckdint.h](stdckdint.h) | `<stdckdint.h>` shim. Uses the real header when one is reachable, otherwise provides `ckd_add`/`ckd_sub`/`ckd_mul` with the compiler's overflow builtins. |
| [tests/](tests/) | Self-test for the `<stdckdint.h>` fallback. |

## Tests

The self-test in [tests/](tests/) forces the fallback with
`OPENBAR_STDCKDINT_FORCE_FALLBACK`, so it is covered even on a host
whose libc already ships a real `<stdckdint.h>`.  It exercises the C23
contract: representability is judged against the result type, operands
are not converted to a common type first, and each argument is
evaluated exactly once.

Run it from the repository root:

```
sh compat/tests/run.sh
```

`make test` runs it first, before the portable-unit suites.

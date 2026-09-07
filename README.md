# OpenBar

`openbar` is a status bar written in C for
[cwm(1)](https://man.openbsd.org/cwm.1) and other X11 window managers on
[OpenBSD](https://www.openbsd.org).

Its visual style, colour palette and configuration syntax are modelled after
cwm so that the bar blends in as if it were part of the window manager.

> This project targets OpenBSD exclusively.  There are no portability layers
> for other operating systems, and there are no plans to add any.

## Features

- Logo and hostname
- CPU speed and temperature
- Free memory
- Load average
- Battery status (with urgent colour at or below 15%)
- Public IPv4 and IPv6 addresses (HTTPS)
- Private IPv4 address
- WireGuard VPN connection status

If the CPU has no sensors or is not supported, an "x" is displayed next to the
CPU speed, which is common in VMs or older machines.

## Configuration

`openbar` uses a cwm-style keyword configuration format.  Lines starting with
`#` are comments; strings with spaces must be quoted.

Configuration is loaded from, in order:

1. A custom path provided with `-c` (which must exist).
2. `~/.openbarrc` if it exists and is readable.
3. The system-wide configuration file `/etc/openbarrc`.

Example `~/.openbarrc`:

```
# Bar dimensions
barheight 24
gap 0 0 0 0

# Colours (Xft colour names or #RRGGBB)
color barbg "#CCCCCC"
color barfg "#000000"
color urgent "#FC8814"

# Font (Xft fontconfig pattern)
fontname "sans-serif:pixelsize=14:bold"

# Logo text (required)
logo OpenBar

# Network interface for private IP
interface iwm0

# Widget toggles (all widgets are hidden by default)
show hostname
show date
show cpu
show mem
show load
show net
show vpn
hide bat
```

Available widgets: `hostname`, `date`, `cpu`, `mem`, `load`, `bat`, `net`,
`vpn`.

See [openbarrc(5)](openbarrc.5) for the complete manual.

## Fonts

`openbar` uses [Xft(3)](https://man.openbsd.org/Xft.3) for font rendering,
the same library used by cwm.  Any fontconfig pattern is accepted:

```
fontname "monospace:pixelsize=13"
fontname "DejaVu Sans Mono:size=12"
fontname "sans-serif:pixelsize=14:bold"
```

## Security

`openbar` reads its configuration and opens X11 and the font before
restricting itself with [pledge(2)](https://man.openbsd.org/pledge.2) and
[unveil(2)](https://man.openbsd.org/unveil.2).

When `net` is enabled, a dedicated worker is forked before sandboxing and is
the only process retaining `inet` and `dns`.  The display process pledges
`stdio rpath`, plus `vminfo` when the memory widget uses `VM_UVMEXP` and
`route` when `getifaddrs(3)` supplies network/VPN state.  Its filesystem view
is locked to the X11 socket directory, the X authority file, the standard
fontconfig directories and, only when the battery widget is enabled,
`/dev/apm`.

The network worker pledges `stdio inet dns` and sees only
`/etc/resolv.conf`, `/etc/hosts` and `/etc/ssl/cert.pem`.  Public addresses
are fetched over **HTTPS** with [libtls](https://man.openbsd.org/tls_init.3),
so an on-path attacker cannot substitute another address without a valid
certificate; responses are bounded and validated with `inet_pton(3)` before
display, and requests are rate-limited to one per five minutes.  Network
failures show `N/A` and never block the bar; if the worker dies, public
addresses remain `N/A` until the bar is restarted.

One documented limitation: no pledge promise permits `APM_IOC_GETPOWER`, so
with the battery widget enabled the display process stays unpledged (its
unveil policy is still locked) and prints a warning.

See [openbar(1)](openbar.1) for the details.

## Display

Add the following to your `.xsession` file:

```sh
openbar &
exec cwm
```

For `cwm`, leave a gap at the top of the screen for the bar in your `.cwmrc`:

```
gap 24 0 0 0
```

## Building

OpenBSD only.  Requires the `comp` set, Xenocara `xbase` headers/libraries
(`libX11`, `libXft`, `libXrender`, `fontconfig`, `freetype`) and `libtls`
from base.  The code is ISO C17 (`-std=c17`).

```sh
git clone https://github.com/daviduhden/openbar.git
cd openbar
make
```

Run the host tests for the portable units (configuration parser, IPC codec,
formatting):

```sh
make test
```

## Installing

```sh
doas make install
```

Installs the executable and the manual pages.  The example configuration is
installed separately, and never overwrites an existing file:

```sh
doas make install-conf
```

## Uninstalling

```sh
doas make uninstall
```

## References

- [cwm(1)](https://man.openbsd.org/cwm.1)
- [openbar(1)](openbar.1)
- [openbarrc(5)](openbarrc.5)
- [pledge(2)](https://man.openbsd.org/pledge.2)
- [unveil(2)](https://man.openbsd.org/unveil.2)
- [tls_init(3)](https://man.openbsd.org/tls_init.3)
- [Xft(3)](https://man.openbsd.org/Xft.3)
- [X(7)](https://man.openbsd.org/X.7)

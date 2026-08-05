# OpenBar

`openbar` is a status bar written in C for
[cwm(1)](https://man.openbsd.org/cwm.1) and other X11 window managers on
[OpenBSD](https://www.openbsd.org).

Its visual style, colour palette and configuration syntax are modelled after
cwm so that the bar blends in as if it were part of the window manager.

> Currently, this project does not support other operating systems, nor does
> the maintainer have any intention of working on it.

**CAVEATS:** This version is still in development and testing.

## Features

- Displaying a "logo" or name
- Hostname
- CPU speed and temperature
- Free memory
- Load average
- Battery status
- Public IPv4 and IPv6 addresses
- Private IPv4 address
- WireGuard VPN connection status

If the CPU has no sensors or is not supported, it will display an "x" next to
the CPU speed, which is common in VMs or older machines.

## Configuration

`openbar` uses a cwm-style keyword configuration format.  Lines starting with
`#` are comments; strings with spaces must be quoted.

Configuration is loaded from, in order:

1. A custom path provided with `-c`.
2. `~/.openbarrc` if it exists.
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

# Widget toggles
show hostname
show date
show cpu
show mem
show load
show net
show vpn
hide bat
```

Available widgets: `hostname`, `date`, `cpu`, `mem`, `load`, `bat`, `net`, `vpn`.

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

`openbar` reads its configuration and opens X11 before restricting itself with
[pledge(2)](https://man.openbsd.org/pledge.2) and
[unveil(2)](https://man.openbsd.org/unveil.2).

When `net` is enabled, a dedicated worker is forked before sandboxing and is
the only process retaining `inet` and `dns`.  The display process normally
pledges `stdio unix`, plus `vminfo` when memory display uses `VM_UVMEXP` and
`route` when `getifaddrs(3)` supplies network/VPN state.

Public addresses are fetched over plain HTTP and are parsed with
`inet_pton(3)` before display.

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

Uses LLVM/Clang by default and requires the OpenBSD `comp` set plus Xenocara
`xbase` headers/libraries including `libXft`.

```sh
git clone https://github.com/daviduhden/openbar.git
cd openbar
make
```

## Installing

```sh
doas make install
```

## Uninstalling

```sh
doas make uninstall
```

## References

- [cwm(1)](https://man.openbsd.org/cwm.1)
- [openbarrc(5)](openbarrc.5)
- [pledge(2)](https://man.openbsd.org/pledge.2)
- [unveil(2)](https://man.openbsd.org/unveil.2)
- [Xft(3)](https://man.openbsd.org/Xft.3)
- [X(7)](https://man.openbsd.org/X.7)

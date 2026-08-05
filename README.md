# OpenBar

`openbar` is a status bar written in C designed for `cwm` (or other X11 window managers) on [OpenBSD](https://www.openbsd.org). Any contribution is highly appreciated.

> Currently, this project does not support other operating systems, nor does the maintainer have any intention of working on it.

**CAVEATS:** This version is still in development and testing. It has been tested on a few machines, but it may not work on all systems and could potentially cause issues. Use with caution and at your own risk. Feedback is welcome and appreciated.

## Features

`openbar` currently supports the following features:
- Displaying a "logo" or name
- Hostname
- CPU speed and temperature
- Free memory
- Load average
- Battery status
- Public IP address
- Private IP address
- VPN connection status

If the CPU has no sensors or is not supported, it will display an "x" next to the CPU speed, which is common in VMs or older machines.

## Usage

`openbar` loads configuration from, in order:

1. A custom path provided with `-c`.
2. `~/.openbar.conf` if it exists.
3. The system-wide configuration file located at `/etc/openbar.conf`.

You can find an example configuration file with all available options [here](openbar.conf):

```ini
logo=OpenBar
date=yes
cpu=yes
bat=no
mem=yes
load=yes
net=yes
hostname=yes
interface=iwm0
vpn=yes
```

The "logo" and "interface" options are configurable. "logo" may contain spaces, and "interface" is used to get the internal IPv4 address of your machine. Keys are matched exactly; leading/trailing whitespace is ignored and full lines beginning with `#` are comments.

The other options are straightforward: set to "yes" to display the information on `openbar`, and "no" to hide it.

## Xresources

You can customize the font and colors using Xresources entries:

```Xresources
openbar.font: fixed
openbar.foreground: black
openbar.background: white
```

Defaults are `fixed`, `black`, and `white` if no entries are set.

## Security

`openbar` reads its configuration and opens X11 before restricting itself. Its filesystem view is then locked to the X11 socket directory, the active X authority file, and `/dev/apm` when battery display is enabled.

When `net=yes`, a dedicated worker is forked before sandboxing and is the only process retaining `inet` and `dns`. The display process normally pledges `stdio unix`, plus `vminfo` when memory display uses `VM_UVMEXP` and `route` when `getifaddrs(3)` supplies network/VPN state. CPU MHz is sampled once before pledge; later temperature reads use the always-permitted `HW_SENSORS` selector.

OpenBSD exposes no pledge promise for repeated `APM_IOC_GETPOWER` ioctls. Consequently, `bat=yes` keeps `unveil` but disables pledge in the display process and emits a warning. Use `bat=no` (the shipped default) when pledge confinement is more important than battery status. Public addresses are fetched over plain HTTP and must be treated as informational, untrusted text; they are parsed with `inet_pton(3)` before display.

## Display

To display `openbar` in your window manager, create an X11 window to show the output. Add a similar line to your `.xsession` file:

```sh
# Keep the window manager as the X session's foreground process.
openbar &
exec cwm
```

For `cwm`, you might want to leave a gap at the top of the screen for `openbar` by adding the following to your `.cwmrc` file:

```sh
gap 30
```

## Building

Building uses LLVM/Clang by default and requires the version-matched OpenBSD `comp` set plus the Xenocara `xbase` headers/libraries. `CC` remains overridable for development. Running it also requires an X server from `xserv`. Git is a package and is needed only for this clone workflow:

```sh
git clone https://github.com/daviduhden/openbar.git
```
```sh
cd openbar
```
```sh
make
```

## Installing

By default, `openbar` will be installed in `/usr/local/bin` and the configuration file in `/etc/openbar.conf`. Ensure you have the appropriate permissions and then run:

```sh
doas make install
```

## Uninstalling

To uninstall `openbar`, run:

```sh
doas make uninstall
```

The install target backs up an existing `/etc/openbar.conf` as `/etc/openbar.conf.old`.

## References

- [pledge(2)](https://man.openbsd.org/pledge.2)
- [unveil(2)](https://man.openbsd.org/unveil.2)
- [sysctl(2)](https://man.openbsd.org/sysctl.2)
- [apm(4)](https://man.openbsd.org/apm.4)
- [X(7)](https://man.openbsd.org/X.7)

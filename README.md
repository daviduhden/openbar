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

`openbar` uses a configuration file that should be located in your home directory as `.openbar.conf` or `openbar.conf`. If these files are not found in your home directory, `openbar` will fall back to using the system-wide configuration file located at `/etc/openbar.conf`. You can find an example configuration file with all available options [here](openbar.conf):

```ini
logo=OpenBar
date=yes
cpu=yes
bat=yes
mem=yes
load=yes
net=yes
hostname=yes
interface=iwm0
vpn=yes
```

The "logo" and "interface" options are configurable. "logo" will display any specified text, and "interface" is used to get the internal IP address of your machine.

The other options are straightforward: set to "yes" to display the information on `openbar`, and "no" to hide it.

## Display

To display `openbar` in your window manager, create an X11 window to show the output. Add a similar line to your `.xsession` file:

```sh
# run cwm along with openbar
exec cwm & exec openbar
```

For `cwm`, you might want to leave a gap at the top of the screen for `openbar` by adding the following to your `.cwmrc` file:

```sh
gap 30
```

## Building

All necessary tools are included in your OpenBSD base installation. To clone the repository and build the project, use the following commands:

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
make install
```

## Uninstalling

To uninstall `openbar`, run:

```sh
make uninstall
```

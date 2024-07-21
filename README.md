# OpenBar

`openbar` is a status bar written in C99 designed for `cwm` (or other window managers) on OpenBSD. 

> Currently this project does not support other operating systems nor does the maintainer have any intention of working on it.

**CAVEATS:** This version is still in development and testing. It has been tested on a few machines, but it may not work on all systems and could potentially cause issues. Use with caution and at your own risk. Feedback is welcome and appreciated.

## Features

`openbar` currently supports the following features:
- Displaying a "logo" or name
- Hostname
- CPU speed and temperature
- Free memory
- Window ID
- Load average
- Battery status (tested primarily on ThinkPads)
- Public IP address
- Private IP address
- VPN connection status

The battery percentage will turn red if the machine is not connected to AC power. The VPN status will display "No VPN" in red if no WireGuard interface is present or if the tunnel is down (still in testing).

If the CPU has no sensors or is not supported, it will display an "x" next to the CPU speed, which is common in VMs or older machines (not extensively tested).

## Usage

`openbar` now utilizes a configuration file, which should be located at `~/.openbar.conf` after installation. An example configuration file is available [here](openbar.conf) with all available options:

```
logo=openbar
date=yes
cpu=yes
bat=yes
mem=yes
load=yes
net=yes
winid=yes
hostname=yes
interface=iwm0
vpn=yes
```

The "logo" and "interface" options are configurable. "logo" will display any specified text, and "interface" is used to get the internal IP address of your machine.

The other options are straightforward: set to "yes" to display the information on `openbar`, and "no" to hide it.

## Display

To display `openbar` in your window manager, create an X11 window to show the output. Add a similar line to your `.xsession` file:

```
...
# openbar
exec ~/bin/openbar &
...
exec cwm
```

For `cwm`, you might want to leave a gap at the top of the screen for `openbar` by adding the following to your `.cwmrc` file:

```
...
gap 35 5 5 5
...
```

## Dependencies

Currently, `curl` is required to check your public IP address.

Install it using:

```
$ doas pkg_add curl
```

## Build

All necessary tools are included in your OpenBSD base installation. To clone the repository and build the project, use the following commands:

```
$ git clone https://github.com/gonzalo-/openbarc/
$ cd openbarc
$ make
```

## Installation

`openbar` will be installed in your `${HOME}/bin` directory. If you want to place it elsewhere, modify the Makefile accordingly. Ensure you have a `~/bin` directory, then run:

```
$ make install
```

## Uninstall

To uninstall `openbar`, run:

```
$ make uninstall
```

# ubved

`ubved` is a small OpenWrt daemon that discovers supported ESP devices over
USB serial and exposes device operations through `ubus`.

The project is part of the [`tuyamotd-package`](https://github.com/mondaidai/tuyamotd-package)
OpenWrt package. The package provides the intended build, dependency, and
installation integration for OpenWrt.

## Building

Follow the OpenWrt build instructions and dependency requirements in the
[`tuyamotd-package`](https://github.com/mondaidai/tuyamotd-package) repository;
it is the intended way to build and install `ubved`.

## ubus API

The daemon registers the `ubved` ubus object with these methods:

- `devices` - list detected supported devices
- `on` and `off` - set a GPIO pin state
- `get` - read sensor data from a device

The daemon logs through syslog and communicates with devices using JSON over
serial.
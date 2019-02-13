# Interception Tools

A minimal composable infrastructure on top of [`libudev`][libudev] and
[`libevdev`][libevdev].

The _Interception Tools_ is a small set of utilities for operating on input
events of `evdev` devices:

### udevmon

```
udevmon - monitor input devices for launching tasks

usage: udevmon [-h] -c configuration.yaml

options:
    -h                    show this message and exit
    -c configuration.yaml use configuration.yaml as configuration
```

### intercept

```
intercept - redirect device input events to stdout

usage: intercept [-h] [-g] devnode

options:
    -h        show this message and exit
    -g        grab device
    devnode   path of device to capture events from
```

### uinput

```
uinput - redirect device input events from stdin to virtual device

usage: uinput [-h] [-p] [-c device.yaml]... [-d devnode]...

options:
    -h                show this message and exit
    -p                show resulting YAML device description merge and exit
    -c device.yaml    merge YAML device description to resulting virtual device
    -d devnode        merge reference device description to resulting virtual device
```

## Dependencies

- [CMake][cmake]
- [libevdev][]
- [yaml-cpp][]

## Official Plugins

- [caps2esc][]
- [space2meta][]
- [hideaway][]

## Execution

The following daemonized sample execution increases `udevmon` priority (since
it'll be responsible for a vital input device, just to make sure it stays
responsible):

`sudo nice -n -20 udevmon -c udevmon.yaml >udevmon.log 2>udevmon.err &`

## Installation

I'm maintaining an Archlinux package on AUR:

- <https://aur.archlinux.org/packages/interception-tools>

It wraps `udevmon` in a systemd service that can be easily started, stopped
and enabled to execute on boot. The service expects the configuration file at
`/etc/udevmon.yaml`.

I don't use Ubuntu and recommend Archlinux instead, as it provides the AUR, so I
don't maintain PPAs. For more information on Ubuntu/Debian installation check
this:

- <https://askubuntu.com/questions/979359/how-do-i-install-caps2esc>

## Building

```
$ git clone git@gitlab.com:interception/linux/tools.git
$ cd tools
$ mkdir build
$ cd build
$ cmake ..
$ make
```

## How It Works

First, lets check where [`libevdev`][libevdev] sits in the input system from its
documentation:

> ### Where does libevdev sit?
>
> libevdev is essentially a read(2) on steroids for /dev/input/eventX devices.
> It sits below the process that handles input events, in between the kernel and
> that process. In the simplest case, e.g. an evtest-like tool the stack would
> look like this:
>
> `kernel → libevdev → evtest`
>
> For X.Org input modules, the stack would look like this:
>
> `kernel → libevdev → xf86-input-evdev → X server → X client`
>
> For Weston/Wayland, the stack would look like this:
>
> `kernel → libevdev → Weston → Wayland client`
>
> libevdev does **not** have knowledge of X clients or Wayland clients, it is
> too low in the stack.

The tools here relying on [`libevdev`][libevdev] are `intercept` and `uinput`.
`intercept`'s purpose is to capture input from a given device (optionally
_grabbing_ it) and write such raw input to `stdout`. `uinput` does the reverse,
it receives raw input from `stdin` and write it to a virtual `uinput` device
created by cloning characteristics of real devices, from YAML configuration, or
both.

So, assuming `$DEVNODE` as the path of the device, something like
`/dev/input/by-id/some-kbd-id`, the following results in a no-op:

`intercept -g $DEVNODE | uinput -d $DEVNODE`

In this case using `-g` is important so that the target device is _grabbed_ for
exclusive access, allowing the new virtual device created by `uinput` to
substitute it completely: we grab it and others can grab the clone.

Now additional processing can be added in the middle easily. For example, with
this trivial program (let's call it `x2y`):

```c
#include <stdio.h>
#include <stdlib.h>
#include <linux/input.h>

int main(void) {
    struct input_event event;

    setbuf(stdin, NULL), setbuf(stdout, NULL);

    while (fread(&event, sizeof(event), 1, stdin) == 1) {
        if (event.type == EV_KEY && event.code == KEY_X)
            event.code = KEY_Y;

        fwrite(&event, sizeof(event), 1, stdout);
    }
}
```

We replace `x` and `y` for a given keyboard with:

`intercept -g $DEVNODE | x2y | uinput -d $DEVNODE`

Now if we also have a `y2z` program we can compose both as

`intercept -g $DEVNODE | x2y | y2z | uinput -d $DEVNODE`

or as

`intercept -g $DEVNODE | y2z | x2y | uinput -d $DEVNODE`

and notice how the composition order `x2y | y2z` vs `y2z | x2y` is relevant in
this case. The first most probably doesn't produce the desired composition
because one affects the other and the final behavior actually becomes `x2z` and
`y2z`, which doesn't happen in the later case.

**The `uinput` tool has another purpose besides emulation which is just to print
a device's description in YAML format**. `uinput -p -d /dev/input/by-id/my-kbd`
prints `my-kbd` characteristics in YAML, which itself can be feed back to
`uinput` as `uinput -c my-kbd.yaml`. It can also merge device and YAML
characteristics, for example,

```
uinput -p -d /dev/input/by-id/my-kbd -d /dev/input/by-id/my-mouse -c my-extra.yaml
```

merges `my-kbd`, `my-mouse` and `my-extra.yaml` into a single YAML output. The
characteristics that aren't lists are "merged" by overriding the previous when
they are present on both inputs. This allows creating hybrid virtual devices
that act as both keyboard and mouse, for example.

Explicitly calling `intercept` and `uinput` on specific devices can be
cumbersome, that's where `udevmon` enters the scene. `udevmon` accepts a YAML
configuration with a list of _jobs_ (`sh` commands) to be executed in case the
device matches a given description. For example:

```yaml
- JOB: "intercept -g $DEVNODE | y2z | x2y | uinput -d $DEVNODE"
  DEVICE:
    EVENTS:
      EV_KEY: [KEY_X, KEY_Y]
```

Calling `udevmon` with this configuration sets it to launch the given command
for whatever device that responds to `KEY_X` or `KEY_Y`. It
will monitor for any device that is already attached or that gets attached. When
executing the task the `$DEVNODE` environment variable is set to the path of the
matching device. The "full" YAML based spec is as follows:

```yaml
- JOB:              S
  DEVICE:
    NAME:           R
    LOCATION:       R
    PRODUCT:        R
    VENDOR:         R
    BUSTYPE:        R
    DRIVER_VERSION: R
    PROPERTIES:     LP
    EVENTS:
      EV_KEY:       LE
      EV_REL:       LE
      ...
```

Where:

- `S`: `sh` command string.
- `R`: regular expression string.
- `LP`: list of all properties (name or code) the device must have.
- `LE`: list of any events (name or code) of a given type the device can respond.
- The regular expression grammar supported is [Modified ECMAScript][ecmascript].
- There can be any number of jobs.
- Empty event list means the device should respond to whatever event of the
  given event type.
- Property names and event types and names are taken from [`<linux/input-event-codes.h>`][input-event-codes].

## Software Alternatives

- [mxk](http://welz.org.za/projects/mxk)
- [uinput-mapper](https://github.com/MerlijnWajer/uinput-mapper/)

[cmake]: https://cmake.org
[uinput]: https://www.kernel.org/doc/html/latest/input/uinput.html
[libudev]: https://www.freedesktop.org/software/systemd/man/libudev.html
[libevdev]: https://www.freedesktop.org/software/libevdev/doc/latest/index.html
[yaml-cpp]: https://github.com/jbeder/yaml-cpp
[caps2esc]: https://gitlab.com/interception/linux/plugins/caps2esc
[space2meta]: https://gitlab.com/interception/linux/plugins/space2meta
[hideaway]: https://gitlab.com/interception/linux/plugins/hideaway
[ecmascript]: http://en.cppreference.com/w/cpp/regex/ecmascript
[input-event-codes]: https://github.com/torvalds/linux/blob/master/include/uapi/linux/input-event-codes.h

## License

_Interception Tools_ is **dual-licensed**.

To be embedded and redistributed as part of a proprietary solution, contact me
at francisco@oblita.com for commercial licensing, otherwise it's under

<a href="https://gitlab.com/interception/linux/tools/blob/master/LICENSE.md">
    <img src="https://www.gnu.org/graphics/gplv3-127x51.png" alt="GPLv3">
</a>

Copyright © 2017 Francisco Lopes da Silva

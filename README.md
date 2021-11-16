# Interception Tools

A minimal composable infrastructure on top of [`libudev`][libudev] and
[`libevdev`][libevdev].

The _Interception Tools_ is a small set of utilities for operating on input
events of `evdev` devices:

### udevmon

```text
udevmon - monitor input devices for launching tasks

usage: udevmon [-h | -c configuration.yaml]

options:
    -h                    show this message and exit
    -c configuration.yaml use configuration.yaml as configuration

/etc/interception/udevmon.d/*.yaml is also read if present
```

### intercept

```text
intercept - redirect device input events to stdout

usage: intercept [-h | [-g] devnode]

options:
    -h        show this message and exit
    -g        grab device
    devnode   path of device to capture events from
```

### uinput

```text
uinput - redirect device input events from stdin to virtual device

usage: uinput [-h | [-p] [-c device.yaml] [-d devnode]]

options:
    -h                show this message and exit
    -p                show resulting YAML device description merge and exit
    -c device.yaml    merge YAML device description to resulting virtual
                      device (repeatable)
    -d devnode        merge reference device description to resulting virtual
                      device (repeatable)
```

### mux

```text
mux - mux streams of input events

usage: mux [-h | [-s size] -c name | [-i name] [-o name]]

options:
    -h        show this message and exit
    -s size   muxer's queue size (default: 100)
    -c name   name of muxer to create (repeatable)
    -i name   name of muxer to read input from or switch on
              (repeatable in switch mode)
    -o name   name of muxer to write output to (repeatable)
```

## Runtime dependencies

- [libevdev][]
- [libudev][]
- [yaml-cpp][]
- [glibc][]

## Build dependencies

- [CMake][cmake]
- [Boost.Interprocess][interprocess]

## Additional Tools

- [uswitch][]: _redirect stdin to a muxer if logged user matches_
- [xswitch][]: _redirect stdin to a muxer if window matches_

## Official Plugins

- [caps2esc][]: _transforming the most useless key ever in the most useful one_
- [space2meta][]: _turn your space key into the meta key when chorded to
  another key (on key release only)_
- [hideaway][]: _move the mouse pointer out of sight after a couple of seconds_
- [dual-function-keys][]: _tap for one key, hold for another_

## Some Community Plugins

- [ralt2hyper][]: _Remap Right Alt (commonly AltGr) to Hyper (i.e. Control, Alt and Super)_
- [chorded_keymap][]
- [interception-vimproved][]
- [interception-k2k][]

## Execution

The following daemonized sample execution increases `udevmon` priority (since
it'll be responsible for a vital input device, just to make sure it stays
responsible):

```text
$ sudo nice -n -20 udevmon -c udevmon.yaml >udevmon.log 2>udevmon.err &
```

The usual route, though, is simply to use the provided systemd unit or OpenRC
init script.

## Installation

### Archlinux

It's available from [community](https://archlinux.org/packages/community/x86_64/interception-tools/):

```text
$ pacman -S interception-tools
```
### Void Linux

```text
$ xbps-install -S interception-tools
```

### Ubuntu ([independent package][ubuntu])

```text
sudo add-apt-repository ppa:deafmute/interception
sudo apt install interception-tools
```

<sub>For Debian and other derivatives you can download directly at
<https://launchpad.net/~deafmute/+archive/ubuntu/interception/+packages>.</sub>

Or if building from sources, these are the dependencies:

```text
$ sudo apt install cmake libevdev-dev libudev-dev libyaml-cpp-dev libboost-dev
```

[ubuntu]: https://gitlab.com/interception/linux/tools/-/issues/38

### Fedora ([independent package][fedora])

```text
$ sudo dnf copr enable fszymanski/interception-tools
$ sudo dnf install interception-tools
```

Or if building from sources, these are the dependencies:

```text
$ dnf install cmake libevdev-devel systemd-devel yaml-cpp-devel boost-devel
```

[fedora]: https://gitlab.com/interception/linux/tools/-/merge_requests/11

## Building

```text
$ git clone https://gitlab.com/interception/linux/tools.git interception-tools
$ cd interception-tools
$ cmake -B build -DCMAKE_BUILD_TYPE=Release
$ cmake --build build
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
    setbuf(stdin, NULL), setbuf(stdout, NULL);

    struct input_event event;
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
`y2z`, which doesn't happen in the later composition.

**The `uinput` tool has another purpose besides emulation which is just to print
a device's description in YAML format**. `uinput -p -d /dev/input/by-id/my-kbd`
prints `my-kbd` characteristics in YAML, which itself can be fed back to
`uinput` as `uinput -c my-kbd.yaml`. It can also merge device and YAML
characteristics, for example,

```text
uinput -p -d /dev/input/by-id/my-kbd -d /dev/input/by-id/my-mouse -c my-extra.yaml
```

merges `my-kbd`, `my-mouse` and `my-extra.yaml` into a single YAML output (the
characteristics that aren't lists are “merged” by overriding the previous when
they are present on both inputs). This allows creating hybrid virtual devices
that, for example, act as both keyboard and mouse (see caveats section on
hybrid devices).

Explicitly calling `intercept` and `uinput` on specific devices can be
cumbersome, that's where `udevmon` helps. `udevmon` accepts a YAML
configuration with a list of _jobs_ (`sh` commands by default) to be executed
in case the device matches a given description. For example:

```yaml
- JOB: intercept -g $DEVNODE | y2z | x2y | uinput -d $DEVNODE
  DEVICE:
    EVENTS:
      EV_KEY: [KEY_X, KEY_Y]
```

Calling `udevmon` with this configuration sets it to launch the given command
for whatever device that responds to `KEY_X` or `KEY_Y`. It will monitor for
any device that is already attached or that gets attached. The `$DEVNODE`
environment variable is set to the path of the matching device.

To only match devices that produce _all the given events_ instead of just _any
of the given events_, you do:

```yaml
- JOB: intercept -g $DEVNODE | magic |  uinput -d $DEVNODE
  DEVICE:
    EVENTS:
      EV_KEY: [KEY_A, KEY_B, [KEY_X, KEY_Y]]
```

Which will match if the device responds to either `KEY_A`, `KEY_B`, `KEY_X`
_and_ `KEY_Y`.

If device specific interception is more desirable, it's simpler to use the
`LINK` configuration as the device selector, for example:

```yaml
- JOB: intercept -g $DEVNODE | caps2esc | uinput -d $DEVNODE
  DEVICE:
    LINK: /dev/input/by-id/usb-SEMITEK_USB-HID_Gaming_Keyboard_SN0000000001-event-kbd
```

This way, only [the device that produces that link][sk61] will have
[caps2esc][] applied.

A more involved configuration may need to combine (or just observe) the input
of two devices to make decisions. That's where the `mux` tool comes at hand:

```yaml
- CMD: mux -c caps2esc
- JOB: mux -i caps2esc | caps2esc | uinput -c /etc/interception/gaming-keyboard.yaml
- JOB: intercept -g $DEVNODE | mux -o caps2esc
  DEVICE:
    LINK: /dev/input/by-id/usb-SEMITEK_USB-HID_Gaming_Keyboard_SN0000000001-event-kbd
- JOB: intercept $DEVNODE | mux -o caps2esc
  DEVICE:
    LINK: /dev/input/by-id/usb-Logitech_USB_Receiver-if02-event-mouse
```

The `mux` tool serves to combine multiple pipelines into one. A _muxer_ first
needs to be created with a name in a `CMD` (differently from `JOB`s, `CMD`s are
executed sequentially when the service starts and are waited for successful
termination). The muxer can then be used from multiple pipelines as an output
or as the input of a given pipeline. After the muxer creation, a _standalone
job_ not associated with any device (which makes it just a command executed
when `udevmon` starts, but not waited for) is launched to consume the muxer and
pass what arrives from it to `caps2esc` and, finally, to the virtual device
created from `gaming-keyboard.yaml` (see caveats section on device links).

In the example above, when the keyboard is connected, it's grabbed and its
input events are sent to the “caps2esc” muxer that was initially created.
_Observed_ input (not grabbed) from mouse is also sent to the same muxer. The
buttons of the mouse generate `EV_KEY` events, so `caps2esc` will accept them,
making “Caps Lock + Click” work as “Control + Click”.

As in this case the final target cloned device clones the keyboard, not a
mouse, if mouse events reach it from muxing multiple pipelines, they won't be
reproduced, hence not duplicating the _observed_ mouse events.

If a device happens to match multiple job descriptions, only the first job that
matches gets executed. This allows for device specific jobs, while still having
fallback configurations:

```yaml
- JOB: intercept -g $DEVNODE | caps2esc -m 2 | uinput -d $DEVNODE
  DEVICE:
    LINK: /dev/input/by-id/usb-SEMITEK_USB-HID_Gaming_Keyboard_SN0000000001-event-kbd
- JOB: intercept -g $DEVNODE | caps2esc | uinput -d $DEVNODE
  DEVICE:
    EVENTS:
      EV_KEY: [[KEY_CAPSLOCK, KEY_ESC]]
    LINK: .*-event-kbd
```

In the above example, if an attached keyboard produces the given link,
`caps2esc -m 2` will be applied to it, otherwise, `caps2esc` in default mode
will be applied (_if_ the keyboard has both `KEY_CAPSLOCK` _and_ `KEY_ESC` and a
device link that ends with `-event-kbd`, [to exclude mice that report those
keys][caps2esc-issue-15-note]). Also, note that configuration files found on
`/etc/interception/udevmon.d/` are read first, so you can have device specific
configurations there, and fallbacks on `/etc/interception/udevmon.yaml`.

Besides combining pipelines, the `mux` tool can duplicate them (multiple `-o`s)
and even act as a _switch_, based on activity in other pipelines (`-i` and `-o`
intermixed). Which brings us to our lasting, _slightly complex_, use case:

Let's imagine the following setup:

- You want to grab keyboards (here after referred as `K`) and mice (`M`) and
  combine input coming from these two groups into `KM`, to apply multi device
  chording
- You have a generic filter (`caps2esc`) you want to apply to combined
  keyboard/mouse input
- But when you're using some specific keyboards (`X`), you want the combined
  input (`XM`) to go through a different filter (`caps2esc -m 2`)

Voilà:

```yaml
- CMD: mux -c K -c X -c M -c KM -c XM -c H
- JOB:
    - mux -i M | mux -o KM -i K -o KM -i X -o XM
    - mux -i KM | caps2esc | mux -o H
    - mux -i XM | caps2esc -m 2 | mux -o H
    - mux -i H | uinput -c /etc/interception/hybrid.yaml
- JOB: intercept -g $DEVNODE | mux -o X -o XM
  DEVICE:
    LINK: /dev/input/by-id/usb-SEMITEK_USB-HID_Gaming_Keyboard_SN0000000001-event-kbd
- JOB: intercept -g $DEVNODE | mux -o M
  DEVICE:
    EVENTS:
      EV_KEY: [BTN_LEFT, BTN_TOUCH]
- JOB: intercept -g $DEVNODE | mux -o K -o KM
  DEVICE:
    EVENTS:
      EV_KEY: [[KEY_CAPSLOCK, KEY_ESC]]
    NAME: .*[Kk]eyboard.*
    LINK: .*-event-kbd
```

Don't be afraid as it's pretty simple to break it down.

First, as can be seen, at the bottom we have device detection for three device
groups (as modeled previously).

The keyboard events are consumed and duplicated out, this happens so that
consumption of these events can happen in parallel both for purposes of
checking there's activity in a particular group (`mux -o K …` and `mux -o X
…`), as for the final consumption of keyboard and mouse events muxed together
(`mux … -o KM` and `mux … -o XM`).

The mouse events are consumed and sent to the `M` muxer for further processing.

<sub>_**Notice** that when multiple devices match a given device job
description, a job instance per device will run, so, consuming a muxer (`mux …
-i …`) from device jobs would create a race condition of multiple job instances
competing for the same events of a given muxer. That's why, here, device jobs
solely write to muxers (`mux -o`), which is fine for muxing the events of all
matching devices into a single stream, but the reading of a muxer only happens
in standalone jobs, for which there's only one instance running for its
consumption. Also, muxer writing doesn't implicate in any problem in case the
device disconnects and its job gets dropped. Dropping a pipeline in reading
state ends up leading to muxer corruption, while standalone jobs only finish
when the `udevmon` service is stopped._</sub>

On `mux -i M | mux -o KM -i K -o KM -i X -o XM` we get to the core of the
design. Here `M` is consumed and gets redirected either to `KM`, if there's
activity in `K` (`-i K -o KM`), or `XM`, if there's activity in `X` (`-i X -o
XM`). The first `-o KM` makes `KM` the default route for input coming from `M`
(in case no activity ever happens in `K` or `X`).

In the end we only have `KM` and `XM` to consume input from, as we have that
input from group `K` goes to `KM`, input from group `X` goes to `XM`, and input
from group `M` goes either to `KM` or `XM`.

For `KM` then we apply `caps2esc`, but for `XM` we apply `caps2esc -m 2`. And
regardless the route that input goes through, we send it to the final `H`
endpoint, which gets consumed by a hybrid virtual device (e.g. `sudo uinput -p
-d /dev/input/by-id/usb-Logitech_USB_Receiver-if02-event-mouse -d
/dev/input/by-id/usb-SEMITEK_USB-HID_Gaming_Keyboard_SN0000000001-event-kbd |
sudo tee /etc/interception/hybrid.yaml`).

As a final note on the `mux` tool in switch mode, `mux -i e | mux -o o -i i`
would redirect `e` to `o` by default, but once there's activity in `i`, `e` is
redirected to nowhere. And if you have `mux -i e | mux -i i -o o`, as there
isn't any default output, `e` gets redirected to `o` only after first detected
activity in `i`.

Besides that, you can have `-i x -i y -i etc -o z` to redirect to `z` out of
activity either from `x`, `y` or `etc` and you can have `-i x -o y -o z -o etc`
to redirect to `y`, `z` and `etc` out of `x` activity. Both aspects can be
combined in `-i w -i x -o y -o z`.

The “full” YAML based spec is as follows:

```yaml
SHELL:              LA
---
- CMD:              S | LS
- JOB:              S | LS
  DEVICE:
    LINK:           R
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

- `LA`: shell replacement, like `[zsh, -c]`, default is `[sh, -c]`.
- `S` | `LS` : shell command string, or a list of shell command strings.
- `R`: regular expression string.
- `LP`: list of any _properties or set of properties_ (by name or code), the
  device can have.
- `LE`: list of any _events or set of events_ (by name or code), of a given
  type, that the device can produce.
- The regular expression grammar supported is [Modified ECMAScript][ecmascript].
- There can be any number of jobs.
- An empty event list means the device should respond to whatever event of the
  given event type.
- Property names and event types and names are taken from
  [`<linux/input-event-codes.h>`][input-event-codes].

## Plugin Guidelines

### Correct synthesization of event sequences

A plugin that's going to synthesize event sequences programmatically (not the
case for simple key swapping, like `x2y` above), for keyboard at least, is
required to provide `EV_SYN` events and a short delay (to have different
event timestamps), between key events. This is what happens when you type on a
real keyboard, and [has been proved necessary][ev-syn] for applications to
behave well. As a general guideline, one should explore how real devices behave
while generating events (with `evtest` for example) for mimicking them with
success.

## Caveats

### Correct process priority

Always use a high process priority (low _niceness_ level, `udevmon.service`
uses `-20`) when executing tools that manipulate input, otherwise you may get
[unwanted effects][niceness]. Without _Interception Tools_, your input is
treated with high priority at kernel level, and you should try to resemble that
now on user mode, which is the level where the tools run.

### Hybrid device configurations

_Note that hybrid devices may not always work_.

For example, on my PC, merging my mouse and keyboard into a single device does
create a working hybrid device that can respond for all their events, but on an
old laptop, merging the built-in i8042 keyboard and i8042 touchpad created a
non-working hybrid that can only respond for the touchpad's events (it seems
`EV_ABS` and keyboard `EV_KEY`s didn't work together in this machine). To fix
that I stored the configurations in different files, and checked they didn't
respond for any identical events. Then I used these two as virtual output for
the same muxed stream of events, but given that the virtual devices don't
respond for the same events, these don't get duplicated but instead effectively
get split from a single stream into their relevant virtual devices:

```yaml
- CMD: mux -c caps2esc -c keyboard -c mouse
- JOB:
    - mux -i caps2esc | caps2esc | mux -o keyboard -o mouse
    - mux -i keyboard | uinput -c /etc/interception/keyboard.yaml
    - mux -i mouse | uinput -c /etc/interception/mouse.yaml
- JOB: intercept -g $DEVNODE | mux -o caps2esc
  DEVICE:
    NAME: AT Translated Set 2 keyboard
- JOB: intercept -g $DEVNODE | mux -o caps2esc
  DEVICE:
    NAME: ETPS/2 Elantech Touchpad
```

### Device links

Depending on the system, device links (`by-id`, `by-path`, etc) may not exist
at all, or not be readily available when the machine boots, which may make it
unreliable to refer to them on _standalone jobs_ which execute when `udevmon`
starts on boot. It's safe to refer to them on device jobs, as these only start
when the link actually becomes present.

Hence, on standalone jobs it's generally better practice to refer to previously
stored YAML device configurations instead.

On a machine that produce links, referring to them on standalone jobs may or
may not work on boot. You may verify that by rebooting and checking whether
`udevmon` give errors on boot or not.

## Software Alternatives

- [mxk](http://welz.org.za/projects/mxk)
- [uinput-mapper](https://github.com/MerlijnWajer/uinput-mapper/)

[cmake]: https://cmake.org
[uinput]: https://www.kernel.org/doc/html/latest/input/uinput.html
[libudev]: https://www.freedesktop.org/software/systemd/man/libudev.html
[libevdev]: https://www.freedesktop.org/software/libevdev/doc/latest/index.html
[yaml-cpp]: https://github.com/jbeder/yaml-cpp
[glibc]: https://www.gnu.org/software/libc
[interprocess]: https://www.boost.org/doc/libs/release/libs/interprocess
[uswitch]: https://gitlab.com/interception/linux/plugins/uswitch
[xswitch]: https://gitlab.com/interception/linux/plugins/xswitch
[caps2esc]: https://gitlab.com/interception/linux/plugins/caps2esc
[space2meta]: https://gitlab.com/interception/linux/plugins/space2meta
[hideaway]: https://gitlab.com/interception/linux/plugins/hideaway
[dual-function-keys]: https://gitlab.com/interception/linux/plugins/dual-function-keys
[ralt2hyper]: https://gitlab.com/oarmstrong/ralt2hyper
[chorded_keymap]: https://gitlab.com/wsha/chorded_keymap
[interception-vimproved]: https://github.com/maricn/interception-vimproved
[interception-k2k]: https://github.com/zsugabubus/interception-k2k
[sk61]: https://epomaker.com/products/epomaker-sk61
[caps2esc-issue-15-note]: https://gitlab.com/interception/linux/plugins/caps2esc/-/issues/15#note_476593423
[ecmascript]: http://en.cppreference.com/w/cpp/regex/ecmascript
[input-event-codes]: https://github.com/torvalds/linux/blob/master/include/uapi/linux/input-event-codes.h
[ev-syn]: https://gitlab.com/interception/linux/tools/-/issues/29#note_474260470
[niceness]: https://gitlab.com/interception/linux/tools/-/issues/29#note_474310306

## License

_Interception Tools_ is **dual-licensed**.

To be embedded and redistributed as part of a proprietary solution, contact me
at francisco+interception@nosubstance.me for commercial licensing, otherwise
it's under

<a href="https://gitlab.com/interception/linux/tools/blob/master/LICENSE.md">
    <img src="https://www.gnu.org/graphics/gplv3-127x51.png" alt="GPLv3">
</a>

Copyright © 2017 Francisco Lopes da Silva

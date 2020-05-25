# lisgd

Lisgd (libinput **synthetic** gesture daemon) lets you bind gestures based on
libinput touch events to run specific commands to execute. For example,
dragging left to right with one finger could execute a particular command
like launching a terminal. L-R, R-L, U-D, and D-U swipe gestures are
supported with 1 through n fingers.

Unlike other libinput gesture daemons, lisgd uses touch events to
recognize **synthetic swipe gestures** rather than using the *libinput*'s
gesture events. The advantage of this is that the synthetic gestures
you define via lisgd can be used on touchscreens, which normal libinput
gestures don't support.

This program was built for use on the [Pinephone](https://www.pine64.org/pinephone/);
however it could be used in general for any device that supports touch events,
like laptop touchscreens or similar. You may want to adjust the threshold
depending on the device you're using.

## Configuration
Configuration can be done in two ways:

1. Through a suckless style `config.h`; see the `config.def.h`
2. Through commandline flags which override the default config.h values

### Suckless-style config.h based configuration
Copy the example `config.def.h` configuration to `config.h`.

### Commandline flags based configuration
Flags:

- **-g [fingers,start,end,command]**: Defines a gesture wherein fingers is a integer, start/end are {l,r,d,u}, and command is the command to execute
  - Example: `lisgd -g "1,l,r,notify-send swiped lr" -g "1,r,l,noitfy-send swiped rl"`
- **-d [devicenodepath]**: Defines the dev filesystem device to monitor
  - Example: `lisgd -d /dev/input/input1`
- **-t [threshold_units]**: Number of libinput units (number) minimum to recognize a gesture
  - Example: `lisgd -t 400`
- **-v**: Verbose mode, useful for debugging
  - Example: `lisgd -v`

Full commandline-based configuration example:

```
lisgd -d /dev/input/input1 -g "1,l,r,notify-send swiped lr" -t 200 -v
```

### TODO
- Diagnol swipe gestures
- Gestures recognition based on screenspace executed in

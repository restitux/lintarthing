lintarthing
====

Use Wii guitars with a Dolphinbar or Bluetooth on Linux
----
This script maps Wii guitar inputs to Xbox 360 controller events when connected to a Dolphinbar or via Bluetooth.


The Dolphinbar or Bluetooth receiver takes Wiimote HID events and emits them via a virtual Xbox 360 controller.
This behavior is incompatible with existing Wiimote drivers on Linux. This
script serves as a replacement for [WiitarThing](https://github.com/Meowmaritus/WiitarThing) on linux.

This script requires the [python-uinput](https://github.com/tuomasjjrasanen/python-uinput)
and [pyhidapi](https://github.com/apmorton/pyhidapi) modules,
and that it be run as root to open the Wiimote HID device.

Thanks to [WiiBrew](wiibrew.org) for their extensive documentation on the Wiimote protocols.

Name inspired by the awesome WiitarThing,
which solves this problem for Windows.

Usage
----
This script works almost exactly like WiitarThing with a few exceptions.

1. There is no driver to install.
2. This script only supports original Wiimotes as of May 4, 2022
3. There is a special way you need to use to connect the virtual controller to [Clone Hero](https://clonehero.net) instead of the wiimote itself.

In order to assign the virtual controller to Clone Hero, you first hold down the A button on the wiimote,
and then press the Assign Controller button in Clone Hero.
If you don't do this, it will assign the wiimote itself, which will not work.

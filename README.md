lintarthing
====

Use Wii guitars with a Dolphinbar or Bluetooth on Linux
----
Fellow GitHub user [restitux](https://github.com/restitux) and I have created a python script and a c++ excecutable which map Wii guitar inputs to Xbox 360 controller events when connected to a Dolphinbar or via Bluetooth. These are separate scripts and are not meant to be ran together.
The reason we chose to convert to Xbox 360 events was because [Clone Hero](https://clonehero.net) has built-in default controls for it.

The scripts use Dolphinbar or Bluetooth receivers to take Wiimote HID events and emit them via a uinput virtual gamepad which mimics an Xbox 360 controller.
These scripts serve as a replacement for [WiitarThing](https://github.com/Meowmaritus/WiitarThing) on linux.

The python script requires the [python-uinput](https://github.com/tuomasjjrasanen/python-uinput)
and [pyhidapi](https://github.com/apmorton/pyhidapi) modules.
The c++ script requires udev for uinput and hidapi to access HID devices. If you want to compile the script yourself, know that it requires c++17 or above to compile. **Both scripts will either require that you run them as root or that your user account have read/write access to `/dev/hidrawX` files and `/dev/uinput`.**

Thanks to [WiiBrew](wiibrew.org) for their extensive documentation on the Wiimote protocols.

Name inspired by the awesome WiitarThing,
which solves this problem for Windows.

Usage
----
This script works almost exactly like WiitarThing with a few exceptions.

1. There is no driver to install.
2. This script only supports original Wiimotes and guitars as of May 24, 2022.
3. Multiplayer and the joystick not supported as of yet.
4. There is a special method you need to use to connect the virtual controller to [Clone Hero](https://clonehero.net) instead of the wiimote itself.

In order to assign the virtual controller to Clone Hero, you first hold down the A button on the wiimote,
and then press the Assign Controller button in Clone Hero.
It should now say that a controller named "Xbox 360 Controller" is bound to the player.
If you don't do this, it will assign the wiimote itself, which will not work.

Upcoming Features
----
* Multiplayer support
* Joystick support
* Support for drums
lintarthing
====

Use Wii guitars with a Dolphinbar on Linux
----
This script maps Wii guitar inputs to keyboard events when connected to a Dolphinbar.


The Dolphinbar takes Wiimote HID events and emits them via a USB HID device.
This behavior is incompatible with existing Wiimote drivers on Linux. This
script serves as a quick hack to map these inputs to keyboard inputs (which are hardcoded in the script).

This script requires [python-uinput](https://github.com/tuomasjjrasanen/python-uinput) and [pyhidapi](https://github.com/apmorton/pyhidapi). 

Thanks to [WiiBrew](wiibrew.org) for their extensive documentation on the Wiimote protocols.

Name inspired by the awesome [WiitarThing](https://github.com/Meowmaritus/WiitarThing),
which solves this problem for Windows.

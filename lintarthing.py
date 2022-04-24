#!/usr/bin/env python3

import hid
import uinput

class KeyboardDevice():
    def __init__(self, binds):
        self.binds = binds
        self.device = uinput.Device(self.binds.values())

    def emit_button(self, button, pos):
        value = False if pos == 0 else True
        self.device.emit(self.binds[button], value)

class Packet():
    def __init__(self, packet_bytes):
        self.packet_bytes = packet_bytes

    def __repr__(self):
        return ' '.join([format(byte, '02x') for byte in self.packet_bytes])


class GuitarStatus():
    def __init__(self):
        self.buttons = {}
        self.buttons["strum_down"] = False
        self.buttons["strum_up"] = False
        self.buttons["green"] = False
        self.buttons["red"] = False
        self.buttons["yellow"] = False
        self.buttons["blue"] = False
        self.buttons["orange"] = False
        self.buttons["plus"] = False
        self.buttons["minus"] = False


    def update(self, packet):
        packet_bytes = packet.packet_bytes

        if packet_bytes[0] != 0x34:
            raise Exception(f"Packet type {packet_bytes[0]} is not parsable")

        previous_state = set(self.buttons.items())

        self.buttons["strum_down"] = ((packet_bytes[7] & 0x40) >> 6) != 0x1
        self.buttons["strum_up"] = ((packet_bytes[8] & 0x01)) != 0x1
        self.buttons["green"] = ((packet_bytes[8] & 0x10) >> 4) != 0x1
        self.buttons["red"] = ((packet_bytes[8] & 0x40) >> 6) != 0x1
        self.buttons["yellow"] = ((packet_bytes[8] & 0x08) >> 3) != 0x1
        self.buttons["blue"] = ((packet_bytes[8] & 0x20) >> 5) != 0x1
        self.buttons["orange"] = ((packet_bytes[8] & 0x80) >> 7) != 0x1
        self.buttons["plus"] = ((packet_bytes[7] & 0x04) >> 2) != 0x1
        self.buttons["minus"] = ((packet_bytes[7] & 0x10) >> 4) != 0x1

        new_state = set(self.buttons.items())

        diff = previous_state ^ new_state

        to_update = set()
        for item in diff:
            to_update.add(item[0])

        return to_update

    
class Wiimote():
    VID = 0x057e
    PID = 0x0306

    def __init__(self):
       self.device = hid.Device(self.VID, self.PID)

    def read_packet(self):
        return Packet(self.device.read(4000))

    def write_packet(self, packet):
        self.device.write(packet)
        return self.read_packet()

    def write_register(self, address, data):
        address_bytes = address.to_bytes(3, 'big')

        data_size = (data // 0xff) + 1
        data_size_bytes = data_size.to_bytes(1, 'big')

        data_bytes = data.to_bytes(1, 'big')
        data_bytes += b'\x00' * (16 - data_size)

        packet = b'\x16' + b'\x04' + address_bytes + data_size_bytes + data_bytes

        return self.write_packet(packet)

    def read_register(self, address, length):
        address_bytes = address.to_bytes(3, 'big')
        length_bytes = length.to_bytes(2, 'big')

        packet = b'\x17' + b'\x04' + address_bytes + length_bytes

        return self.write_packet(packet)

    def set_reporting_mode(self, continuous, mode):
        if continuous:
            cont_bytes = b'\x04'
        else:
            cont_bytes = b'\x00'

        mode_bytes = mode.to_bytes(1, 'big')

        packet = b'\x12' + cont_bytes + mode_bytes

        return self.write_packet(packet)


def main():
    binds = {
        "strum_down": uinput.KEY_DOWN,
        "strum_up": uinput.KEY_UP,
        "green": uinput.KEY_A,
        "red": uinput.KEY_S,
        "yellow": uinput.KEY_J,
        "blue": uinput.KEY_K,
        "orange": uinput.KEY_L,
        "plus": uinput.KEY_ENTER,
        "minus": uinput.KEY_H,
    }
    keyboard = KeyboardDevice(binds)

    wiimote = Wiimote()
    ret = wiimote.write_register(0xa400f0, 0x55)
    print(ret)

    ret = wiimote.read_register(0xa400fa, 6)
    print(ret)

    ret = wiimote.set_reporting_mode(False, 0x34)
    print(ret)

    status = GuitarStatus()

    while(True):
        packet = wiimote.read_packet()
        #print(packet)
        updated = status.update(packet)

        for key in updated:
            keyboard.emit_button(key, status.buttons[key])

        #print(status.buttons)


if __name__ == "__main__":
    main()

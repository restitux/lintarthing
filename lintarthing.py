#!/usr/bin/env python3

import hid
import uinput
from math import floor

# DATA FORMAT: https://www.wiibrew.org/wiki/Wiimote/Extension_Controllers/Guitar_Hero_(Wii)_Guitars#Data_Format
# each packet is 6 bytes long and returns data in the following format:
#
#          7  6  5  4  3  2  1  0
# Byte 1:       [     Stick X    ]
# Byte 2:       [     Stick Y    ]
# Byte 3:          [  Touch Bar  ]
# Byte 4:          [ Whammy Bar  ]
# Byte 5:    SD    B-    B+
# Byte 6: BO BR BB BG BY       SU
#
# Note: All button & strum inputs are inverted
#
# BO, BR, BB, BG, BY: Fret Buttons
# B+: Base Buttons
# B-: Base Button / Star Power Button
# SU, SD: Strum Up and Strum Down
# Stick X, Stick Y: Joystick axes

class guitar_button_map:
    bt_plus    = 0x04
    bt_minus   = 0x10
    strum_down = 0x40
    
    strum_up   = 0x01
    yellow     = 0x08
    green      = 0x10
    blue       = 0x20
    red        = 0x40
    orange     = 0x80

btn_map = guitar_button_map()

# Touch Bar Data Format:
#
# (TOP)
# Nothing:      0F    0000 1111
# 1st Fret:     04    0000 0100
# 1st + 2nd:    07    0000 0111
# 2nd Fret:     0A    0000 1010
# 2nd + 3rd:    0C/0D 0000 1100 / 0000 1101
# 3rd Fret:     12/13 0001 0010 / 0001 0011
# 3rd + 4th:    14/15 0001 0100 / 0001 0101
# 4th Fret:     17/18 0001 0111 / 0001 1000
# 4th + 5th:    1A    0001 1010
# 5th Fret:     1F    0001 1111
# (BOTTOM)

class guitar_touch_map:
    none      = 0x0f
    f1        = 0x04
    f1_f2     = 0x07
    f2        = 0x0A
    f2_f3     = 0x0C
    f2_f3_alt = 0x0D
    f3        = 0x12
    f3_alt    = 0x13
    f3_f4     = 0x14
    f3_f4_alt = 0x15
    f4        = 0x17
    f4_alt    = 0x18
    f4_f5     = 0x1A
    f5        = 0x1F

touch_map = guitar_touch_map()

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
        self.guitarConnected = False

    def handle_status_report(self, packet_bytes, wiimote, was_requested):
        # if the report was not requested, then we need to send another packet to set the reporting mode
        # https://wiibrew.org/wiki/Wiimote/Protocol#Status_Reporting

        

        battery_percentage = (packet_bytes[6] / 255) * 100
        print("Battery at " + str(floor(battery_percentage * 100) / 100) + "%")
        
        if not was_requested:
            wiimote.set_reporting_mode(False, 0x32)
            flags = packet_bytes[3]
            if not (flags & 0x02) and self.guitarConnected:
                led_num = 1 if (flags >> 4 == 1) else 2 if (flags >> 5 == 1) else 3 if (flags >> 6 == 1) else 4
                self.guitarConnected = False
                print(f"WARNING: Player with LED { led_num } disconnected their wii guitar extension!")

    def handle_read_response(self, packet_bytes):
        if packet_bytes[4] == 0x00 and packet_bytes[5] == 0xfa:
            # This is the extension identifying itself
            ext_id = 0x00
            for i in range(6):
                ext_id |= packet_bytes[6 + i] << (8 * (5 - i))
            if ext_id == 0xa4200103 and self.guitarConnected != True:
                self.guitarConnected = True
                print("Guitar Hero Guitar Detected")

    def update(self, packet, wiimote):
        packet_bytes = packet.packet_bytes
        
        packet_id = packet_bytes[0]
        btn_bytes = []

        if packet_id == 0x20:
            self.handle_status_report(packet_bytes, wiimote, False)
            return set()
        elif packet_id == 0x21:
            self.handle_read_response(packet_bytes)
            return set()
        elif packet_id == 0x22:
            return set()

        elif packet_id == 0x30 or packet_id == 0x31:
            return set()

        elif packet_id == 0x32 or packet_id == 0x34:
            btn_bytes = [packet_bytes[7], packet_bytes[8]]
        
        elif packet_id == 0x35:
            btn_bytes = [packet_bytes[10], packet_bytes[11]]

        elif packet_id == 0x36:
            btn_bytes = [packet_bytes[17], packet_bytes[18]]

        elif packet_id == 0x37:
            btn_bytes = [packet_bytes[20], packet_bytes[21]]

        elif packet_id == 0x3d:
            btn_bytes = [packet_bytes[5], packet_bytes[6]]

        else:
            raise Exception(f"Packet type {hex(packet_id)} is not parsable")

        previous_state = set(self.buttons.items())

        self.buttons["plus"]       = not (btn_bytes[0] & btn_map.bt_plus)
        self.buttons["minus"]      = not (btn_bytes[0] & btn_map.bt_minus)
        
        self.buttons["strum_down"] = not (btn_bytes[0] & btn_map.strum_down)
        self.buttons["strum_up"]   = not (btn_bytes[1] & btn_map.strum_up)

        self.buttons["green"]      = not (btn_bytes[1] & btn_map.green)
        self.buttons["red"]        = not (btn_bytes[1] & btn_map.red)
        self.buttons["yellow"]     = not (btn_bytes[1] & btn_map.yellow)
        self.buttons["blue"]       = not (btn_bytes[1] & btn_map.blue)
        self.buttons["orange"]     = not (btn_bytes[1] & btn_map.orange)

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
    
    # Make transmissions unencrypted
    ret = wiimote.write_register(0xa400f0, 0x55)
    print(ret)

    ret = wiimote.write_register(0xa400fb, 0x00)
    print(ret)

    # get extension identification
    ret = wiimote.read_register(0xa400fa, 6)
    print(ret)

    ret = wiimote.set_reporting_mode(False, 0x32)
    print(ret)

    status = GuitarStatus()

    while(True):
        packet = wiimote.read_packet()
        #print('Packet: ', end='')
        #print(packet)
        updated = status.update(packet, wiimote)

        for key in updated:
            keyboard.emit_button(key, status.buttons[key])

        #print(status.buttons)


if __name__ == "__main__":
    main()

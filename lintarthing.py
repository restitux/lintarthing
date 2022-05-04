#!/usr/bin/env python3

import hid
import uinput
from time import sleep
from math import floor

ABS_MAX_VAL = 32767  # 0x7FFF
ABS_MIN_VAL = -32768 # 0x8000 Two's Compliment

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

class Packet():
    def __init__(self, packet_bytes):
        self.packet_bytes = packet_bytes

    def __repr__(self):
        return ' '.join([format(byte, '02x') for byte in self.packet_bytes])


class GuitarStatus():
    def __init__(self):
        self.inputs = {}
        self.tilt_min = {"x": 100, "y": 130}
        self.tilt_max = {"x": 130, "y": 100}
        self.inputs["strum_down"] = False
        self.inputs["strum_up"] = False
        self.inputs["green"] = False
        self.inputs["red"] = False
        self.inputs["yellow"] = False
        self.inputs["blue"] = False
        self.inputs["orange"] = False
        self.inputs["plus"] = False
        self.inputs["minus"] = False
        self.inputs["whammy_bar"] = 0
        self.whammy_min = 15
        self.whammy_max = 16
        self.guitarConnected = False

    def handle_status_report(self, packet_bytes, wiimote, was_requested):
        # if the report was not requested, then we need to send another packet to set the reporting mode
        # https://wiibrew.org/wiki/Wiimote/Protocol#Status_Reporting

        battery_percentage = (packet_bytes[6] / 255) * 100
        print("Battery at " + str(floor(battery_percentage * 100) / 100) + "%")
        
        if not was_requested:
            wiimote.set_reporting_mode(False, 0x35)
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

    def getSpamButtons(self, packet):
        return packet.packet_bytes[2] == 3

    def getTilt(self, packet):
        packet_bytes = packet.packet_bytes
        # Calibrate Tilt
        xb, yb = [packet_bytes[3], packet_bytes[4]]


        if packet_bytes[2] == 2:
            self.tilt_min={"x": int(xb), "y": int(yb)}
            #print("Min Tilt: ", end='')
            #print(f"X: {int(xb)}, y: {int(yb)}")
        elif packet_bytes[2] == 1:
            self.tilt_max={"x": int(xb), "y": int(yb)}
            #print("Max Tilt Set")
            #print(f"X: {int(xb)}, y: {int(yb)}")
        x_val = int(xb) - 128
        y_val = int(yb) - 128
        percent_x = (x_val - (self.tilt_min["x"] - 128)) / (self.tilt_max["x"] - self.tilt_min["x"])
        percent_y = (y_val - (self.tilt_min["y"] - 128)) / (self.tilt_max["y"] - self.tilt_min["y"])

        total_tilt = -(percent_x + percent_y) / 2
        
        #print(f'Tilt X: {floor(percent_x * 100000) / 1000}%, Tilt Y: {floor(percent_y * 100000) / 1000}%')
        #print(f'Total Tilt: {floor((total_tilt) * 100000) / 1000}%')
        return total_tilt

    def update(self, packet, wiimote):
        packet_bytes = packet.packet_bytes
        
        packet_id = packet_bytes[0]
        btn_bytes = []
        whammy_byte = None

        if packet_id == 0x20:
            self.handle_status_report(packet_bytes, wiimote, False)
            return set()
        elif packet_id == 0x21:
            self.handle_read_response(packet_bytes)
            return set()
        elif packet_id == 0x22:
            return set()

        elif packet_id == 0x30:
            return set()
        
        elif packet_id == 0x31:
            tilt = self.getTilt(packet)
            return set()

        elif packet_id == 0x32 or packet_id == 0x34:
            btn_bytes = [packet_bytes[7], packet_bytes[8]]
            whammy_byte = packet_bytes[6]
        
        elif packet_id == 0x35:
            btn_bytes = [packet_bytes[10], packet_bytes[11]]
            whammy_byte = packet_bytes[9]
            tilt = self.getTilt(packet)

        elif packet_id == 0x36:
            btn_bytes = [packet_bytes[17], packet_bytes[18]]
            whammy_byte = packet_bytes[16]

        elif packet_id == 0x37:
            btn_bytes = [packet_bytes[20], packet_bytes[21]]
            whammy_byte = packet_bytes[19]
            tilt = self.getTilt(packet)

        elif packet_id == 0x3d:
            btn_bytes = [packet_bytes[5], packet_bytes[6]]
            whammy_byte = packet_bytes[4]

        else:
            raise Exception(f"Packet type {hex(packet_id)} is not parsable")

        previous_state = set(self.inputs.items())

        self.inputs["plus"]       = not (btn_bytes[0] & btn_map.bt_plus)
        self.inputs["minus"]      = not (btn_bytes[0] & btn_map.bt_minus)
        
        self.inputs["strum_down"] = not (btn_bytes[0] & btn_map.strum_down)
        self.inputs["strum_up"]   = not (btn_bytes[1] & btn_map.strum_up)

        self.inputs["green"]      = not (btn_bytes[1] & btn_map.green)
        self.inputs["red"]        = not (btn_bytes[1] & btn_map.red)
        self.inputs["yellow"]     = not (btn_bytes[1] & btn_map.yellow)
        self.inputs["blue"]       = not (btn_bytes[1] & btn_map.blue)
        self.inputs["orange"]     = not (btn_bytes[1] & btn_map.orange)

        if whammy_byte < self.whammy_min: self.whammy_min = whammy_byte
        if whammy_byte > self.whammy_max: self.whammy_max = whammy_byte
        calibrated_whammy = (int(whammy_byte) - self.whammy_min) / (self.whammy_max - self.whammy_min)

        self.inputs["whammy_bar"] = calibrated_whammy
        self.inputs["tilt"] = tilt

        new_state = set(self.inputs.items())

        diff = new_state - previous_state

        return diff

    
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

axis_transform = (ABS_MIN_VAL, ABS_MAX_VAL, 0, 0) #The first two values set the min and max values of the axis respectively. I don't know that the other two do

def main():
    VID = 0x057e
    PID = 0x0306
    while len(hid.enumerate(VID, PID)) == 0:
        print("Waiting for connection")
        sleep(1)
        pass

    # uinput mappings to xbox 360 inputs:
    events = (
        uinput.BTN_A, uinput.BTN_B,             # Buttons
        uinput.BTN_X, uinput.BTN_Y,             # Buttons
        uinput.BTN_TL, uinput.BTN_TR,           # Bumpers
        uinput.ABS_HAT0X, uinput.ABS_HAT0Y,     # D-PAD
        uinput.BTN_THUMBL, uinput.BTN_THUMBR,   # Stick Presses
        uinput.ABS_X + axis_transform,          # Lext Stick X
        uinput.ABS_Y + axis_transform,          # Left Stick Y
        uinput.ABS_RX + axis_transform,         # Right Stick X
        uinput.ABS_RY + axis_transform,         # Right Stick Y
        uinput.ABS_Z + (0, ABS_MAX_VAL, 0, 0),  # Left Trigger
        uinput.ABS_RZ + (0, ABS_MAX_VAL, 0, 0), # Right Trigger
        uinput.BTN_START, uinput.BTN_SELECT,    # Menu Buttons (The buttons under the home button)
        uinput.BTN_MODE                         # Home Button (a.k.a: the xbox button)
    )

    binds = {
        "green":      events[0],  # BTN_A
        "red":        events[1],  # BTN_B
        "yellow":     events[3],  # BTN_Y
        "blue":       events[2],  # BTN_X
        "orange":     events[4],  # BTN_TL
        "strum_up":   events[7],  # D-Pad Up / ABS_HAT0Y+
        "strum_down": events[7],  # D-Pad Down / ABS_HAT0Y-
        "whammy_bar": uinput.ABS_RX, # Right Stick X
        "tilt":       uinput.ABS_RY, # Right Stick Y
        "plus":       events[16], # Start Button
        "minus":      events[17]  # Select Button
    }
    
    # These settings must be set (besides bustype) for Clone Hero's config to set itself
    controller = uinput.Device(events, "Xbox 360 Controller", 0x06, 0x045E, 0x0B13)

    wiimote = Wiimote()
    
    # Make transmissions unencrypted
    ret = wiimote.write_register(0xa400f0, 0x55)
    print(ret)

    ret = wiimote.write_register(0xa400fb, 0x00)
    print(ret)

    # get extension identification
    ret = wiimote.read_register(0xa400fa, 6)
    print(ret)

    ret = wiimote.set_reporting_mode(False, 0x35)
    print(ret)

    status = GuitarStatus()

    while(True):
        packet = wiimote.read_packet()
        #print('Packet: ', end='')
        #print(packet)
        updated = status.update(packet, wiimote)

        # This way of assigning the controller to clone hero is a bit weird, but it will work when tilt controlls are added.
        # Without this, clone hero will detect the wii controller input before the virtual controller input
        # and assign the wii controller to the player instead of the virtual controller.
        # This just works by having the user hold the 1 & 2 buttons on their wiimote
        # and then press the assign controller button. The tilt input from the wiimote will change constantly
        # but not enough for clone hero to assign the wiimote. However, the updates will cause
        # the virtual controller to change an unused input and it will assign the virtual controller instead
        pressed = status.getSpamButtons(packet)

        if pressed: controller.emit_click(uinput.BTN_TR)

        for key in updated:
            if key[0] == 'strum_up':
                controller.emit(binds[key[0]], -1 * key[1])
            elif key[0] == 'strum_down':
                controller.emit(binds[key[0]], 1 * key[1])
            elif key[0] == 'tilt':
                tilt = int(key[1] * ABS_MAX_VAL)
                #print("Tilt: " + str(key[1]))
                controller.emit(binds[key[0]], tilt)
                pass
            elif key[0] == 'whammy_bar':
                mapped_val = int(key[1] * ABS_MAX_VAL) # map the value from 0-31 to 0-ABS_MAX_VAL
                controller.emit(binds[key[0]], mapped_val)
            else:
                controller.emit(binds[key[0]], key[1])

if __name__ == "__main__":
    main()

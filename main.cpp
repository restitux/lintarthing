#include <iostream>
#include <hidapi/hidapi.h>
#include <cwchar>
#include <time.h>
#include <signal.h>
#include <vector>
#include <optional>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>

#define __STDC_WANT_LIB_EXT1__ 1

#include <string.h>

#define ABS_MAX_VAL 32767
#define ABS_MIN_VAL -32768

struct input_diff {
    int id;
    std::optional<float> val_float;
    std::optional<int> val_int;
    std::optional<bool> val_bool;
};

// The index is the button id, the value is the uinput id
const int uinput_map[11] = {
    BTN_A, BTN_B, BTN_Y, BTN_X,
    BTN_TL, ABS_HAT0Y, ABS_HAT0Y,
    BTN_START, BTN_SELECT, ABS_RX, ABS_RY
};

struct input_obj {
    int num_buttons = 10;

    bool green = false;       // BTN_A
    bool red = false;         // BTN_B
    bool yellow = false;      // BTN_Y
    bool blue = false;        // BTN_X
    bool orange = false;      // BTN_TL
    bool strum_down = false;  // ABS_HAT0Y+
    bool strum_up = false;    // ABS_HAT0Y-
    bool plus = false;        // BTN_START
    bool minus = false;       // BTN_SELECT
    bool guitar_connected = false;
    
    int num_int = 3;

    float whammy_bar = 0;       // ABS_RX
    int whammy_min = 15;
    int whammy_max = 16;
    
    float tilt = 0;           // ABS_RY
    // TODO: Change out with better values
    uint8_t tilt_min[2] = {100, 130};
    uint8_t tilt_max[2] = {130, 100};

    bool &get_button_state(int id) {
        switch (id)
        {
            case 0: return green;
            case 1: return red;
            case 2: return yellow;
            case 3: return blue;
            case 4: return orange;
            case 5: return strum_down;
            case 6: return strum_up;
            case 7: return plus;
            case 8: return minus;
            case 9: return guitar_connected;
            default: throw std::range_error("index out of bounds");
        }
    }
};

struct Wiimote_Button_Map {
    // byte 1
    const uint8_t plus =     0x10;
    const uint8_t dp_up =    0x08;
    const uint8_t dp_down =  0x04;
    const uint8_t dp_right = 0x02;
    const uint8_t dp_left =  0x01;
    // byte 2
    const uint8_t home =     0x80;
    const uint8_t minus =    0x10;
    const uint8_t btn_a =    0x08;
    const uint8_t btn_b =    0x04;
    const uint8_t one =      0x02;
    const uint8_t two =      0x01;
} button_map;

struct Guitar_Map {
    // byte 1
    const uint8_t bt_plus    = 0x04;
    const uint8_t bt_minus   = 0x10;
    const uint8_t strum_down = 0x40;
    // byte 2
    const uint8_t strum_up   = 0x01;
    const uint8_t yellow     = 0x08;
    const uint8_t green      = 0x10;
    const uint8_t blue       = 0x20;
    const uint8_t red        = 0x40;
    const uint8_t orange     = 0x80;
} guitar_map;

class Wiimote
{
    private:
        hid_device *_device;
    public:
        Wiimote(hid_device *device)
        {
            _device = device;
            std::cout << device << std::endl;
        }

    void read_packet(unsigned char *data)
    {
        // The max data sent in one packet AFAIK is 22 bytes.
        hid_read(_device, data, 22);
    }

    void set_active_leds(bool one = true, bool two = false, bool three = false, bool four = false)
    {
        uint8_t bitmask = 0x10 * one + 0x20 * two + 0x40 * three + 0x80 * four;
        uint8_t data[2] = {0x11, bitmask};
        hid_write(_device, data, 2);
    }

    void set_reporting_mode(bool continuous, uint8_t desired_report_id)
    {
        unsigned char cont_byte = continuous ? 0x00 : 0x04;
        unsigned char packet[3] = {0x12, cont_byte, desired_report_id};
        hid_write(_device, packet, 3);
        unsigned char *temp = (unsigned char *) new unsigned char(22);
        read_packet(temp);
        delete temp;
    }

    void write_register(unsigned int address, unsigned char *data, uint8_t length)
    {
        unsigned char addr[3];
        addr[0] = address >> 16 & 0xff;
        addr[1] = address >> 8 & 0xff;
        addr[2] = address & 0xff;
        
        unsigned char padded_data[16];
        std::fill_n(padded_data, 16, 0x00);
        memcpy(padded_data, data, length);

        unsigned char packet[22] = {0x16, 0x04};
        memcpy(packet + 2, addr, 3);
        packet[5] = length - 1;
        memcpy(packet + 6, padded_data, 16);

        hid_write(_device, packet, 22);
        unsigned char *temp = (unsigned char*) new unsigned char(22);
        read_packet(temp);
        delete temp;
        return;
    }
};

class Guitar_Status
{
    private:
        input_obj _inputs;
    public:
        Guitar_Status(input_obj inputs)
        {
            _inputs = inputs;
        }
    
    void handle_read_report(const unsigned char *packet)
    {
        // This is the extension identifying itself
        if (packet[4] == 0x00 && packet[5] == 0xfa)
        {
            // Technically the extension ID we're looking for is only 32 bits long,
            // there are other extension IDs that are 48 bits long, so to capture the
            // whole ID, a long is needed.
            unsigned long ext_id = 0x00;
            for (int i = 0; i < 6; i++)
            {
                ext_id |= packet[6 + i] << (8 * (5 - i));
            }
            if (ext_id == 0xa4200103 && !_inputs.guitar_connected)
            {
                _inputs.guitar_connected = true;
                puts("Guitar Hero Guitar Detected");
            }
        }
    }

    void handle_status_report(const unsigned char *packet, Wiimote *wiimote, bool was_requested)
    {
        // if the report was not requested, then we need to send another packet to set the reporting mode
        // https://wiibrew.org/wiki/Wiimote/Protocol#Status_Reporting

        float battery_percentage = ((float) packet[6] / 255) * 200;
        printf("Battery at %f%\n", battery_percentage);
        
        if (!was_requested)
        {
            wiimote->set_reporting_mode(false, 0x35);
            uint8_t flags = packet[3];
            if (!(flags & 0x02) && _inputs.guitar_connected)
            {
                int led_num = (flags & 0x80) ? 4 : (flags & 0x40) ? 3 : (flags & 0x20) ? 2 : 1;
                _inputs.guitar_connected = false;
                printf("WARNING: Player with LED %d disconnected their wii guitar extension!", led_num);
            }
        }
    }

    bool getSpamButtons(const unsigned char *packet)
    {
        return (packet[2] & 0x08) > 0; // 0x08 corresponds to the A button
    }

    float getTilt(const unsigned char *packet)
    {
        // Calibrate Tilt
        unsigned char xb = packet[3];
        unsigned char yb = packet[4];


        if (packet[2] & 0x02)
        {
            _inputs.tilt_min[0] = xb;
            _inputs.tilt_min[1] = yb;
            //printf("Min Tilt: X: %u, Y: %u\n", xb, yb);
        }
        else if (packet[2] & 0x01)
        {
            _inputs.tilt_max[0] = xb;
            _inputs.tilt_max[1] = yb;
            //printf("Max Tilt: X: %u, Y: %u\n", xb, yb);
        }
        int8_t x_val = xb - 128;
        int8_t y_val = yb - 128;
        float percent_x = (x_val - (_inputs.tilt_min[0] - 128) * 1.0) / (_inputs.tilt_max[0] - _inputs.tilt_min[0] * 1.0);
        float percent_y = (y_val - (_inputs.tilt_min[1] - 128) * 1.0) / (_inputs.tilt_max[1] - _inputs.tilt_min[1] * 1.0);

        float total_tilt = -(percent_x + percent_y) / 2;
        
        //printf("Tilt X: %.2f, Tilt Y: %.2f", percent_x * 100, percent_y * 100);
        //printf("Total Tilt: %.2f", total_tilt * 100);
        return total_tilt;
    }
    
    void update(const unsigned char *packet, std::vector<input_diff> &data, Wiimote *wiimote)
    {
        unsigned char packet_id = packet[0];
        unsigned char btn_bytes[2];
        unsigned char whammy_byte;
        float tilt = _inputs.tilt;
        data.clear();

        switch (packet_id)
        {
            case 0x20:
                handle_status_report(packet, wiimote, false);
                return;
            case 0x21:
                handle_read_report(packet);
                return;
            case 0x22:
            case 0x30:
                return;
            case 0x31:
                tilt = getTilt(packet);
                return;
            case 0x32:
            case 0x34:
                whammy_byte = packet[6];
                btn_bytes[0] = packet[7];
                btn_bytes[1] = packet[8];
                break;
            case 0x35:
                whammy_byte = packet[9];
                btn_bytes[0] = packet[10];
                btn_bytes[1] = packet[11];
                tilt = getTilt(packet);
                break;
            case 0x36:
                whammy_byte = packet[16];
                btn_bytes[0] = packet[17];
                btn_bytes[1] = packet[18];
                break;
            case 0x37:
                whammy_byte = packet[19];
                btn_bytes[0] = packet[20];
                btn_bytes[1] = packet[21];
                tilt = getTilt(packet);
                break;
            case 0x3d:
                whammy_byte = packet[4];
                btn_bytes[0] = packet[5];
                btn_bytes[1] = packet[6];
                break;
            default:
                printf("Cannot parse packet type %hhx.", packet_id);
                return;
        }

        input_obj prev_state = _inputs;

        _inputs.plus       = !(btn_bytes[0] & guitar_map.bt_plus);
        _inputs.minus      = !(btn_bytes[0] & guitar_map.bt_minus);
        
        _inputs.strum_down = !(btn_bytes[0] & guitar_map.strum_down);
        _inputs.strum_up   = !(btn_bytes[1] & guitar_map.strum_up);

        _inputs.green      = !(btn_bytes[1] & guitar_map.green);
        _inputs.red        = !(btn_bytes[1] & guitar_map.red);
        _inputs.yellow     = !(btn_bytes[1] & guitar_map.yellow);
        _inputs.blue       = !(btn_bytes[1] & guitar_map.blue);
        _inputs.orange     = !(btn_bytes[1] & guitar_map.orange);

        if (whammy_byte < _inputs.whammy_min) _inputs.whammy_min = whammy_byte;
        if (whammy_byte > _inputs.whammy_max) _inputs.whammy_max = whammy_byte;

        float calibrated_whammy = (whammy_byte - _inputs.whammy_min * 1.0) / (_inputs.whammy_max - _inputs.whammy_min * 1.0);

        _inputs.whammy_bar = calibrated_whammy;
        _inputs.tilt = tilt;

        // std::cout << "Previous State: ";
        // std::cout << "Green: " << (_inputs.green ? u8"\u2713" : "x");
        // std::cout << ", Red: " << (_inputs.red ? u8"\u2713" : "x");
        // std::cout << ", Yellow: " << (_inputs.yellow ? u8"\u2713" : "x");
        // std::cout << ", Blue: " << (_inputs.blue ? u8"\u2713" : "x");
        // std::cout << ", Orange: " << (_inputs.orange ? u8"\u2713" : "x");
        // std::cout << std::endl;
        
        input_diff temp;
        for (int i = 0; i < _inputs.num_buttons; i++)
        {
            if (_inputs.get_button_state(i) != prev_state.get_button_state(i))
            {
                temp.id = i; temp.val_bool = _inputs.get_button_state(i);
                data.push_back(temp);
            }
        }
        temp.val_bool = {};
    
        if (_inputs.whammy_bar != prev_state.whammy_bar)
        {
            temp.id = _inputs.num_buttons - 1; temp.val_float = _inputs.whammy_bar;
            data.push_back(temp);
        }
        temp.val_int = {};

        if (_inputs.tilt != prev_state.tilt)
        {
            temp.id = _inputs.num_buttons; temp.val_float = _inputs.tilt;
            data.push_back(temp);
        }
    }
};

void set_axis_values(uinput_user_dev &uidev, int axis, int min, int max, int flat, int fuzz)
{
    uidev.absmin[axis] = min;
    uidev.absmax[axis] = max;
    uidev.absflat[axis] = flat;
    uidev.absfuzz[axis] = fuzz;
}

int emit(int fd, int ev_type, int ev_code, int value)
{
    struct input_event ev;
    memset(&ev, 0, sizeof(struct input_event));
    ev.type = ev_type;
    ev.code = ev_code;
    ev.value = value;
    return write(fd, &ev, sizeof(ev));
}

int dev_sync(int fd)
{
    struct input_event ev;
    memset(&ev, 0, sizeof(struct input_event));
    ev.code = 0;
    ev.value = 0;
    ev.type = EV_SYN;
    return write(fd, &ev, sizeof(ev));
}

int fd;

void exit_handler(int s)
{
    printf("\n\nDetected Interrupt Signal: %d\nCleaning up HID & UInput devices\n", s);
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    hid_exit();
    exit(0);
}

int main() {
    hid_init();

    // handling ctrl+c to shutdown properly
    struct sigaction sigIntHandler;

    sigIntHandler.sa_handler = exit_handler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;

    sigaction(SIGINT, &sigIntHandler, NULL);

    // initializing the HID devices (a.k.a the wiimotes)
    hid_device_info *devices = hid_enumerate(0x057e, 0x0306);

    int num_devices = 0;
    hid_device_info *dev = devices;
    while (dev) {
        num_devices++;
        dev = dev->next;
    }

    if (num_devices < 1) return 0;
    hid_device *open_dev = hid_open_path(devices->path);
    if (!open_dev) {
        printf("Error: failed to open HID device located at %s. Do you have read/write access to device files?\n", devices->path);
        return 1;
    }
    
    hid_free_enumeration(devices);

    Wiimote *wiimote = new Wiimote(open_dev);
    wiimote->write_register(0xa400f0, (unsigned char *) new unsigned char(0x55), 1);
    wiimote->write_register(0xa400fb, (unsigned char *) new unsigned char(0x00), 1);
    wiimote->set_reporting_mode(false, 0x35);

    // UINPUT---------------------------------------------------------------------

    struct uinput_user_dev uidev;
    struct input_event ev;

    // This creates our virtual device
    // each time the file is opened, it returns a unique file descriptor
    // which will allow us to create and use multiple devices at a time
    fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);

    if (fd < 0)
    {
        perror("error: couldn't open uinput file");
        exit(1);
    }

    const int num_btns = 11;
    const int btn_inputs[num_btns] = {
        BTN_A, BTN_B, BTN_Y, BTN_X,
        BTN_SELECT, BTN_START, BTN_MODE,
        BTN_TL, BTN_TR, BTN_THUMBL, BTN_THUMBR
    };

    const int num_abs = 8;
    const int abs_inputs[num_abs] = {
        ABS_HAT0X, ABS_HAT0Y,
        ABS_X, ABS_Y, ABS_RX, ABS_RY,
        ABS_Z, ABS_RZ
    };

    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    
    memset(&uidev, 0, sizeof(uidev));
    snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "Xbox 360 Controller");

    uidev.id.bustype = BUS_USB;
    uidev.id.version = 1;
    uidev.id.vendor = 0x045E; // VID Microsoft Corp.
    uidev.id.product = 0x028e; // PID Xbox360 Controller
    // Alternate PIDs: 0x028F (Xbox360 Wireless Controller),
    // 0x02D1 (Xbox One Controller),
    // 0x02DD (Xbox One Controller (Firmware 2015)),
    // 0x02E3 Xbox One Elite Controller,
    // 0x0B12 (Xbox Wireless Controller (model 1914))

    for (int i = 0; i < num_btns; i++)
    {
        int j = ioctl(fd, UI_SET_KEYBIT, btn_inputs[i]);
        if (j < 0) printf("Failed to set button input at step %u", i);
    }

    for (int i = 0; i < num_abs; i++)
    {
        int j = ioctl(fd, UI_SET_ABSBIT, abs_inputs[i]);
        if (j < 0) printf("Failed to set abs axis input at step %u", i);
    }

    set_axis_values(uidev, ABS_X, ABS_MIN_VAL, ABS_MAX_VAL, 0, 0);
    set_axis_values(uidev, ABS_Y, ABS_MIN_VAL, ABS_MAX_VAL, 0, 0);
    set_axis_values(uidev, ABS_Z, 0, ABS_MAX_VAL, 0, 0);
    set_axis_values(uidev, ABS_RX, ABS_MIN_VAL, ABS_MAX_VAL, 0, 0);
    set_axis_values(uidev, ABS_RY, ABS_MIN_VAL, ABS_MAX_VAL, 0, 0);
    set_axis_values(uidev, ABS_RZ, 0, ABS_MAX_VAL, 0, 0);
    set_axis_values(uidev, ABS_HAT0X, -1, 1, 0, 0);
    set_axis_values(uidev, ABS_HAT0Y, -1, 1, 0, 0);

    write(fd, &uidev, sizeof(uidev));
    ioctl(fd, UI_DEV_CREATE);

    sleep(1);

    // MAIN LOOP -----------------------------------------------------------------

    input_obj input1;
    unsigned char *latest_packet = new unsigned char(22);
    Guitar_Status *status = new Guitar_Status(input1);
    wiimote->set_active_leds();
    std::vector<input_diff> diffs;
    bool spam;
    bool spamState = false;
    while (true)
    {
        wiimote->read_packet(latest_packet);
        status->update(latest_packet, diffs, wiimote);
        diffs.shrink_to_fit();
        spam = status->getSpamButtons(latest_packet);
        // The wide char string compare function returns 0 if the values are equal
        if (hid_error(open_dev) && wcscmp(hid_error(open_dev), L"Success"))
        {
            printf("[%i] hid_error: %ls\n", time(NULL), hid_error(open_dev));
            continue;
        }
        // printf("Packet: ");
        // for (int i = 0; i < 22; i++)
        // {
        //     printf("%02hhx ", latest_packet[i]);
        // }
        // std::cout << "\n";

        if (spam)
        {
            spamState = !spamState;
            emit(fd, EV_KEY, BTN_TR, spamState);
        }
        else if (spamState)
        {
            spamState = false;
            emit(fd, EV_KEY, BTN_TR, false);
        }

        for (int i = 0; i < diffs.size(); i++)
        {
            input_diff changed_input = diffs[i];
            if (changed_input.id < 9)
            {
                int j;
                // strum_down
                if (changed_input.id == 5) 
                {
                    j = emit(fd, EV_ABS, uinput_map[changed_input.id], changed_input.val_bool.value_or(0));
                }
                // strum_up
                else if (changed_input.id == 6)
                {
                    j = emit(fd, EV_ABS, uinput_map[changed_input.id], -1 * changed_input.val_bool.value_or(0));
                }

                else j = emit(fd, EV_KEY, uinput_map[changed_input.id], changed_input.val_bool.value_or(false));
            }
            // whammy & tilt
            else if (changed_input.id >= 9)
            {
                int mapped_val = changed_input.val_float.value_or(0) * ABS_MAX_VAL;
                emit(fd, EV_ABS, uinput_map[changed_input.id], mapped_val);
            }
        }
        dev_sync(fd);
    }
    hid_close(open_dev);
    delete wiimote;
} 
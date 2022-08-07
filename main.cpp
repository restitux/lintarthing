#include <iostream>
#include <linux/input.h>
#include <linux/uinput.h>
#include <hidapi/hidapi.h>
#include <time.h>
#include <cmath>
#include <vector>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>

#define __STDC_WANT_LIB_EXT1__ 1

#include <string.h>

#define ABS_MAX_VAL 32767
#define ABS_MIN_VAL -32768

struct input_diff {
    int id;
    float val_float = 0;
    int val_int = 0;
    bool val_bool = false;
};

// The index is the button id, the value is the uinput id
const char *uinput_strmap[11] {
    "Green", "Red", "Yellow", "Blue",
    "Orange", "Strum Down", "Strum Up",
    "Plus", "Star Power", "Whammy", "Tilt"
};

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
    int whammy_max = 26;
    
    float tilt = 0;           // ABS_RY
    // TODO: Change out with better values
    float tilt_min = 0;
    float tilt_max = 90;

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

int child_id;

class Wiimote
{
    private:
        hid_device *_device;
    public:
        Wiimote(hid_device *device)
        {
            _device = device;
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

    void read_calibration_offsets(uint8_t zero_offs[3], uint8_t grav_offs[3])
    {
        uint8_t first_addr = 0x16;
        uint8_t packet[7] = {0x00};

        packet[0] = 0x17;
        packet[4] = first_addr;
        packet[6] = 0x0A;
        hid_write(_device, packet, 7);
        uint8_t calibration_data[22] = {0x00};
        while (calibration_data[0] != 0x21 && calibration_data[5] != 0x16) {
            read_packet(calibration_data);
        }
        printf("[CHILD %i]: Calibration Data @ 0x%02hhx: ", child_id, first_addr);

        for (int i = 0; i < 22; i++)
        {
            printf("%02x ", calibration_data[i]);
        }
        puts("\n");

        zero_offs[0] = calibration_data[6];
        zero_offs[1] = calibration_data[7];
        zero_offs[2] = calibration_data[8];

        grav_offs[0] = calibration_data[10];
        grav_offs[1] = calibration_data[11];
        grav_offs[2] = calibration_data[12];

        printf("[CHILD %i]: 0G X: %02hhx | 1G X: %02hhx\n", child_id, zero_offs[0], grav_offs[0]);
        printf("[CHILD %i]: 0G Y: %02hhx | 1G Y: %02hhx\n", child_id, zero_offs[1], grav_offs[1]);
        printf("[CHILD %i]: 0G Z: %02hhx | 1G Z: %02hhx\n", child_id, zero_offs[2], grav_offs[2]);
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
        uint8_t x_zero = 0x80;
        uint8_t y_zero = 0x80;
        uint8_t z_zero = 0x80;

        uint8_t x_grav = 0x90;
        uint8_t y_grav = 0x90;
        uint8_t z_grav = 0x90;
    public:
        Guitar_Status(input_obj inputs)
        {
            _inputs = inputs;
        }
    
    void set_calibration_offsets(uint8_t zero_offs[3], uint8_t grav_offs[3])
    {
        x_zero = zero_offs[0];
        y_zero = zero_offs[1];
        z_zero = zero_offs[2];

        x_grav = grav_offs[0];
        y_grav = grav_offs[1];
        z_grav = grav_offs[2];
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
        printf("Battery input: %u\n", packet[6]);
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

    bool get_spam_buttons(const unsigned char *packet)
    {
        return (packet[2] & 0x08) > 0; // 0x08 corresponds to the A button
    }

    float get_tilt(const unsigned char *packet)
    {
        // Calibrate Tilt
        unsigned char xb = packet[3];
        unsigned char yb = packet[4];
        unsigned char zb = packet[5];

        float cal_x = (float)(xb - x_zero) / (x_grav - x_zero);
        float cal_y = -(float)(yb - y_zero) / (y_grav - y_zero); //The top side of the remote is -y, so we invert the value
        float cal_z = (float)(zb - z_zero) / (z_grav - z_zero);

        // This is an escape sequence that clears the terminal and resets the cursor position
        // std::cout << "\x1b[2J\x1b[;H";
        // printf("Raw Values:  X:   0x%02hhx |   Y: 0x%02hhx | Z:   0x%02hhx\n", xb, yb, zb);
        // printf("Raw Decimal: X: %6hhu | Y: %6hhu | Z: %6hhu\n", xb, yb, zb);
        // printf("New Decimal: X: %6.03f | Y: %6.03f | Z: %6.03f\n", cal_x, cal_y, cal_z);
        float magnitude = std::sqrt(pow(cal_z, 2) + pow(cal_x, 2));
        float angle = std::atan(cal_y / magnitude) * 180/M_PI; // multiply by 180/pi to convert from radians to degrees
        // printf("\nMagnitude:  %.03f\n", magnitude);
        // printf("Angle (radians): %.03f\n", angle);
        // printf("Angle (degrees): %.03f\n", angle * (180/M_PI));

        if (packet[2] & 0x02)
        {
            _inputs.tilt_min = angle;
            //printf("Min Tilt: X: %u, Y: %u\n", xb, yb);
        }
        else if (packet[2] & 0x01)
        {
            _inputs.tilt_max = angle;
            //printf("Max Tilt: X: %u, Y: %u\n", xb, yb);
        }
        
        return (angle - _inputs.tilt_min) / (_inputs.tilt_max - _inputs.tilt_min);
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
                tilt = get_tilt(packet);
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
                tilt = get_tilt(packet);
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
                tilt = get_tilt(packet);
                break;
            case 0x3d:
                whammy_byte = packet[4];
                btn_bytes[0] = packet[5];
                btn_bytes[1] = packet[6];
                break;
            default:
                printf("Cannot parse packet type 0x%hhx.", packet_id);
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

int set_axis_values(int fd, short axis, int min, int max, int flat, int fuzz)
{
    struct uinput_abs_setup axis_setup;
    axis_setup.code = axis;
    axis_setup.absinfo.minimum = min;
    axis_setup.absinfo.maximum = max;
    axis_setup.absinfo.flat = flat;
    axis_setup.absinfo.fuzz = fuzz;
    axis_setup.absinfo.resolution = max;
    return ioctl(fd, UI_ABS_SETUP, &axis_setup);
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
pid_t *child_pids;
int num_devices = 0;

void exit_handler(int s)
{
    printf("\n\n[CHILD %i]: Detected shutdown signal. Cleaning up HID & UInput device\n", child_id);
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    hid_exit();
    exit(0);
}

void terminate_children(int s)
{
    puts("\n\n[PARENT]: Detected shutdown signal. Terminating child process(es).");
    for (int i = 0; i < num_devices; i++)
    {
        kill(child_pids[i], SIGUSR1);
    }
}


void child_exec(int id, char *device_path)
{
    child_id = id;
    hid_device *hid_dev;
    Wiimote    *wiimote;
    uint8_t    zero_offs[3];
    uint8_t    grav_offs[3];

    // Open the wiimote's HID device and initialize the Wiimote class

    hid_dev = hid_open_path(device_path);
    if (!hid_dev) {
        printf("[CHILD %i]: Error: failed to open HID device at %s. Do you have read/write access to device files?\n", child_id, device_path);
        return;
    }

    wiimote = new Wiimote(hid_dev);
    
    // This unencrypts the traffic from the extension
    // TODO: Do this every time a guitar extension is plugged in
    wiimote->write_register(0xa400f0, (unsigned char *) new unsigned char(0x55), 1);
    wiimote->write_register(0xa400fb, (unsigned char *) new unsigned char(0x00), 1);
    
    // Initialize controller input communication
    wiimote->set_reporting_mode(false, 0x35);
    //TODO: Allow end user to set their player LED or do it automatically
    wiimote->set_active_leds();
    wiimote->read_calibration_offsets(zero_offs, grav_offs);

    // UInput setup --------------------------------------------------------------------------------

    struct uinput_setup usetup;
    struct input_event ev;
    
    // These two arrays store the input capabilities of our
    // virtual uinput controllers

    const int num_btns = 11;
    const int btn_inputs[num_btns] = {
        BTN_A, BTN_B, BTN_Y, BTN_X,
        BTN_SELECT, BTN_START, BTN_MODE,
        BTN_TL, BTN_TR, BTN_THUMBL, BTN_THUMBR
    };

    const char *btn_names[num_btns] = {
        "BTN_A", "BTN_B", "BTN_Y", "BTN_X",
        "BTN_SELECT", "BTN_START", "BTN_MODE",
        "BTN_TL", "BTN_TR", "BTN_THUMBL", "BTN_THUMBR"
    };

    const int num_abs = 8;
    const int abs_inputs[num_abs] = {
        ABS_HAT0X, ABS_HAT0Y,
        ABS_X, ABS_Y, ABS_RX, ABS_RY,
        ABS_Z, ABS_RZ
    };

    const char *abs_names[num_abs] = {
        "ABS_HAT0X", "ABS_HAT0Y",
        "ABS_X", "ABS_Y", "ABS_RX", "ABS_RY",
        "ABS_Z", "ABS_RZ"
    };

    // Clear out any garbage data
    memset(&usetup, 0, sizeof(usetup));

    // Set virtual controller identification values
    strcpy(usetup.name, "Xbox 360 Controller");
    usetup.id.bustype = BUS_USB;
    usetup.id.version = 1;
    usetup.id.vendor = 0x045E; // VID of Microsoft Corp.
    usetup.id.product = 0x028E; // PID for Xbox360 Controller
    // Alternate PIDs: 0x028F (Xbox360 Wireless Controller),
    // 0x02D1 (Xbox One Controller),
    // 0x02DD (Xbox One Controller (Firmware 2015)),
    // 0x02E3 Xbox One Elite Controller,
    // 0x0B12 (Xbox Wireless Controller (model 1914))

    fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0)
    {
        printf("[CHILD %i]: Error: couldn't open uinput file", child_id);
        exit(1);
    }

    // Enabling all controller inputs

    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    ioctl(fd, UI_SET_EVBIT, EV_KEY);

    for (int i = 0; i < num_btns; i++)
    {
        int j = ioctl(fd, UI_SET_KEYBIT, btn_inputs[i]);
        if (j < 0) printf("[CHILD %i]: Failed to activate button input number %u: %s\n", child_id, i, btn_names[i]);
    }

    for (int i = 0; i < num_abs; i++)
    {
        int j = ioctl(fd, UI_SET_ABSBIT, abs_inputs[i]);
        if (j < 0) printf("[CHILD %i]: Failed to activate absolute axis input number %u: %s\n", child_id, i, abs_names[i]);

        if (abs_inputs[i] == ABS_HAT0Y) {
            j = set_axis_values(fd, abs_inputs[i], -1, 1, 0, 0);
        } else {
            j = set_axis_values(fd, abs_inputs[i], ABS_MIN_VAL, ABS_MAX_VAL, 0, 32);
        }
        if (j != 0) printf("[CHILD %i]: Failed to set absolute axis settings for input number %u: %s\n", child_id, i, abs_names[i]);
    }

    ioctl(fd, UI_DEV_SETUP, &usetup);
    ioctl(fd, UI_DEV_CREATE);
    char sysfs_device_name[16];
    ioctl(fd, UI_GET_SYSNAME(sizeof(sysfs_device_name)), sysfs_device_name);
    printf("[CHILD %i]: UInput virtual device located at /sys/devices/virtual/input/%s\n", child_id, sysfs_device_name);

    // State setups --------------------------------------------------------------------------------

    input_obj inputs;
    Guitar_Status *status = new Guitar_Status(inputs);
    unsigned char *latest_packet = new unsigned char(22);
    std::vector<input_diff> diffs;
    bool spam = false;
    bool spam_state = false;

    status->set_calibration_offsets(zero_offs, grav_offs);

    // MAIN LOOP -----------------------------------------------------------------------------------

    while (true)
    {
        // failsafe. If the parent process terminates for any reason then shutdown immediately.
        if (getppid() == 1) {
            exit_handler(0);
            exit(0);
        }
        wiimote->read_packet(latest_packet);
        status->update(latest_packet, diffs, wiimote);
        diffs.shrink_to_fit();
        spam = status->get_spam_buttons(latest_packet);

        // The wide char string compare function returns 0 if the values are equal
        // if (hid_error(hid_dev) && wcscmp(hid_error(hid_dev), L"Success"))
        // {
        //     printf("[CHILD %i]: hid_error: %ls\n", child_id, hid_error(hid_dev));
        //     continue;
        // }

        if (spam)
        {
            spam_state = !spam_state;
            emit(fd, EV_KEY, BTN_TR, spam_state);
        }
        else if (spam_state)
        {
            spam_state = false;
            emit(fd, EV_KEY, BTN_TR, false);
        }

        // Update UInput input states --------------------------------------------------------------

        for (int i = 0; i < diffs.size(); i++)
        {
            input_diff changed_input = diffs[i];
            if (changed_input.id < 9)
            {
                int j;
                // strum_down
                if (changed_input.id == 5)
                {
                    j = emit(fd, EV_ABS, uinput_map[changed_input.id], changed_input.val_bool);
                }
                // strum_up
                else if (changed_input.id == 6)
                {
                    j = emit(fd, EV_ABS, uinput_map[changed_input.id], -1 * changed_input.val_bool);
                }

                else {
                    j = emit(fd, EV_KEY, uinput_map[changed_input.id], changed_input.val_bool);
                }
            }
            // whammy & tilt
            else if (changed_input.id >= 9)
            {
                int mapped_val = changed_input.val_float * ABS_MAX_VAL;
                emit(fd, EV_ABS, uinput_map[changed_input.id], mapped_val);
            }
        }
        dev_sync(fd);
    }
}

int main() {
    hid_init();

    // handling ctrl+c to shutdown properly
    struct sigaction sig_handler;
    sigset_t signal_set;
    sigemptyset(&signal_set);

    sigemptyset(&sig_handler.sa_mask);
    sig_handler.sa_flags = 0;

    sigaction(SIGINT, &sig_handler, NULL);

    // Get all connected wiimotes ------------------------------------------------------------------

    //TODO: periodically check for wiimotes
    hid_device_info *devices = hid_enumerate(0x057e, 0x0306);

    hid_device_info *dev = devices;
    while (dev)
    {
        num_devices++;
        dev = dev->next;
    }

    if (num_devices < 1)
    {
        puts("No connected wiimotes. Shutting down.\n");
        return 0;
    }

    child_pids = (pid_t *) calloc(num_devices, sizeof(pid_t));

    // Summon child processes to handle each device ------------------------------------------------
    for (int i = 0; i < num_devices; i++)
    {
        pid_t pid = fork();
        if (pid == 0)
        {
            // block the SIGINT signal (ctrl+c) because only the parent process needs to handle it.
            sigaddset(&signal_set, SIGINT);
            sigprocmask(SIG_BLOCK, &signal_set, NULL);

            sig_handler.sa_handler = exit_handler;
            sigaction(SIGUSR1, &sig_handler, NULL);
            child_exec(i + 1, devices->path);
            return 0; //failsafe. if child_exec (a.k.a. the main loop) ever returns then just stop the process.
        }
        else
        {
            child_pids[i] = pid;
            devices = devices->next;
        }
    }
    hid_free_enumeration(devices);

    sig_handler.sa_handler = terminate_children;
    sigaction(SIGINT, &sig_handler, NULL);

    bool all_exited = false;
    while (!all_exited)
    {
        all_exited = true;
        for (int i = 0; i < num_devices; i++)
        {
            if (waitpid(child_pids[i], nullptr, WNOHANG) == 0)
            {
                all_exited = false;
                break;
            }
        }
    }
    hid_exit();
} 
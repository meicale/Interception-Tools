#include <map>
#include <cstdio>
#include <string>
#include <vector>
#include <cstdlib>
#include <algorithm>
#include <stdexcept>

extern "C" {
#include <fcntl.h>
#include <unistd.h>
}

#include <yaml-cpp/yaml.h>
#include <libevdev/libevdev-uinput.h>

std::map<int, std::string> bus_string = {
#ifdef BUS_PCI
    {BUS_PCI, "BUS_PCI"},
#endif
#ifdef BUS_ISAPNP
    {BUS_ISAPNP, "BUS_ISAPNP"},
#endif
#ifdef BUS_USB
    {BUS_USB, "BUS_USB"},
#endif
#ifdef BUS_HIL
    {BUS_HIL, "BUS_HIL"},
#endif
#ifdef BUS_BLUETOOTH
    {BUS_BLUETOOTH, "BUS_BLUETOOTH"},
#endif
#ifdef BUS_VIRTUAL
    {BUS_VIRTUAL, "BUS_VIRTUAL"},
#endif
#ifdef BUS_ISA
    {BUS_ISA, "BUS_ISA"},
#endif
#ifdef BUS_I8042
    {BUS_I8042, "BUS_I8042"},
#endif
#ifdef BUS_XTKBD
    {BUS_XTKBD, "BUS_XTKBD"},
#endif
#ifdef BUS_RS232
    {BUS_RS232, "BUS_RS232"},
#endif
#ifdef BUS_GAMEPORT
    {BUS_GAMEPORT, "BUS_GAMEPORT"},
#endif
#ifdef BUS_PARPORT
    {BUS_PARPORT, "BUS_PARPORT"},
#endif
#ifdef BUS_AMIGA
    {BUS_AMIGA, "BUS_AMIGA"},
#endif
#ifdef BUS_ADB
    {BUS_ADB, "BUS_ADB"},
#endif
#ifdef BUS_I2C
    {BUS_I2C, "BUS_I2C"},
#endif
#ifdef BUS_HOST
    {BUS_HOST, "BUS_HOST"},
#endif
#ifdef BUS_GSC
    {BUS_GSC, "BUS_GSC"},
#endif
#ifdef BUS_ATARI
    {BUS_ATARI, "BUS_ATARI"},
#endif
#ifdef BUS_SPI
    {BUS_SPI, "BUS_SPI"},
#endif
#ifdef BUS_RMI
    {BUS_RMI, "BUS_RMI"},
#endif
#ifdef BUS_CEC
    {BUS_CEC, "BUS_CEC"},
#endif
#ifdef BUS_INTEL_ISHTP
    {BUS_INTEL_ISHTP, "BUS_INTEL_ISHTP"},
#endif
};

std::map<std::string, int> string_bus = {
#ifdef BUS_PCI
    {"BUS_PCI", BUS_PCI},
#endif
#ifdef BUS_ISAPNP
    {"BUS_ISAPNP", BUS_ISAPNP},
#endif
#ifdef BUS_USB
    {"BUS_USB", BUS_USB},
#endif
#ifdef BUS_HIL
    {"BUS_HIL", BUS_HIL},
#endif
#ifdef BUS_BLUETOOTH
    {"BUS_BLUETOOTH", BUS_BLUETOOTH},
#endif
#ifdef BUS_VIRTUAL
    {"BUS_VIRTUAL", BUS_VIRTUAL},
#endif
#ifdef BUS_ISA
    {"BUS_ISA", BUS_ISA},
#endif
#ifdef BUS_I8042
    {"BUS_I8042", BUS_I8042},
#endif
#ifdef BUS_XTKBD
    {"BUS_XTKBD", BUS_XTKBD},
#endif
#ifdef BUS_RS232
    {"BUS_RS232", BUS_RS232},
#endif
#ifdef BUS_GAMEPORT
    {"BUS_GAMEPORT", BUS_GAMEPORT},
#endif
#ifdef BUS_PARPORT
    {"BUS_PARPORT", BUS_PARPORT},
#endif
#ifdef BUS_AMIGA
    {"BUS_AMIGA", BUS_AMIGA},
#endif
#ifdef BUS_ADB
    {"BUS_ADB", BUS_ADB},
#endif
#ifdef BUS_I2C
    {"BUS_I2C", BUS_I2C},
#endif
#ifdef BUS_HOST
    {"BUS_HOST", BUS_HOST},
#endif
#ifdef BUS_GSC
    {"BUS_GSC", BUS_GSC},
#endif
#ifdef BUS_ATARI
    {"BUS_ATARI", BUS_ATARI},
#endif
#ifdef BUS_SPI
    {"BUS_SPI", BUS_SPI},
#endif
#ifdef BUS_RMI
    {"BUS_RMI", BUS_RMI},
#endif
#ifdef BUS_CEC
    {"BUS_CEC", BUS_CEC},
#endif
#ifdef BUS_INTEL_ISHTP
    {"BUS_INTEL_ISHTP", BUS_INTEL_ISHTP},
#endif
};

void print_usage(std::FILE *stream, const char *program) {
    // clang-format off
    std::fprintf(stream,
                 "uinput - redirect device input events from stdin to virtual device\n"
                 "\n"
                 "usage: %s [-h | [-p] [-c device.yaml] [-d devnode]]\n"
                 "\n"
                 "options:\n"
                 "    -h                show this message and exit\n"
                 "    -p                show resulting YAML device description merge and exit\n"
                 "    -c device.yaml    merge YAML device description to resulting virtual\n"
                 "                      device (repeatable)\n"
                 "    -d devnode        merge reference device description to resulting virtual\n"
                 "                      device (repeatable)\n",
                 program);
    // clang-format on
}

bool is_int(const std::string &s) {
    return s.find_first_not_of("0123456789") == std::string::npos;
}

using block_type = unsigned long;

constexpr int block_size = sizeof(block_type) * 8;

constexpr int blocks_needed(int n_bits) {
    return (n_bits - 1) / block_size + 1;
}

bool bit(const block_type buffer[], int bit_index) {
    return buffer[bit_index / block_size] &
           (block_type{1} << (bit_index % block_size));
}

std::string yaml_create_from_evdev(libevdev *dev) {
    using std::map;
    using std::vector;
    using std::string;
    using std::any_of;

    YAML::Emitter yaml;
    yaml << YAML::BeginMap;

    if (auto name = libevdev_get_name(dev))
        yaml << YAML::Key << "NAME" << YAML::Value << name;
    if (auto location = libevdev_get_phys(dev))
        yaml << YAML::Key << "LOCATION" << YAML::Value << location;
    if (auto id = libevdev_get_uniq(dev))
        yaml << YAML::Key << "ID" << YAML::Value << id;
    if (auto product = libevdev_get_id_product(dev))
        yaml << YAML::Key << "PRODUCT" << YAML::Value << product;
    if (auto vendor = libevdev_get_id_vendor(dev))
        yaml << YAML::Key << "VENDOR" << YAML::Value << vendor;
    if (auto bustype = libevdev_get_id_bustype(dev)) {
        if (bus_string.find(bustype) != bus_string.end())
            yaml << YAML::Key << "BUSTYPE" << YAML::Value
                 << bus_string[bustype];
        else
            yaml << YAML::Key << "BUSTYPE" << YAML::Value << bustype;
    }
    if (auto driver_version = libevdev_get_driver_version(dev))
        yaml << YAML::Key << "DRIVER_VERSION" << YAML::Value << driver_version;

    vector<string> properties;

    if (libevdev_has_property(dev, INPUT_PROP_POINTER))
        properties.push_back("INPUT_PROP_POINTER");
    if (libevdev_has_property(dev, INPUT_PROP_DIRECT))
        properties.push_back("INPUT_PROP_DIRECT");
    if (libevdev_has_property(dev, INPUT_PROP_BUTTONPAD))
        properties.push_back("INPUT_PROP_BUTTONPAD");
    if (libevdev_has_property(dev, INPUT_PROP_SEMI_MT))
        properties.push_back("INPUT_PROP_SEMI_MT");
    if (libevdev_has_property(dev, INPUT_PROP_TOPBUTTONPAD))
        properties.push_back("INPUT_PROP_TOPBUTTONPAD");
    if (libevdev_has_property(dev, INPUT_PROP_POINTING_STICK))
        properties.push_back("INPUT_PROP_POINTING_STICK");
    if (libevdev_has_property(dev, INPUT_PROP_ACCELEROMETER))
        properties.push_back("INPUT_PROP_ACCELEROMETER");

    if (!properties.empty())
        yaml << YAML::Key << "PROPERTIES" << YAML::Value << properties;

    int fd = libevdev_get_fd(dev);

    block_type type_mask[blocks_needed(EV_MAX)]   = {};
    block_type event_mask[blocks_needed(KEY_MAX)] = {};
    if (ioctl(fd, EVIOCGBIT(0, EV_MAX), type_mask) != -1 &&
        any_of(type_mask, type_mask + blocks_needed(EV_MAX),
               [](block_type block) { return block != block_type{}; })) {
        yaml << YAML::Key << "EVENTS" << YAML::Value;
        yaml << YAML::BeginMap;
        for (int type_code = 0; type_code <= EV_MAX; ++type_code) {
            if (!bit(type_mask, type_code))
                continue;
            int event_max         = libevdev_event_type_get_max(type_code);
            const char *type_name = libevdev_event_type_get_name(type_code);
            yaml << YAML::Key;
            if (type_name)
                yaml << libevdev_event_type_get_name(type_code);
            else
                yaml << type_code;
            yaml << YAML::Value;
            switch (type_code) {
                case EV_SYN:
                    yaml << YAML::Flow << YAML::BeginSeq;
                    if (libevdev_has_event_code(dev, EV_SYN, SYN_REPORT))
                        yaml << "SYN_REPORT";
                    if (libevdev_has_event_code(dev, EV_SYN, SYN_CONFIG))
                        yaml << "SYN_CONFIG";
                    if (libevdev_has_event_code(dev, EV_SYN, SYN_MT_REPORT))
                        yaml << "SYN_MT_REPORT";
                    if (libevdev_has_event_code(dev, EV_SYN, SYN_DROPPED))
                        yaml << "SYN_DROPPED";
                    yaml << YAML::EndSeq;
                    break;
                case EV_REP: {
                    int delay, period;
                    libevdev_get_repeat(dev, &delay, &period);
                    yaml << YAML::BeginMap;
                    yaml << YAML::Key << "REP_DELAY" << YAML::Value << delay;
                    yaml << YAML::Key << "REP_PERIOD" << YAML::Value << period;
                    yaml << YAML::EndMap;
                } break;
                case EV_ABS:
                    if (ioctl(fd, EVIOCGBIT(type_code, event_max),
                              event_mask) != -1) {
                        yaml << YAML::BeginMap;
                        for (int event_code = 0; event_code <= event_max;
                             ++event_code) {
                            if (!bit(event_mask, event_code))
                                continue;
                            if (auto absinfo =
                                    libevdev_get_abs_info(dev, event_code)) {
                                if (auto event_name =
                                        libevdev_event_code_get_name(
                                            type_code, event_code))
                                    yaml << YAML::Key << event_name
                                         << YAML::Value;
                                else
                                    yaml << YAML::Key << event_code
                                         << YAML::Value;
                                yaml << YAML::BeginMap;
                                yaml << YAML::Key << "VALUE" << YAML::Value
                                     << absinfo->value;
                                yaml << YAML::Key << "MIN" << YAML::Value
                                     << absinfo->minimum;
                                yaml << YAML::Key << "MAX" << YAML::Value
                                     << absinfo->maximum;
                                if (absinfo->flat > 0)
                                    yaml << YAML::Key << "FLAT" << YAML::Value
                                         << absinfo->flat;
                                if (absinfo->fuzz > 0)
                                    yaml << YAML::Key << "FUZZ" << YAML::Value
                                         << absinfo->fuzz;
                                if (absinfo->resolution > 0)
                                    yaml << YAML::Key << "RES" << YAML::Value
                                         << absinfo->resolution;
                                yaml << YAML::EndMap;
                            }
                        }
                        yaml << YAML::EndMap;
                    }
                    break;
                default:
                    if (ioctl(fd, EVIOCGBIT(type_code, event_max),
                              event_mask) != -1) {
                        yaml << YAML::Flow << YAML::BeginSeq;
                        for (int event_code = 0; event_code <= event_max;
                             ++event_code) {
                            if (!bit(event_mask, event_code))
                                continue;
                            if (auto event_name = libevdev_event_code_get_name(
                                    type_code, event_code))
                                yaml << event_name;
                            else
                                yaml << event_code;
                        }
                        yaml << YAML::EndSeq;
                    }
                    break;
            }
        }
        yaml << YAML::EndMap;
    }

    yaml << YAML::EndMap;
    return yaml.c_str();
}

libevdev *evdev_create_from_yaml(const std::vector<YAML::Node> &configs) {
    using std::map;
    using std::stoi;
    using std::vector;
    using std::string;

    libevdev *dev = libevdev_new();

    for (const auto &config : configs) {
        if (auto name = config["NAME"])
            libevdev_set_name(dev, name.as<string>().c_str());
        if (auto id = config["ID"])
            libevdev_set_uniq(dev, id.as<string>().c_str());
        if (auto product = config["PRODUCT"])
            libevdev_set_id_product(dev, product.as<int>());
        if (auto vendor = config["VENDOR"])
            libevdev_set_id_vendor(dev, vendor.as<int>());
        if (auto bustype = config["BUSTYPE"])
            libevdev_set_id_bustype(dev, string_bus[bustype.as<string>()]);
        if (auto version = config["VERSION"])
            libevdev_set_id_version(dev, version.as<int>());
        if (auto property = config["PROPERTIES"])
            for (auto it = property.begin(); it != property.end(); ++it) {
                auto property =
                    libevdev_property_from_name(it->as<string>().c_str());
                if (property != -1)
                    libevdev_enable_property(dev, property);
            }
        if (auto event_types = config["EVENTS"]) {
            for (const auto &event_type : event_types) {
                auto event_type_string = event_type.first.as<string>();
                if (event_type_string == "EV_REP") {
                    if (auto rep_delay = event_type.second["REP_DELAY"]) {
                        auto rep_delay_value = rep_delay.as<int>();
                        libevdev_enable_event_code(dev, EV_REP, REP_DELAY,
                                                   &rep_delay_value);
                    }
                    if (auto rep_period = event_type.second["REP_PERIOD"]) {
                        auto rep_period_value = rep_period.as<int>();
                        libevdev_enable_event_code(dev, EV_REP, REP_PERIOD,
                                                   &rep_period_value);
                    }
                } else if (event_type_string == "EV_ABS") {
                    for (const auto &axis : event_type.second) {
                        input_absinfo absinfo = {};
                        if (auto axis_value = axis.second["VALUE"])
                            absinfo.value = axis_value.as<int>();
                        if (auto axis_min = axis.second["MIN"])
                            absinfo.minimum = axis_min.as<int>();
                        if (auto axis_max = axis.second["MAX"])
                            absinfo.maximum = axis_max.as<int>();
                        if (auto axis_flat = axis.second["FLAT"])
                            absinfo.flat = axis_flat.as<int>();
                        if (auto fuzz = axis.second["FUZZ"])
                            absinfo.fuzz = fuzz.as<int>();
                        if (auto res = axis.second["RES"])
                            absinfo.resolution = res.as<int>();

                        if (!axis.second["VALUE"] && axis.second["MAX"])
                            absinfo.value = absinfo.maximum;
                        if (!axis.second["VALUE"] && axis.second["MIN"])
                            absinfo.value = absinfo.minimum;

                        auto axis_code = libevdev_event_code_from_name(
                            EV_ABS, axis.first.as<string>().c_str());
                        if (axis_code != -1)
                            libevdev_enable_event_code(dev, EV_ABS, axis_code,
                                                       &absinfo);
                    }
                } else {
                    auto event_type_code = libevdev_event_type_from_name(
                        event_type_string.c_str());

                    for (const auto &event : event_type.second) {
                        auto event_string = event.as<string>();
                        if (is_int(event_string))
                            libevdev_enable_event_code(dev, event_type_code,
                                                       stoi(event_string),
                                                       nullptr);
                        else {
                            auto event_code = libevdev_event_code_from_name(
                                event_type_code, event_string.c_str());
                            if (event_code != -1)
                                libevdev_enable_event_code(dev, event_type_code,
                                                           event_code, nullptr);
                        }
                    }
                }
            }
        }
    }

    return dev;
}

int main(int argc, char *argv[]) try {
    using std::perror;

    std::vector<YAML::Node> configs;
    bool print = false;

    for (int opt; (opt = getopt(argc, argv, "hc:d:p")) != -1;) {
        switch (opt) {
            case 'h':
                return print_usage(stdout, argv[0]), EXIT_SUCCESS;
            case 'c':
                configs.push_back(YAML::LoadFile(optarg));
                continue;
            case 'd': {
                int fd = open(optarg, O_RDONLY);
                if (fd < 0)
                    return perror("open failed"), EXIT_FAILURE;
                struct defer1 {
                    int fd;
                    ~defer1() { close(fd); }
                } defer1{fd};
                libevdev *dev;
                if (libevdev_new_from_fd(fd, &dev) < 0)
                    return perror("libevdev_new_from_fd failed"), EXIT_FAILURE;
                struct defer2 {
                    libevdev *dev;
                    ~defer2() { libevdev_free(dev); }
                } defer2{dev};
                configs.push_back(YAML::Load(yaml_create_from_evdev(dev)));
                continue;
            }
            case 'p':
                if (print)
                    break;
                print = true;
                continue;
        }

        return print_usage(stderr, argv[0]), EXIT_FAILURE;
    }

    if (configs.empty())
        return print_usage(stderr, argv[0]), EXIT_FAILURE;

    libevdev *dev = evdev_create_from_yaml(configs);
    struct defer1 {
        libevdev *dev;
        ~defer1() { libevdev_free(dev); }
    } defer1{dev};
    libevdev_uinput *uidev;
    if (libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED,
                                           &uidev) < 0)
        return perror("libevdev_uinput_create_from_device failed"),
               EXIT_FAILURE;
    struct defer2 {
        libevdev_uinput *uidev;
        ~defer2() { libevdev_uinput_destroy(uidev); }
    } defer2{uidev};

    if (print) {
        int fd = open(libevdev_uinput_get_devnode(uidev), O_RDONLY);
        if (fd < 0)
            return perror("open failed"), EXIT_FAILURE;
        struct defer1 {
            int fd;
            ~defer1() { close(fd); }
        } defer1{fd};
        libevdev *dev;
        if (libevdev_new_from_fd(fd, &dev) < 0)
            return perror("libevdev_new_from_fd failed"), EXIT_FAILURE;
        struct defer2 {
            libevdev *dev;
            ~defer2() { libevdev_free(dev); }
        } defer2{dev};
        return puts(yaml_create_from_evdev(dev).c_str()), EXIT_SUCCESS;
    }

    std::setbuf(stdin, nullptr);
    input_event input;
    while (fread(&input, sizeof input, 1, stdin) == 1)
        if (libevdev_uinput_write_event(uidev, input.type, input.code,
                                        input.value) < 0)
            return perror("libevdev_uinput_write_event failed"), EXIT_FAILURE;
} catch (const std::exception &e) {
    return std::fprintf(stderr,
                        R"(an exception occurred: "%s")"
                        "\n",
                        e.what()),
           EXIT_FAILURE;
}

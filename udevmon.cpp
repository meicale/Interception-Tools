#include <map>
#include <regex>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <stdexcept>

extern "C" {
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/select.h>
}

#include <libudev.h>
#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>

#include <yaml-cpp/yaml.h>

namespace {
void print_usage(std::FILE *stream, const char *program) {
    // clang-format off
    std::fprintf(stream,
                 "udevmon - monitor input devices for launching tasks\n"
                 "\n"
                 "usage: %s [-h] [-c configuration.yaml]\n"
                 "\n"
                 "options:\n"
                 "    -h                    show this message and exit\n"
                 "    -c configuration.yaml use configuration.yaml as configuration\n"
                 "\n"
                 "/etc/interception/udevmon.d/*.yaml is read if -c is not provided\n",
                 program);
    // clang-format on
}

bool is_int(const std::string &s) {
    return s.find_first_not_of("0123456789") == std::string::npos;
}

class jobs_launcher {
public:
    jobs_launcher(const std::vector<YAML::Node> &configs) {
        for (const auto &config : configs)
            for (const auto &job : config)
                job_matchers.emplace_back(job);
    }

    void operator()(const std::string &devnode) const {
        if (devnode.empty())
            return;

        int fd = open(devnode.c_str(), O_RDONLY);
        if (fd < 0) {
            std::fprintf(stderr,
                         R"(failed to open %s with error "%s")"
                         "\n",
                         devnode.c_str(), std::strerror(errno));
            return;
        }

        libevdev *dev;
        if (libevdev_new_from_fd(fd, &dev) < 0) {
            std::fprintf(
                stderr,
                R"(failed to create evdev device for %s with error "%s")"
                "\n",
                devnode.c_str(), std::strerror(errno));
            close(fd);
            return;
        }

        for (const job_matcher &match : job_matchers)
            if (match(dev))
                launch_job(match.job, devnode);

        libevdev_free(dev);
        close(fd);
    }

private:
    struct job_matcher {
        // clang-format off
        std::string job;
        std::regex  name      {".*", std::regex::optimize | std::regex::nosubs};
        std::regex  location  {".*", std::regex::optimize | std::regex::nosubs};
        std::regex  id        {".*", std::regex::optimize | std::regex::nosubs};
        std::regex  product   {".*", std::regex::optimize | std::regex::nosubs};
        std::regex  vendor    {".*", std::regex::optimize | std::regex::nosubs};
        std::regex  bustype   {".*", std::regex::optimize | std::regex::nosubs};
        std::regex  driver_version {".*", std::regex::optimize |
                                          std::regex::nosubs};
        std::vector<int> properties;
        std::map<int, std::vector<int>> events;
        // clang-format on

        job_matcher(const YAML::Node &job_node) {
            using std::map;
            using std::regex;
            using std::all_of;
            using std::string;
            using std::vector;
            using std::invalid_argument;

            if (auto job = job_node["JOB"])
                this->job = job.as<string>();
            else
                throw invalid_argument("missing JOB field in job node");

            auto device = job_node["DEVICE"];
            if (!device)
                throw invalid_argument("missing DEVICE field in job node");

            if (auto name = device["NAME"])
                this->name.assign(name.as<string>(),
                                  regex::optimize | regex::nosubs);
            if (auto location = device["LOCATION"])
                this->location.assign(location.as<string>(),
                                      regex::optimize | regex::nosubs);
            if (auto id = device["ID"])
                this->id.assign(id.as<string>(),
                                regex::optimize | regex::nosubs);
            if (auto product = device["PRODUCT"])
                this->product.assign(product.as<string>(),
                                     regex::optimize | regex::nosubs);
            if (auto vendor = device["VENDOR"])
                this->vendor.assign(vendor.as<string>(),
                                    regex::optimize | regex::nosubs);
            if (auto bustype = device["BUSTYPE"])
                this->bustype.assign(bustype.as<string>(),
                                     regex::optimize | regex::nosubs);
            if (auto driver_version = device["DRIVER_VERSION"])
                this->driver_version.assign(
                    driver_version.as<string>(),
                    regex::optimize | regex::nosubs);
            if (auto properties = device["PROPERTIES"]) {
                for (const auto &property_node : properties) {
                    auto property_name = property_node.as<string>();
                    int property       = is_int(property_name)
                                       ? stoi(property_name)
                                       : libevdev_property_from_name(
                                             property_name.c_str());
                    if (property < 0)
                        throw invalid_argument("invalid PROPERTY: " +
                                               property_name);
                    this->properties.push_back(property);
                }
            }
            if (auto events = device["EVENTS"]) {
                for (const auto &event : events) {
                    auto event_type_name = event.first.as<string>();
                    int event_type       = is_int(event_type_name)
                                         ? stoi(event_type_name)
                                         : libevdev_event_type_from_name(
                                               event_type_name.c_str());
                    if (event_type < 0)
                        throw invalid_argument("invalid EVENT TYPE: " +
                                               event_type_name);
                    this->events[event_type] = {};
                    for (const auto &event_code_node : event.second) {
                        auto event_code_name = event_code_node.as<string>();
                        int event_code =
                            is_int(event_code_name)
                                ? stoi(event_code_name)
                                : libevdev_event_code_from_name(
                                      event_type, event_code_name.c_str());
                        if (event_code < 0)
                            throw invalid_argument("invalid EVENT CODE: " +
                                                   event_code_name);
                        this->events[event_type].emplace_back(event_code);
                    }
                }
            }
        }

        bool operator()(libevdev *dev) const {
            using std::pair;
            using std::stoi;
            using std::all_of;
            using std::any_of;
            using std::string;
            using std::vector;
            using std::to_string;
            using std::regex_match;

            auto empty_if_null = [](const char *s) { return s ? s : ""; };

            if (!regex_match(empty_if_null(libevdev_get_name(dev)), name))
                return false;
            if (!regex_match(empty_if_null(libevdev_get_phys(dev)), location))
                return false;
            if (!regex_match(empty_if_null(libevdev_get_uniq(dev)), id))
                return false;

            if (!regex_match(to_string(libevdev_get_id_product(dev)),
                             product) ||
                !regex_match(to_string(libevdev_get_id_vendor(dev)), vendor) ||
                !regex_match(to_string(libevdev_get_id_bustype(dev)),
                             bustype) ||
                !regex_match(to_string(libevdev_get_driver_version(dev)),
                             driver_version))
                return false;

            for (int property : properties)
                if (!libevdev_has_property(dev, property))
                    return false;

            return all_of(
                events.begin(), events.end(),
                [dev](const pair<int, vector<int>> &event) {
                    return libevdev_has_event_type(dev, event.first) &&
                           (event.second.empty() ||
                            any_of(event.second.begin(), event.second.end(),
                                   [dev, &event](int event_code) {
                                       return libevdev_has_event_code(
                                           dev, event.first, event_code);
                                   }));
                });
        }
    };

    static void launch_job(const std::string &job, const std::string &devnode) {
        switch (fork()) {
            case -1:
                std::fprintf(stderr,
                             R"(fork failed for devnode %s, job "%s" )"
                             R"(with error "%s")"
                             "\n",
                             devnode.c_str(), job.c_str(),
                             std::strerror(errno));
                break;
            case 0: {
                char *command[] = {(char *)"sh", (char *)"-c",
                                   (char *)job.c_str(), nullptr};
                std::string variables = "DEVNODE=" + devnode;
                char *environment[]   = {(char *)variables.c_str(), nullptr};
                execvpe(command[0], command, environment);
                std::fprintf(stderr,
                             R"(exec failed for devnode %s, job "%s" )"
                             R"(with error "%s")"
                             "\n",
                             devnode.c_str(), job.c_str(),
                             std::strerror(errno));
            } break;
        }
    }

    std::vector<job_matcher> job_matchers;
};

std::string get_devnode(udev_device *device, bool initial_scan = false) {
    using std::strcmp;

    if (device == nullptr)
        return "";

    const char virtual_devices_directory[] = "/sys/devices/virtual/input/";
    if (strncmp(udev_device_get_syspath(device), virtual_devices_directory,
                sizeof(virtual_devices_directory) - 1) == 0)
        return "";

    if (!initial_scan) {
        const char *action = udev_device_get_action(device);
        if (!action || strcmp(action, "add"))
            return "";
    }

    const char input_prefix[] = "/dev/input/event";
    const char *devnode       = udev_device_get_devnode(device);
    if (!devnode || strncmp(devnode, input_prefix, sizeof(input_prefix) - 1))
        return "";

    return devnode;
}

std::vector<YAML::Node> scan_config(const std::string &directory) {
    static const std::regex yaml_extension{R"(.*\.ya?ml)",
                                           std::regex::optimize};
    std::vector<YAML::Node> configs;

    if (DIR *dir = opendir(directory.c_str()))
        while (dirent *entry = readdir(dir))
            if (entry->d_type == DT_REG &&
                regex_match(entry->d_name, yaml_extension))
                configs.push_back(
                    YAML::LoadFile(directory + '/' + entry->d_name));

    return configs;
}

void kill_zombies(int /*signum*/) {
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0)
        ;
}
}

int main(int argc, char *argv[]) try {
    using std::perror;

    std::vector<YAML::Node> configs;

    int opt;
    while ((opt = getopt(argc, argv, "hc:")) != -1) {
        switch (opt) {
            case 'h':
                return print_usage(stdout, argv[0]), EXIT_SUCCESS;
            case 'c':
                configs.push_back(YAML::LoadFile(optarg));
                continue;
        }

        return print_usage(stderr, argv[0]), EXIT_FAILURE;
    }

    if (configs.empty())
        configs = scan_config("/etc/interception/udevmon.d");

    if (configs.empty())
        return print_usage(stderr, argv[0]), EXIT_FAILURE;

    jobs_launcher launch_jobs_for_devnode(configs);

    struct sigaction sa {};
    sa.sa_flags   = SA_NOCLDSTOP;
    sa.sa_handler = &kill_zombies;
    if (sigaction(SIGCHLD, &sa, nullptr) == -1)
        return perror("couldn't summon zombie killer"), EXIT_FAILURE;

    udev *udev = udev_new();
    if (!udev)
        return perror("can't create udev"), EXIT_FAILURE;

    udev_enumerate *enumerate = udev_enumerate_new(udev);
    udev_enumerate_add_match_subsystem(enumerate, "input");
    udev_enumerate_scan_devices(enumerate);
    udev_list_entry *devices = udev_enumerate_get_list_entry(enumerate);
    udev_list_entry *dev_list_entry;
    udev_list_entry_foreach(dev_list_entry, devices) {
        if (udev_device *device = udev_device_new_from_syspath(
                udev, udev_list_entry_get_name(dev_list_entry))) {
            launch_jobs_for_devnode(
                get_devnode(device, /*initial_scan =*/true));
            udev_device_unref(device);
        }
    }
    udev_enumerate_unref(enumerate);

    udev_monitor *monitor = udev_monitor_new_from_netlink(udev, "udev");
    if (!monitor)
        return perror("can't create monitor"), EXIT_FAILURE;

    udev_monitor_filter_add_match_subsystem_devtype(monitor, "input", nullptr);
    udev_monitor_enable_receiving(monitor);
    int fd = udev_monitor_get_fd(monitor);
    for (;;) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);

        if (select(fd + 1, &fds, nullptr, nullptr, nullptr) > 0 &&
            FD_ISSET(fd, &fds)) {
            if (udev_device *device = udev_monitor_receive_device(monitor)) {
                launch_jobs_for_devnode(get_devnode(device));
                udev_device_unref(device);
            }
        }
    }

    udev_monitor_unref(monitor);
    udev_unref(udev);
} catch (const std::exception &e) {
    return std::fprintf(stderr,
                        R"(an exception occurred: "%s")"
                        "\n",
                        e.what()),
           EXIT_FAILURE;
}

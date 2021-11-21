#include <map>
#include <regex>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <memory>
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

using yaml = std::vector<YAML::Node>;

void print_usage(std::FILE *stream, const char *program) {
    // clang-format off
    std::fprintf(stream,
                 "udevmon - monitor input devices for launching tasks\n"
                 "\n"
                 "usage: %s [-h | -c configuration.yaml]\n"
                 "\n"
                 "options:\n"
                 "    -h                    show this message and exit\n"
                 "    -c configuration.yaml use configuration.yaml as configuration\n"
                 "\n"
                 "/etc/interception/udevmon.d/*.yaml is also read if present\n",
                 program);
    // clang-format on
}

struct cmd {
    cmd(const YAML::Node &job_node, const YAML::Node &settings_doc = {}) {
        using std::string;
        using std::vector;
        using std::invalid_argument;

        if (job_node.size() != 1)
            throw invalid_argument("wrong number of fields in job node");

        YAML::Node cmd_node;

        if (job_node["CMD"]) {
            this->wait = true;
            cmd_node   = job_node["CMD"];
        } else if (job_node["JOB"]) {
            this->wait = false;
            cmd_node   = job_node["JOB"];
        } else
            throw invalid_argument("missing JOB or CMD field in job node");

        vector<string> cmd_pieces = {"sh", "-c"};
        if (auto shell = settings_doc["SHELL"])
            cmd_pieces = shell.as<vector<string>>();
        if (!cmd_node.IsSequence()) {
            cmd_pieces.push_back(cmd_node.as<string>());
            this->cmds.push_back(cmd_pieces);
        } else
            for (const auto &subcmd_node : cmd_node) {
                auto pieces = cmd_pieces;
                pieces.push_back(subcmd_node.as<string>());
                this->cmds.push_back(pieces);
            }
    }

    std::vector<pid_t> launch() const {
        std::vector<pid_t> pids;
        for (size_t i = 0; i < cmds.size(); ++i) {
            pid_t pid = fork();
            switch (pid) {
                case -1: {
                    for (auto pid : pids)
                        kill(-pid, SIGTERM);
                    std::string e = "fork failed for \"";
                    e.append(cmds[i].back());
                    e.append("\" with error \"");
                    e.append(std::strerror(errno));
                    e.append("\"");
                    throw std::runtime_error(e);
                }
                case 0: {
                    std::unique_ptr<char *[]> command {
                        new char *[cmds[i].size() + 1]
                    };
                    for (size_t j = 0; j < cmds[i].size(); ++j)
                        command[j] = const_cast<char *>(cmds[i][j].c_str());
                    command[cmds[i].size()] = nullptr;
                    char *environment[]     = {nullptr};
                    setpgid(0, 0);
                    execvpe(command[0], command.get(), environment);
                    std::string e = "exec failed for \"";
                    e.append(cmds[i].back());
                    e.append("\" with error \"");
                    e.append(std::strerror(errno));
                    e.append("\"");
                    throw std::runtime_error(e);
                } break;
                default:
                    if (wait) {
                        siginfo_t info;
                        waitid(P_PID, pid, &info, WEXITED);
                        if (info.si_code != CLD_EXITED) {
                            for (auto pid : pids)
                                kill(-pid, SIGTERM);
                            std::string e = "command \"";
                            e.append(cmds[i].back());
                            e.append("\" terminated abnormally");
                            throw std::runtime_error(e);
                        } else if (info.si_status != EXIT_SUCCESS) {
                            for (auto pid : pids)
                                kill(-pid, SIGTERM);
                            std::string e = "command \"";
                            e.append(cmds[i].back());
                            e.append("\" exited with error \"");
                            e.append(std::strerror(info.si_errno));
                            e.append("\"");
                            throw std::runtime_error(e);
                        }
                    } else
                        pids.push_back(pid);
                    break;
            }
        }

        return pids;
    }

    bool wait;
    std::vector<std::vector<std::string>> cmds;
};

struct job {
    job(const YAML::Node &job_node, const YAML::Node &settings_doc = {}) {
        using std::regex;
        using std::string;
        using std::vector;
        using std::invalid_argument;

        if (job_node.size() != 2)
            throw invalid_argument("wrong number of fields in job node");

        if (!job_node["JOB"])
            throw invalid_argument("missing JOB field in job node");

        if (!job_node["DEVICE"])
            throw invalid_argument("missing DEVICE field in job node");

        auto cmd_node             = job_node["JOB"];
        vector<string> cmd_pieces = {"sh", "-c"};
        if (auto shell = settings_doc["SHELL"])
            cmd_pieces = shell.as<vector<string>>();
        if (!cmd_node.IsSequence()) {
            cmd_pieces.push_back(cmd_node.as<string>());
            this->cmds.push_back(cmd_pieces);
        } else
            for (const auto &subcmd_node : cmd_node) {
                auto pieces = cmd_pieces;
                pieces.push_back(subcmd_node.as<string>());
                this->cmds.push_back(pieces);
            }

        auto device = job_node["DEVICE"];

        if (auto link = device["LINK"]) {
            this->has_link = true;
            this->link.assign(link.as<string>(), regex::optimize);
        }
        if (auto name = device["NAME"])
            this->name.assign(name.as<string>(), regex::optimize);
        if (auto location = device["LOCATION"])
            this->location.assign(location.as<string>(), regex::optimize);
        if (auto id = device["ID"])
            this->id.assign(id.as<string>(), regex::optimize);
        if (auto product = device["PRODUCT"])
            this->product.assign(product.as<string>(), regex::optimize);
        if (auto vendor = device["VENDOR"])
            this->vendor.assign(vendor.as<string>(), regex::optimize);
        if (auto bustype = device["BUSTYPE"])
            this->bustype.assign(bustype.as<string>(), regex::optimize);
        if (auto driver_version = device["DRIVER_VERSION"])
            this->driver_version.assign(driver_version.as<string>(),
                                        regex::optimize);

        auto is_int = [](const std::string &s) {
            return s.find_first_not_of("0123456789") == std::string::npos;
        };

        if (auto properties = device["PROPERTIES"]) {
            for (const auto &property_node : properties) {
                vector<string> property_names;
                if (property_node.IsScalar())
                    property_names.push_back(property_node.as<string>());
                else
                    property_names = property_node.as<vector<string>>();

                vector<int> properties;
                for (const auto &property_name : property_names) {
                    int property = is_int(property_name)
                                       ? stoi(property_name)
                                       : libevdev_property_from_name(
                                             property_name.c_str());
                    if (property < 0)
                        throw invalid_argument("invalid EVENT CODE: " +
                                               property_name);
                    properties.push_back(property);
                }

                this->properties.push_back(std::move(properties));
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
                    vector<string> event_code_names;
                    if (event_code_node.IsScalar())
                        event_code_names.push_back(
                            event_code_node.as<string>());
                    else
                        event_code_names = event_code_node.as<vector<string>>();

                    vector<int> event_codes;
                    for (const auto &event_code_name : event_code_names) {
                        int event_code =
                            is_int(event_code_name)
                                ? stoi(event_code_name)
                                : libevdev_event_code_from_name(
                                      event_type, event_code_name.c_str());
                        if (event_code < 0)
                            throw invalid_argument("invalid EVENT CODE: " +
                                                   event_code_name);
                        event_codes.push_back(event_code);
                    }

                    this->events[event_type].push_back(std::move(event_codes));
                }
            }
        }
    }

    bool matches(udev_device *u, libevdev *e) const {
        using std::pair;
        using std::all_of;
        using std::any_of;
        using std::vector;
        using std::none_of;
        using std::to_string;
        using std::regex_match;

        if (has_link) {
            udev_list_entry *dev_list_entry;
            udev_list_entry_foreach(dev_list_entry,
                                    udev_device_get_devlinks_list_entry(u)) {
                if (regex_match(udev_list_entry_get_name(dev_list_entry), link))
                    goto next;
            }
            return false;
        }

    next:

        auto empty_if_null = [](const char *s) { return s ? s : ""; };

        if (!regex_match(empty_if_null(libevdev_get_name(e)), name))
            return false;
        if (!regex_match(empty_if_null(libevdev_get_phys(e)), location))
            return false;
        if (!regex_match(empty_if_null(libevdev_get_uniq(e)), id))
            return false;

        if (!regex_match(to_string(libevdev_get_id_product(e)), product) ||
            !regex_match(to_string(libevdev_get_id_vendor(e)), vendor) ||
            !regex_match(to_string(libevdev_get_id_bustype(e)), bustype) ||
            !regex_match(to_string(libevdev_get_driver_version(e)),
                         driver_version))
            return false;

        if (!properties.empty() &&
            none_of(properties.begin(), properties.end(),
                    [e](const vector<int> &property) {
                        return all_of(property.begin(), property.end(),
                                      [e](int property) {
                                          return libevdev_has_property(
                                              e, property);
                                      });
                    }))
            return false;

        return all_of(
            events.begin(), events.end(),
            [e](const pair<int, vector<vector<int>>> &event) {
                return libevdev_has_event_type(e, event.first) &&
                       (event.second.empty() ||
                        any_of(event.second.begin(), event.second.end(),
                               [e, &event](const vector<int> &event_codes) {
                                   return all_of(
                                       event_codes.begin(), event_codes.end(),
                                       [e, &event](int event_code) {
                                           return libevdev_has_event_code(
                                               e, event.first, event_code);
                                       });
                               }));
            });
    }

    std::vector<pid_t> launch_for(const std::string &devnode) const {
        std::vector<pid_t> pids;
        for (size_t i = 0; i < cmds.size(); ++i) {
            pid_t pid = fork();
            switch (pid) {
                case -1:
                    std::fprintf(stderr,
                                 R"(fork failed for devnode %s, job "%s" )"
                                 R"(with error "%s")"
                                 "\n",
                                 devnode.c_str(), cmds[i].back().c_str(),
                                 std::strerror(errno));
                    break;
                case 0: {
                    std::unique_ptr<char *[]> command {
                        new char *[cmds[i].size() + 1]
                    };
                    for (size_t j = 0; j < cmds[i].size(); ++j)
                        command[j] = const_cast<char *>(cmds[i][j].c_str());
                    command[cmds[i].size()] = nullptr;
                    std::string variables   = "DEVNODE=" + devnode;
                    char *environment[]     = {
                        const_cast<char *>(variables.c_str()), nullptr};
                    setpgid(0, 0);
                    execvpe(command[0], command.get(), environment);
                    std::string e = "exec failed for devnode ";
                    e.append(devnode);
                    e.append(", job \"");
                    e.append(cmds[i].back());
                    e.append("\" with error \"");
                    e.append(std::strerror(errno));
                    e.append("\"");
                    throw std::runtime_error(e);
                } break;
                default:
                    pids.push_back(pid);
                    break;
            }
        }

        return pids;
    }

    std::vector<std::vector<std::string>> cmds;

    // clang-format off
    bool        has_link       {false};
    std::regex  link;
    std::regex  name           {".*", std::regex::optimize};
    std::regex  location       {".*", std::regex::optimize};
    std::regex  id             {".*", std::regex::optimize};
    std::regex  product        {".*", std::regex::optimize};
    std::regex  vendor         {".*", std::regex::optimize};
    std::regex  bustype        {".*", std::regex::optimize};
    std::regex  driver_version {".*", std::regex::optimize};
    // clang-format on
    std::vector<std::vector<int>> properties;
    std::map<int, std::vector<std::vector<int>>> events;
};

struct jobs_manager {
    jobs_manager(const std::vector<yaml> &configs) {
        using std::invalid_argument;

        for (const auto &config : configs)
            switch (config.size()) {
                case 1:
                    if (!config[0].IsSequence())
                        throw invalid_argument(
                            "configuration must contain a job node's sequence "
                            "document");
                    for (const auto &job_node : config[0])
                        if (job_node["JOB"] && job_node.size() == 2)
                            jobs.emplace_back(job_node);
                        else
                            cmds.emplace_back(job_node);
                    break;
                case 2:
                    if (config[0].IsSequence() == config[1].IsSequence())
                        throw invalid_argument(
                            "configuration must contain one job node's "
                            "sequence document");
                    size_t settings, sequence;
                    if (config[0].IsSequence())
                        settings = 1, sequence = 0;
                    else
                        settings = 0, sequence = 1;
                    for (const auto &job_node : config[sequence])
                        if (job_node["JOB"] && job_node.size() == 2)
                            jobs.emplace_back(job_node, config[settings]);
                        else
                            cmds.emplace_back(job_node, config[settings]);
                    break;
                default:
                    throw invalid_argument(
                        "unexpected number of documents in configuration");
                    break;
            }
    }

    void launch() {
        for (const auto &cmd : cmds)
            for (auto pid : cmd.launch())
                running_cmds.push_back(pid);
    }

    void launch_for(udev_device *u) {
        const char virtual_devices_directory[] = "/sys/devices/virtual/input/";
        if (strncmp(udev_device_get_syspath(u), virtual_devices_directory,
                    sizeof(virtual_devices_directory) - 1) == 0)
            return;

        const char input_prefix[] = "/dev/input/event";
        const char *devnode       = udev_device_get_devnode(u);
        if (!devnode ||
            std::strncmp(devnode, input_prefix, sizeof(input_prefix) - 1))
            return;

        int fd = open(devnode, O_RDONLY);
        if (fd < 0) {
            std::fprintf(stderr,
                         R"(failed to open %s with error "%s")"
                         "\n",
                         devnode, std::strerror(errno));
            return;
        }
        struct defer1 {
            int fd;
            ~defer1() { close(fd); }
        } defer1{fd};

        libevdev *e;
        if (libevdev_new_from_fd(fd, &e) < 0) {
            std::fprintf(
                stderr,
                R"(failed to create evdev device for %s with error "%s")"
                "\n",
                devnode, std::strerror(errno));
            return;
        }
        struct defer2 {
            libevdev *e;
            ~defer2() { libevdev_free(e); }
        } defer2{e};

        for (const auto &job : jobs)
            if (job.matches(u, e)) {
                auto pids = running_jobs.find(devnode);
                if (pids == running_jobs.end()) {
                    auto new_pids = job.launch_for(devnode);
                    if (!new_pids.empty())
                        running_jobs[devnode] = new_pids;
                }
                break;
            }
    }

    void manage(udev_device *u) {
        const char virtual_devices_directory[] = "/sys/devices/virtual/input/";
        if (strncmp(udev_device_get_syspath(u), virtual_devices_directory,
                    sizeof(virtual_devices_directory) - 1) == 0)
            return;

        const char input_prefix[] = "/dev/input/event";
        const char *devnode       = udev_device_get_devnode(u);
        if (!devnode ||
            std::strncmp(devnode, input_prefix, sizeof(input_prefix) - 1))
            return;

        const char *action = udev_device_get_action(u);

        if (!action)
            return;

        if (!std::strcmp(action, "add")) {
            int fd = open(devnode, O_RDONLY);
            if (fd < 0) {
                std::fprintf(stderr,
                             R"(failed to open %s with error "%s")"
                             "\n",
                             devnode, std::strerror(errno));
                return;
            }
            struct defer1 {
                int fd;
                ~defer1() { close(fd); }
            } defer1{fd};

            libevdev *e;
            if (libevdev_new_from_fd(fd, &e) < 0) {
                std::fprintf(
                    stderr,
                    R"(failed to create evdev device for %s with error "%s")"
                    "\n",
                    devnode, std::strerror(errno));
                return;
            }
            struct defer2 {
                libevdev *e;
                ~defer2() { libevdev_free(e); }
            } defer2{e};

            for (const auto &job : jobs)
                if (job.matches(u, e)) {
                    auto pids = running_jobs.find(devnode);
                    if (pids == running_jobs.end()) {
                        auto new_pids = job.launch_for(devnode);
                        if (!new_pids.empty())
                            running_jobs[devnode] = new_pids;
                    }
                    break;
                }
        } else if (!std::strcmp(action, "remove")) {
            auto pids = running_jobs.find(devnode);
            if (pids != running_jobs.end()) {
                for (auto pid : pids->second)
                    kill(-pid, SIGTERM);
                running_jobs.erase(pids);
            }
        }
    }

    ~jobs_manager() {
        for (auto pid : running_cmds)
            kill(-pid, SIGTERM);
        for (const auto &running_job : running_jobs)
            for (auto pid : running_job.second)
                kill(-pid, SIGTERM);
    }

    std::vector<cmd> cmds;
    std::vector<job> jobs;
    std::vector<pid_t> running_cmds;
    std::map<std::string, std::vector<pid_t>> running_jobs;
};

std::vector<yaml> scan_config(const std::string &directory) {
    static const std::regex yaml_extension{R"(.*\.ya?ml)",
                                           std::regex::optimize};
    std::vector<yaml> configs;

    if (DIR *dir = opendir(directory.c_str()))
        while (dirent *entry = readdir(dir))
            if ((entry->d_type == DT_REG || entry->d_type == DT_LNK) &&
                regex_match(entry->d_name, yaml_extension))
                configs.push_back(
                    YAML::LoadAllFromFile(directory + '/' + entry->d_name));

    return configs;
}

void kill_zombies(int /*signum*/) {
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0)
        ;
}

volatile sig_atomic_t quit = false;

void set_quit_handler(int /*signal*/) { quit = true; }

int main(int argc, char *argv[]) try {
    using std::perror;

    std::regex default_config("/etc/interception/udevmon.ya?ml");
    std::vector<yaml> configs = scan_config("/etc/interception/udevmon.d");

    if (configs.size() > 0)
        printf(
            "%zu configuration files read from /etc/interception/udevmon.d\n",
            configs.size());

    for (int opt; (opt = getopt(argc, argv, "hc:")) != -1;) {
        switch (opt) {
            case 'h':
                return print_usage(stdout, argv[0]), EXIT_SUCCESS;
            case 'c':
                try {
                    configs.push_back(YAML::LoadAllFromFile(optarg));
                } catch (const YAML::BadFile &e) {
                    if (std::regex_match(optarg, default_config) &&
                        configs.size() > 0)
                        continue;
                    printf("ignoring %s, reason: %s\n", optarg, e.msg.c_str());
                }
                continue;
        }

        return print_usage(stderr, argv[0]), EXIT_FAILURE;
    }

    if (configs.empty())
        return perror("couldn't read any configuration"), EXIT_FAILURE;

    jobs_manager jobs(configs);

    struct sigaction sa {};

    sa.sa_flags   = SA_NOCLDSTOP;
    sa.sa_handler = &kill_zombies;
    if (sigaction(SIGCHLD, &sa, nullptr) == -1)
        return perror("couldn't summon zombie killer"), EXIT_FAILURE;

    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = &set_quit_handler;
    if (sigaction(SIGINT, &sa, nullptr) == -1)
        return perror("couldn't register SIGINT signal handler"), EXIT_FAILURE;
    if (sigaction(SIGTERM, &sa, nullptr) == -1)
        return perror("couldn't register SIGTERM signal handler"), EXIT_FAILURE;

    jobs.launch();

    udev *udev = udev_new();
    if (!udev)
        return perror("can't create udev"), EXIT_FAILURE;
    struct defer {
        struct udev *udev;
        ~defer() { udev_unref(udev); }
    } defer{udev};

    {
        udev_enumerate *enumerate = udev_enumerate_new(udev);
        struct defer {
            udev_enumerate *enumerate;
            ~defer() { udev_enumerate_unref(enumerate); }
        } defer{enumerate};
        udev_enumerate_add_match_subsystem(enumerate, "input");
        udev_enumerate_scan_devices(enumerate);
        udev_list_entry *dev_list_entry;
        udev_list_entry_foreach(dev_list_entry,
                                udev_enumerate_get_list_entry(enumerate)) {
            if (udev_device *u = udev_device_new_from_syspath(
                    udev, udev_list_entry_get_name(dev_list_entry))) {
                struct defer {
                    udev_device *u;
                    ~defer() { udev_device_unref(u); }
                } defer{u};
                jobs.launch_for(u);
            }
        }
    }

    {
        udev_monitor *monitor = udev_monitor_new_from_netlink(udev, "udev");
        if (!monitor)
            return perror("can't create monitor"), EXIT_FAILURE;
        struct defer {
            udev_monitor *monitor;
            ~defer() { udev_monitor_unref(monitor); }
        } defer{monitor};

        udev_monitor_filter_add_match_subsystem_devtype(monitor, "input",
                                                        nullptr);
        udev_monitor_enable_receiving(monitor);
        int fd = udev_monitor_get_fd(monitor);
        while (!quit) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(fd, &fds);

            if (select(fd + 1, &fds, nullptr, nullptr, nullptr) > 0 &&
                FD_ISSET(fd, &fds)) {
                if (udev_device *u = udev_monitor_receive_device(monitor)) {
                    struct defer {
                        udev_device *u;
                        ~defer() { udev_device_unref(u); }
                    } defer{u};
                    jobs.manage(u);
                }
            }
        }
    }
} catch (const std::exception &e) {
    return std::fprintf(stderr,
                        R"(an exception occurred: "%s")"
                        "\n",
                        e.what()),
           EXIT_FAILURE;
}

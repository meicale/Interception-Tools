#include <map>
#include <atomic>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <cstdlib>
#include <stdexcept>

extern "C" {
#include <unistd.h>
#include <linux/input.h>
}

#include <boost/interprocess/ipc/message_queue.hpp>

using boost::interprocess::open_only;
using boost::interprocess::create_only;
using boost::interprocess::message_queue;

void print_usage(std::FILE *stream, const char *program) {
    // clang-format off
    std::fprintf(stream,
                 "mux - mux streams of input events\n"
                 "\n"
                 "usage: %s [-h | [-s size] -c name | [-i name] [-o name]]\n"
                 "\n"
                 "options:\n"
                 "    -h        show this message and exit\n"
                 "    -s size   muxer's queue size (default: 100)\n"
                 "    -c name   name of muxer to create (repeatable)\n"
                 "    -i name   name of muxer to read input from or switch on\n"
                 "              (repeatable in switch mode)\n"
                 "    -o name   name of muxer to write output to (repeatable)\n",
                 program);
    // clang-format on
}

std::atomic<size_t> current_muxer{0};

int main(int argc, char *argv[]) try {
    enum {
        NO_MODE,
        CREATE_MODE,
        INPUT_MODE,
        OUTPUT_MODE,
        SWITCH_MODE
    } mode = NO_MODE;

    std::map<std::string, std::vector<std::string>> muxer_names;
    std::vector<size_t> muxer_sizes;
    size_t muxer_size = 100;

    std::vector<std::string> input_muxer_names = {""};
    for (int opt, last_opt = 0;
         (opt = getopt(argc, argv, "hs:c:i:o:")) != -1;) {
        switch (opt) {
            case 'h':
                return print_usage(stdout, argv[0]), EXIT_SUCCESS;
            case 's':
                if (last_opt && last_opt != 'c')
                    break;

                muxer_size = std::stoul(optarg);
                last_opt   = 's';
                continue;
            case 'c':
                if (last_opt && last_opt != 'c' && last_opt != 's')
                    break;

                mode = CREATE_MODE;
                muxer_names[""].push_back(optarg);
                muxer_sizes.push_back(muxer_size);
                last_opt = 'c';
                continue;
            case 'i':
                if (last_opt && last_opt != 'i' && last_opt != 'o')
                    break;

                if (!last_opt) {
                    mode              = INPUT_MODE;
                    input_muxer_names = {};
                } else if (last_opt == 'o') {
                    mode              = SWITCH_MODE;
                    input_muxer_names = {};
                }

                muxer_names[optarg];
                input_muxer_names.push_back(optarg);
                last_opt = 'i';
                continue;
            case 'o':
                if (last_opt && last_opt != 'i' && last_opt != 'o')
                    break;

                if (!last_opt)
                    mode = OUTPUT_MODE;
                else if (last_opt == 'i')
                    mode = SWITCH_MODE;

                for (const auto &name : input_muxer_names)
                    muxer_names[name].push_back(optarg);

                last_opt = 'o';
                continue;
        }

        return print_usage(stderr, argv[0]), EXIT_FAILURE;
    }

    switch (mode) {
        case NO_MODE:
            return print_usage(stderr, argv[0]), EXIT_FAILURE;

        case CREATE_MODE: {
            auto muxer_size = muxer_sizes.begin();
            for (const auto &muxer_name : muxer_names[""]) {
                message_queue::remove(muxer_name.c_str());
                message_queue(create_only, muxer_name.c_str(), *muxer_size,
                              sizeof(input_event), 0600);
                ++muxer_size;
            }
        } break;

        case INPUT_MODE: {
            if (muxer_names.size() != 1)
                return print_usage(stderr, argv[0]), EXIT_FAILURE;

            message_queue muxer(open_only, muxer_names.begin()->first.c_str());

            std::setbuf(stdout, nullptr);
            input_event input;
            unsigned int priority;
            message_queue::size_type size;
            for (;;) {
                muxer.receive(&input, sizeof input, size, priority);
                if (size != sizeof input)
                    throw std::runtime_error(
                        "unexpected input event size while reading from input "
                        "event queue");
                else if (std::fwrite(&input, sizeof input, 1, stdout) != 1)
                    throw std::runtime_error(
                        "error writing input event to stdout");
            }
        } break;

        case OUTPUT_MODE: {
            std::vector<std::unique_ptr<message_queue>> muxers;

            for (const auto &muxer_name : muxer_names[""])
                muxers.emplace_back(
                    new message_queue(open_only, muxer_name.c_str()));

            std::setbuf(stdin, nullptr);
            input_event input;
            for (;;)
                if (std::fread(&input, sizeof input, 1, stdin) == 1) {
                    for (auto &muxer : muxers)
                        if (!muxer->try_send(&input, sizeof input, 0))
                            throw std::runtime_error(
                                "outgoing muxer is full, exiting");
                } else if (std::ferror(stdin))
                    throw std::runtime_error(
                        "error reading input event from stdin");
                else if (std::feof(stdin))
                    break;
        } break;

        case SWITCH_MODE: {
            std::vector<std::vector<std::unique_ptr<message_queue>>> muxers;

            muxers.emplace_back();
            for (const auto &muxer_name : muxer_names[""])
                muxers.back().emplace_back(
                    new message_queue(open_only, muxer_name.c_str()));

            size_t id = 0;
            for (const auto &muxer_name : muxer_names) {
                if (muxer_name.first.empty())
                    continue;

                muxers.emplace_back();
                for (const auto &name : muxer_name.second)
                    muxers.back().emplace_back(
                        new message_queue(open_only, name.c_str()));

                std::thread(
                    [](std::unique_ptr<message_queue> muxer, size_t id) {
                        try {
                            input_event input;
                            unsigned int priority;
                            message_queue::size_type size;
                            for (;;) {
                                muxer->receive(&input, sizeof input, size,
                                               priority);
                                if (size == sizeof input)
                                    current_muxer = id;
                            }
                        } catch (...) {
                        }
                    },
                    std::unique_ptr<message_queue>(
                        new message_queue(open_only, muxer_name.first.c_str())),
                    ++id)
                    .detach();
            }

            std::setbuf(stdin, nullptr);

            input_event input;
            for (;;)
                if (std::fread(&input, sizeof input, 1, stdin) == 1) {
                    size_t current = current_muxer;
                    for (auto &muxer : muxers[current])
                        if (!muxer->try_send(&input, sizeof input, 0))
                            throw std::runtime_error(
                                "outgoing muxer is full, exiting");
                } else if (std::ferror(stdin))
                    throw std::runtime_error(
                        "error reading input event from stdin");
                else if (std::feof(stdin))
                    break;
        } break;
    }
} catch (const std::exception &e) {
    return std::fprintf(stderr,
                        R"(an exception occurred: "%s")"
                        "\n",
                        e.what()),
           EXIT_FAILURE;
}

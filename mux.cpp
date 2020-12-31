#include <cstdio>
#include <memory>
#include <string>
#include <vector>
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
                 "usage: %s [-h] [-c name | -i name | -o name]\n"
                 "\n"
                 "options:\n"
                 "    -h        show this message and exit\n"
                 "    -c name   name of muxer to create (repeatable)\n"
                 "    -i name   name of muxer to read input from\n"
                 "    -o name   name of muxer to write output to (repeatable)\n",
                 program);
    // clang-format on
}

int main(int argc, char *argv[]) try {
    char mode = 0;
    std::vector<std::string> muxer_names;
    for (int opt; (opt = getopt(argc, argv, "hc:i:o:")) != -1;) {
        switch (opt) {
            case 'h':
                return print_usage(stdout, argv[0]), EXIT_SUCCESS;
            case 'c':
                if (mode && mode != 'c')
                    break;
                mode = 'c';
                muxer_names.push_back(optarg);
                continue;
            case 'i':
                if (mode)
                    break;
                mode = 'i';
                muxer_names.push_back(optarg);
                continue;
            case 'o':
                if (mode && mode != 'o')
                    break;
                mode = 'o';
                muxer_names.push_back(optarg);
                continue;
        }

        return print_usage(stderr, argv[0]), EXIT_FAILURE;
    }

    if (!mode)
        return print_usage(stderr, argv[0]), EXIT_FAILURE;

    if (mode == 'c') {
        for (const auto &muxer_name : muxer_names) {
            message_queue::remove(muxer_name.c_str());
            message_queue(create_only, muxer_name.c_str(), 256,
                          sizeof(input_event), 0600);
        }
        return EXIT_SUCCESS;
    }

    std::vector<std::unique_ptr<message_queue>> muxers;
    for (const auto &muxer_name : muxer_names)
        muxers.emplace_back(new message_queue(open_only, muxer_name.c_str()));

    switch (mode) {
        case 'i': {
            std::setbuf(stdout, nullptr);
            input_event input;
            unsigned int priority;
            message_queue::size_type size;
            for (;;) {
                muxers.front()->receive(&input, sizeof input, size, priority);
                if (size != sizeof input)
                    throw std::runtime_error(
                        "unexpected input event size while reading from input "
                        "event queue");
                else if (std::fwrite(&input, sizeof input, 1, stdout) != 1)
                    throw std::runtime_error(
                        "error writing input event to stdout");
            }
        } break;
        case 'o': {
            std::setbuf(stdin, nullptr);
            input_event input;
            for (;;)
                if (std::fread(&input, sizeof input, 1, stdin) == 1)
                    for (auto &muxer : muxers)
                        muxer->try_send(&input, sizeof input, 0);
                else if (std::ferror(stdin))
                    throw std::runtime_error(
                        "error reading input event from stdin");
        } break;
    }
} catch (const std::exception &e) {
    return std::fprintf(stderr,
                        R"(an exception occurred: "%s")"
                        "\n",
                        e.what()),
           EXIT_FAILURE;
}

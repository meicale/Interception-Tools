#include <cstdio>
#include <string>
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
                 "    -c name   name of muxer to create\n"
                 "    -i name   name of muxer to read input from\n"
                 "    -o name   name of muxer to write output to\n",
                 program);
    // clang-format on
}

std::string muxer_name;

int main(int argc, char *argv[]) try {
    bool input_mode = false;
    int opt;
    while ((opt = getopt(argc, argv, "hc:i:o:")) != -1) {
        switch (opt) {
            case 'h':
                return print_usage(stdout, argv[0]), EXIT_SUCCESS;
            case 'c':
                message_queue::remove(optarg);
                message_queue(create_only, optarg, 256, sizeof(input_event));
                return EXIT_SUCCESS;
            case 'i':
                muxer_name = optarg;
                input_mode = true;
                continue;
            case 'o':
                muxer_name = optarg;
                input_mode = false;
                continue;
        }

        return print_usage(stderr, argv[0]), EXIT_FAILURE;
    }

    if (muxer_name.empty())
        return print_usage(stderr, argv[0]), EXIT_FAILURE;

    message_queue muxer(open_only, muxer_name.c_str());

    if (input_mode) {
        std::setbuf(stdout, nullptr);
        input_event input;
        unsigned int priority;
        message_queue::size_type size;
        for (;;) {
            muxer.receive(&input, sizeof input, size, priority);
            if (size != sizeof input)
                std::fprintf(stderr,
                             "unexpected input event size while reading from "
                             "input event queue\n");
            else if (fwrite(&input, sizeof input, 1, stdout) != 1)
                std::fprintf(stderr, "error writing input event to stdout\n");
        }
    } else {
        std::setbuf(stdin, nullptr);
        input_event input;
        for (;;)
            if (fread(&input, sizeof input, 1, stdin) == 1)
                muxer.send(&input, sizeof input, 0);
            else
                std::fprintf(stderr, "error reading input event from stdin\n");
    }
} catch (const std::exception &e) {
    return std::fprintf(stderr,
                        R"(an exception occurred: "%s")"
                        "\n",
                        e.what()),
           EXIT_FAILURE;
}

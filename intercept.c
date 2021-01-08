#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <fcntl.h>
#include <unistd.h>

#include <libevdev/libevdev.h>

void print_usage(FILE *stream, const char *program) {
    fprintf(stream,
            "intercept - redirect device input events to stdout\n"
            "\n"
            "usage: %s [-h | [-g] devnode]\n"
            "\n"
            "options:\n"
            "    -h        show this message and exit\n"
            "    -g        grab device\n"
            "    devnode   path of device to capture events from\n",
            program);
}

int main(int argc, char *argv[]) {
    int grab = 0;

    for (int opt; (opt = getopt(argc, argv, "hg")) != -1;) {
        switch (opt) {
            case 'h':
                return print_usage(stdout, argv[0]), EXIT_SUCCESS;
            case 'g':
                if (grab)
                    break;
                grab = 1;
                continue;
        }

        return print_usage(stderr, argv[0]), EXIT_FAILURE;
    }

    if (optind != argc - 1)
        return print_usage(stderr, argv[0]), EXIT_FAILURE;

    int fd = open(argv[optind], O_RDONLY);
    if (fd < 0)
        return perror("open failed"), EXIT_FAILURE;

    int result = EXIT_FAILURE;

    struct libevdev *dev;
    if (libevdev_new_from_fd(fd, &dev) < 0)
        goto teardown_fd;

    if (grab && libevdev_grab(dev, LIBEVDEV_GRAB) < 0)
        goto teardown_dev;

    setbuf(stdout, NULL);
    for (;;) {
        struct input_event input;
        int rc = libevdev_next_event(
            dev, LIBEVDEV_READ_FLAG_NORMAL | LIBEVDEV_READ_FLAG_BLOCKING,
            &input);

        while (rc == LIBEVDEV_READ_STATUS_SYNC)
            rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_SYNC, &input);

        if (rc == -EAGAIN)
            continue;

        if (rc != LIBEVDEV_READ_STATUS_SUCCESS)
            break;

        if (fwrite(&input, sizeof input, 1, stdout) != 1)
            goto teardown_grab;
    }

    result = EXIT_SUCCESS;

teardown_grab:
    if (grab)
        libevdev_grab(dev, LIBEVDEV_UNGRAB);
teardown_dev:
    libevdev_free(dev);
teardown_fd:
    close(fd);

    return result;
}

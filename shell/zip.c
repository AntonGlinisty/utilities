#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

ssize_t read_text(int fd, char* buffer, size_t size) {
    while (true) {
        ssize_t bytes_read = read(fd, buffer, size);

        if (bytes_read > 0) {
            return bytes_read;
        }

        if (bytes_read == -1 && errno == EINTR) {
            continue;
        }

        return bytes_read;
    }
}

bool write_text(int fd, const char* buffer, size_t size) {
    size_t progress = 0;

    while (progress < size) {
        ssize_t bytes_write = write(fd, buffer + progress, size - progress);

        if (bytes_write > 0) {
            progress += (size_t)bytes_write;
            continue;
        }

        if (bytes_write == -1 && errno == EINTR) {
            continue;
        }

        if (bytes_write == 0) {
            errno = EIO;
        }

        return false;
    }

    return true;
}

bool write_run(int fd, int32_t count, char value) {
    if (!write_text(fd, (const char*)&count, sizeof(count))) {
        return false;
    }
    return write_text(fd, &value, sizeof(value));
}

struct Run {
    int32_t count;
    char value;
};

bool process(int fd, struct Run* run) {
    char buffer[4096];

    while (true) {
        ssize_t bytes_read = read_text(fd, buffer, sizeof(buffer));
        if (bytes_read == -1) {
            perror("read");
            return false;
        }
        if (bytes_read == 0) {
            return true;
        }

        for (ssize_t i = 0; i < bytes_read; i++) {
            char value = buffer[i];

            if (run->count > 0 && (value != run->value || run->count == INT32_MAX)) {
                if (!write_run(STDOUT_FILENO, run->count, run->value)) {
                    perror("write");
                    return false;
                }
                run->count = 0;
            }

            if (run->count == 0) {
                run->value = value;
            }
            ++run->count;
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc == 1) {
        return 1;
    }

    struct Run run = {0};
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd == -1) {
            perror("open");
            return 1;
        }

        if (!process(fd, &run)) {
            close(fd);
            return 1;
        }

        if (close(fd) == -1) {
            perror("close");
            return 1;
        }
    }

    if (run.count > 0 && !write_run(STDOUT_FILENO, run.count, run.value)) {
        perror("write");
        return 1;
    }

    return 0;
}

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

ssize_t read_text(int fd, char* buffer, size_t size) {
    size_t progress = 0;
    
    while (progress < size) {
        ssize_t bytes_read = read(fd, buffer + progress, size - progress);
        
        if (bytes_read > 0) {
            progress += bytes_read;
            continue;
        }

        if (bytes_read == -1 && errno == EINTR) {
            continue;
        }

        if (bytes_read == -1) {
            return -1;
        }

        break;
    }

    return progress;
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

bool process(int fd) {
    char buffer[2000];
    char output[4096];

    while(true) {
        ssize_t bytes_read = read_text(fd, buffer, sizeof(buffer));
        if (bytes_read == -1) {
            perror("read");
            return false;
        }

        if (bytes_read % 5 != 0) {
            fprintf(stderr, "The number of bytes read is not a multiple of 5\n");
            return false;
        }

        for (ssize_t i = 0; i < bytes_read / 5; i++) {
            int32_t count = 0;
            memcpy(&count, buffer + i * 5, sizeof(count));
            if (count <= 0) {
                fprintf(stderr, "Encoded count <= 0\n");
                return false;
            }

            char value = *(buffer + i * 5 + 4);
            memset(output, value, sizeof(output));

            int32_t remaining = count;
            while (remaining > 0) {
                size_t chunk = (size_t)remaining < sizeof(output) ? (size_t)remaining : sizeof(output);
                if (!write_text(STDOUT_FILENO, output, chunk)) {
                    perror("write");
                    return false;
                }
                remaining -= chunk;
            }
        }

        if ((size_t)bytes_read < sizeof(buffer)) {
            break;
        }
    }

    return true;
}

int main(int argc, char* argv[]) {
    if (argc == 1) {
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd == -1) {
            perror("open");
            return 1;
        }

        if (!process(fd)) {
            close(fd);
            return 1;
        }

        if (close(fd) == -1) {
            perror("close");
            return 1;
        }
    }

    return 0;
}

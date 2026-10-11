#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

ssize_t read_buffer(int fd, char* buffer, size_t size) {
    while(true) {
        ssize_t bytes_read = read(fd, buffer, size);

        if (bytes_read > 0) {
            return bytes_read;
        }

        if (bytes_read == -1 && errno == EINTR) {
            continue;
        }

        if (bytes_read == 0) {
            return 0;
        }

        return -1;
    }
}

bool write_buffer(int fd, const char* buffer, size_t size) {
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

bool process_read(int fd, size_t size) {
    void* mapping = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (mapping == MAP_FAILED) {
        perror("mmap");
        return false;
    }

    while (true) {
        ssize_t count = read_buffer(fd, mapping, size);
        if (count == 0) {
            break;
        }
        if (count == -1) {
            perror("read");
            munmap(mapping, size);
            return false;
        }
        if (!write_buffer(STDOUT_FILENO, mapping, (size_t)count)) {
            perror("write");
            munmap(mapping, size);
            return false;
        }
    }

    if (munmap(mapping, size) == -1) {
        perror("munmap");
        return false;
    }

    return true;
}

bool process_mmap(int fd, size_t size) {
    void* mapping = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapping == MAP_FAILED) {
        perror("mmap");
        return false;
    }

    if (!write_buffer(STDOUT_FILENO, mapping, size)) {
        perror("write");
        munmap(mapping, size);
        return false;
    }

    if (munmap(mapping, size) == -1) {
        perror("munmap");
        return false;
    }

    return true;
}


int main(int argc, char* argv[]) {
    const char* mode = "read";
    size_t buffer_size = 4096;
    const struct option options[] = {
        {"mode", required_argument, NULL, 'm'},
        {"buffer-size", required_argument, NULL, 'b'},
        {NULL, 0, NULL, 0}
    };

    int option;
    while ((option = getopt_long(argc, argv, "", options, NULL)) != -1) {
        switch (option) {
        case 'm':
            mode = optarg;
            break;
        case 'b': {
            unsigned long long value = strtoull(optarg, NULL, 10);
            buffer_size = (size_t)value;
            break;
        }
        default:
            return 1;
        }
    }
    if (argc - optind != 1) {
        fprintf(stderr, "Usage: %s [--mode read|mmap] [--buffer-size BYTES] FILE\n", argv[0]);
        return 1;
    }

    int fd = open(argv[optind], O_RDONLY);
    if (fd == -1) {
        perror("open");
        return 1;
    }

    bool result = false;
    if (strcmp(mode, "read") == 0) {
        result = process_read(fd, buffer_size);
    } else if (strcmp(mode, "mmap") == 0) {
        struct stat input;
        if (fstat(fd, &input) == -1) {
            perror("fstat");
            return 1;
        }

        result = input.st_size == 0 ? true : process_mmap(fd, (size_t)input.st_size);
    } else {
        fprintf(stderr, "Mode must be one of [read|mmap]\n");
        return 1;
    }

    if (close(fd) == -1) {
        perror("close");
        result = false;
    }

    return result ? 0 : 1;
}

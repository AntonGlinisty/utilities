#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
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

enum ProcessResult {
    PROCESS_OK,
    PROCESS_HAS_ERRORS,
    PROCESS_WRITE_ERROR
};

enum ProcessResult process_common(int fd) {
    char buffer[4096];

    while (true) {
        ssize_t count = read_buffer(fd, buffer, sizeof(buffer));
        if (count == 0) {
            return PROCESS_OK;
        }
        if (count == -1) {
            perror("read");
            return PROCESS_HAS_ERRORS;
        }
        if (!write_buffer(STDOUT_FILENO, buffer, (size_t)count)) {
            perror("write");
            return PROCESS_WRITE_ERROR;
        }
    }
}

enum ProcessResult process_regular(int fd, size_t size) {
    void* mapping = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapping == MAP_FAILED) {
        return process_common(fd);
    }

    enum ProcessResult result = PROCESS_OK;

    int write_error = 0;
    if (!write_buffer(STDOUT_FILENO, mapping, size)) {
        write_error = errno;
        perror("write");
        result = PROCESS_WRITE_ERROR;
    }

    if (munmap(mapping, size) == -1) {
        perror("munmap");
        if (result == PROCESS_OK) {
            result = PROCESS_HAS_ERRORS;
        }
    }

    if (result == PROCESS_WRITE_ERROR) {
        errno = write_error;
    }
    return result;
}

enum ProcessResult process_input(int fd, bool is_stdin, const struct stat* output) {
    struct stat input;
    if (fstat(fd, &input) == -1) {
        perror("fstat");
        return PROCESS_HAS_ERRORS;
    }

    if (S_ISREG(input.st_mode) && S_ISREG(output->st_mode) &&
        input.st_dev == output->st_dev && input.st_ino == output->st_ino) {
        fprintf(stderr, "wcat: input file is output file\n");
        return PROCESS_HAS_ERRORS;
    }

    if (is_stdin || !S_ISREG(input.st_mode)) {
        return process_common(fd);
    }

    if (input.st_size < 0) {
        fprintf(stderr, "wcat: invalid file size\n");
        return PROCESS_HAS_ERRORS;
    }
    if (input.st_size == 0) {
        return PROCESS_OK;
    }

    if ((uintmax_t)input.st_size > (uintmax_t)SIZE_MAX) {
        return process_common(fd);
    }
    return process_regular(fd, (size_t)input.st_size);
}

int main(int argc, char* argv[]) {
    struct stat output;
    if (fstat(STDOUT_FILENO, &output) == -1) {
        perror("fstat");;
        return 1;
    }
    if (argc == 1) {
        return process_input(STDIN_FILENO, true, &output) == PROCESS_OK ? 0 : 1;
    }

    bool has_errors = false;
    for (int i = 1; i < argc; ++i) {
        bool is_stdin = strcmp(argv[i], "-") == 0;
        int fd = STDIN_FILENO;

        if (!is_stdin) {
            fd = open(argv[i], O_RDONLY);
            if (fd == -1) {
                perror("open");
                has_errors = true;
                continue;
            }
        }

        enum ProcessResult result = process_input(fd, is_stdin, &output);
        if (!is_stdin && close(fd) == -1) {
            perror("close");
            has_errors = true;
        }
        if (result == PROCESS_WRITE_ERROR) {
            return 1;
        }
        if (result == PROCESS_HAS_ERRORS) {
            has_errors = true;
        }
    }
    return has_errors ? 1 : 0;
}

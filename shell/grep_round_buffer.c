#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bool write_text(int fd, const char* buffer, size_t size) {
    size_t progress = 0;

    while (progress < size) {
        ssize_t bytes_written = write(fd, buffer + progress, size - progress);

        if (bytes_written > 0) {
            progress += (size_t)bytes_written;
            continue;
        }

        if (bytes_written == -1 && errno == EINTR) {
            continue;
        }

        if (bytes_written == 0) {
            errno = EIO;
        }

        return false;
    }

    return true;
}

ssize_t read_text(int fd, char* buffer, size_t size) {
    size_t progress = 0;

    while (progress < size) {
        ssize_t bytes_read = read(fd, buffer + progress, size - progress);

        if (bytes_read > 0) {
            progress += (size_t)bytes_read;
            continue;
        }

        if (bytes_read == -1 && errno == EINTR) {
            continue;
        }

        if (bytes_read == 0) {
            break;
        }

        return -1;
    }

    return (ssize_t)progress;
}

bool write_round_buffer(int file_descriptor, const char* buffer, size_t buffer_size, size_t start, size_t data_length) {
    size_t first_part = buffer_size - start;
    if (first_part >= data_length) {
        return write_text(file_descriptor, buffer + start, data_length);
    }

    if (!write_text(file_descriptor, buffer + start, first_part)) {
        return false;
    }

    return write_text(file_descriptor, buffer, data_length - first_part);
}

ssize_t read_round_buffer(int file_descriptor, char* buffer, size_t buffer_size, size_t start, size_t data_length) {
    size_t first_part = buffer_size - start;
    if (first_part >= data_length) {
        return read_text(file_descriptor, buffer + start, data_length);
    }

    ssize_t first_part_read = read_text(file_descriptor, buffer + start, first_part);
    if (first_part_read == -1) {
        return -1;
    }
    if ((size_t)first_part_read < first_part) {
        return first_part_read;
    }

    ssize_t second_part_read = read_text(file_descriptor, buffer, data_length - first_part);
    if (second_part_read == -1) {
        return -1;
    }

    return first_part_read + second_part_read;
}

ssize_t search_newline_round_buffer(const char* buffer, size_t buffer_size, size_t start, size_t data_length) {
    for (size_t offset = 0; offset < data_length; offset++) {
        size_t pos = (start + offset) % buffer_size;

        if (buffer[pos] == '\n') {
            return (ssize_t)pos;
        }
    }

    return -1;
}

bool search_pattern_round_buffer(const char* pattern, const char* buffer, size_t buffer_size, size_t start, size_t data_length) {
    size_t pattern_size = strlen(pattern);

    if (pattern_size == 0) {
        return true;
    }

    if (data_length < pattern_size) {
        return false;
    }

    for (size_t i = 0; i < data_length - pattern_size + 1; i++) {
        for (size_t j = 0; j < pattern_size; j++) {
            if (buffer[(start + i + j) % buffer_size] != pattern[j]) {
                break;
            }
            if (j == pattern_size - 1) {
                return true;
            }
        }
    }

    return false;
}

char* allocate_buffer(size_t size) {
    return malloc(size);
}

char* reallocate_buffer(char* buffer, size_t size) {
    return realloc(buffer, size);
}

void free_buffer(char* buffer) {
    free(buffer);
}

bool process(int file_descriptor, const char* pattern) {
    size_t buffer_size = 1024;
    char* buffer = allocate_buffer(buffer_size);
    if (buffer == NULL) {
        perror("malloc");
        return false;
    }

    size_t start = 0;
    size_t end = 0;

    size_t free_length = buffer_size;
    size_t data_length = 0;

    while (true) {
        size_t available = free_length;
        ssize_t read_result = read_round_buffer(file_descriptor, buffer, buffer_size, end, available);
        if (read_result == -1) {
            perror("read");
            free_buffer(buffer);
            return false;
        }

        bool reached_eof = (size_t)read_result < available;

        end = (end + (size_t)read_result) % buffer_size;
        free_length -= (size_t)read_result;

        while (true) {
            data_length = buffer_size - free_length;
            ssize_t newline_pos = search_newline_round_buffer(buffer, buffer_size, start, data_length);
            if (newline_pos == -1) {
                break;
            }

            size_t line_length = (buffer_size + (size_t)newline_pos - start) % buffer_size;
            if (search_pattern_round_buffer(pattern, buffer, buffer_size, start, line_length)) {
                if (!write_round_buffer(STDOUT_FILENO, buffer, buffer_size, start, line_length + 1)) {
                    perror("write");
                    free_buffer(buffer);
                    return false;
                }
            }

            start = (start + line_length + 1) % buffer_size;
            free_length += line_length + 1;
        }

        if (reached_eof) {
            if (data_length > 0 && search_pattern_round_buffer(pattern, buffer, buffer_size, start, data_length)) {
                if (!write_round_buffer(STDOUT_FILENO, buffer, buffer_size, start, data_length)) {
                    perror("write");
                    free_buffer(buffer);
                    return false;
                }
            }
            break;
        }

        if (free_length > 0) {
            continue;
        }

        if (buffer_size > SIZE_MAX / 2) {
            errno = ENOMEM;
            perror("realloc");
            free_buffer(buffer);
            return false;
        }

        size_t new_buffer_size = buffer_size * 2;
        char* new_buffer = reallocate_buffer(buffer, new_buffer_size);
        if (new_buffer == NULL) {
            perror("realloc");
            free_buffer(buffer);
            return false;
        }

        buffer = new_buffer;
        free_length += buffer_size;
        if (data_length > 0 && end <= start) {
            memmove(buffer + buffer_size, buffer, end);
            end += buffer_size;
        }
        buffer_size = new_buffer_size;
    }

    free_buffer(buffer);
    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        return 1;
    }
    char* pattern = argv[1];

    if (argc == 2) {
        return process(STDIN_FILENO, pattern) ? 0 : 1;
    }

    for (int i = 2; i < argc; i++) {
        int file_descriptor = open(argv[i], O_RDONLY);
        if (file_descriptor == -1) {
            perror("open");
            return 1;
        }

        bool processed = process(file_descriptor, pattern);
        if (close(file_descriptor) == -1) {
            perror("close");
            return 1;
        }

        if (!processed) {
            return 1;
        }
    }

    return 0;
}

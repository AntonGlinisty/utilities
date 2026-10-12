#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
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

bool check_pattern_matching(const char* data_start_ptr, const char* n_pos, const char* pattern) {
    size_t pattern_size = strlen(pattern);
    if (pattern_size == 0) {
        return true;
    }

    size_t line_length = n_pos - data_start_ptr;
    if (pattern_size > line_length) {
        return false;
    }

    for (size_t i = 0; i < line_length - pattern_size + 1; i++) {
        for (size_t j = 0; j < pattern_size; j++) {
            if (*(data_start_ptr + i + j) != *(pattern + j)) {
                break;
            }
            if (j == pattern_size - 1) {
                return true;
            }
        }
    }

    return false;
}

bool realloc_pages(char** alloc_start_ptr, char** alloc_end_ptr, char** data_start_ptr, size_t mapping_size) {
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size == -1) {
        perror("sysconf");
        return false;
    }

    size_t clear_pages_size = (*data_start_ptr - *alloc_start_ptr) / page_size * page_size;
    
    size_t alloc_size = *alloc_end_ptr - *alloc_start_ptr;
    if (alloc_size - clear_pages_size > SIZE_MAX - mapping_size) {
        errno = ENOMEM;
        return false;
    }

    size_t data_from_start_size = *data_start_ptr - *alloc_start_ptr - clear_pages_size;
    size_t new_mapping_size = alloc_size - clear_pages_size + mapping_size;

    char* new_mapping = mmap(NULL, new_mapping_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (new_mapping == MAP_FAILED) {
        perror("mmap");
        return false;
    }

    for (size_t i = 0; i < alloc_size - clear_pages_size; i++) {
        *(new_mapping + i) = *(*alloc_start_ptr + clear_pages_size + i);
    }

    if (munmap(*alloc_start_ptr, alloc_size) == -1) {
        perror("munmap");
        munmap(new_mapping, new_mapping_size);
        return false;
    }

    *alloc_start_ptr = new_mapping;
    *alloc_end_ptr = new_mapping + new_mapping_size;
    *data_start_ptr = new_mapping + data_from_start_size;

    return true;
}

bool process(int fd, const char* pattern) {
    size_t mapping_size = 1024;
    char* mapping = mmap(NULL, mapping_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (mapping == MAP_FAILED) {
        perror("mmap");
        return false;
    }

    char* alloc_start_ptr = mapping;
    char* alloc_end_ptr = mapping + mapping_size;

    char* data_start_ptr = mapping;
    char* data_checked_ptr = mapping;

    while (true) {
        size_t alloc_size = alloc_end_ptr - alloc_start_ptr;
        size_t space_left = alloc_end_ptr - data_checked_ptr;

        ssize_t bytes_read = read_text(fd, data_checked_ptr, space_left);
        if (bytes_read == -1) {
            perror("read");
            munmap(alloc_start_ptr, alloc_size);
            return false;
        }

        if (bytes_read == 0) {
            if (check_pattern_matching(data_start_ptr, data_checked_ptr, pattern)) {
                bool bytes_write = write_text(STDOUT_FILENO, data_start_ptr, data_checked_ptr - data_start_ptr);
                if (!bytes_write) {
                    perror("write");
                    munmap(alloc_start_ptr, alloc_size);
                    return false;
                }
            }
            if (munmap(alloc_start_ptr, alloc_size) == -1) {
                perror("munmap");
                return false;
            }
            return true;
        }

        for (ssize_t i = 0; i < bytes_read; i++) {
            char* symbol_ptr = data_checked_ptr + i;
            if (*(symbol_ptr) == '\n') {
                if (check_pattern_matching(data_start_ptr, symbol_ptr, pattern)) {
                    bool bytes_write = write_text(STDOUT_FILENO, data_start_ptr, symbol_ptr - data_start_ptr + 1);
                    if (!bytes_write) {
                        perror("write");
                        munmap(alloc_start_ptr, alloc_size);
                        return false;
                    }
                }
                data_start_ptr = symbol_ptr + 1;
            }
        }

        ptrdiff_t pending_size = data_checked_ptr + bytes_read - data_start_ptr;

        if ((size_t)bytes_read == space_left) {
            if (!realloc_pages(&alloc_start_ptr, &alloc_end_ptr, &data_start_ptr, mapping_size)) {
                munmap(alloc_start_ptr, alloc_size);
                return false;
            };
        }

        data_checked_ptr = data_start_ptr + pending_size;
    }
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        return 1;
    }

    const char* pattern = argv[1];
    if (argc == 2) {
        return process(STDIN_FILENO, pattern) ? 0 : 1;
    }

    for (int i = 2; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd == -1) {
            perror("open");
            return 1;
        }

        if (!process(fd, pattern)) {
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

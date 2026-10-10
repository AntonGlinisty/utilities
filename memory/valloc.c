#include "memory.h"

#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

void* my_valloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size == -1) {
        return NULL;
    }

    size_t header_size = get_header_size();
    if (size > SIZE_MAX - page_size) {
        errno = ENOMEM;
        return NULL;
    }

    void* ptr = mmap(NULL, size + page_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (ptr == MAP_FAILED) {
        return NULL;
    }

    struct Header header;
    header.alignment_size = page_size - header_size;
    header.allocated_size = size;
    header.data_size = size;

    *(struct Header*)((char*)ptr + page_size - header_size) = header;
    return (char*)ptr + page_size;
}
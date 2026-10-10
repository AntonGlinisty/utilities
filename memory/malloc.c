#include "memory.h"

#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>

void* my_malloc(size_t size) {
    if (size <= 0) {
        return NULL;
    }

    size_t header_size = get_header_size();
    if (size > SIZE_MAX - header_size) {
        errno = ENOMEM;
        return NULL;
    }

    void* ptr = mmap(NULL, size + header_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (ptr == MAP_FAILED) {
        return NULL;
    }

    struct Header header;
    header.alignment_size = 0;
    header.allocated_size = size;
    header.data_size = size;

    *(struct Header*)ptr = header;
    return (char*)ptr + header_size;
}

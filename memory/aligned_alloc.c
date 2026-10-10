#include "memory.h"

#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>

void* my_aligned_alloc(size_t alignment, size_t size) {
    if (size == 0) {
        return NULL;
    }

    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return NULL;
    }

    if (size % alignment != 0) {
        return NULL;
    }

    size_t max_alignment = _Alignof(max_align_t);
    if (alignment < max_alignment) {
        alignment = max_alignment;
    }

    size_t header_size = get_header_size();
    if (size > SIZE_MAX - header_size - alignment + 1) {
        errno = ENOMEM;
        return NULL;
    }

    size_t mapping_size = header_size + size + alignment - 1;

    void* ptr = mmap(NULL, mapping_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (ptr == MAP_FAILED) {
        return NULL;
    }

    uintptr_t candidate = (uintptr_t)ptr + header_size;
    size_t remainder = candidate % alignment;
    size_t padding = remainder == 0 ? 0 : alignment - remainder;

    struct Header header;
    header.alignment_size = padding;
    header.allocated_size = mapping_size - header_size - padding;
    header.data_size = size;

    *(struct Header*)((char*)ptr + header.alignment_size) = header;
    return (char*)ptr + header.alignment_size + header_size;
}

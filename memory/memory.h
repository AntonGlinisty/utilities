#pragma once

#include <stddef.h>

void* my_malloc(size_t size);
void* my_aligned_alloc(size_t alignment, size_t size);
void my_free(void* ptr);
void* my_calloc(size_t count, size_t size);
void* my_realloc(void* ptr, size_t size);
void* my_reallocf(void* ptr, size_t size);
void* my_valloc(size_t size);

struct Header {
    size_t alignment_size;
    size_t allocated_size;
    size_t data_size;
};

size_t get_header_size(void);

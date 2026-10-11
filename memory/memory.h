#pragma once

#include <stddef.h>

void* my_malloc(size_t size);
void* my_aligned_alloc(size_t alignment, size_t size);
void my_free(void* ptr);
void* my_calloc(size_t count, size_t size);
void* my_realloc(void* ptr, size_t size);
void* my_reallocf(void* ptr, size_t size);
void* my_valloc(size_t size);

void* my_memset(void* b, int c, size_t len);
void* my_memcpy(void* dst, const void* src, size_t n);
int my_memcmp(const void* s1, const void* s2, size_t n);
void* my_memmove(void* dst, const void* src, size_t len);

struct Header {
    size_t alignment_size;
    size_t allocated_size;
    size_t data_size;
};

size_t get_header_size(void);

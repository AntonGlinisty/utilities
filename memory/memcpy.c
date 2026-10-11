#include "memory.h"

#include <stddef.h>

void* my_memcpy(void* dst, const void* src, size_t n) {
    for (size_t i = 0; i < n; i++) {
        *((unsigned char*)dst + i) = *((const unsigned char*)src + i); 
    }
    return dst;
}

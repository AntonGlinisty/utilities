#include "memory.h"

#include <stddef.h>
#include <stdint.h>

void* my_memmove(void* dst, const void* src, size_t len) {
    if (dst == src) {
        return dst;
    }

    if ((uintptr_t)dst < (uintptr_t)src) {
        for (size_t i = 0; i < len; i++) {
            *((unsigned char*)dst + i) = *((const unsigned char*)src + i);
        }
        return dst;
    }

    for (size_t i = 0; i < len; i++) {
        *((unsigned char*)dst + len - 1 - i) = *((const unsigned char*)src + len - 1 - i);
    }
    return dst;
}

#include "memory.h"

#include <stddef.h>

void* my_memset(void* b, int c, size_t len) {
    for (size_t i = 0; i < len; i++) {
        *((unsigned char*)b + i) = (unsigned char)c;
    }
    return b;
}

#include "memory.h"

#include <errno.h>
#include <stdint.h>

void* my_calloc(size_t count, size_t size) {
    if (count == 0 || size == 0) {
        return NULL;
    }

    if (count > SIZE_MAX / size) {
        errno = ENOMEM;
        return NULL;
    }

    size_t data_size = count * size;
    return my_malloc(data_size);
}

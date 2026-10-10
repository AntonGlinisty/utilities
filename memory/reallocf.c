#include "memory.h"

#include <errno.h>

void* my_reallocf(void* ptr, size_t size) {
    void* new_ptr = my_realloc(ptr, size);
    if (new_ptr == NULL && size != 0) {
        int error = errno;
        my_free(ptr);
        errno = error;
    }
    return new_ptr;
}

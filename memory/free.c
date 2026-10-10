#include "memory.h"

#include <sys/mman.h>

void my_free(void* ptr) {
    if (ptr == NULL) {
        return;
    }

    size_t header_size = get_header_size();
    char* header_start = (char*)ptr - header_size;

    struct Header header = *(struct Header*)header_start;
    munmap(header_start - header.alignment_size, header.allocated_size + header.alignment_size + header_size);
}

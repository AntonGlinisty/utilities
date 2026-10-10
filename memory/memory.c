#include "memory.h"

size_t get_header_size(void) {
    size_t max_alignment = _Alignof(max_align_t);
    return (sizeof(struct Header) + max_alignment - 1) / max_alignment * max_alignment;
}

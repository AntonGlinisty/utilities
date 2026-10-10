#include "memory.h"

#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

void* my_realloc(void* ptr, size_t size) {
    if (ptr == NULL) {
        return my_malloc(size);
    }

    if (size == 0) {
        my_free(ptr);
        return NULL;
    }
    
    size_t header_size = get_header_size();

    void* prev_header_start = (char*)ptr - header_size;
    struct Header* prev_header = (struct Header*)prev_header_start;

    if (prev_header->allocated_size == size) {
        prev_header->data_size = size;
        return ptr;
    }

    if (prev_header->allocated_size > size) {
        prev_header->data_size = size;

        long page_size = sysconf(_SC_PAGESIZE);
        if (page_size == -1) {
            return ptr;
        }

        uintptr_t addr = (uintptr_t)ptr;
        if ((addr + prev_header->allocated_size - 1) / page_size == (addr + size - 1) / page_size) {
            return ptr;
        }

        uintptr_t old_end = addr + prev_header->allocated_size;
        uintptr_t old_end_page = old_end;

        uintptr_t old_r = old_end % page_size;
        if (old_r != 0) {
            if (UINTPTR_MAX - old_end < page_size - old_r) {
                return ptr;
            }
            old_end_page = (old_end + page_size - old_r) / page_size * page_size;
        }

        uintptr_t new_end = addr + size;
        uintptr_t new_end_page = new_end;

        uintptr_t new_r = new_end % page_size;
        if (new_r != 0) {
            if (UINTPTR_MAX - new_end < page_size - new_r) {
                return ptr;
            }
            new_end_page = (new_end + page_size - new_r) / page_size * page_size;
        }

        size_t free_size = old_end_page - new_end_page;

        if(munmap((void*)new_end_page, free_size) == -1) {
            return ptr;
        }

        prev_header->allocated_size = new_end_page - addr;
        return ptr;
    }
    
    void* new_ptr = my_malloc(size);
    if (new_ptr == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < prev_header->data_size && i < size; ++i) {
        *((char*)new_ptr + i) = *((char*)ptr + i);
    }

    my_free(ptr);
    return new_ptr;
}

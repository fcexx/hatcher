#ifndef HEAP_H
#define HEAP_H
#include <stddef.h>
#include <stdint.h>

#define HEAP_START 0x200000
#define HEAP_SIZE 1024 * 1024 * 16

void heap_init(uint64_t heap_start, uint64_t heap_size);
void *kmalloc(size_t size);
void *kcalloc(size_t nmemb, size_t size);
void *krealloc(void *ptr, size_t size);
void kfree(void *ptr);
size_t heap_total(void);
size_t heap_used(void);
size_t heap_free(void);
#endif 
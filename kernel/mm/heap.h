#pragma once

#include "../../include/types.h"

/*
 * Kernel heap — slab allocator for small objects, direct PMM+VMM for large.
 *
 * Size classes:  16, 32, 64, 128, 256, 512, 1024 bytes  (slab)
 *                > 1024 bytes                            (page-rounded)
 *
 * All returned pointers are at least 16-byte aligned.
 */

void  heap_init(void);

void *kmalloc (size_t size);
void *kzalloc (size_t size);          /* zeroed */
void *krealloc(void *ptr, size_t sz); /* NULL-safe */
void  kfree   (void *ptr);

void  heap_stats(void);

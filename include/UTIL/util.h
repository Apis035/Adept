
#ifndef UTIL_H
#define UTIL_H

/*
    ================================== util.h ==================================
    Module for general memory-related utilities
    ----------------------------------------------------------------------------
*/

#include "UTIL/ground.h"

// ---------------- expand ----------------
// Expands an array if it won't be able hold new elements
void expand(void **inout_memory, length_t unit_size, length_t length, length_t *inout_capacity,
    length_t amount, length_t default_capacity);

// ---------------- coexpand ----------------
// Expands two arrays if they won't be able hold new elements
void coexpand(void **inout_memory1, length_t unit_size1, void **inout_memory2, length_t unit_size2,
    length_t length, length_t *inout_capacity, length_t amount, length_t default_capacity);

// ---------------- grow ----------------
// Forces growth of an array to a certain length
void grow(void **inout_memory, length_t unit_size, length_t old_length, length_t new_length);

// ---------------- strclone ----------------
// Clones a string, producing a duplicate
char* strclone(const char *src);

#endif // UTIL_H
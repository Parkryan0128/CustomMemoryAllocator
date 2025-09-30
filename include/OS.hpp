#pragma once
#include <cstddef> // To use size_t

/**
 * @brief Allocates a memory chunk of a specified size from the operating system.
 * @param size The size of the memory to allocate in bytes.
 * @return The starting address of the allocated memory. Returns nullptr on failure.
 */
void* alloc_chunk(size_t size);

/**
 * @brief Returns a previously allocated memory chunk to the operating system.
 * @param ptr The starting address of the memory to return.
 * @param size The size of the memory to return.
 */
void free_chunk(void* ptr, size_t size);
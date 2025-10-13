#pragma once

#include "OS.hpp"
#include <cstddef>
#include <iostream>

class IAllocator {
public:
    virtual ~IAllocator() = default;
    virtual void* allocate() = 0;
    virtual void deallocate(void* ptr) = 0;
};

template <size_t BlockSize>
class MemoryPool : public IAllocator {
public:
    MemoryPool() {
        grow();
    }

    ~MemoryPool() {
        Chunk* current = m_chunkList;
        while (current != nullptr) {
            Chunk* next = current->next;
            free_chunk(current, CHUNK_SIZE);
            current = next;
        }
    }

    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    void* allocate() {
        if (m_head == nullptr) {
            grow();
        }
        if (m_head == nullptr) {
            return nullptr;
        }
        Block* freeBlock = m_head;
        m_head = m_head->next;
        return static_cast<void*>(freeBlock);
    }

    void deallocate(void* ptr) {
        if (ptr == nullptr) {
            return;
        }
        Block* blockToFree = static_cast<Block*>(ptr);
        blockToFree->next = m_head;
        m_head = blockToFree;
    }

private:
    struct Chunk {
        Chunk* next; 
    };

    union Block {
        char data[BlockSize];
        Block* next;         
    };
    
    static constexpr size_t CHUNK_SIZE = 64 * 1024;
    static_assert(BlockSize >= sizeof(Block), "BlockSize must be large enough to hold Block metadata.");
    static_assert(CHUNK_SIZE > sizeof(Chunk), "CHUNK_SIZE must be larger than the Chunk metadata struct.");

    Block* m_head = nullptr;
    Chunk* m_chunkList = nullptr;

    void grow() {
        Chunk* newChunk = static_cast<Chunk*>(alloc_chunk(CHUNK_SIZE));
        if (newChunk == nullptr) return;
        newChunk->next = m_chunkList;
        m_chunkList = newChunk;
        char* const chunkStart = reinterpret_cast<char*>(newChunk);
        char* const blockStart = chunkStart + sizeof(Chunk);
        char* const chunkEnd = chunkStart + CHUNK_SIZE;
        for (char* p = blockStart; p + BlockSize <= chunkEnd; p += BlockSize) {
            Block* newBlock = reinterpret_cast<Block*>(p);
            newBlock->next = m_head;
            m_head = newBlock;
        }
    }
};
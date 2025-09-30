#include "SingleSizeAllocator.hpp"
#include "OS.hpp"
#include <iostream> // For logging

template <size_t BlockSize>
SingleSizeAllocator<BlockSize>::SingleSizeAllocator() {
    std::cout << "Creating SingleSizeAllocator for " << BlockSize << " byte blocks." << std::endl;

    m_chunkStart = alloc_chunk(CHUNK_SIZE);

    if (m_chunkStart) {
        m_chunkSize = CHUNK_SIZE;
        std::cout << "Acquired " << m_chunkSize << " bytes from OS at " << m_chunkStart << std::endl;

        const size_t numBlocks = m_chunkSize / BlockSize;
        std::cout << "Splicing chunk into " << numBlocks << " free blocks." << std::endl;

        m_head = static_cast<Block*>(m_chunkStart);
        Block* current = m_head;
        for (size_t i = 0; i < numBlocks - 1; ++i) {
            Block* nextBlock = reinterpret_cast<Block*>(reinterpret_cast<char*>(current) + BlockSize);
            current->next = nextBlock;
            current = nextBlock;
        }
        current->next = nullptr;
    } else {
        std::cerr << "FATAL: Failed to acquire memory from OS. Allocator is unusable." << std::endl;
        m_chunkSize = 0;
        m_head = nullptr;
    }
}

template <size_t BlockSize>
SingleSizeAllocator<BlockSize>::~SingleSizeAllocator() {
    std::cout << "Destroying SingleSizeAllocator. Returning " << m_chunkSize << " bytes to OS." << std::endl;
    free_chunk(m_chunkStart, m_chunkSize);
}

template <size_t BlockSize>
void* SingleSizeAllocator<BlockSize>::allocate() {
    if (m_head == nullptr) {
        std::cerr << "OUT OF MEMORY: SingleSizeAllocator has no free blocks." << std::endl;
        return nullptr;
    }

    Block* blockToReturn = m_head;
    m_head = m_head->next;

    std::cout << "Allocated block at address " << static_cast<void*>(blockToReturn) << std::endl;
    return blockToReturn;
}

template <size_t BlockSize>
void SingleSizeAllocator<BlockSize>::deallocate(void* ptr) {
    if (ptr == nullptr) {
        return;
    }

    Block* freedBlock = static_cast<Block*>(ptr);
    freedBlock->next = m_head;
    m_head = freedBlock;

    std::cout << "Deallocated block at address " << ptr << std::endl;
}
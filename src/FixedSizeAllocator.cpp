#include "FixedSizeAllocator.hpp"
#include <iostream> // 임시

FixedSizeAllocator::FixedSizeAllocator(size_t blockSize) : m_blockSize(blockSize) {
    std::cout << m_blockSize << " byte allocator created." << std::endl;
}

FixedSizeAllocator::~FixedSizeAllocator() {
    // TODO
}

void* FixedSizeAllocator::allocate() {
    // TODO
    std::cout << "Allocating " << m_blockSize << " bytes..." << std::endl;
    return nullptr; // 임시
}

void FixedSizeAllocator::deallocate(void* ptr) {
    // TODO
    std::cout << "Deallocating " << m_blockSize << " bytes..." << std::endl;
}
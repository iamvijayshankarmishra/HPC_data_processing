#include "StringPool.hpp"
#include <cstring>
#include <iostream>
#include <cstdlib>

StringPool::StringPool(uint32_t capacity_bytes)
    : buffer_(new char[capacity_bytes])
    , capacity_(capacity_bytes)
    , top_(0)
    {}

StringPool::~StringPool()
{
    delete[] buffer_;
}

StringRef StringPool::store(const char* str, uint16_t len)
{
    if (top_ + len > capacity_) {
        std::cerr << "StringPool overflow: used=" << top_
        << " capacity=" << capacity_ << "\n";
        std::abort();
    }

    StringRef ref{top_, len};
    std::memcpy(buffer_ + top_, str, len);
    top_ += len;
    return ref;
}

const char* StringPool::get(StringRef ref) const
{
    return buffer_ + ref.offset;
}

uint32_t StringPool::used() const {
    return top_;
}

uint32_t StringPool::capacity() const {
    return capacity_;
}
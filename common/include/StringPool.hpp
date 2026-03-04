#pragma once
#include <cstdint>

struct StringRef
{
    uint32_t offset;
    uint16_t length;
};


class StringPool
{
    public:
        explicit StringPool(uint32_t capacity_bytes);

        ~StringPool();
        StringPool(const StringPool&) = delete;
        StringPool& operator=(const StringPool&) = delete;

        StringRef store(const char* str, uint16_t len);
        const char* get(StringRef ref) const;
        uint32_t used() const;
        uint32_t capacity() const;

    private:
        char* buffer_;     
        uint32_t capacity_;
        uint32_t top_;        
};

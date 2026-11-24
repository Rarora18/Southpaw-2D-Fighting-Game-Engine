#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace sp {

// FNV-1a. Fields are fed one at a time so struct padding never leaks into the hash.
struct Hasher {
    uint64_t h = 1469598103934665603ull;

    void bytes(const void* data, size_t n) {
        const auto* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < n; ++i) {
            h ^= p[i];
            h *= 1099511628211ull;
        }
    }

    template <class T>
    void add(T v) {
        static_assert(std::is_integral<T>::value || std::is_enum<T>::value, "hash integers only");
        bytes(&v, sizeof v);
    }
};

}  // namespace sp

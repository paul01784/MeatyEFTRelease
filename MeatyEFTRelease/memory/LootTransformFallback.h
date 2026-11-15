#pragma once
#include <array>
#include <cstdint>
#include <limits>
namespace LootTransformFallback {
template<class ReadExact, class ValidPointer>
bool Resolve(uint64_t gameObject, const std::array<uint64_t,4>& offsets,
             uint64_t& output, ReadExact&& readExact, ValidPointer&& validPointer) {
    uint64_t current = gameObject;
    constexpr auto maxAddress = (std::numeric_limits<uint64_t>::max)();
    for (const auto offset : offsets) {
        if (!validPointer(current) || offset > maxAddress - 7 || current > maxAddress - offset - 7)
            return false;
        uint64_t next = 0;
        if (!readExact(current + offset, &next, sizeof(next)) || !validPointer(next)) return false;
        current = next;
    }
    output = current;
    return true;
}
}

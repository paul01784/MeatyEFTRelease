#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

namespace LootIdFallback {
template<class ReadExact, class ValidPointer>
bool Resolve(uint64_t field, std::string& output, ReadExact&& readExact, ValidPointer&& validPointer) {
    constexpr auto maxAddress = (std::numeric_limits<uint64_t>::max)();
    if (!validPointer(field) || field > maxAddress - 23) return false;
    std::array<unsigned char, 24> header{};
    if (!readExact(field, header.data(), header.size())) return false;
    uint64_t stringPointer = 0;
    std::memcpy(&stringPointer, header.data() + 16, sizeof(stringPointer));
    if (!validPointer(stringPointer) || stringPointer > maxAddress - 0x43) return false;
    int32_t length = 0;
    if (!readExact(stringPointer + 0x10, &length, sizeof(length)) || length != 24) return false;
    std::array<uint16_t, 24> chars{};
    if (!readExact(stringPointer + 0x14, chars.data(), sizeof(chars))) return false;
    std::string candidate;
    candidate.reserve(24);
    for (const auto ch : chars) {
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')))
            return false;
        candidate.push_back(static_cast<char>(ch));
    }
    output = std::move(candidate);
    return true;
}
}

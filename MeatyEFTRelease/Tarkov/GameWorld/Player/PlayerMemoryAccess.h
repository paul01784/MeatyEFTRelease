#pragma once

#include "../../../Core/Utilities.h"
#include "../../../memory/Memory.h"

#include <algorithm>
#include <initializer_list>
#include <string>
#include <vector>

namespace PlayerMemoryAccess
{
    template <typename T>
    bool tryRead(uint64_t address, T& value, DmaCacheMode cacheMode = DmaCacheMode::Cached)
    {
        value = {};

        if (!Utils::valid_pointer(address))
            return false;

        DWORD bytesRead = 0;
        const Memory::ScatterReadRequest request{ address, &value, sizeof(T) };
        return mem.TryReadScatter(&request, 1, cacheMode, "Player pointer", std::span<DWORD>(&bytesRead, 1), DmaPagingMode::NoPaging, false) == Memory::TryScatterReadResult::Success && bytesRead == sizeof(T);
    }

    inline bool tryReadPointer(uint64_t address, uint64_t& value, DmaCacheMode cacheMode = DmaCacheMode::Cached)
    {
        return tryRead(address, value, cacheMode) && Utils::valid_pointer(value);
    }

    inline bool tryReadChain(uint64_t base, std::initializer_list<uint64_t> offsets, uint64_t& value, DmaCacheMode cacheMode = DmaCacheMode::Cached)
    {
        value = 0;

        for (const uint64_t offset : offsets)
        {
            if (!tryReadPointer(base + offset, base, cacheMode))
                return false;
        }

        value = base;
        return true;
    }

    inline std::string readUtf8(uint64_t address, std::size_t maximumLength, DmaCacheMode cacheMode = DmaCacheMode::Uncached)
    {
        if (!Utils::valid_pointer(address) || maximumLength == 0)
            return {};

        std::vector<char> buffer(maximumLength + 1, '\0');
        DWORD bytesRead = 0;
        const Memory::ScatterReadRequest request{ address, buffer.data(), maximumLength };
        const auto result = mem.TryReadScatter(&request, 1, cacheMode, "Player string", std::span<DWORD>(&bytesRead, 1), DmaPagingMode::NoPaging, false);

        if (result != Memory::TryScatterReadResult::Success || bytesRead != maximumLength)
            return {};

        const auto end = std::find(buffer.begin(), buffer.end(), '\0');
        return std::string(buffer.begin(), end);
    }

    inline std::string readString(uint64_t stringPointer, int maximumLength = 128)
    {
        int length = 0;

        if (!tryRead(stringPointer + 0x10, length) || length <= 0 || length > maximumLength)
            return {};

        std::vector<wchar_t> buffer(static_cast<std::size_t>(length) + 1, L'\0');
        DWORD bytesRead = 0;
        const std::size_t byteCount = static_cast<std::size_t>(length) * sizeof(wchar_t);
        const Memory::ScatterReadRequest request{ stringPointer + 0x14, buffer.data(), byteCount };
        const auto result = mem.TryReadScatter(&request, 1, DmaCacheMode::Uncached, "Player managed string", std::span<DWORD>(&bytesRead, 1), DmaPagingMode::NoPaging, false);

        if (result != Memory::TryScatterReadResult::Success || bytesRead != byteCount)
            return {};

        const int utf8Length = WideCharToMultiByte(CP_UTF8, 0, buffer.data(), length, nullptr, 0, nullptr, nullptr);
        if (utf8Length <= 0)
            return {};

        std::string output(static_cast<std::size_t>(utf8Length), '\0');
        WideCharToMultiByte(CP_UTF8, 0, buffer.data(), length, output.data(), utf8Length, nullptr, nullptr);
        return output;
    }
}

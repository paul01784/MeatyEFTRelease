#pragma once

#include "../../../Core/Utilities.h"
#include "../../../memory/Memory.h"

#include <initializer_list>
#include <string>

namespace PlayerMemoryAccess
{
    template <typename T>
    bool tryRead(uint64_t address, T& value, DmaCacheMode cacheMode = DmaCacheMode::Cached)
    {
        value = {};

        if (!Utils::valid_pointer(address))
            return false;

        try
        {
            return mem.Read(address, &value, sizeof(T), cacheMode);
        }
        catch (...)
        {
            return false;
        }
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

    inline std::string readString(uint64_t stringPointer, int maximumLength = 128)
    {
        int length = 0;

        if (!tryRead(stringPointer + 0x10, length) || length <= 0 || length > maximumLength)
            return {};

        try
        {
            return mem.readUnicodeString(stringPointer + 0x14, length);
        }
        catch (...)
        {
            return {};
        }
    }
}

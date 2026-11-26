#pragma once

#include "../../../Core/Utilities.h"
#include "../../../memory/Memory.h"

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace PlayerMemoryAccess
{
    struct InitializationReads
    {
        const char* stage{ "class pointer" };
        const char* failure{ "validation" };
        uint64_t failedAddress{};
        unsigned directRetries{};
        unsigned directRecovered{};
    };
    inline thread_local InitializationReads* initializationReads = nullptr;

    class InitializationScope
    {
        InitializationReads* previous_;
    public:
        explicit InitializationScope(InitializationReads& reads) : previous_(initializationReads)
        { initializationReads = &reads; }
        ~InitializationScope() { initializationReads = previous_; }
        InitializationScope(const InitializationScope&) = delete;
        InitializationScope& operator=(const InitializationScope&) = delete;
    };

    inline void initializationStage(const char* stage)
    {
        if (initializationReads)
        {
            initializationReads->stage = stage;
            initializationReads->failure = "validation";
            initializationReads->failedAddress = 0;
        }
    }

    inline bool readExact(const Memory::ScatterReadRequest& request, DmaCacheMode mode, const char* label)
    {
        DWORD bytesRead = 0;
        bool complete = mem.TryReadScatter(&request, 1, mode, label,
            std::span<DWORD>(&bytesRead, 1)) == Memory::TryScatterReadResult::Success && bytesRead == request.size;
        // Only construction opts in. Runtime pose, camera and aim keep their current policy.
        if (!complete && initializationReads)
        {
            ++initializationReads->directRetries;
            complete = mem.Read(request.address, request.buffer, request.size,
                DmaCacheMode::Uncached, "Player initialization recovery");
            if (complete)
                ++initializationReads->directRecovered;
            else
            {
                initializationReads->failure = "unreadable";
                initializationReads->failedAddress = request.address;
            }
        }
        if (!complete)
            std::memset(request.buffer, 0, request.size);
        return complete;
    }

    template <typename T>
    bool tryRead(uint64_t address, T& value, DmaCacheMode cacheMode = DmaCacheMode::Uncached)
    {
        value = {};

        if (!Utils::valid_pointer(address))
            return false;

        const Memory::ScatterReadRequest request{ address, &value, sizeof(T) };
        return readExact(request, cacheMode, "Player pointer");
    }

    inline bool tryReadPointer(uint64_t address, uint64_t& value, DmaCacheMode cacheMode = DmaCacheMode::Uncached)
    {
        if (!tryRead(address, value, cacheMode))
            return false;
        if (Utils::valid_pointer(value))
            return true;
        if (initializationReads)
        {
            initializationReads->failure = "invalid_pointer";
            initializationReads->failedAddress = address;
        }
        return false;
    }

    inline bool tryReadChain(uint64_t base, std::initializer_list<uint64_t> offsets, uint64_t& value, DmaCacheMode cacheMode = DmaCacheMode::Uncached)
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
        const Memory::ScatterReadRequest request{ address, buffer.data(), maximumLength };
        if (!readExact(request, cacheMode, "Player string"))
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
        const std::size_t byteCount = static_cast<std::size_t>(length) * sizeof(wchar_t);
        const Memory::ScatterReadRequest request{ stringPointer + 0x14, buffer.data(), byteCount };
        if (!readExact(request, DmaCacheMode::Uncached, "Player managed string"))
            return {};

        const int utf8Length = WideCharToMultiByte(CP_UTF8, 0, buffer.data(), length, nullptr, 0, nullptr, nullptr);
        if (utf8Length <= 0)
            return {};

        std::string output(static_cast<std::size_t>(utf8Length), '\0');
        WideCharToMultiByte(CP_UTF8, 0, buffer.data(), length, output.data(), utf8Length, nullptr, nullptr);
        return output;
    }
}

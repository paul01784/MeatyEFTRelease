#pragma once

#include "Memory.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

class ScatterReadBatch final
{
public:
    explicit ScatterReadBatch(
        Memory& memory,
        DmaCacheMode cacheMode = DmaCacheMode::Cached,
        std::string_view label = {})
        : memory_(memory),
        cacheMode_(cacheMode),
        label_(label)
    {
    }

    ~ScatterReadBatch() noexcept = default;

    ScatterReadBatch(const ScatterReadBatch&) = delete;
    ScatterReadBatch& operator=(const ScatterReadBatch&) = delete;
    ScatterReadBatch(ScatterReadBatch&&) = delete;
    ScatterReadBatch& operator=(ScatterReadBatch&&) = delete;

    [[nodiscard]] bool Valid() const noexcept
    {
        return memory_.IsDmaOperational();
    }

    template <typename T>
    bool Add(std::uint64_t address, T& destination)
    {
        static_assert(
            std::is_trivially_copyable_v<T>,
            "Scatter read destinations must be trivially copyable");

        return AddBytes(address, &destination, sizeof(T));
    }

    bool AddBytes(
        std::uint64_t address,
        void* destination,
        std::size_t size)
    {
        if (!Memory::IsValidPointer(address) ||
            !destination ||
            size == 0)
        {
            return false;
        }

        requests_.push_back({ address, destination, size });
        return true;
    }

    bool Execute(std::string_view label = {})
    {
        const std::string_view effectiveLabel =
            label.empty() ? std::string_view(label_) : label;

        if (requests_.empty())
            return true;

        const bool result = memory_.ReadScatter(
            requests_.data(),
            requests_.size(),
            cacheMode_,
            effectiveLabel);

        requests_.clear();
        return result;
    }

private:
    Memory& memory_;
    DmaCacheMode cacheMode_ = DmaCacheMode::Cached;
    std::string label_;
    std::vector<Memory::ScatterReadRequest> requests_;
};

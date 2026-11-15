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

    template <typename T>
    bool AddTraced(uint64_t base, uint64_t offset, T& destination, const char* expression)
    {
        if (!Add(base + offset, destination)) return false;
        auto& request = requests_.back();
        request.diagnosticBase = base;
        request.diagnosticOffset = offset;
        request.diagnosticExpression = expression;
        return true;
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

    bool TryExecuteNoPaging(std::string_view label = {})
    {
        return ExecuteNoPagingImpl(DmaLockMode::Try, label);
    }

    bool ExecuteNoPaging(std::string_view label = {})
    {
        return ExecuteNoPagingImpl(DmaLockMode::Wait, label);
    }

private:
    bool ExecuteNoPagingImpl(DmaLockMode lockMode, std::string_view label)
    {
        const std::string_view effectiveLabel = label.empty() ? std::string_view(label_) : label;

        if (requests_.empty())
            return true;

        bool complete = true;
        std::size_t start = 0;

        while (start < requests_.size())
        {
            std::size_t end = start;
            std::size_t bytes = 0;

            while (end < requests_.size() && end - start < Memory::MaxDmaScatterRequests && bytes + requests_[end].size <= Memory::MaxDmaTransferBytes)
            {
                bytes += requests_[end].size;
                ++end;
            }

            if (end == start)
            {
                complete = false;
                break;
            }

            const std::size_t count = end - start;
            std::vector<DWORD> bytesRead(count, 0);
            const Memory::TryScatterReadResult result = memory_.TryReadScatter(
                requests_.data() + start,
                count,
                cacheMode_,
                effectiveLabel,
                bytesRead,
                DmaPagingMode::NoPaging,
                false,
                lockMode);

            if (result != Memory::TryScatterReadResult::Success)
            {
                complete = false;
                break;
            }

            for (std::size_t index = 0; index < count; ++index)
            {
                if (bytesRead[index] != static_cast<DWORD>(requests_[start + index].size))
                {
                    complete = false;
                    break;
                }
            }

            if (!complete)
                break;

            start = end;
        }

        requests_.clear();
        return complete;
    }

    Memory& memory_;
    DmaCacheMode cacheMode_ = DmaCacheMode::Cached;
    std::string label_;
    std::vector<Memory::ScatterReadRequest> requests_;
};

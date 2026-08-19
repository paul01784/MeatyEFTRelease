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
        bool useCache = false,
        std::string_view label = {})
        : memory_(memory),
        useCache_(useCache),
        label_(label)
    {
    }

    ~ScatterReadBatch() noexcept
    {
        if (!handle_)
            return;

        try
        {
            memory_.CloseScatterHandle(handle_);
        }
        catch (...)
        {
            // Destructors must not allow cleanup failures to escape
        }
    }

    ScatterReadBatch(const ScatterReadBatch&) = delete;
    ScatterReadBatch& operator=(const ScatterReadBatch&) = delete;
    ScatterReadBatch(ScatterReadBatch&&) = delete;
    ScatterReadBatch& operator=(ScatterReadBatch&&) = delete;

    [[nodiscard]] bool Valid() const noexcept
    {
        if (handleRequested_)
            return handle_ != nullptr;

        return memory_.IsDmaOperational();
    }

    [[nodiscard]] VMMDLL_SCATTER_HANDLE Handle()
    {
        if (handleRequested_)
            return handle_;

        handleRequested_ = true;
        handle_ = memory_.CreateScatterHandle(useCache_);

        if (!handle_)
            return nullptr;

        for (const Memory::ScatterReadRequest& request : requests_)
        {
            memory_.AddScatterReadRequest(
                handle_,
                request.address,
                request.buffer,
                request.size);
        }

        requests_.clear();
        return handle_;
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

        if (handleRequested_)
        {
            return handle_ && memory_.AddScatterReadRequest(
                handle_,
                address,
                destination,
                size);
        }

        requests_.push_back({ address, destination, size });
        return true;
    }

    bool Execute(std::string_view label = {})
    {
        const std::string_view effectiveLabel =
            label.empty() ? std::string_view(label_) : label;

        if (handleRequested_)
        {
            return handle_ && memory_.ExecuteReadScatter(
                handle_,
                useCache_,
                effectiveLabel);
        }

        if (requests_.empty())
            return true;

        const bool result = memory_.ReadScatter(
            requests_.data(),
            requests_.size(),
            useCache_,
            effectiveLabel);

        requests_.clear();
        return result;
    }

private:
    Memory& memory_;
    bool useCache_ = false;
    std::string label_;
    std::vector<Memory::ScatterReadRequest> requests_;
    VMMDLL_SCATTER_HANDLE handle_ = nullptr;
    bool handleRequested_ = false;
};

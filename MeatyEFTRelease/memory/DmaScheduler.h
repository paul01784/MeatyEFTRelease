#pragma once

#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>

// DMA hardware is a serialized resource.  This priority is attached to the
// task currently running on a thread and is observed whenever that task enters
// the DMA operation gate
enum class DmaPriority : std::uint8_t
{
    Background = 0,
    Normal = 1,
    High = 2,
    Critical = 3
};

inline DmaPriority& CurrentDmaPriorityStorage() noexcept
{
    thread_local DmaPriority priority = DmaPriority::Normal;
    return priority;
}

[[nodiscard]] inline DmaPriority GetCurrentDmaPriority() noexcept
{
    return CurrentDmaPriorityStorage();
}

class ScopedDmaPriority final
{
public:
    explicit ScopedDmaPriority(DmaPriority priority) noexcept
        : previous_(GetCurrentDmaPriority())
    {
        CurrentDmaPriorityStorage() = priority;
    }

    ~ScopedDmaPriority() noexcept
    {
        CurrentDmaPriorityStorage() = previous_;
    }

    ScopedDmaPriority(const ScopedDmaPriority&) = delete;
    ScopedDmaPriority& operator=(const ScopedDmaPriority&) = delete;

private:
    DmaPriority previous_;
};

// BasicLockable implementation used in place of a plain std::mutex.  A DMA
// operation that has already started is never interrupted, but when the gate
// becomes free the highest-priority waiter is admitted first.
class PriorityDmaMutex final
{
public:
    void lock()
    {
        const std::size_t priority = ToIndex(GetCurrentDmaPriority());
        std::unique_lock stateLock(stateMutex_);

        ++waiters_[priority];
        stateChanged_.wait(
            stateLock,
            [&]
            {
                return !locked_ && !HasWaiterAbove(priority);
            });
        --waiters_[priority];
        locked_ = true;
    }

    [[nodiscard]] bool try_lock()
    {
        const std::size_t priority = ToIndex(GetCurrentDmaPriority());
        std::lock_guard stateLock(stateMutex_);

        if (locked_ || HasWaiterAbove(priority))
            return false;

        locked_ = true;
        return true;
    }

    void unlock()
    {
        {
            std::lock_guard stateLock(stateMutex_);
            locked_ = false;
        }

        stateChanged_.notify_all();
    }

    [[nodiscard]] bool ShouldDefer(DmaPriority priority) const
    {
        const std::size_t priorityIndex = ToIndex(priority);
        std::lock_guard stateLock(stateMutex_);
        return locked_ || HasWaiterAbove(priorityIndex);
    }

private:
    static constexpr std::size_t kPriorityCount = 4;

    [[nodiscard]] static constexpr std::size_t ToIndex(
        DmaPriority priority) noexcept
    {
        return static_cast<std::size_t>(priority);
    }

    [[nodiscard]] bool HasWaiterAbove(std::size_t priority) const noexcept
    {
        for (std::size_t index = priority + 1;
            index < waiters_.size();
            ++index)
        {
            if (waiters_[index] != 0)
                return true;
        }

        return false;
    }

    mutable std::mutex stateMutex_;
    std::condition_variable stateChanged_;
    std::array<std::size_t, kPriorityCount> waiters_{};
    bool locked_ = false;
};

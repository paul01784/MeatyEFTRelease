#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>

namespace LootRetryPolicy {
inline constexpr std::uint8_t MaxAttempts = 20;

inline std::chrono::seconds Delay(std::uint8_t completedAttempts) {
    switch (completedAttempts) {
    case 1: return std::chrono::seconds(1);
    case 2: return std::chrono::seconds(2);
    case 3: return std::chrono::seconds(5);
    case 4: return std::chrono::seconds(10);
    default: return std::chrono::seconds(15);
    }
}
template<class Item>
void Apply(Item& item, std::uint8_t previousAttempts, std::chrono::steady_clock::time_point now) {
    item.resolveAttempts = static_cast<std::uint8_t>(
        (std::min)(static_cast<int>(previousAttempts) + 1, static_cast<int>(MaxAttempts)));
    if (item.failed) {
        item.wanted = false;
        if (!item.retryableFailure || item.resolveAttempts >= MaxAttempts) {
            item.pendingResolve = false;
            item.nextResolveAttempt = {};
        } else {
            item.pendingResolve = true;
            item.failed = false;
            item.nextResolveAttempt = now + Delay(item.resolveAttempts);
        }
    } else {
        item.pendingResolve = false;
        item.retryableFailure = true;
        item.failureReason.clear();
        item.nextResolveAttempt = {};
    }
}
}

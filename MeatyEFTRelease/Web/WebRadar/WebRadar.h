#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

struct LootEntity;

class WebRadar {
public:
    WebRadar();
    ~WebRadar();

    bool Start(std::uint16_t port = 8742);
    void Stop();
    void PublishFrame();
    void SetPrivacyEnabled(bool enabled);

    [[nodiscard]] bool IsRunning() const noexcept;
    [[nodiscard]] bool PrivacyEnabled() const noexcept;
    [[nodiscard]] bool IsProtectedCorpse(const LootEntity& corpse) const;
    [[nodiscard]] std::string SharePath() const;
    [[nodiscard]] std::string PublicSharePath() const;
    [[nodiscard]] std::string PublicIpStatus() const;
    [[nodiscard]] std::string LastError() const;

private:
    void WebSocketLoop(std::stop_token stop);
    void PublishLootSnapshot();
    void ClearLootSnapshot();

    std::atomic_bool m_running{ false };
    std::atomic_bool m_privacyEnabled{ true };
    std::atomic_bool m_connected{ false };
    std::atomic<std::shared_ptr<const std::string>> m_frame;
    std::atomic<std::shared_ptr<const std::string>> m_lootFrame;
    std::atomic_uint64_t m_frameVersion{ 0 };
    std::atomic_uint64_t m_lootVersion{ 0 };
    std::jthread m_thread;
    std::string m_sessionId;
    std::string m_hostSecret;
    std::string m_viewerToken;
    std::string m_status;
    std::string m_error;
    mutable std::mutex m_mutex;
    std::chrono::steady_clock::time_point m_lastPublish{};
    std::chrono::steady_clock::time_point m_nextLootPublish{};
    bool m_lastRaidActive = false;
    std::unordered_map<std::uint64_t, std::uint64_t> m_playerIds;
    std::uint64_t m_nextPlayerId = 1;
    std::unordered_set<std::string> m_protectedProfileIds;
    std::unordered_set<std::string> m_protectedNames;
    std::unordered_set<std::uint64_t> m_protectedCorpsePointers;
};

extern WebRadar webRadar;

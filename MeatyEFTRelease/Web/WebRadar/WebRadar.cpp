#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>

#include "WebRadar.h"
#include "../../Tarkov/GameWorld/MainGame.h"
#include "../../Tarkov/GameWorld/RegisteredPlayers.h"
#include "../../Tarkov/GameWorld/Loot/Loot.h"
#include "../../Tarkov/GameWorld/Exits/Exfil.h"
#include "../../Tarkov/GameWorld/Player/WatchList.h"
#include "../../UI/globals.h"
#include "../../external/nlohmann/json.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <string_view>
#include <unordered_set>

#pragma comment(lib, "Bcrypt.lib")
#pragma comment(lib, "Winhttp.lib")

namespace {
    constexpr wchar_t kWebRadarHost[] = L"webradar.meatyradar.co.uk";
    constexpr char kWebRadarBaseUrl[] = "https://webradar.meatyradar.co.uk";
    constexpr auto kStatePublishInterval = std::chrono::milliseconds(200);
    constexpr auto kReconnectDelay = std::chrono::seconds(2);
    constexpr auto kHeartbeatInterval = std::chrono::seconds(15);
    constexpr auto kInitialLootDelay = std::chrono::seconds(5);
    constexpr auto kLootPublishInterval = std::chrono::minutes(1);

    bool IsFinite(const glm::vec3& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    std::string NormalizeName(std::string_view value) {
        std::string normalized(value);
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return normalized;
    }

    std::string MakeToken() {
        std::array<unsigned char, 24> bytes{};
        if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
            return {};

        constexpr char hex[] = "0123456789abcdef";
        std::string token;
        token.reserve(bytes.size() * 2);
        for (unsigned char value : bytes) {
            token.push_back(hex[value >> 4]);
            token.push_back(hex[value & 0x0f]);
        }
        return token;
    }

    std::wstring WidenAscii(std::string_view value) {
        return std::wstring(value.begin(), value.end());
    }

    bool StopAwareSleep(std::stop_token stop, std::chrono::milliseconds duration) {
        const auto end = std::chrono::steady_clock::now() + duration;
        while (!stop.stop_requested() && std::chrono::steady_clock::now() < end)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return !stop.stop_requested();
    }

    struct CloudflareSocket {
        HINTERNET session = nullptr;
        HINTERNET connection = nullptr;
        HINTERNET socket = nullptr;

        ~CloudflareSocket() {
            Reset();
        }

        void Reset() {
            if (socket) {
                WinHttpWebSocketClose(socket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
                WinHttpCloseHandle(socket);
                socket = nullptr;
            }
            if (connection) {
                WinHttpCloseHandle(connection);
                connection = nullptr;
            }
            if (session) {
                WinHttpCloseHandle(session);
                session = nullptr;
            }
        }
    };

    bool ConnectCloudflare(CloudflareSocket& client, std::string_view sessionId, std::string_view hostSecret, std::string_view viewerToken, std::string& error) {
        client.Reset();

        client.session = WinHttpOpen(L"Meaty WebRadar/2.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!client.session) {
            error = "WinHttpOpen failed (" + std::to_string(GetLastError()) + ")";
            return false;
        }

        WinHttpSetTimeouts(client.session, 5000, 5000, 5000, 5000);

        client.connection = WinHttpConnect(client.session, kWebRadarHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!client.connection) {
            error = "Could not connect to Cloudflare (" + std::to_string(GetLastError()) + ")";
            return false;
        }

        const std::wstring path = L"/ws/host/" + WidenAscii(sessionId);
        HINTERNET request = WinHttpOpenRequest(client.connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!request) {
            error = "Could not create WebSocket request (" + std::to_string(GetLastError()) + ")";
            return false;
        }

        if (!WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) {
            error = "Could not enable WebSocket upgrade (" + std::to_string(GetLastError()) + ")";
            WinHttpCloseHandle(request);
            return false;
        }

        const std::wstring headers = L"Authorization: Bearer " + WidenAscii(hostSecret) + L"\r\nX-Viewer-Token: " + WidenAscii(viewerToken) + L"\r\n";
        if (!WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(headers.size()), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request, nullptr)) {
            error = "Cloudflare WebSocket request failed (" + std::to_string(GetLastError()) + ")";
            WinHttpCloseHandle(request);
            return false;
        }

        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX)) {
            error = "Could not read Cloudflare response (" + std::to_string(GetLastError()) + ")";
            WinHttpCloseHandle(request);
            return false;
        }

        if (status != 101) {
            error = "Cloudflare WebSocket handshake returned HTTP " + std::to_string(status);
            WinHttpCloseHandle(request);
            return false;
        }

        client.socket = WinHttpWebSocketCompleteUpgrade(request, 0);
        WinHttpCloseHandle(request);
        if (!client.socket) {
            error = "WebSocket upgrade failed (" + std::to_string(GetLastError()) + ")";
            return false;
        }

        error.clear();
        return true;
    }
}

WebRadar webRadar;

WebRadar::WebRadar()
    : m_frame(std::make_shared<const std::string>(R"({"type":"state","raid":false,"map":"","players":[],"exfils":[]})")),
      m_lootFrame(std::make_shared<const std::string>(R"({"type":"loot","items":[]})")) {
}

WebRadar::~WebRadar() {
    Stop();
}

bool WebRadar::Start(std::uint16_t port) {
    (void)port;
    if (m_running.load(std::memory_order_acquire))
        return true;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_error.clear();
    m_sessionId = MakeToken();
    m_hostSecret = MakeToken();
    m_viewerToken = MakeToken();

    if (m_sessionId.empty() || m_hostSecret.empty() || m_viewerToken.empty()) {
        m_error = "Could not generate WebRadar session credentials";
        m_sessionId.clear();
        m_hostSecret.clear();
        m_viewerToken.clear();
        return false;
    }

    m_status = "Connecting to Cloudflare...";
    m_connected.store(false, std::memory_order_release);
    m_frame.store(std::make_shared<const std::string>(R"({"type":"state","raid":false,"map":"","players":[],"exfils":[]})"), std::memory_order_release);
    m_lootFrame.store(std::make_shared<const std::string>(R"({"type":"loot","items":[]})"), std::memory_order_release);
    m_frameVersion.store(1, std::memory_order_release);
    m_lootVersion.store(1, std::memory_order_release);
    m_protectedProfileIds.clear();
    m_protectedNames.clear();
    m_protectedCorpsePointers.clear();
    m_lastPublish = {};
    m_nextLootPublish = {};
    m_lastRaidActive = false;
    m_running.store(true, std::memory_order_release);
    m_thread = std::jthread([this](std::stop_token stop) { WebSocketLoop(stop); });
    return true;
}

void WebRadar::Stop() {
    if (!m_running.exchange(false, std::memory_order_acq_rel))
        return;

    m_thread.request_stop();
    if (m_thread.joinable())
        m_thread.join();

    m_frame.store(std::make_shared<const std::string>(R"({"type":"state","raid":false,"map":"","players":[],"exfils":[]})"), std::memory_order_release);
    m_lootFrame.store(std::make_shared<const std::string>(R"({"type":"loot","items":[]})"), std::memory_order_release);
    m_frameVersion.fetch_add(1, std::memory_order_acq_rel);
    m_lootVersion.fetch_add(1, std::memory_order_acq_rel);
    m_connected.store(false, std::memory_order_release);

    std::lock_guard<std::mutex> lock(m_mutex);
    m_sessionId.clear();
    m_hostSecret.clear();
    m_viewerToken.clear();
    m_status.clear();
    m_error.clear();
    m_lastPublish = {};
    m_nextLootPublish = {};
    m_lastRaidActive = false;
    m_playerIds.clear();
    m_nextPlayerId = 1;
    m_protectedProfileIds.clear();
    m_protectedNames.clear();
    m_protectedCorpsePointers.clear();
}

void WebRadar::SetPrivacyEnabled(bool enabled) {
    if (PrivacyEnabled() == enabled)
        return;

    if (enabled) {
        m_frame.store(std::make_shared<const std::string>(R"({"type":"state","raid":false,"map":"","players":[],"exfils":[]})"), std::memory_order_release);
        m_frameVersion.fetch_add(1, std::memory_order_acq_rel);
    }

    m_privacyEnabled.store(enabled, std::memory_order_release);
    m_lastPublish = {};
    PublishFrame();
}

bool WebRadar::IsRunning() const noexcept {
    return m_running.load(std::memory_order_acquire);
}

bool WebRadar::PrivacyEnabled() const noexcept {
    return m_privacyEnabled.load(std::memory_order_acquire);
}

bool WebRadar::IsProtectedCorpse(const LootEntity& corpse) const {
    if (!IsRunning() || !PrivacyEnabled() || !corpse.isCorpse())
        return false;

    const CorpseLootState& owner = corpse.getCorpseState();
    if (corpse.m_interactiveClass != 0 && m_protectedCorpsePointers.contains(corpse.m_interactiveClass))
        return true;
    if (!owner.ownerProfileId.empty() && (m_protectedProfileIds.contains(owner.ownerProfileId) || watchListManager.IsFriend(owner.ownerProfileId)))
        return true;
    if (!owner.ownerName.empty() && m_protectedNames.contains(NormalizeName(owner.ownerName)))
        return true;
    return !corpse.longName.empty() && m_protectedNames.contains(NormalizeName(corpse.longName));
}

std::string WebRadar::SharePath() const {
    return PublicSharePath();
}

std::string WebRadar::PublicSharePath() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_sessionId.empty() || m_viewerToken.empty())
        return {};
    return std::string(kWebRadarBaseUrl) + "/r/" + m_sessionId + "?token=" + m_viewerToken;
}

std::string WebRadar::PublicIpStatus() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_status;
}

std::string WebRadar::LastError() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_error;
}

void WebRadar::ClearLootSnapshot() {
    m_lootFrame.store(std::make_shared<const std::string>(R"({"type":"loot","items":[]})"), std::memory_order_release);
    m_lootVersion.fetch_add(1, std::memory_order_acq_rel);
}

void WebRadar::PublishLootSnapshot() {
    nlohmann::json lootFrame = { {"type", "loot"}, {"items", nlohmann::json::array()} };
    const LootCacheSnapshot loot = Loot.getCacheSnapshot();

    for (const LootEntity& item : *loot) {
        if (item.pendingResolve || item.failed || !item.hasValidPosition || !IsFinite(item.worldLocation) || glm::dot(item.worldLocation, item.worldLocation) < 1.0f)
            continue;

        if (item.isCorpse()) {
            lootFrame["items"].push_back({
                {"id", "corpse:" + std::to_string(item.instance)},
                {"kind", "corpse"},
                {"name", "Corpse"},
                {"x", item.worldLocation.x},
                {"y", item.worldLocation.y},
                {"z", item.worldLocation.z}
            });
            continue;
        }

        const bool isContainer = item.m_objectClassName == "LootableContainer" || item.shortName == "AirDrop";
        if (isContainer) {
            std::string containerName = !item.shortName.empty() ? item.shortName : item.longName;
            if (containerName.empty())
                containerName = "Container";

            const std::string containerId = !item.bsgId.empty()
                ? item.bsgId
                : "container:" + NormalizeName(containerName);

            lootFrame["items"].push_back({
                {"id", containerId},
                {"kind", "container"},
                {"name", containerName},
                {"x", item.worldLocation.x},
                {"y", item.worldLocation.y},
                {"z", item.worldLocation.z}
            });
            continue;
        }

        if (item.bsgId.empty())
            continue;

        lootFrame["items"].push_back({
            {"id", item.bsgId},
            {"kind", "item"},
            {"x", item.worldLocation.x},
            {"y", item.worldLocation.y},
            {"z", item.worldLocation.z}
        });
    }

    const auto serialized = std::make_shared<const std::string>(lootFrame.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    m_lootFrame.store(serialized, std::memory_order_release);
    m_lootVersion.fetch_add(1, std::memory_order_acq_rel);
}

void WebRadar::PublishFrame() {
    if (!IsRunning())
        return;

    const auto now = std::chrono::steady_clock::now();
    const bool privacyEnabled = PrivacyEnabled();
    const bool raidActive = appGlobals::runRadar.load(std::memory_order_acquire);

    if (!raidActive && !m_lastRaidActive)
        return;

    if (raidActive && m_lastPublish != std::chrono::steady_clock::time_point{} && now - m_lastPublish < kStatePublishInterval)
        return;

    m_lastPublish = now;
    nlohmann::json frame = { {"type", "state"}, {"raid", false}, {"map", ""}, {"players", nlohmann::json::array()}, {"exfils", nlohmann::json::array()} };

    if (raidActive) {
        frame["raid"] = true;
        frame["map"] = mainGame.selectedLocation;

        if (!m_lastRaidActive) {
            ClearLootSnapshot();
            m_nextLootPublish = now + kInitialLootDelay;
        }

        const PlayerSnapshot players = registeredPlayers.getCacheSnapshot();
        std::string localGroupId = mainGame.localGroupId;

        if (localGroupId.empty()) {
            const auto local = std::find_if(players->begin(), players->end(), [](const Player& player) { return player.isLocal; });
            if (local != players->end() && local->groupId != "0")
                localGroupId = local->groupId;
        }

        for (const Player& player : *players) {
            const bool friendly = player.isFriend || (!player.profileId.empty() && watchListManager.IsFriend(player.profileId)) || (!localGroupId.empty() && player.groupId == localGroupId);

            if (player.isLocal || friendly) {
                if (!player.profileId.empty())
                    m_protectedProfileIds.insert(player.profileId);
                if (!player.DT_profileId.empty())
                    m_protectedProfileIds.insert(player.DT_profileId);
                if (ShouldRenderCorpseOwnerLabel(player.name))
                    m_protectedNames.insert(NormalizeName(player.name));
                if (ShouldRenderCorpseOwnerLabel(player.DT_nickname))
                    m_protectedNames.insert(NormalizeName(player.DT_nickname));
                if (player.P_CorpseClass != 0)
                    m_protectedCorpsePointers.insert(player.P_CorpseClass);
            }

            if (player.hasExfiled || !IsFinite(player.location) || glm::dot(player.location, player.location) < 1.0f)
                continue;

            const auto [id, inserted] = m_playerIds.try_emplace(player.instance, m_nextPlayerId);
            if (inserted)
                ++m_nextPlayerId;

            const std::string name = privacyEnabled && (player.isLocal || friendly) ? (player.isLocal ? "Local player" : "Friend " + std::to_string(id->second)) : player.name;
            frame["players"].push_back({
                {"x", player.location.x},
                {"y", player.location.y},
                {"z", player.location.z},
                {"id", id->second},
                {"name", name},
                {"local", player.isLocal},
                {"friend", friendly},
                {"ai", player.isAi},
                {"dead", player.isDead},
                {"facing", std::isfinite(player.rotation.x) ? nlohmann::json(player.rotation.x) : nlohmann::json(nullptr)}
            });
        }

        const ExfilCacheSnapshot exfils = exfil.getCacheExfilSnapshot();
        for (const exfilsMemory& point : *exfils) {
            if (!IsFinite(point.locationWorld))
                continue;

            frame["exfils"].push_back({
                {"x", point.locationWorld.x},
                {"y", point.locationWorld.y},
                {"z", point.locationWorld.z},
                {"name", point.extractName},
                {"status", point.status}
            });
        }

        if (m_nextLootPublish != std::chrono::steady_clock::time_point{} && now >= m_nextLootPublish) {
            PublishLootSnapshot();
            m_nextLootPublish = now + kLootPublishInterval;
        }
    }
    else {
        if (m_lastRaidActive)
            ClearLootSnapshot();

        m_playerIds.clear();
        m_nextPlayerId = 1;
        m_protectedProfileIds.clear();
        m_protectedNames.clear();
        m_protectedCorpsePointers.clear();
        m_nextLootPublish = {};
    }

    m_lastRaidActive = raidActive;
    const auto serialized = std::make_shared<const std::string>(frame.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    m_frame.store(serialized, std::memory_order_release);
    m_frameVersion.fetch_add(1, std::memory_order_acq_rel);
}

void WebRadar::WebSocketLoop(std::stop_token stop) {
    CloudflareSocket client;
    std::uint64_t sentFrameVersion = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t sentLootVersion = std::numeric_limits<std::uint64_t>::max();
    auto lastStateSend = std::chrono::steady_clock::time_point{};

    const auto sendPayload = [&](const std::shared_ptr<const std::string>& payload) -> DWORD {
        if (!payload || !client.socket)
            return ERROR_INVALID_HANDLE;
        return WinHttpWebSocketSend(client.socket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, const_cast<char*>(payload->data()), static_cast<DWORD>(payload->size()));
    };

    const auto markDisconnected = [&](DWORD result) {
        m_connected.store(false, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_status = "Cloudflare disconnected - reconnecting...";
            m_error = "WebSocket send failed (" + std::to_string(result) + ")";
        }
        client.Reset();
    };

    while (!stop.stop_requested()) {
        if (!client.socket) {
            std::string sessionId;
            std::string hostSecret;
            std::string viewerToken;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                sessionId = m_sessionId;
                hostSecret = m_hostSecret;
                viewerToken = m_viewerToken;
                m_status = "Connecting to Cloudflare...";
            }

            std::string error;
            if (!ConnectCloudflare(client, sessionId, hostSecret, viewerToken, error)) {
                m_connected.store(false, std::memory_order_release);
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_status = "Cloudflare connection failed";
                    m_error = error;
                }

                if (!StopAwareSleep(stop, std::chrono::duration_cast<std::chrono::milliseconds>(kReconnectDelay)))
                    break;
                continue;
            }

            m_connected.store(true, std::memory_order_release);
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_status = "Cloudflare connected";
                m_error.clear();
            }

            sentFrameVersion = std::numeric_limits<std::uint64_t>::max();
            sentLootVersion = std::numeric_limits<std::uint64_t>::max();
            lastStateSend = {};
        }

        const auto now = std::chrono::steady_clock::now();
        const std::uint64_t frameVersion = m_frameVersion.load(std::memory_order_acquire);
        const std::uint64_t lootVersion = m_lootVersion.load(std::memory_order_acquire);
        const bool heartbeat = lastStateSend == std::chrono::steady_clock::time_point{} || now - lastStateSend >= kHeartbeatInterval;

        if (frameVersion != sentFrameVersion || heartbeat) {
            const DWORD result = sendPayload(m_frame.load(std::memory_order_acquire));
            if (result != NO_ERROR) {
                markDisconnected(result);
                if (!StopAwareSleep(stop, std::chrono::duration_cast<std::chrono::milliseconds>(kReconnectDelay)))
                    break;
                continue;
            }

            sentFrameVersion = frameVersion;
            lastStateSend = now;
        }

        if (lootVersion != sentLootVersion) {
            const DWORD result = sendPayload(m_lootFrame.load(std::memory_order_acquire));
            if (result != NO_ERROR) {
                markDisconnected(result);
                if (!StopAwareSleep(stop, std::chrono::duration_cast<std::chrono::milliseconds>(kReconnectDelay)))
                    break;
                continue;
            }

            sentLootVersion = lootVersion;
        }

        StopAwareSleep(stop, std::chrono::milliseconds(50));
    }

    client.Reset();
    m_connected.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_running.load(std::memory_order_acquire))
        m_status.clear();
}

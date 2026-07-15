#include "headers/watchList.h"

#include "headers/players.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

using json = nlohmann::json;

// Global instance.
WatchListManager watchListManager{};

WatchListManager::WatchListManager(std::filesystem::path watchListPath) : watchListPath_(std::move(watchListPath))
{
    raidHistory_.clear();
    activeRaidIndex_.reset();
    raidCounter_ = 0;

    LoadWatchList();
}

bool WatchListManager::LoadWatchList()
{
    std::unique_lock<std::shared_mutex> lock(mutex_);

    try
    {
        lastError_.clear();

        if (!std::filesystem::exists(watchListPath_))
        {
            watchList_.clear();
            watchIndex_.clear();
            return true;
        }

        std::ifstream input(watchListPath_, std::ios::binary);

        if (!input.is_open())
        {
            lastError_ = "Failed to open watchlist file for reading: " + watchListPath_.string();

            return false;
        }

        json root;
        input >> root;

        const json* playerArray = nullptr;

        if (root.is_object())
        {
            auto playersIt = root.find("players");

            if (playersIt == root.end() || !playersIt->is_array())
            {
                throw std::runtime_error("Watchlist JSON does not contain a valid players array.");
            }

            playerArray = &(*playersIt);
        }
        else if (root.is_array())
        {
            playerArray = &root;
        }
        else
        {
            throw std::runtime_error("Watchlist JSON root must be an object or array.");
        }

        std::vector<WatchListEntry> loadedWatchList;
        std::unordered_map<std::string, std::size_t> loadedIndex;

        loadedWatchList.reserve(playerArray->size());
        loadedIndex.reserve(playerArray->size());

        for (const json& item : *playerArray)
        {
            if (!item.is_object())
                continue;

            WatchListEntry entry;

            entry.dateAdded = item.value("dateAdded", std::string{});
            entry.profileId = item.value("profileId", std::string{});
            entry.name = item.value("name", std::string{});
            entry.reason = item.value("reason", std::string{});

            //watchlist entry cannot work without a profileid
            if (entry.profileId.empty())
                continue;

            //duplicate profile IDs in the JSON
            if (loadedIndex.find(entry.profileId) != loadedIndex.end())
                continue;

            if (entry.dateAdded.empty())
            {
                entry.dateAdded = FormatTimestamp(std::chrono::system_clock::now());
            }

            const std::size_t newIndex = loadedWatchList.size();

            loadedWatchList.push_back(std::move(entry));

            loadedIndex.emplace(loadedWatchList[newIndex].profileId, newIndex);
        }

        watchList_ = std::move(loadedWatchList);
        watchIndex_ = std::move(loadedIndex);

        return true;
    }
    catch (const std::exception& ex)
    {
        lastError_ = "Failed to load watchlist JSON: " + std::string(ex.what());

        return false;
    }
    catch (...)
    {
        lastError_ = "Failed to load watchlist JSON because of an unknown error.";

        return false;
    }
}

bool WatchListManager::SaveWatchList() const
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    return SaveWatchListUnlocked();
}

bool WatchListManager::SaveWatchListUnlocked() const
{
    try
    {
        json root;

        root["version"] = 1;
        root["players"] = json::array();

        for (const WatchListEntry& entry : watchList_)
        {
            root["players"].push_back(
                {
                    { "dateAdded", entry.dateAdded },
                    { "profileId", entry.profileId },
                    { "name", entry.name },
                    { "reason", entry.reason }
                }
            );
        }

        const std::filesystem::path parentPath =
            watchListPath_.parent_path();

        if (!parentPath.empty())
        {
            std::error_code directoryError;

            std::filesystem::create_directories(
                parentPath,
                directoryError
            );

            if (directoryError)
            {
                lastError_ = "Failed to create watchlist directory: " + directoryError.message();

                return false;
            }
        }

        std::filesystem::path temporaryPath = watchListPath_;
        temporaryPath += ".tmp";

        {
            std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);

            if (!output.is_open())
            {
                lastError_ = "Failed to open temporary watchlist file for writing: " + temporaryPath.string();

                return false;
            }

            output << std::setw(4) << root;
            output.flush();

            if (!output.good())
            {
                lastError_ = "Failed while writing temporary watchlist file: " + temporaryPath.string();

                output.close();

                std::error_code removeError;
                std::filesystem::remove(temporaryPath, removeError);

                return false;
            }
        }

        std::error_code fileError;

        if (std::filesystem::exists(watchListPath_, fileError))
        {
            fileError.clear();

            std::filesystem::remove(watchListPath_, fileError);

            if (fileError)
            {
                lastError_ = "Failed to replace existing watchlist file: " + fileError.message();

                std::error_code cleanupError;
                std::filesystem::remove(temporaryPath, cleanupError);

                return false;
            }
        }

        fileError.clear();

        std::filesystem::rename(temporaryPath, watchListPath_, fileError);

        if (fileError)
        {
            lastError_ = "Failed to move temporary watchlist file into place: " + fileError.message();

            std::error_code cleanupError;
            std::filesystem::remove(temporaryPath,cleanupError);

            return false;
        }

        lastError_.clear();
        return true;
    }
    catch (const std::exception& ex)
    {
        lastError_ = "Failed to save watchlist JSON: " + std::string(ex.what());

        return false;
    }
    catch (...)
    {
        lastError_ = "Failed to save watchlist JSON because of an unknown error.";

        return false;
    }
}

bool WatchListManager::AddPlayer(PlayerCache& player, const std::string& reason)
{
    if (!IsEligiblePlayer(player))
    {
        player.isWatched = false;
        return false;
    }

    const std::string profileId = ResolveProfileId(player);

    if (profileId.empty())
    {
        player.isWatched = false;
        return false;
    }

    const bool added = AddPlayer(profileId, ResolvePlayerName(player), reason);

    player.isWatched = IsWatched(profileId);

    return added;
}

bool WatchListManager::AddPlayer(const std::string& profileId, const std::string& name, const std::string& reason)
{
    if (profileId.empty())
        return false;

    std::unique_lock<std::shared_mutex> lock(mutex_);

    lastError_.clear();

    // profileId is the unique key
    if (watchIndex_.find(profileId) != watchIndex_.end())
        return false;

    WatchListEntry newEntry;

    newEntry.dateAdded = FormatTimestamp(std::chrono::system_clock::now());

    newEntry.profileId = profileId;

    newEntry.name =
        name.empty()
        ? "Unknown Player"
        : name;

    newEntry.reason = reason;

    const std::size_t newIndex = watchList_.size();

    watchList_.push_back(std::move(newEntry));

    watchIndex_[profileId] = newIndex;

    if (!SaveWatchListUnlocked())
    {
        watchList_.pop_back();
        watchIndex_.erase(profileId);

        return false;
    }

    for (RaidRecord& raid : raidHistory_)
    {
        for (RaidPlayerEntry& raidPlayer : raid.players)
        {
            if (raidPlayer.profileId == profileId)
                raidPlayer.isWatched = true;
        }
    }

    return true;
}

bool WatchListManager::RemovePlayer(const std::string& profileId)
{
    if (profileId.empty())
        return false;

    std::unique_lock<std::shared_mutex> lock(mutex_);

    lastError_.clear();

    const auto indexIt = watchIndex_.find(profileId);

    if (indexIt == watchIndex_.end())
        return false;

    const std::size_t removedIndex = indexIt->second;
    const WatchListEntry removedEntry = watchList_[removedIndex];

    watchList_.erase(watchList_.begin() + static_cast<std::ptrdiff_t>(removedIndex));

    RebuildWatchIndexUnlocked();

    if (!SaveWatchListUnlocked())
    {
        //restore the removed entry if the JSON save failed
        watchList_.insert(
            watchList_.begin() +
            static_cast<std::ptrdiff_t>(removedIndex),
            removedEntry
        );

        RebuildWatchIndexUnlocked();
        return false;
    }

    //update currently retained raid-history records
    for (RaidRecord& raid : raidHistory_)
    {
        for (RaidPlayerEntry& raidPlayer : raid.players)
        {
            if (raidPlayer.profileId == profileId)
                raidPlayer.isWatched = false;
        }
    }

    return true;
}

bool WatchListManager::UpdateReason(const std::string& profileId, const std::string& newReason)
{
    if (profileId.empty())
        return false;

    std::unique_lock<std::shared_mutex> lock(mutex_);

    lastError_.clear();

    const auto indexIt = watchIndex_.find(profileId);

    if (indexIt == watchIndex_.end())
        return false;

    WatchListEntry& entry = watchList_[indexIt->second];

    const std::string previousReason = entry.reason;

    entry.reason = newReason;

    if (!SaveWatchListUnlocked())
    {
        entry.reason = previousReason;
        return false;
    }

    return true;
}

bool WatchListManager::IsWatched(const std::string& profileId) const
{
    if (profileId.empty())
        return false;

    std::shared_lock<std::shared_mutex> lock(mutex_);

    return watchIndex_.find(profileId) != watchIndex_.end();
}

std::optional<WatchListManager::WatchListEntry> WatchListManager::GetWatchedPlayer(const std::string& profileId) const
{
    if (profileId.empty())
        return std::nullopt;

    std::shared_lock<std::shared_mutex> lock(mutex_);

    const auto indexIt = watchIndex_.find(profileId);

    if (indexIt == watchIndex_.end())
        return std::nullopt;

    return watchList_[indexIt->second];
}

std::vector<WatchListManager::WatchListEntry> WatchListManager::GetWatchList() const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return watchList_;
}

std::size_t WatchListManager::GetWatchListCount() const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return watchList_.size();
}

bool WatchListManager::ClearWatchList()
{
    std::unique_lock<std::shared_mutex> lock(mutex_);

    lastError_.clear();

    if (watchList_.empty())
        return true;

    const std::vector<WatchListEntry> previousWatchList = watchList_;

    const std::unordered_map<std::string, std::size_t> previousIndex = watchIndex_;

    watchList_.clear();
    watchIndex_.clear();

    if (!SaveWatchListUnlocked())
    {
        watchList_ = previousWatchList;
        watchIndex_ = previousIndex;

        return false;
    }

    for (RaidRecord& raid : raidHistory_)
    {
        for (RaidPlayerEntry& raidPlayer : raid.players)
            raidPlayer.isWatched = false;
    }

    return true;
}

void WatchListManager::UpdateWatchStatus(PlayerCache& player) const
{
    const std::string profileId = ResolveProfileId(player);

    std::shared_lock<std::shared_mutex> lock(mutex_);

    player.isWatched = !profileId.empty() && watchIndex_.find(profileId) != watchIndex_.end();
}

bool WatchListManager::logRaidStart(const std::string& mapName)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);

    const auto systemNow = std::chrono::system_clock::now();
    const auto steadyNow = std::chrono::steady_clock::now();

    //two active raids from existing at once
    if (activeRaidIndex_.has_value())
    {
        EndActiveRaidUnlocked(systemNow, steadyNow);
    }

    ++raidCounter_;

    RaidRecord newRaid;

    newRaid.raidId = FormatRaidId(raidCounter_, systemNow);

    newRaid.mapName = mapName.empty() ? "Unknown Map" : mapName;

    newRaid.startedAt = FormatTimestamp(systemNow);
    newRaid.endedAt.clear();

    newRaid.durationSeconds = 0;
    newRaid.durationText = "00:00:00";

    newRaid.active = true;

    raidHistory_.push_back(std::move(newRaid));

    activeRaidIndex_ = raidHistory_.size() - 1;
    activeRaidStartedSteady_ = steadyNow;

    return true;
}

bool WatchListManager::logRaidEnd()
{
    std::unique_lock<std::shared_mutex> lock(mutex_);

    if (!activeRaidIndex_.has_value())
        return false;

    EndActiveRaidUnlocked(std::chrono::system_clock::now(), std::chrono::steady_clock::now());

    return true;
}

bool WatchListManager::logAddPlayer(PlayerCache& player)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);

    return UpsertRaidPlayerUnlocked(player, false);
}

bool WatchListManager::logUpdatePlayerPID(PlayerCache& player)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);

    return UpsertRaidPlayerUnlocked(player, true);
}

bool WatchListManager::HasActiveRaid() const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);

    return activeRaidIndex_.has_value() &&
        *activeRaidIndex_ < raidHistory_.size() &&
        raidHistory_[*activeRaidIndex_].active;
}

std::optional<WatchListManager::RaidRecord> WatchListManager::GetActiveRaid() const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);

    if (!activeRaidIndex_.has_value())
        return std::nullopt;

    if (*activeRaidIndex_ >= raidHistory_.size())
        return std::nullopt;

    RaidRecord raidCopy = raidHistory_[*activeRaidIndex_];

    if (raidCopy.active)
    {
        raidCopy.durationSeconds =
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() -
                activeRaidStartedSteady_
            ).count();

        raidCopy.durationText = FormatDuration(raidCopy.durationSeconds);
    }

    return raidCopy;
}

std::vector<WatchListManager::RaidRecord>
WatchListManager::GetRaidHistory() const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);

    std::vector<RaidRecord> historyCopy = raidHistory_;

    if (activeRaidIndex_.has_value() && *activeRaidIndex_ < historyCopy.size())
    {
        RaidRecord& activeRaid = historyCopy[*activeRaidIndex_];

        if (activeRaid.active)
        {
            activeRaid.durationSeconds =
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() -
                    activeRaidStartedSteady_
                ).count();

            activeRaid.durationText = FormatDuration(activeRaid.durationSeconds);
        }
    }

    return historyCopy;
}

std::string WatchListManager::GetLastError() const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return lastError_;
}

std::filesystem::path WatchListManager::GetWatchListPath() const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return watchListPath_;
}

void WatchListManager::RebuildWatchIndexUnlocked()
{
    watchIndex_.clear();
    watchIndex_.reserve(watchList_.size());

    for (std::size_t index = 0;
        index < watchList_.size();
        ++index)
    {
        const std::string& profileId = watchList_[index].profileId;

        if (!profileId.empty())
            watchIndex_[profileId] = index;
    }
}

bool WatchListManager::UpsertRaidPlayerUnlocked(PlayerCache& player, bool requireProfileId)
{
    if (!activeRaidIndex_.has_value())
        return false;

    if (*activeRaidIndex_ >= raidHistory_.size())
        return false;

    if (!IsEligiblePlayer(player))
        return false;

    const std::string profileId = ResolveProfileId(player);

    if (requireProfileId && profileId.empty())
        return false;

    // A player must have at least one usable temporary or permanent ID
    if (player.instance == 0 && profileId.empty())
        return false;

    RaidRecord& raid = raidHistory_[*activeRaidIndex_];

    if (!raid.active)
        return false;

    player.isWatched = !profileId.empty() && watchIndex_.find(profileId) != watchIndex_.end();

    const std::optional<std::size_t> instanceIndex = FindRaidPlayerByInstanceUnlocked(raid, player.instance);

    const std::optional<std::size_t> profileIndex = FindRaidPlayerByProfileIdUnlocked(raid, profileId);

    if (instanceIndex.has_value() && profileIndex.has_value() && *instanceIndex != *profileIndex)
    {
        const std::size_t keepIndex = *instanceIndex;
        const std::size_t duplicateIndex = *profileIndex;

        std::string preservedFirstSeen = raid.players[keepIndex].firstSeenAt;

        if (preservedFirstSeen.empty())
        {
            preservedFirstSeen = raid.players[duplicateIndex].firstSeenAt;
        }

        CopyPlayerDataUnlocked(raid.players[keepIndex], player);

        raid.players[keepIndex].firstSeenAt = preservedFirstSeen;

        raid.players.erase(raid.players.begin() + static_cast<std::ptrdiff_t>(duplicateIndex));

        return true;
    }

    std::optional<std::size_t> existingIndex;

    if (instanceIndex.has_value())
        existingIndex = instanceIndex;
    else if (profileIndex.has_value())
        existingIndex = profileIndex;

    if (existingIndex.has_value())
    {
        RaidPlayerEntry& existingPlayer =
            raid.players[*existingIndex];

        const std::string preservedFirstSeen =
            existingPlayer.firstSeenAt;

        CopyPlayerDataUnlocked(existingPlayer, player);

        existingPlayer.firstSeenAt =
            preservedFirstSeen.empty()
            ? FormatTimestamp(std::chrono::system_clock::now())
            : preservedFirstSeen;

        return true;
    }

    RaidPlayerEntry newPlayer;

    newPlayer.firstSeenAt = FormatTimestamp(std::chrono::system_clock::now());

    CopyPlayerDataUnlocked( newPlayer, player);

    raid.players.push_back(std::move(newPlayer));

    return true;
}

void WatchListManager::CopyPlayerDataUnlocked(RaidPlayerEntry& destination, const PlayerCache& player) const
{
    const std::string profileId = ResolveProfileId(player);

    const std::string accountId = ResolveAccountId(player);

    const std::string playerName = ResolvePlayerName(player);

    if (player.instance != 0)
        destination.instance = player.instance;

    if (!profileId.empty())
        destination.profileId = profileId;

    if (!accountId.empty())
        destination.accountId = accountId;

    if (!playerName.empty())
        destination.name = playerName;

    if (!player.groupId.empty())
        destination.groupId = player.groupId;

    if (!player.side.empty())
        destination.side = player.side;

    destination.lastUpdatedAt = FormatTimestamp(std::chrono::system_clock::now());

    destination.isLocal = player.isLocal;

    destination.isWatched =
        !destination.profileId.empty() &&
        watchIndex_.find(destination.profileId) != watchIndex_.end();

    destination.isDead = player.isDead;
    destination.hasExfiled = player.hasExfiled;

    if (player.DT_lvl > 0)
        destination.level = player.DT_lvl;

    destination.kd = player.kd;
    destination.pmcKd = player.pkd;
    destination.hours = player.hours;
    destination.playerValue = player.playerValue;
    destination.itemInHand = player.itemInHand;
}

std::optional<std::size_t>
WatchListManager::FindRaidPlayerByInstanceUnlocked(const RaidRecord& raid, uint64_t instance) const
{
    if (instance == 0)
        return std::nullopt;

    for (std::size_t index = 0;
        index < raid.players.size();
        ++index)
    {
        if (raid.players[index].instance == instance)
            return index;
    }

    return std::nullopt;
}

std::optional<std::size_t> WatchListManager::FindRaidPlayerByProfileIdUnlocked(const RaidRecord& raid, const std::string& profileId) const
{
    if (profileId.empty())
        return std::nullopt;

    for (std::size_t index = 0;
        index < raid.players.size();
        ++index)
    {
        if (raid.players[index].profileId == profileId)
            return index;
    }

    return std::nullopt;
}

void WatchListManager::EndActiveRaidUnlocked(const std::chrono::system_clock::time_point& systemNow, const std::chrono::steady_clock::time_point& steadyNow)
{
    if (!activeRaidIndex_.has_value())
        return;

    if (*activeRaidIndex_ >= raidHistory_.size())
    {
        activeRaidIndex_.reset();
        activeRaidStartedSteady_ = {};
        return;
    }

    RaidRecord& raid = raidHistory_[*activeRaidIndex_];

    raid.endedAt = FormatTimestamp(systemNow);

    raid.durationSeconds =
        std::chrono::duration_cast<std::chrono::seconds>(
            steadyNow - activeRaidStartedSteady_
        ).count();

    if (raid.durationSeconds < 0)
        raid.durationSeconds = 0;

    raid.durationText = FormatDuration(raid.durationSeconds);

    raid.active = false;

    activeRaidIndex_.reset();
    activeRaidStartedSteady_ = {};
}

bool WatchListManager::IsEligiblePlayer(const PlayerCache& player)
{
    return player.isPlayer &&
        !player.isPlayerScav;
}

std::string WatchListManager::ResolveProfileId(const PlayerCache& player)
{
    if (!player.profileId.empty())
        return player.profileId;

    if (!player.DT_profileId.empty())
        return player.DT_profileId;

    return {};
}

std::string WatchListManager::ResolveAccountId(const PlayerCache& player)
{
    if (!player.accountId.empty())
        return player.accountId;

    if (!player.DT_accountId.empty())
        return player.DT_accountId;

    return {};
}

std::string WatchListManager::ResolvePlayerName(const PlayerCache& player)
{
    if (!player.name.empty())
        return player.name;

    if (!player.DT_nickname.empty())
        return player.DT_nickname;

    return "Unknown Player";
}

std::string WatchListManager::FormatTimestamp(const std::chrono::system_clock::time_point& time)
{
    const std::time_t rawTime =
        std::chrono::system_clock::to_time_t(time);

    std::tm localTime{};

    localtime_s(&localTime, &rawTime);

    std::ostringstream output;

    output << std::put_time(
        &localTime,
        "%H:%M %d/%m/%y"
    );

    return output.str();
}

std::string WatchListManager::FormatRaidId(std::size_t raidNumber, const std::chrono::system_clock::time_point& time)
{
    const std::time_t rawTime = std::chrono::system_clock::to_time_t(time);

    std::tm localTime{};

    localtime_s(&localTime, &rawTime);

    std::ostringstream output;

    output
        << "Raid #"
        << raidNumber
        << " - "
        << localTime.tm_mday
        << ' '
        << std::put_time(&localTime, "%B %H:%M");

    return output.str();
}

std::string WatchListManager::FormatDuration(long long durationSeconds)
{
    if (durationSeconds < 0)
        durationSeconds = 0;

    const long long hours =
        durationSeconds / 3600;

    const long long minutes =
        (durationSeconds % 3600) / 60;

    const long long seconds =
        durationSeconds % 60;

    std::ostringstream output;

    output
        << std::setfill('0')
        << std::setw(2)
        << hours
        << ':'
        << std::setw(2)
        << minutes
        << ':'
        << std::setw(2)
        << seconds;

    return output.str();
}

void WatchListManager::RenderWindow()
{
    if (!appMenu::appWatchList)
        return;

    static constexpr ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;


    static std::string editProfileId;
    static std::string editPlayerName;

    static std::string removeProfileId;
    static std::string removePlayerName;

    static std::string addProfileId;
    static std::string addPlayerName;

    static std::array<char, 512> editReasonBuffer{};
    static std::array<char, 512> addReasonBuffer{};

    static bool requestEditPopup = false;
    static bool requestRemovePopup = false;
    static bool requestAddPopup = false;

    static std::string actionError;

    const auto CopyStringToBuffer =
        [](std::array<char, 512>& buffer,
            const std::string& value)
        {
            buffer.fill('\0');

            const std::size_t copyLength =
                std::min(
                    value.size(),
                    buffer.size() - 1
                );

            if (copyLength > 0)
            {
                std::memcpy(
                    buffer.data(),
                    value.data(),
                    copyLength
                );
            }

            buffer[copyLength] = '\0';
        };

    const std::vector<WatchListEntry> watchListSnapshot = GetWatchList();

    const std::vector<RaidRecord> raidHistorySnapshot = GetRaidHistory();

    // Window

    const ImGuiViewport* viewport =
        ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(
        ImVec2(
            (viewport->Pos.x + viewport->Size.x) - 810.0f,
            viewport->Pos.y + 10.0f
        )
    );

    ImGui::SetNextWindowSize(
        ImVec2(
            750.0f,
            viewport->Size.y - 50.0f
        )
    );

    if (ImGui::Begin(
        "WatchList Manager",
        &appMenu::appWatchList,
        windowFlags
    ))
    {
        ImGui::SetCursorPos(
            ImVec2(10.0f, 45.0f)
        );

        const ImVec2 childSize(
            ImGui::GetWindowSize().x - 20.0f,
            ImGui::GetWindowSize().y - 60.0f
        );

        if (ImGui::BeginChild(
            "##WatchListManagerContent",
            childSize,
            true
        ))
        {
            if (ImGui::BeginTabBar(
                "##WatchListManagerTabs",
                ImGuiTabBarFlags_None
            ))
            {
                // WATCHLIST TAB

                const std::string watchListTabName =
                    "Watchlist (" +
                    std::to_string(watchListSnapshot.size()) +
                    ")";

                if (ImGui::BeginTabItem(
                    watchListTabName.c_str()
                ))
                {
                    ImGui::TextDisabled(
                        "Players stored in watchlist.json"
                    );

                    ImGui::Spacing();

                    if (watchListSnapshot.empty())
                    {
                        ImGui::TextDisabled(
                            "The watchlist is currently empty."
                        );
                    }
                    else
                    {
                        const ImGuiTableFlags tableFlags =
                            ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_BordersInnerH |
                            ImGuiTableFlags_BordersInnerV |
                            ImGuiTableFlags_Resizable |
                            ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_SizingStretchProp;

                        const ImVec2 tableSize(
                            0.0f,
                            ImGui::GetContentRegionAvail().y
                        );

                        if (ImGui::BeginTable(
                            "##CurrentWatchListTable",
                            5,
                            tableFlags,
                            tableSize
                        ))
                        {
                            ImGui::TableSetupScrollFreeze(
                                0,
                                1
                            );

                            ImGui::TableSetupColumn(
                                "Player",
                                ImGuiTableColumnFlags_WidthFixed,
                                135.0f
                            );

                            ImGui::TableSetupColumn(
                                "Profile ID",
                                ImGuiTableColumnFlags_WidthStretch,
                                1.0f
                            );

                            ImGui::TableSetupColumn(
                                "Added",
                                ImGuiTableColumnFlags_WidthFixed,
                                105.0f
                            );

                            ImGui::TableSetupColumn(
                                "Reason",
                                ImGuiTableColumnFlags_WidthStretch,
                                1.4f
                            );

                            ImGui::TableSetupColumn(
                                "Actions",
                                ImGuiTableColumnFlags_WidthFixed,
                                120.0f
                            );

                            ImGui::TableHeadersRow();

                            for (const WatchListEntry& entry :
                                watchListSnapshot)
                            {
                                ImGui::PushID(
                                    entry.profileId.c_str()
                                );

                                ImGui::TableNextRow();

                                // Player
                                ImGui::TableSetColumnIndex(0);

                                if (!entry.name.empty())
                                {
                                    ImGui::TextUnformatted(
                                        entry.name.c_str()
                                    );
                                }
                                else
                                {
                                    ImGui::TextDisabled(
                                        "Unknown Player"
                                    );
                                }

                                // Profile ID
                                ImGui::TableSetColumnIndex(1);

                                ImGui::TextWrapped(
                                    "%s",
                                    entry.profileId.c_str()
                                );

                                // Date added
                                ImGui::TableSetColumnIndex(2);

                                ImGui::TextUnformatted(
                                    entry.dateAdded.c_str()
                                );

                                // Reason
                                ImGui::TableSetColumnIndex(3);

                                if (!entry.reason.empty())
                                {
                                    ImGui::TextWrapped(
                                        "%s",
                                        entry.reason.c_str()
                                    );
                                }
                                else
                                {
                                    ImGui::TextDisabled(
                                        "No reason entered"
                                    );
                                }

                                // Actions
                                ImGui::TableSetColumnIndex(4);

                                if (ImGui::SmallButton("Edit"))
                                {
                                    editProfileId =
                                        entry.profileId;

                                    editPlayerName =
                                        entry.name;

                                    CopyStringToBuffer(
                                        editReasonBuffer,
                                        entry.reason
                                    );

                                    actionError.clear();
                                    requestEditPopup = true;
                                }

                                ImGui::SameLine();

                                if (ImGui::SmallButton("Remove"))
                                {
                                    removeProfileId =
                                        entry.profileId;

                                    removePlayerName =
                                        entry.name;

                                    actionError.clear();
                                    requestRemovePopup = true;
                                }

                                ImGui::PopID();
                            }

                            ImGui::EndTable();
                        }
                    }

                    ImGui::EndTabItem();
                }

                // RAID HISTORY TAB

                const std::string raidTabName =
                    "Raid List (" +
                    std::to_string(raidHistorySnapshot.size()) +
                    ")";

                if (ImGui::BeginTabItem(
                    raidTabName.c_str()
                ))
                {
                    ImGui::TextDisabled(
                        "Raid history is held in memory and is cleared when the application closes."
                    );

                    ImGui::Spacing();

                    if (raidHistorySnapshot.empty())
                    {
                        ImGui::TextDisabled(
                            "No raids have been recorded this session."
                        );
                    }
                    else
                    {
                        if (ImGui::BeginChild(
                            "##RaidHistoryScroll",
                            ImVec2(0.0f, 0.0f),
                            false,
                            ImGuiWindowFlags_AlwaysVerticalScrollbar
                        ))
                        {
                            // Newest raid first.
                            for (std::size_t reverseIndex =
                                raidHistorySnapshot.size();
                                reverseIndex > 0;
                                --reverseIndex)
                            {
                                const std::size_t raidIndex =
                                    reverseIndex - 1;

                                const RaidRecord& raid =
                                    raidHistorySnapshot[raidIndex];

                                ImGui::PushID(
                                    static_cast<int>(raidIndex)
                                );

                                std::string raidHeader =
                                    raid.raidId +
                                    "  |  " +
                                    raid.mapName +
                                    "  |  " +
                                    std::to_string(
                                        raid.players.size()
                                    ) +
                                    " players";

                                if (raid.active)
                                    raidHeader += "  |  CURRENT";

                                ImGuiTreeNodeFlags headerFlags =
                                    ImGuiTreeNodeFlags_SpanAvailWidth;

                                // Current raid opens automatically
                                // Older raids start collapsed
                                if (raid.active)
                                {
                                    headerFlags |=
                                        ImGuiTreeNodeFlags_DefaultOpen;
                                }

                                const bool raidOpen =
                                    ImGui::CollapsingHeader(
                                        raidHeader.c_str(),
                                        headerFlags
                                    );

                                if (raidOpen)
                                {
                                    ImGui::Indent();

                                    ImGui::Text(
                                        "Started: %s",
                                        raid.startedAt.c_str()
                                    );

                                    ImGui::SameLine();

                                    if (raid.active)
                                    {
                                        ImGui::TextDisabled(
                                            "| In progress: %s",
                                            raid.durationText.c_str()
                                        );
                                    }
                                    else
                                    {
                                        ImGui::TextDisabled(
                                            "| Ended: %s | Duration: %s",
                                            raid.endedAt.c_str(),
                                            raid.durationText.c_str()
                                        );
                                    }

                                    ImGui::Spacing();

                                    if (raid.players.empty())
                                    {
                                        ImGui::TextDisabled(
                                            "No eligible PMC players were recorded."
                                        );
                                    }
                                    else
                                    {
                                        const ImGuiTableFlags raidTableFlags =
                                            ImGuiTableFlags_RowBg |
                                            ImGuiTableFlags_BordersInnerH |
                                            ImGuiTableFlags_BordersInnerV |
                                            ImGuiTableFlags_Resizable |
                                            ImGuiTableFlags_SizingStretchProp;

                                        std::string raidTableId =
                                            "##RaidPlayerTable_" +
                                            std::to_string(raidIndex);

                                        if (ImGui::BeginTable(
                                            raidTableId.c_str(),
                                            6,
                                            raidTableFlags
                                        ))
                                        {
                                            ImGui::TableSetupColumn(
                                                "Player",
                                                ImGuiTableColumnFlags_WidthFixed,
                                                130.0f
                                            );

                                            ImGui::TableSetupColumn(
                                                "Profile ID",
                                                ImGuiTableColumnFlags_WidthStretch,
                                                1.0f
                                            );

                                            ImGui::TableSetupColumn(
                                                "Group",
                                                ImGuiTableColumnFlags_WidthFixed,
                                                80.0f
                                            );

                                            ImGui::TableSetupColumn(
                                                "Details",
                                                ImGuiTableColumnFlags_WidthFixed,
                                                105.0f
                                            );

                                            ImGui::TableSetupColumn(
                                                "Status",
                                                ImGuiTableColumnFlags_WidthFixed,
                                                75.0f
                                            );

                                            ImGui::TableSetupColumn(
                                                "Watchlist",
                                                ImGuiTableColumnFlags_WidthFixed,
                                                85.0f
                                            );

                                            ImGui::TableHeadersRow();

                                            for (const RaidPlayerEntry& player :
                                                raid.players)
                                            {
                                                const std::string playerRowId =
                                                    !player.profileId.empty()
                                                    ? player.profileId
                                                    : "instance_" +
                                                    std::to_string(
                                                        player.instance
                                                    );

                                                ImGui::PushID(
                                                    playerRowId.c_str()
                                                );

                                                ImGui::TableNextRow();

                                                // Player
                                                ImGui::TableSetColumnIndex(0);

                                                if (!player.name.empty())
                                                {
                                                    ImGui::TextUnformatted(
                                                        player.name.c_str()
                                                    );
                                                }
                                                else
                                                {
                                                    ImGui::TextDisabled(
                                                        "Unknown Player"
                                                    );
                                                }

                                                if (!player.side.empty())
                                                {
                                                    ImGui::TextDisabled(
                                                        "%s",
                                                        player.side.c_str()
                                                    );
                                                }

                                                // Profile ID
                                                ImGui::TableSetColumnIndex(1);

                                                if (!player.profileId.empty())
                                                {
                                                    ImGui::TextWrapped(
                                                        "%s",
                                                        player.profileId.c_str()
                                                    );
                                                }
                                                else
                                                {
                                                    ImGui::TextDisabled(
                                                        "Waiting for profile scan"
                                                    );
                                                }

                                                // Group
                                                ImGui::TableSetColumnIndex(2);

                                                if (!player.groupId.empty())
                                                {
                                                    ImGui::TextWrapped(
                                                        "%s",
                                                        player.groupId.c_str()
                                                    );
                                                }
                                                else
                                                {
                                                    ImGui::TextDisabled("-");
                                                }

                                                // Details
                                                ImGui::TableSetColumnIndex(3);

                                                if (player.level > 0)
                                                {
                                                    ImGui::Text(
                                                        "Level: %d",
                                                        player.level
                                                    );
                                                }
                                                else
                                                {
                                                    ImGui::TextDisabled(
                                                        "Level: --"
                                                    );
                                                }

                                                if (player.hours > 0)
                                                {
                                                    ImGui::TextDisabled(
                                                        "%d hours",
                                                        player.hours
                                                    );
                                                }

                                                if (player.playerValue > 0)
                                                {
                                                    ImGui::TextDisabled(
                                                        "Value: %d",
                                                        player.playerValue
                                                    );
                                                }

                                                // Status
                                                ImGui::TableSetColumnIndex(4);

                                                if (player.isLocal)
                                                {
                                                    ImGui::TextDisabled(
                                                        "Local"
                                                    );
                                                }
                                                else if (player.hasExfiled)
                                                {
                                                    ImGui::TextDisabled(
                                                        "Extracted"
                                                    );
                                                }
                                                else if (player.isDead)
                                                {
                                                    ImGui::TextDisabled(
                                                        "Dead"
                                                    );
                                                }
                                                else
                                                {
                                                    ImGui::TextUnformatted(
                                                        "Active"
                                                    );
                                                }

                                                // Add to watchlist
                                                ImGui::TableSetColumnIndex(5);

                                                if (player.profileId.empty())
                                                {
                                                    ImGui::BeginDisabled();
                                                    ImGui::SmallButton(
                                                        "No ID"
                                                    );
                                                    ImGui::EndDisabled();
                                                }
                                                else if (player.isWatched)
                                                {
                                                    ImGui::BeginDisabled();
                                                    ImGui::SmallButton(
                                                        "Watched"
                                                    );
                                                    ImGui::EndDisabled();
                                                }
                                                else
                                                {
                                                    if (ImGui::SmallButton(
                                                        "Add"
                                                    ))
                                                    {
                                                        addProfileId =
                                                            player.profileId;

                                                        addPlayerName =
                                                            player.name.empty()
                                                            ? "Unknown Player"
                                                            : player.name;

                                                        addReasonBuffer.fill(
                                                            '\0'
                                                        );

                                                        actionError.clear();
                                                        requestAddPopup = true;
                                                    }
                                                }

                                                ImGui::PopID();
                                            }

                                            ImGui::EndTable();
                                        }
                                    }

                                    ImGui::Unindent();
                                    ImGui::Spacing();
                                }

                                ImGui::Separator();
                                ImGui::PopID();
                            }

                            ImGui::EndChild();
                        }
                    }

                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }

            ImGui::EndChild();
        }

        // Open requested popups outside row/table ID stacks

        if (requestEditPopup)
        {
            ImGui::OpenPopup(
                "Edit watchlist reason"
            );

            requestEditPopup = false;
        }

        if (requestRemovePopup)
        {
            ImGui::OpenPopup(
                "Remove watchlist player"
            );

            requestRemovePopup = false;
        }

        if (requestAddPopup)
        {
            ImGui::OpenPopup(
                "Add raid player to watchlist"
            );

            requestAddPopup = false;
        }

        // EDIT REASON POPUP

        ImGui::SetNextWindowSize(
            ImVec2(430.0f, 220.0f),
            ImGuiCond_Appearing
        );

        if (ImGui::BeginPopupModal(
            "Edit watchlist reason",
            nullptr,
            ImGuiWindowFlags_NoResize
        ))
        {
            ImGui::TextUnformatted(
                editPlayerName.empty()
                ? "Unknown Player"
                : editPlayerName.c_str()
            );

            ImGui::TextDisabled(
                "%s",
                editProfileId.c_str()
            );

            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextUnformatted("Reason");

            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();

            ImGui::InputTextMultiline(
                "##EditWatchReason",
                editReasonBuffer.data(),
                editReasonBuffer.size(),
                ImVec2(-1.0f, 75.0f)
            );

            if (!actionError.empty())
            {
                ImGui::TextWrapped(
                    "Error: %s",
                    actionError.c_str()
                );
            }

            if (ImGui::Button(
                "Save",
                ImVec2(90.0f, 0.0f)
            ))
            {
                const std::string newReason(
                    editReasonBuffer.data()
                );

                if (UpdateReason(
                    editProfileId,
                    newReason
                ))
                {
                    actionError.clear();
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    actionError = GetLastError();

                    if (actionError.empty())
                    {
                        actionError =
                            "The watchlist entry could not be updated.";
                    }
                }
            }

            ImGui::SameLine();

            if (ImGui::Button(
                "Cancel",
                ImVec2(90.0f, 0.0f)
            ))
            {
                actionError.clear();
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        // REMOVE CONFIRMATION POPUP

        ImGui::SetNextWindowSize(
            ImVec2(390.0f, 155.0f),
            ImGuiCond_Appearing
        );

        if (ImGui::BeginPopupModal(
            "Remove watchlist player",
            nullptr,
            ImGuiWindowFlags_NoResize
        ))
        {
            ImGui::TextWrapped(
                "Remove %s from the watchlist?",
                removePlayerName.empty()
                ? "this player"
                : removePlayerName.c_str()
            );

            ImGui::TextDisabled(
                "%s",
                removeProfileId.c_str()
            );

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (!actionError.empty())
            {
                ImGui::TextWrapped(
                    "Error: %s",
                    actionError.c_str()
                );
            }

            if (ImGui::Button(
                "Remove",
                ImVec2(90.0f, 0.0f)
            ))
            {
                if (RemovePlayer(removeProfileId))
                {
                    actionError.clear();
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    actionError = GetLastError();

                    if (actionError.empty())
                    {
                        actionError =
                            "The watchlist entry could not be removed.";
                    }
                }
            }

            ImGui::SameLine();

            if (ImGui::Button(
                "Cancel",
                ImVec2(90.0f, 0.0f)
            ))
            {
                actionError.clear();
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        // ADD RAID PLAYER POPUP

        ImGui::SetNextWindowSize(
            ImVec2(430.0f, 230.0f),
            ImGuiCond_Appearing
        );

        if (ImGui::BeginPopupModal(
            "Add raid player to watchlist",
            nullptr,
            ImGuiWindowFlags_NoResize
        ))
        {
            ImGui::TextUnformatted(
                addPlayerName.empty()
                ? "Unknown Player"
                : addPlayerName.c_str()
            );

            ImGui::TextDisabled(
                "%s",
                addProfileId.c_str()
            );

            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextUnformatted(
                "Reason for adding this player"
            );

            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();

            ImGui::InputTextMultiline(
                "##NewWatchReason",
                addReasonBuffer.data(),
                addReasonBuffer.size(),
                ImVec2(-1.0f, 75.0f)
            );

            const std::string reason(
                addReasonBuffer.data()
            );

            if (!actionError.empty())
            {
                ImGui::TextWrapped(
                    "Error: %s",
                    actionError.c_str()
                );
            }

            const bool hasReason =
                !reason.empty();

            ImGui::BeginDisabled(!hasReason);

            if (ImGui::Button(
                "Add",
                ImVec2(90.0f, 0.0f)
            ))
            {
                if (AddPlayer(
                    addProfileId,
                    addPlayerName,
                    reason
                ))
                {
                    actionError.clear();
                    ImGui::CloseCurrentPopup();
                }
                else if (IsWatched(addProfileId))
                {
                    actionError.clear();
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    actionError = GetLastError();

                    if (actionError.empty())
                    {
                        actionError =
                            "The player could not be added.";
                    }
                }
            }

            ImGui::EndDisabled();

            ImGui::SameLine();

            if (ImGui::Button(
                "Cancel",
                ImVec2(90.0f, 0.0f)
            ))
            {
                actionError.clear();
                ImGui::CloseCurrentPopup();
            }

            if (!hasReason)
            {
                ImGui::SameLine();
                ImGui::TextDisabled(
                    "Enter a reason first"
                );
            }

            ImGui::EndPopup();
        }
    }

    ImGui::End();
}
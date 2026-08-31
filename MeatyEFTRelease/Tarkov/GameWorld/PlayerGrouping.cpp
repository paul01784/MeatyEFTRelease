#include "../../UI/includes.h"
#include "RegisteredPlayers.h"

#include "../../UI/globals.h"
#include "MainGame.h"
#include "../../Core/Utilities.h"
#include "../../memory/Memory.h"

void RegisteredPlayers::checkGroupIDs()
{
    if (groupIDSet)
        return;

    if (!mem.vHandle)
        return;

    if (!mainGame.checkIfRaidStarted())
        return;

    constexpr std::size_t MinimumCacheEntries = 5; // More than 4.
    constexpr float GroupDistanceMeters = 30.0f; // max distance between players

    struct GroupSnapshot
    {
        std::uint64_t instance = 0;
        glm::vec3 worldLocation{};
        bool isLocal = false;
    };

    auto isGroupingTarget = [](const Player& player) -> bool
        {
            if (player.isBTR)
                return false;

            if (player.isDead || player.hasExfiled)
                return false;

            if (!Utils::valid_pointer(player.instance))
                return false;

            if (!(player.isPlayer || player.isPlayerScav))
                return false;

            return true;
        };

    auto hasValidWorldLocation = [](const glm::vec3& position) -> bool
        {
            if (!std::isfinite(position.x) ||
                !std::isfinite(position.y) ||
                !std::isfinite(position.z))
            {
                return false;
            }

            return position.x != 0.0f ||
                position.y != 0.0f ||
                position.z != 0.0f;
        };

    std::vector<GroupSnapshot> snapshot;
    int localSnapshotIndex = -1;

    {
        std::lock_guard<std::mutex> lock(playerMutex);

        auto& cache = registeredPlayers.getCache();

        if (cache.size() < MinimumCacheEntries)
            return;

        snapshot.reserve(cache.size());

        for (const Player& player : cache)
        {
            if (!isGroupingTarget(player))
                continue;

            if (!hasValidWorldLocation(player.location))
                return;

            GroupSnapshot entry{};
            entry.instance = player.instance;
            entry.worldLocation = player.location;
            entry.isLocal =
                player.isLocal &&
                player.instance == mainGame.localPlayerPtr;

            const int snapshotIndex = static_cast<int>(snapshot.size());
            snapshot.emplace_back(entry);

            if (entry.isLocal)
                localSnapshotIndex = snapshotIndex;
        }
    }

    //local player is included
    if (localSnapshotIndex < 0)
        return;

    const int targetCount = static_cast<int>(snapshot.size());

    std::vector<int> parent(targetCount);
    for (int i = 0; i < targetCount; ++i)
        parent[i] = i;

    auto Find = [&](int value) -> int
        {
            while (parent[value] != value)
            {
                parent[value] = parent[parent[value]];
                value = parent[value];
            }

            return value;
        };

    auto Union = [&](int left, int right)
        {
            const int leftRoot = Find(left);
            const int rightRoot = Find(right);

            if (leftRoot != rightRoot)
                parent[rightRoot] = leftRoot;
        };

    for (int a = 0; a < targetCount; ++a)
    {
        for (int b = a + 1; b < targetCount; ++b)
        {
            const float distance = registeredPlayers.getDistance(
                snapshot[a].worldLocation,
                snapshot[b].worldLocation
            );

            if (!std::isfinite(distance))
                continue;

            if (distance <= GroupDistanceMeters)
                Union(a, b);
        }
    }

    std::unordered_map<int, int> componentCounts;
    componentCounts.reserve(snapshot.size());

    for (int i = 0; i < targetCount; ++i)
        ++componentCounts[Find(i)];

    std::unordered_map<int, std::string> componentGroupIds;
    componentGroupIds.reserve(componentCounts.size());

    int nextGroupNumber = 1;
    int groupedComponents = 0;
    int groupedPlayers = 0;

    for (int i = 0; i < targetCount; ++i)
    {
        const int root = Find(i);

        if (componentCounts[root] < 2)
            continue;

        if (componentGroupIds.find(root) != componentGroupIds.end())
            continue;

        componentGroupIds.emplace(
            root,
            std::to_string(nextGroupNumber++)
        );

        ++groupedComponents;
    }

    std::unordered_map<std::uint64_t, std::string> assignments;
    assignments.reserve(snapshot.size());

    std::string resolvedLocalGroupId;

    for (int i = 0; i < targetCount; ++i)
    {
        const int root = Find(i);
        std::string groupId;

        const auto groupIt = componentGroupIds.find(root);
        if (groupIt != componentGroupIds.end())
        {
            groupId = groupIt->second;
            ++groupedPlayers;
        }

        // Solo players
        assignments.emplace(snapshot[i].instance, groupId);

        if (i == localSnapshotIndex)
            resolvedLocalGroupId = groupId;
    }

    {
        std::lock_guard<std::mutex> lock(playerMutex);

        auto& cache = registeredPlayers.getCache();

        // If the player list changed between snapshot and commit, do not finalise a partial/incorrect one-time grouping pass
        std::unordered_map<std::uint64_t, Player*> currentPlayers;
        currentPlayers.reserve(cache.size());

        for (Player& player : cache)
        {
            if (Utils::valid_pointer(player.instance))
                currentPlayers.emplace(player.instance, &player);
        }

        for (const GroupSnapshot& entry : snapshot)
        {
            if (currentPlayers.find(entry.instance) == currentPlayers.end())
                return;
        }

        for (const auto& [instance, groupId] : assignments)
        {
            const auto playerIt = currentPlayers.find(instance);
            if (playerIt != currentPlayers.end())
                playerIt->second->groupId = groupId;
        }
    }

    // Empty means local player is solo.
    mainGame.localGroupId = resolvedLocalGroupId;

    // Scan is complete even if every player is solo
    groupIDSet = true;

    std::ostringstream ss;
    ss << "[PLAYERS][GROUP] One-time grouping complete"
        << " | cache=" << snapshot.size()
        << " | groups=" << groupedComponents
        << " | groupedPlayers=" << groupedPlayers
        << " | solos=" << (targetCount - groupedPlayers)
        << " | localGroupId="
        << (mainGame.localGroupId.empty()
            ? "none"
            : mainGame.localGroupId);

    LOGS.logInfo(ss.str());
}

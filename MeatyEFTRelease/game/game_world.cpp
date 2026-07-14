#include "headers/game_world.h"

#include "headers/maingame.h"
#include "headers/sdk.h"
#include "headers/unitysdk.h"
#include "headers/utils.h"
#include "../memory/Memory.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <format>
#include <vector>
#include <unordered_set>

extern Memory mem;

namespace {

std::uint64_t g_last_disposed_game_world{};

#pragma pack(push, 8)
struct LinkedListObject {
    std::uint64_t previous{};
    std::uint64_t next{};
    std::uint64_t this_object{};
};
#pragma pack(pop)

constexpr std::uint64_t kGomLastActiveNode = 0x20;
constexpr std::uint64_t kGomActiveNodes = 0x28;

bool mapKnown(const std::string& map)
{
    static const char* kMaps[] = {
        "default", "Labyrinth", "woods", "shoreline", "rezervbase", "laboratory", "interchange",
        "factory4_day", "factory4_night", "bigmap", "lighthouse", "tarkovstreets", "Sandbox",
        "Sandbox_high", "Sandbox_start", "Icebreaker", "laboratory_dark"
    };
    for (const char* m : kMaps) {
        if (_stricmp(map.c_str(), m) == 0)
            return true;
    }
    return false;
}

std::uint64_t readGomListPtr(std::uint64_t gom, std::uint64_t field_offset)
{
    const std::uint64_t list_ptr = mem.Read<std::uint64_t>(gom + field_offset);
    if (!Utils::valid_pointer(list_ptr))
        return 0;
    return list_ptr;
}

bool countRegisteredPlayers(std::uint64_t local_gw, int& out_count)
{
    out_count = 0;

    const std::uint64_t registered =
        mem.Read<std::uint64_t>(local_gw + sdk::ClientLocalGameWorld::RegisteredPlayers);
    if (!Utils::valid_pointer(registered))
        return false;

    const std::uint64_t list = mem.Read<std::uint64_t>(registered + 0x10);
    if (!Utils::valid_pointer(list))
        return false;

    const std::int32_t count = mem.Read<std::int32_t>(registered + 0x18);
    if (count <= 0)
        return false;

    out_count = count;
    return true;
}

bool fillRaidFromLocalGameWorld(std::uint64_t gom, std::uint64_t local_gw, std::uint64_t local_player,
                                std::uint64_t game_world_object, RaidState& raid, std::string& debug_out)
{
    raid = {};
    debug_out.clear();

    if (!Utils::valid_pointer(local_gw))
        return false;

    if (Utils::valid_pointer(g_last_disposed_game_world) && local_gw == g_last_disposed_game_world) {
        debug_out = std::format("stale gw=0x{:X}", local_gw);
        return false;
    }

    std::uint64_t map_ptr = mem.Read<std::uint64_t>(local_gw + sdk::GameWorld::Location);
    if (!Utils::valid_pointer(map_ptr) && Utils::valid_pointer(local_player))
        map_ptr = mem.Read<std::uint64_t>(local_player + sdk::Player::Location);

    std::string map;
    if (Utils::valid_pointer(map_ptr)) {
        const int len = mem.Read<int>(map_ptr + 0x10);
        if (len > 0 && len <= 64)
            map = mem.readUnicodeString(map_ptr + 0x14, static_cast<SIZE_T>(len));
    }

    int reg_count{};
    if (!countRegisteredPlayers(local_gw, reg_count)) {
        debug_out = map.empty() ? "lobby: players=0" : std::format("lobby: map={} players=0", map);
        return false;
    }

    if (isLobbyMapName(map)) {
        debug_out = std::format("lobby: map={} players={}", map, reg_count);
        return false;
    }

    if (!map.empty() && !mapKnown(map)) {
        debug_out = std::format("unknown map '{}' (players={})", map, reg_count);
        return false;
    }

    raid.in_raid = true;
    raid.game_object_manager = gom;
    raid.game_world_object = game_world_object;
    raid.local_game_world = local_gw;
    raid.local_player = local_player;
    raid.map_name = map.empty() ? "unknown" : map;
    raid.registered_count = reg_count;
    debug_out = std::format("raid ok map={} players={} gw=0x{:X}", raid.map_name, reg_count, local_gw);
    return true;
}

} // namespace

bool isLobbyMapName(const std::string& map)
{
    if (map.empty())
        return true;
    return _stricmp(map.c_str(), "hideout") == 0 || _stricmp(map.c_str(), "default") == 0;
}

bool tryPromotePendingRaid(std::uint64_t gom, std::uint64_t local_game_world, std::uint64_t game_world_object,
                           RaidState& raid, std::string& debug_out)
{
    if (!Utils::valid_pointer(local_game_world))
        return false;

    const std::uint64_t local_player =
        mem.Read<std::uint64_t>(local_game_world + sdk::ClientLocalGameWorld::MainPlayer);

    return fillRaidFromLocalGameWorld(gom, local_game_world, local_player, game_world_object, raid, debug_out);
}

bool tryResolveRaid(std::uint64_t gom, RaidState& raid, std::string& debug_out, RaidPendingState* pending_out)
{
    if (pending_out)
        *pending_out = {};

    raid = {};
    debug_out.clear();

    if (!Utils::valid_pointer(gom))
    {
        debug_out = "gom=0";
        return false;
    }

    const std::uint64_t active_list_ptr =
        readGomListPtr(gom, kGomActiveNodes);

    const std::uint64_t last_list_ptr =
        readGomListPtr(gom, kGomLastActiveNode);

    if (!Utils::valid_pointer(active_list_ptr) ||
        !Utils::valid_pointer(last_list_ptr))
    {
        debug_out = std::format(
            "gom=0x{:X} list ptr missing",
            gom
        );
        return false;
    }

    constexpr std::size_t kMaxGomNodes = 8192;

    std::vector<std::uint64_t> node_addrs;
    node_addrs.reserve(4096);

    std::unordered_set<std::uint64_t> visited_nodes;
    visited_nodes.reserve(4096);

    std::uint64_t curr = active_list_ptr;

    bool reached_sampled_tail = false;
    bool detected_cycle = false;
    bool hit_walk_cap = false;
    bool ended_on_invalid_next = false;

    while (Utils::valid_pointer(curr) &&
        node_addrs.size() < kMaxGomNodes)
    {
        
        if (!visited_nodes.emplace(curr).second)
        {
            detected_cycle = true;
            break;
        }

        node_addrs.push_back(curr);

        if (curr == last_list_ptr)
        {
            reached_sampled_tail = true;
            break;
        }

        curr = mem.Read<std::uint64_t>(
            curr + offsetof(LinkedListObject, next)
        );
    }

    if (!reached_sampled_tail)
    {
        if (!Utils::valid_pointer(curr))
            ended_on_invalid_next = true;
        else if (node_addrs.size() >= kMaxGomNodes)
            hit_walk_cap = true;
    }

    if (node_addrs.empty())
    {
        debug_out = std::format(
            "gom=0x{:X} nodes=0 (menu/loading?)",
            gom
        );
        return false;
    }

    std::string walk_status;

    if (!reached_sampled_tail)
    {
        std::ostringstream ss;

        ss << " | active-list partial"
            << " nodes=" << node_addrs.size();

        if (detected_cycle)
            ss << " cycle";

        if (hit_walk_cap)
            ss << " cap";

        if (ended_on_invalid_next)
            ss << " invalid-next";

        walk_status = ss.str();
    }

    const std::size_t count = node_addrs.size();
    std::vector<LinkedListObject> nodes(count);

    {
        auto scatter = mem.CreateScatterHandle();

        if (!scatter)
        {
            debug_out = "scatter open failed (nodes)";
            return false;
        }

        for (std::size_t i = 0; i < count; ++i)
        {
            mem.AddScatterReadRequest(
                scatter,
                node_addrs[i],
                &nodes[i],
                sizeof(LinkedListObject)
            );
        }

        if (!mem.ExecuteReadScatter(scatter))
        {
            mem.CloseScatterHandle(scatter);
            debug_out = "scatter read failed (nodes)";
            return false;
        }

        mem.CloseScatterHandle(scatter);
    }

    std::vector<std::uint64_t> name_ptrs(count, 0);

    {
        auto scatter = mem.CreateScatterHandle();

        if (!scatter)
        {
            debug_out = "scatter open failed (names)";
            return false;
        }

        bool has_name_requests = false;

        for (std::size_t i = 0; i < count; ++i)
        {
            if (!Utils::valid_pointer(nodes[i].this_object))
                continue;

            has_name_requests = true;

            mem.AddScatterReadRequest(
                scatter,
                nodes[i].this_object + UnityOffsets::GameObject_NameOffset,
                &name_ptrs[i],
                sizeof(std::uint64_t)
            );
        }

        if (has_name_requests && !mem.ExecuteReadScatter(scatter))
        {
            mem.CloseScatterHandle(scatter);
            debug_out = "scatter read failed (names)";
            return false;
        }

        mem.CloseScatterHandle(scatter);
    }

    bool saw_game_world = false;

    std::string last_reject;

    std::uint64_t pending_gw = 0;
    std::uint64_t pending_gw_object = 0;
    std::string pending_map;

    for (std::size_t i = 0; i < count; ++i)
    {
        if (!Utils::valid_pointer(nodes[i].this_object))
            continue;

        if (!Utils::valid_pointer(name_ptrs[i]))
            continue;

        const std::string name =
            mem.readUTF8String(name_ptrs[i], 64);

        if (_stricmp(name.c_str(), "GameWorld") != 0)
            continue;

        saw_game_world = true;

        const std::uint64_t local_gw = mem.ReadChain(
            nodes[i].this_object,
            {
                UnityOffsets::GameObject_ComponentsOffset,
                0x18,
                UnityOffsets::Component_ObjectClassOffset
            }
        );

        if (!Utils::valid_pointer(local_gw))
        {
            last_reject = "GameWorld component chain failed";
            continue;
        }

        const std::uint64_t local_player = mem.Read<std::uint64_t>(
            local_gw + sdk::ClientLocalGameWorld::MainPlayer
        );

        RaidState attempt{};
        std::string reject;

        if (fillRaidFromLocalGameWorld(
            gom,
            local_gw,
            local_player,
            nodes[i].this_object,
            attempt,
            reject))
        {
            raid = attempt;
            debug_out = reject;
            return true;
        }

        last_reject = reject;

        pending_gw = local_gw;
        pending_gw_object = nodes[i].this_object;

        std::uint64_t map_ptr = mem.Read<std::uint64_t>(
            local_gw + sdk::GameWorld::Location
        );

        if (!Utils::valid_pointer(map_ptr) &&
            Utils::valid_pointer(local_player))
        {
            map_ptr = mem.Read<std::uint64_t>(
                local_player + sdk::Player::Location
            );
        }

        if (Utils::valid_pointer(map_ptr))
        {
            const int len = mem.Read<int>(map_ptr + 0x10);

            if (len > 0 && len <= 64)
            {
                pending_map = mem.readUnicodeString(
                    map_ptr + 0x14,
                    static_cast<SIZE_T>(len)
                );
            }
        }
    }

    if (pending_out && Utils::valid_pointer(pending_gw))
    {
        pending_out->active = true;
        pending_out->game_world_object = pending_gw_object;
        pending_out->local_game_world = pending_gw;
        pending_out->map_name = pending_map;
    }

    if (!saw_game_world)
    {
        debug_out = std::format(
            "gom=0x{:X} nodes={} no GameWorld (menu/hideout/loading?){}",
            gom,
            count,
            walk_status
        );
    }
    else if (!last_reject.empty())
    {
        debug_out = std::format(
            "gom=0x{:X} GameWorld pending: {}{}",
            gom,
            last_reject,
            walk_status
        );
    }
    else
    {
        debug_out = std::format(
            "gom=0x{:X} nodes={} GameWorld reject{}",
            gom,
            count,
            walk_status
        );
    }

    return false;
}

bool readRegisteredPlayerPtrs(const RaidState& raid, std::vector<std::uint64_t>& out_ptrs)
{
    out_ptrs.clear();
    if (!raid.in_raid || !Utils::valid_pointer(raid.local_game_world))
        return false;

    const std::uint64_t registered =
        mem.Read<std::uint64_t>(raid.local_game_world + sdk::ClientLocalGameWorld::RegisteredPlayers);
    if (!Utils::valid_pointer(registered))
        return false;

    const std::uint64_t list = mem.Read<std::uint64_t>(registered + 0x10);
    if (!Utils::valid_pointer(list))
        return false;

    std::int32_t count = mem.Read<std::int32_t>(registered + 0x18);
    if (count <= 0)
        return false;

    constexpr int kMax = 1024;
    if (count > kMax)
        count = kMax;

    out_ptrs.resize(static_cast<std::size_t>(count), 0);
    if (!mem.Read(list + 0x20, out_ptrs.data(), sizeof(std::uint64_t) * out_ptrs.size()))
        return false;

    std::erase_if(out_ptrs, [](std::uint64_t p) { return !Utils::valid_pointer(p); });
    return !out_ptrs.empty();
}

void recordDisposedGameWorld(std::uint64_t local_game_world)
{
    if (Utils::valid_pointer(local_game_world))
        g_last_disposed_game_world = local_game_world;
}

bool isStaleGameWorld(std::uint64_t local_game_world)
{
    return Utils::valid_pointer(g_last_disposed_game_world) &&
           local_game_world == g_last_disposed_game_world;
}

void applyRaidStateToMainGame(const RaidState& raid)
{
    mainGame.gameObjectManager = raid.game_object_manager;
    mainGame.gameWorld = raid.game_world_object;
    mainGame.localGameWorld = raid.local_game_world;
    mainGame.localPlayerPtr = raid.local_player;
    mainGame.selectedLocation = raid.map_name;

    const std::uint64_t map_ptr = mem.Read<std::uint64_t>(raid.local_game_world + sdk::GameWorld::Location);
    mainGame.onlineRaid = Utils::valid_pointer(map_ptr);

    const std::uint64_t registered =
        mem.Read<std::uint64_t>(raid.local_game_world + sdk::ClientLocalGameWorld::RegisteredPlayers);
    mainGame.registeredPlayers = registered;
    mainGame.registeredPlayersList = mem.Read<std::uint64_t>(registered + 0x10);
    mainGame.registeredPlayersCount = mem.Read<int>(registered + 0x18);

    std::vector<std::uint64_t> ptrs;
    if (readRegisteredPlayerPtrs(raid, ptrs)) {
        const size_t n = (std::min)(ptrs.size(), std::size(mainGame.player_buffer));
        std::fill(std::begin(mainGame.player_buffer), std::end(mainGame.player_buffer), 0);
        for (size_t i = 0; i < n; ++i)
            mainGame.player_buffer[i] = ptrs[i];
        mainGame.registeredPlayersCount = static_cast<int>(n);
    }
}

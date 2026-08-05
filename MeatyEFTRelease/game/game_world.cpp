#include "headers/game_world.h"

#include "headers/maingame.h"
#include "headers/sdk.h"
#include "headers/unitysdk.h"
#include "headers/utils.h"
#include "../app/debug.h"
#include "../memory/Memory.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <format>
#include <iostream>
#include <sstream>
#include <unordered_set>
#include <vector>

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

bool plausibleGameObjectName(const std::string& name)
{
    if (name.empty() || name.size() >= 64)
        return false;

    bool has_alphanumeric = false;

    for (const unsigned char character : name)
    {
        if (character < 0x20 || character > 0x7E)
            return false;

        if (std::isalnum(character) != 0)
            has_alphanumeric = true;
    }

    return has_alphanumeric;
}

void saveGameWorldObjectDump(const std::string& contents)
{
    std::error_code file_error;
    const std::filesystem::path logs_directory = "logs";

    std::filesystem::create_directories(logs_directory, file_error);

    if (file_error)
    {
        LOGS.logWarn(
            "[GameWorld] Failed to create object dump directory: ",
            file_error.message()
        );
        return;
    }

    const std::filesystem::path dump_path =
        logs_directory / "game_world_objects.txt";

    std::ofstream output(dump_path, std::ios::out | std::ios::trunc);

    if (!output.is_open())
    {
        LOGS.logWarn(
            "[GameWorld] Failed to open object dump: ",
            dump_path.string()
        );
        return;
    }

    output << contents;
}

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
        debug_out = "previously disposed GameWorld";
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
        debug_out = map.empty()
            ? "registered player list is not ready"
            : std::format("map={} registered player list is not ready", map);
        return false;
    }

    if (isLobbyMapName(map)) {
        debug_out = std::format(
            "lobby map={} registeredPlayers={}",
            map,
            reg_count
        );
        return false;
    }

    if (!map.empty() && !mapKnown(map)) {
        debug_out = std::format(
            "unknown map='{}' registeredPlayers={}",
            map,
            reg_count
        );
        return false;
    }

    raid.in_raid = true;
    raid.game_object_manager = gom;
    raid.game_world_object = game_world_object;
    raid.local_game_world = local_gw;
    raid.local_player = local_player;
    raid.map_name = map.empty() ? "unknown" : map;
    raid.registered_count = reg_count;
    debug_out = std::format(
        "GameWorld ready: map={} registeredPlayers={}",
        raid.map_name,
        reg_count
    );
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
    {
        debug_out = "Pending GameWorld pointer is invalid";
        return false;
    }

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

    std::ostringstream object_dump;
    object_dump << "Game World object scan\n";

    if (!Utils::valid_pointer(gom))
    {
        debug_out =
            "Game World scan skipped: GameObjectManager pointer is invalid";

        object_dump << "Status: " << debug_out << '\n';
        saveGameWorldObjectDump(object_dump.str());
        return false;
    }

    const std::uint64_t active_list_ptr =
        readGomListPtr(gom, kGomActiveNodes);

    const std::uint64_t last_list_ptr =
        readGomListPtr(gom, kGomLastActiveNode);

    if (!Utils::valid_pointer(active_list_ptr) ||
        !Utils::valid_pointer(last_list_ptr))
    {
        debug_out =
            "Game World scan failed: active-list pointers are unavailable";

        object_dump
            << "Active list node: 0x" << std::hex << active_list_ptr << '\n'
            << "Sampled tail node: 0x" << last_list_ptr << std::dec << '\n'
            << "Status: " << debug_out << '\n';

        saveGameWorldObjectDump(object_dump.str());
        return false;
    }

    object_dump
        << "Scan order: active node to sampled tail\n"
        << "Active list node: 0x" << std::hex << active_list_ptr << '\n'
        << "Sampled tail node: 0x" << last_list_ptr << std::dec << '\n';

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

    object_dump
        << "Objects discovered: " << node_addrs.size() << '\n'
        << "Reached sampled tail: "
        << (reached_sampled_tail ? "yes" : "no") << '\n'
        << "Cycle detected: " << (detected_cycle ? "yes" : "no") << '\n'
        << "Walk cap reached: " << (hit_walk_cap ? "yes" : "no") << '\n'
        << "Ended on invalid next pointer: "
        << (ended_on_invalid_next ? "yes" : "no") << '\n';

    if (node_addrs.empty())
    {
        debug_out =
            "Game World scan complete: no active objects (menu/loading?)";

        object_dump << "Status: " << debug_out << '\n';
        saveGameWorldObjectDump(object_dump.str());
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
            debug_out =
                "Game World scan failed: could not open the object read batch";

            object_dump << "Status: " << debug_out << '\n';
            saveGameWorldObjectDump(object_dump.str());
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
            debug_out =
                "Game World scan failed: object list read failed";

            object_dump << "Status: " << debug_out << '\n';
            saveGameWorldObjectDump(object_dump.str());
            return false;
        }

        mem.CloseScatterHandle(scatter);
    }

    std::vector<std::uint64_t> name_ptrs(count, 0);
    std::vector<std::string> object_names(count);

    std::string name_read_error;

    const auto readNamePointers =
        [&](std::uint64_t name_offset)
        {
            std::fill(name_ptrs.begin(), name_ptrs.end(), 0);
            name_read_error.clear();

            auto scatter = mem.CreateScatterHandle();

            if (!scatter)
            {
                name_read_error =
                    "could not open the name read batch";
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
                    nodes[i].this_object + name_offset,
                    &name_ptrs[i],
                    sizeof(std::uint64_t)
                );
            }

            if (has_name_requests && !mem.ExecuteReadScatter(scatter))
            {
                mem.CloseScatterHandle(scatter);
                name_read_error = "object name pointer read failed";
                return false;
            }

            mem.CloseScatterHandle(scatter);
            return true;
        };

    const auto readObjectNames = [&]()
        {
            std::fill(
                object_names.begin(),
                object_names.end(),
                std::string{}
            );

            std::size_t readable_name_count = 0;

            for (std::size_t i = 0; i < count; ++i)
            {
                if (!Utils::valid_pointer(name_ptrs[i]))
                    continue;

                object_names[i] =
                    mem.readUTF8String(name_ptrs[i], 64);

                if (plausibleGameObjectName(object_names[i]))
                    ++readable_name_count;
            }

            return readable_name_count;
        };

    constexpr std::uint64_t name_offset = UnityOffsets::GameObject_NameOffset;

    if (!readNamePointers(name_offset))
    {
        debug_out = "Game World scan failed: " + name_read_error;

        object_dump << "Status: " << debug_out << '\n';
        saveGameWorldObjectDump(object_dump.str());
        return false;
    }

    const std::size_t readable_name_count = readObjectNames();

    object_dump
        << "GameObject name offset used: 0x"
        << std::hex << name_offset << std::dec << '\n'
        << "Readable object names: " << readable_name_count << '\n';

    bool saw_game_world = false;
    bool resolved_raid = false;

    std::size_t game_world_matches = 0;
    std::size_t selected_index = 0;

    RaidState selected_raid{};
    std::string selected_result;
    std::string last_reject;

    std::uint64_t pending_gw = 0;
    std::uint64_t pending_gw_object = 0;
    std::string pending_map;

    object_dump << "\nEntries:\n";

    for (std::size_t i = 0; i < count; ++i)
    {
        object_dump
            << '[' << std::dec << i << ']'
            << " | Node: 0x" << std::hex << node_addrs[i]
            << " | Previous: 0x" << nodes[i].previous
            << " | Next: 0x" << nodes[i].next
            << " | Object: 0x" << nodes[i].this_object;

        if (!Utils::valid_pointer(nodes[i].this_object))
        {
            object_dump
                << std::dec
                << " | Status: invalid object pointer\n";
            continue;
        }

        object_dump << " | NamePtr: 0x" << name_ptrs[i] << std::dec;

        if (!Utils::valid_pointer(name_ptrs[i]))
        {
            object_dump << " | Status: invalid name pointer\n";
            continue;
        }

        const std::string& name = object_names[i];

        if (name.empty())
        {
            object_dump
                << " | Name: <empty>"
                << " | Status: empty object name\n";
            continue;
        }

        const bool is_game_world =
            _stricmp(name.c_str(), "GameWorld") == 0;

        object_dump
            << " | Name: \"" << name << '"'
            << " | GameWorld match: "
            << (is_game_world ? "yes" : "no");

        if (!is_game_world)
        {
            object_dump << " | Status: ok\n";
            continue;
        }

        saw_game_world = true;
        ++game_world_matches;

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

            object_dump
                << " | LocalGameWorld: 0x" << std::hex << local_gw
                << std::dec
                << " | Status: " << last_reject << '\n';
            continue;
        }

        const std::uint64_t local_player = mem.Read<std::uint64_t>(
            local_gw + sdk::ClientLocalGameWorld::MainPlayer
        );

        RaidState attempt{};
        std::string candidate_result;

        const bool raid_ready = fillRaidFromLocalGameWorld(
            gom,
            local_gw,
            local_player,
            nodes[i].this_object,
            attempt,
            candidate_result
        );

        const bool selected = raid_ready && !resolved_raid;

        object_dump
            << " | LocalGameWorld: 0x" << std::hex << local_gw
            << " | LocalPlayer: 0x" << local_player << std::dec
            << " | Raid ready: " << (raid_ready ? "yes" : "no")
            << " | Selected: " << (selected ? "yes" : "no")
            << " | Status: " << candidate_result << '\n';

        if (raid_ready)
        {
            if (selected)
            {
                resolved_raid = true;
                selected_index = i;
                selected_raid = attempt;
                selected_result = candidate_result;
            }

            continue;
        }

        last_reject = candidate_result;

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

    object_dump
        << "\nGameWorld matches: " << game_world_matches << '\n';

    if (resolved_raid)
    {
        raid = selected_raid;
        debug_out = std::format(
            "{}; scan complete: {} objects checked{}",
            selected_result,
            count,
            walk_status
        );

        object_dump
            << "Selected index: " << selected_index << '\n'
            << "Selected object: 0x" << std::hex
            << selected_raid.game_world_object << '\n'
            << "Selected LocalGameWorld: 0x"
            << selected_raid.local_game_world << std::dec << '\n'
            << "Selected map: " << selected_raid.map_name << '\n'
            << "Registered players: "
            << selected_raid.registered_count << '\n'
            << "Result: " << debug_out << '\n';

        saveGameWorldObjectDump(object_dump.str());
        return true;
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
            "Game World scan complete: {} objects checked; "
            "no GameWorld found (menu/hideout/loading?){}",
            count,
            walk_status
        );
    }
    else if (!last_reject.empty())
    {
        debug_out = std::format(
            "Game World scan complete: {} objects checked; "
            "GameWorld found but raid data is pending: {}{}",
            count,
            last_reject,
            walk_status
        );
    }
    else
    {
        debug_out = std::format(
            "Game World scan complete: {} objects checked; "
            "GameWorld candidates were rejected{}",
            count,
            walk_status
        );
    }

    object_dump << "Result: " << debug_out << '\n';
    saveGameWorldObjectDump(object_dump.str());
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

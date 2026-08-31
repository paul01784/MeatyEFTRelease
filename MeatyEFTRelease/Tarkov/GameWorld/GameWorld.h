#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct RaidState {
    bool in_raid{};
    std::uint64_t game_object_manager{};
    std::uint64_t game_world_object{};
    std::uint64_t local_game_world{};
    std::uint64_t local_player{};
    std::string map_name;
    int registered_count{};
};

struct RaidPendingState {
    bool active{};
    std::uint64_t game_world_object{};
    std::uint64_t local_game_world{};
    std::string map_name;
};

bool isLobbyMapName(const std::string& map);

bool tryResolveRaid(std::uint64_t gom, RaidState& raid, std::string& debug_out,
                    RaidPendingState* pending_out = nullptr);
bool tryPromotePendingRaid(std::uint64_t gom, std::uint64_t local_game_world, std::uint64_t game_world_object,
                           RaidState& raid, std::string& debug_out);
bool readRegisteredPlayerPtrs(const RaidState& raid, std::vector<std::uint64_t>& out_ptrs);

void recordDisposedGameWorld(std::uint64_t local_game_world);
bool isStaleGameWorld(std::uint64_t local_game_world);

void applyRaidStateToMainGame(const RaidState& raid);

#pragma once

#include "Player.h"

#include <algorithm>
#include <cstdint>

namespace PlayerLookup
{
    inline Player* findByInstance(PlayerCollection& players, std::uint64_t instance)
    {
        const auto player = std::find_if(
            players.begin(),
            players.end(),
            [instance](const Player& candidate)
            {
                return candidate.instance == instance;
            });

        return player == players.end() ? nullptr : &(*player);
    }
}

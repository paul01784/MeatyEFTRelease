#pragma once
#include "BodySilhouette.h"
#include "render.h"
#include "../Tarkov/GameWorld/Player/Player.h"

namespace BodySilhouette
{
    static_assert(static_cast<int>(BodySilhouette::Head)==static_cast<int>(boneListIndexes::Head));
    static_assert(static_cast<int>(BodySilhouette::Base)==static_cast<int>(boneListIndexes::Base));
    static_assert(static_cast<int>(BodySilhouette::RKnee)==static_cast<int>(boneListIndexes::RKnee));
    static_assert(static_cast<int>(BodySilhouette::RToe)==static_cast<int>(boneListIndexes::RToe));
    constexpr Style styleForType(PlayerType type)
    {
        return type==PlayerType::AIScav || type==PlayerType::PScav
            ?Style::Plain:Style::Soldier;
    }
    static_assert(styleForType(PlayerType::AIScav)==Style::Plain);
    static_assert(styleForType(PlayerType::PScav)==Style::Plain);
    static_assert(styleForType(PlayerType::PMC)==Style::Soldier);
    inline Headwear headwearForPlayer(const Player& player, bool equipmentEnabled=true)
    {
        if(!equipmentEnabled) return Headwear::Unknown;
        for(const auto& slot:player._slots)
        {
            std::string_view name=slot.name;
            const auto padding=[](char c){return c=='\0'||std::isspace(static_cast<unsigned char>(c))!=0;};
            while(!name.empty()&&padding(name.front())) name.remove_prefix(1);
            while(!name.empty()&&padding(name.back())) name.remove_suffix(1);
            if(name!="Headwear") continue;
            return classifyHeadwear(slot.equipName);
        }
        return Headwear::Unknown;
    }
    inline Style styleForPlayer(const Player& player)
    {
        // Preserve the player-Scav identity if presentation changes its type,
        // e.g. a teammate/custom player marker.
        return player.isPlayerScav?Style::Plain:styleForType(player.type);
    }
}

#include "PlayerAppearance.h"

#include "Player.h"

#include "../../../UI/globals.h"
#include "../../../UI/Appearance.h"
#include "../MainGame.h"
#include "../../../Core/Utilities.h"

namespace PlayerAppearance
{
    PlayerMarkerType resolveMarkerType(const Player& player)
    {
        if (player.isBTR)
            return PlayerMarkerType::Btr;

        if (player.isLocal &&
            Utils::valid_pointer(player.instance) &&
            mainGame.localPlayerPtr == player.instance)
            return PlayerMarkerType::Local;

        if (player.isFriend ||
            (!mainGame.localGroupId.empty() && player.groupId == mainGame.localGroupId))
            return PlayerMarkerType::Friendly;

        if (player.isWatched)
            return PlayerMarkerType::Watched;

        if (player.isBlackDivision)
            return PlayerMarkerType::BlackDivision;

        if (player.isBoss)
            return PlayerMarkerType::Boss;

        if (player.isPlayerScav && !player.isAi && player.isPlayer)
            return PlayerMarkerType::PlayerScav;

        if (player.isPlayer && !player.isPlayerScav && !player.isAi)
            return PlayerMarkerType::Pmc;

        return PlayerMarkerType::Ai;
    }

    glm::vec4 resolveColour(const Player& player)
    {
        glm::vec4 colour = {1, 1, 1, 1};

        if (player.isDead)
            return coloursGlobals::playerCorpse;

        if (player.isBTR)
            return coloursGlobals::aiBTR;

        if (player.isAi && !player.isPlayerScav && !player.isPlayer)
            colour = coloursGlobals::playerAI;

        if (player.isPlayerScav && !player.isAi && player.isPlayer)
            colour = coloursGlobals::playerScav;

        if (player.isBoss)
            colour = coloursGlobals::playerBoss;

        if (player.isBlackDivision)
            colour = coloursGlobals::playerBlackDiv;

        if (player.isPlayer && !player.isPlayerScav && !player.isAi)
            colour = coloursGlobals::playerPMC;

        if (player.isWatched)
            colour = coloursGlobals::playerWatched;

        if (player.isFriend)
            colour = coloursGlobals::playerFriendly;

        if (!mainGame.localGroupId.empty() &&
            player.groupId == mainGame.localGroupId)
            colour = coloursGlobals::playerFriendly;

        if (player.isLocal &&
            Utils::valid_pointer(player.instance) &&
            mainGame.localPlayerPtr == player.instance)
            colour = coloursGlobals::playerLocal;

        return colour;
    }

    void updateColour(Player& player)
    {
        player.colour = resolveColour(player);
    }
}

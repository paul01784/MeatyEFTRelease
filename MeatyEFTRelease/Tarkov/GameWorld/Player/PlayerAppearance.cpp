#include "PlayerAppearance.h"

#include "Player.h"

#include "../../../UI/globals.h"
#include "../MainGame.h"
#include "../../../Core/Utilities.h"

namespace PlayerAppearance
{
    void updateColour(Player& player)
    {
        player.colour = { 1, 1, 1, 1 };

        if (player.isDead)
        {
            player.colour = coloursGlobals::playerCorpse;
            return;
        }

        if (player.isAi && !player.isPlayerScav && !player.isPlayer)
            player.colour = coloursGlobals::playerAI;

        if (player.isPlayerScav && !player.isAi && player.isPlayer)
            player.colour = coloursGlobals::playerScav;

        if (player.isBoss)
            player.colour = coloursGlobals::playerBoss;

        if (player.isBlackDivision)
            player.colour = coloursGlobals::playerBlackDiv;

        if (player.isPlayer && !player.isPlayerScav && !player.isAi)
            player.colour = coloursGlobals::playerPMC;

        if (player.isWatched)
            player.colour = coloursGlobals::playerWatched;

        if (player.isFriend)
            player.colour = coloursGlobals::playerFriendly;

        if (!mainGame.localGroupId.empty() &&
            player.groupId == mainGame.localGroupId)
        {
            player.colour = coloursGlobals::playerFriendly;
        }

        if (player.isLocal &&
            Utils::valid_pointer(player.instance) &&
            mainGame.localPlayerPtr == player.instance)
        {
            player.colour = coloursGlobals::playerLocal;
        }
    }
}

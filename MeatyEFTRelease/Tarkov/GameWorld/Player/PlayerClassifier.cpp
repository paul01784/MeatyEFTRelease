#include "PlayerClassifier.h"

#include "BtrOperator.h"
#include "ClientPlayer.h"
#include "LocalPlayer.h"
#include "ObservedPlayer.h"

namespace
{
    const BtrOperator btrOperator;
    const ClientPlayer clientPlayer;
    const ObservedPlayer observedPlayer;
    const LocalPlayer localPlayer;
}

const PlayerRuntimeModel& PlayerClassifier::classify(std::string_view className, bool isLocal) noexcept
{
    if (localPlayer.matches(className, isLocal))
        return localPlayer;

    if (clientPlayer.matches(className, isLocal))
        return clientPlayer;

    return observedPlayer;
}

const PlayerRuntimeModel& PlayerClassifier::get(PlayerKind kind) noexcept
{
    switch (kind)
    {
    case PlayerKind::Client: return clientPlayer;
    case PlayerKind::Local: return localPlayer;
    case PlayerKind::Btr: return btrOperator;
    case PlayerKind::Observed:
    case PlayerKind::Unknown:
    default: return observedPlayer;
    }
}

const PlayerRuntimeModel& PlayerClassifier::get(const Player& player) noexcept
{
    return get(player.getKind());
}

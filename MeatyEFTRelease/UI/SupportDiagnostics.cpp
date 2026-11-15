#include "SupportDiagnostics.h"

#include "DxRenderWindow.h"
#include "debug.h"
#include "globals.h"
#include "perfMonitor.h"

#include "../Core/InputDevice.h"
#include "../Core/Utilities.h"
#include "../Core/Makcu/Makcu.h"
#include "../Core/Ferrum/Ferrum.h"
#include "../Tarkov/Features/Aim/FireportTracker.h"
#include "../Tarkov/Features/Aim/ReadOnlyAim.h"
#include "../Tarkov/GameWorld/Explosives/ExplosiveManager.h"
#include "../Tarkov/GameWorld/Loot/Loot.h"
#include "../Tarkov/GameWorld/MainGame.h"
#include "../Tarkov/GameWorld/QuestManager.h"
#include "../Tarkov/GameWorld/RegisteredPlayers.h"
#include "../Tarkov/Unity/cameraManager.h"
#include "../external/nlohmann/json.hpp"
#include "../memory/Memory.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace
{
    using json = nlohmann::json;

    std::string BoolText(bool value)
    {
        return value ? "yes" : "no";
    }

    std::string PointerText(std::uint64_t value)
    {
        std::ostringstream stream;
        stream << "0x" << std::uppercase << std::hex << std::setw(16) << std::setfill('0') << value;
        return stream.str();
    }

    std::string Timestamp(bool forFilename)
    {
        const std::time_t now = std::time(nullptr);
        std::tm local{};
        localtime_s(&local, &now);

        std::ostringstream stream;
        stream << std::put_time(&local, forFilename ? "%Y%m%d_%H%M%S" : "%Y-%m-%d %H:%M:%S");
        return stream.str();
    }

    const char* HealthLevelText(SupportHealthLevel level)
    {
        switch (level)
        {
        case SupportHealthLevel::Good:
            return "good";
        case SupportHealthLevel::Warning:
            return "warning";
        case SupportHealthLevel::Error:
            return "error";
        case SupportHealthLevel::Inactive:
        default:
            return "inactive";
        }
    }

    const char* DmaStateText(DmaConnectionState state)
    {
        switch (state)
        {
        case DmaConnectionState::Disconnected:
            return "disconnected";
        case DmaConnectionState::Connecting:
            return "connecting";
        case DmaConnectionState::WaitingForProcess:
            return "waiting_for_process";
        case DmaConnectionState::Connected:
            return "connected";
        case DmaConnectionState::Failed:
            return "failed";
        default:
            return "unknown";
        }
    }

    const char* MessageLevelText(MessageLevel level)
    {
        switch (level)
        {
        case MessageLevel::INFO:
            return "info";
        case MessageLevel::NOTICE:
            return "notice";
        case MessageLevel::WARN:
            return "warning";
        case MessageLevel::ERR:
            return "error";
        default:
            return "unknown";
        }
    }

    std::string RedactSensitive(std::string text)
    {
        const std::string& apiKey = globals::dogTagAPIKey;
        if (!apiKey.empty())
        {
            std::size_t position = 0;
            while ((position = text.find(apiKey, position)) != std::string::npos)
            {
                text.replace(position, apiKey.size(), "[REDACTED_API_KEY]");
                position += 18;
            }
        }

        constexpr std::string_view bearerPrefix = "Authorization: Bearer ";
        std::size_t bearer = text.find(bearerPrefix);
        while (bearer != std::string::npos)
        {
            const std::size_t valueStart = bearer + bearerPrefix.size();
            const std::size_t valueEnd = text.find_first_of(" \t\r\n,;)", valueStart);
            text.replace(valueStart, valueEnd == std::string::npos ? std::string::npos : valueEnd - valueStart, "[REDACTED]");
            bearer = text.find(bearerPrefix, valueStart + 10);
        }

        return text;
    }

    const char* PlayerKindText(const Player& player)
    {
        if (player.isBTR)
            return "btr";
        if (player.isLocal)
            return "local";
        if (player.isBoss)
            return "boss";
        if (player.isBlackDivision)
            return "black_division";
        if (player.isCultist)
            return "cultist";
        if (player.isPlayerScav)
            return "player_scav";
        if (player.isPlayer)
            return "pmc";
        if (player.isAi)
            return "ai";
        return "unknown";
    }

    json Vec2Json(const glm::vec2& value)
    {
        return { { "x", value.x }, { "y", value.y } };
    }

    json Vec3Json(const glm::vec3& value)
    {
        return { { "x", value.x }, { "y", value.y }, { "z", value.z } };
    }

    json Vec4Json(const glm::vec4& value)
    {
        return { { "x", value.x }, { "y", value.y }, { "z", value.z }, { "w", value.w } };
    }

    double AgeMilliseconds(std::chrono::steady_clock::time_point value)
    {
        if (value == std::chrono::steady_clock::time_point{})
            return -1.0;

        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - value).count();
    }

    double TimeUntilMilliseconds(std::chrono::steady_clock::time_point value)
    {
        if (value == std::chrono::steady_clock::time_point{})
            return -1.0;

        return std::chrono::duration<double, std::milli>(value - std::chrono::steady_clock::now()).count();
    }

    double SystemAgeMilliseconds(std::chrono::system_clock::time_point value)
    {
        if (value == std::chrono::system_clock::time_point{})
            return -1.0;

        return std::chrono::duration<double, std::milli>(std::chrono::system_clock::now() - value).count();
    }

    json PlayerJson(const Player& player, bool includeIdentity, bool currentlyRegistered)
    {
        json equipment = json::array();
        for (const slots& slot : player._slots)
        {
            equipment.push_back({
                { "slot_name", slot.name },
                { "address", PointerText(slot.addr) },
                { "equipment_name", slot.equipName },
                { "price", slot.price },
                { "wanted", slot.wanted }
            });
        }

        json bones = json::array();
        const std::size_t boneCount = std::max(player.bonePtrs.size(), player.bonePositions.size());
        for (std::size_t index = 0; index < boneCount; ++index)
        {
            bones.push_back({
                { "index", index },
                { "bone_id", index < Player::boneList.size() ? static_cast<int>(Player::boneList[index]) : -1 },
                { "pointer", PointerText(index < player.bonePtrs.size() ? player.bonePtrs[index] : 0) },
                { "position", Vec3Json(index < player.bonePositions.size() ? player.bonePositions[index] : glm::vec3{}) }
            });
        }

        json transformCache = json::array();
        for (std::size_t index = 0; index < player.boneTransformCache.size(); ++index)
        {
            const BoneTransformCacheEntry& entry = player.boneTransformCache[index];
            transformCache.push_back({
                { "index", index },
                { "bone_transform", PointerText(entry.boneTransform) },
                { "transform_data", PointerText(entry.transformData) },
                { "transform_array", PointerText(entry.transformArray) },
                { "transform_indices", PointerText(entry.transformIndices) },
                { "transform_index", entry.transformIndex },
                { "next_validation_in_ms", TimeUntilMilliseconds(entry.nextValidation) },
                { "valid", entry.valid }
            });
        }

        const HandsInfo& hands = player.observedHandsInfo;
        json otherCounters = json::array();
        for (const auto& [keys, value] : player.profileStats.otherCounters)
            otherCounters.push_back({ { "keys", keys }, { "value", value } });

        json result = {
            { "kind", PlayerKindText(player) },
            { "player_kind_value", static_cast<int>(player.kind) },
            { "player_type_value", static_cast<int>(player.type) },
            { "class_name", player.className },
            { "instance", PointerText(player.instance) },
            { "cache_entry_id", player.cacheEntryId },
            { "currently_registered", currentlyRegistered },
            { "side", player.side },
            { "player_side_value", static_cast<int>(player.playerSide) },
            { "voice", player.voice },
            { "position", Vec3Json(player.location) },
            { "velocity", Vec3Json(player.velocity) },
            { "velocity_valid", player.velocityValid },
            { "last_velocity_update_age_ms", AgeMilliseconds(player.lastVelocityUpdate) },
            { "rotation_raw", Vec3Json(player.rotationRAW) },
            { "rotation", Vec2Json(player.rotation) },
            { "distance", player.distance },
            { "health_etag", player.healthETAG },
            { "colour", Vec4Json(player.colour) },
            { "classification", {
                { "local", player.isLocal },
                { "computer_controlled", player.isAi },
                { "zombie", player.isZombie },
                { "player", player.isPlayer },
                { "player_scav", player.isPlayerScav },
                { "boss", player.isBoss },
                { "black_division", player.isBlackDivision },
                { "cultist", player.isCultist },
                { "watched", player.isWatched },
                { "friend", player.isFriend },
                { "friend_group_override", player.friendGroupOverride },
                { "btr", player.isBTR }
            } },
            { "lifecycle", {
                { "dead", player.isDead },
                { "exfiled", player.hasExfiled },
                { "consecutive_roster_misses", player.consecutiveRosterMisses },
                { "roster_missing_age_ms", AgeMilliseconds(player.rosterMissingSince) },
                { "dog_tag_cache_lookup_attempted", player.dogTagCacheLookupAttempted },
                { "dog_tag_cache_found", player.foundDogTagCache },
                { "dogtag_api_lookup_attempted", player.dogTagApiLookupAttempted }
            } },
            { "btr_state", {
                { "controller", PointerText(player.btrController) },
                { "view", PointerText(player.btrView) },
                { "turret", PointerText(player.btrTurret) },
                { "in_btr", player.isInBTR },
                { "passenger_vehicle", PointerText(player.btrPassengerVehicle) },
                { "previous_position", Vec3Json(player.btrPreviousPosition) },
                { "position_sampled", player.btrPositionSampled },
                { "heading_valid", player.btrHeadingValid },
                { "exit_bone_refresh_pending", player.btrExitBoneRefreshPending }
            } },
            { "aim_state", {
                { "aiming", player.isAiming },
                { "line_target_confirmed", player.aimLineTargetConfirmed },
                { "line_target_is_local", player.aimLineTargetIsLocal },
                { "line_target_location", Vec3Json(player.aimLineTargetLocation) },
                { "line_target_age_ms", AgeMilliseconds(player.aimLineTargetSince) },
                { "visible_to_local", player.visibleToLocal }
            } },
            { "scheduled_reads", {
                { "corpse_in_ms", TimeUntilMilliseconds(player.nextCorpseRead) },
                { "health_in_ms", TimeUntilMilliseconds(player.nextHealthRead) },
                { "hands_controller_in_ms", TimeUntilMilliseconds(player.nextHandsControllerRead) },
                { "held_item_refresh_in_ms", TimeUntilMilliseconds(player.nextHeldItemRefresh) }
            } },
            { "equipment", {
                { "initialised", player.equipInited },
                { "last_update_age_ms", AgeMilliseconds(player.lastEquipmentUpdate) },
                { "update_interval_ms", player.equipmentUpdateInterval.count() },
                { "player_value", player.playerValue },
                { "slots", std::move(equipment) }
            } },
            { "hands", {
                { "last_controller", PointerText(player.lastHeldItemHandsController) },
                { "item_in_hand", player.itemInHand },
                { "cached_item", PointerText(hands.cachedItem) },
                { "cached_item_template", PointerText(hands.cachedItemTemplate) },
                { "cached_market_revision", hands.cachedMarketRevision },
                { "loaded_ammo_template", PointerText(hands.loadedAmmoTemplate) },
                { "cached_is_weapon", hands.cachedIsWeapon },
                { "weapon_version", hands.weaponVersion },
                { "ballistics_refresh_in_ms", TimeUntilMilliseconds(hands.nextBallisticsRefresh) },
                { "item_pointer", PointerText(hands.itemPtr) },
                { "item_name", hands.itemName },
                { "ammo_name", hands.ammoName },
                { "chamber_count", hands.chamberCount },
                { "magazine_count", hands.magazineCount },
                { "weapon_velocity_modifier", hands.weaponVelocityModifier },
                { "weapon_velocity_modifier_valid", hands.weaponVelocityModifierValid },
                { "velocity_modifier_refresh_in_ms", TimeUntilMilliseconds(hands.nextVelocityModifierRefresh) },
                { "ballistics", {
                    { "valid", hands.ballistics.IsValid() },
                    { "bullet_speed", hands.ballistics.bulletSpeed },
                    { "bullet_mass_grams", hands.ballistics.bulletMassGrams },
                    { "bullet_diameter_millimetres", hands.ballistics.bulletDiameterMillimeters },
                    { "ballistic_coefficient", hands.ballistics.ballisticCoefficient }
                } }
            } },
            { "bones", {
                { "invalid", player.invalidBones },
                { "pointers_need_resolve", player.bonePointersNeedResolve },
                { "minimal_pointers_ready", player.minimalBonePointersReady },
                { "full_pointers_ready", player.fullBonePointersReady },
                { "minimal_pose_valid", player.minimalBonePoseValid },
                { "full_pose_valid", player.fullBonePoseValid },
                { "consecutive_bad_samples", player.consecutiveBadBoneSamples },
                { "matrix", PointerText(player.playerBoneMatrixPtr) },
                { "using_camera_position_fallback", player.usingCameraPositionFallback },
                { "entries", std::move(bones) },
                { "transform_cache", std::move(transformCache) }
            } },
            { "pointer_state", {
                { "profile", PointerText(player.P_Profile) },
                { "info", PointerText(player.P_Info) },
                { "pwa", PointerText(player.P_PWA) },
                { "body", PointerText(player.P_Body) },
                { "inventory_controller", PointerText(player.P_InventoryControllerAddr) },
                { "hands_controller_address", PointerText(player.P_HandsControllerAddr) },
                { "hands_controller", PointerText(player.P_HandsController) },
                { "corpse", PointerText(player.P_CorpseAddr) },
                { "corpse_class", PointerText(player.P_CorpseClass) },
                { "observed_controller", PointerText(player.P_ObservedPlayerController) },
                { "health_controller", PointerText(player.P_ObservedHealthController) },
                { "movement_context", PointerText(player.P_MovementContext) },
                { "rotation_address", PointerText(player.P_RotationAddress) },
                { "velocity_address", PointerText(player.P_VelocityAddress) }
            } },
            { "profile_data", {
                { "available", player.hasProfileData },
                { "mode", player.profileDataMode },
                { "attempted_modes", player.attemptedProfileDataModes },
                { "dog_tag_level", player.DT_lvl },
                { "kd", player.kd },
                { "player_kd", player.pkd },
                { "hours", player.hours },
                { "experience", player.profileStats.experience },
                { "member_category", player.profileStats.memberCategory },
                { "selected_member_category", player.profileStats.selectedMemberCategory },
                { "prestige_level", player.profileStats.prestigeLevel },
                { "level", player.profileStats.level },
                { "hours_played", player.profileStats.hoursPlayed },
                { "kills", player.profileStats.Kills },
                { "pmc_kills", player.profileStats.killedPMC },
                { "pmc_deaths", player.profileStats.deathsPMC },
                { "survived_raids", player.profileStats.survivedRaids },
                { "killed_in_raids", player.profileStats.killedInRaids },
                { "runs_through", player.profileStats.runsThrough },
                { "total_raids", player.profileStats.totalRaids },
                { "updated_age_ms", SystemAgeMilliseconds(player.profileStats.updated) },
                { "other_counters", std::move(otherCounters) }
            } }
        };

        if (includeIdentity)
        {
            result["identity"] = {
                { "name", player.name },
                { "group_id", player.groupId },
                { "group_id_before_friend", player.groupIdBeforeFriend },
                { "account_id", player.accountId },
                { "profile_id", player.profileId },
                { "profile_account_id", player.profileStats.aid },
                { "profile_nickname", player.profileStats.nickname },
                { "profile_side", player.profileStats.side }
            };
        }

        return result;
    }

    json MatrixJson(const glm::highp_mat4& matrix)
    {
        json rows = json::array();
        for (int row = 0; row < 4; ++row)
        {
            json values = json::array();
            for (int column = 0; column < 4; ++column)
                values.push_back(matrix[column][row]);
            rows.push_back(std::move(values));
        }
        return rows;
    }

    json TimingJson(const DxTimingSnapshot& timing)
    {
        return {
            { "last_ms", timing.lastMs },
            { "average_ms", timing.averageMs },
            { "peak_ms", timing.peakMs }
        };
    }

    json TargetJson(const std::optional<TargetResult>& target, bool includeIdentity)
    {
        if (!target)
            return nullptr;

        json result = {
            { "instance", PointerText(target->player.instance) },
            { "kind", PlayerKindText(target->player) },
            { "distance_metres", std::sqrt(target->worldDistanceSq) },
            { "screen_distance_pixels", std::sqrt(target->screenDistanceSq) },
            { "selected_bone", static_cast<int>(target->selectedBone) },
            { "screen_position", Vec2Json(target->screenPos) },
            { "bone_world_position", Vec3Json(target->boneWorldPos) }
        };

        if (includeIdentity)
            result["name"] = target->player.name;

        return result;
    }

    void AddHealthItem(SupportHealthSnapshot& snapshot, std::string module, std::string status, std::string detail, SupportHealthLevel level)
    {
        if (level == SupportHealthLevel::Warning)
            ++snapshot.warningCount;
        else if (level == SupportHealthLevel::Error)
            ++snapshot.errorCount;

        snapshot.items.push_back({ std::move(module), std::move(status), std::move(detail), level });
    }

    bool WriteTextFile(const std::filesystem::path& path, const std::string& content, std::string& error)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            error = "Could not create " + path.string();
            return false;
        }

        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        file.flush();

        if (!file.good())
        {
            error = "Could not finish writing " + path.string();
            return false;
        }

        return true;
    }
}

SupportHealthSnapshot CaptureSupportHealthSnapshot()
{
    SupportHealthSnapshot snapshot;
    const MemoryConnectionStats connection = mem.GetConnectionStats();
    const PlayerRosterStatus roster = registeredPlayers.getRosterStatus();
    const PlayerLocalStateSnapshot local = registeredPlayers.getLocalStateSnapshot();
    const CameraManagerSnapshot camera = cameraManagerTest.snapshot();
    const bool inRaid = mainGame.onlineRaid;
    const bool dmaReady = mem.IsDmaOperational();
    const bool worldReady = Utils::valid_pointer(mainGame.gameWorld) && Utils::valid_pointer(mainGame.localGameWorld);
    const bool localReady = local && Utils::valid_pointer(local->instance);
    const bool workersRunning = appGlobals::runThreads.load(std::memory_order_acquire);
    const bool inputReady = inputDevice.IsConnected();

    AddHealthItem(
        snapshot,
        "DMA connection",
        dmaReady ? "Ready" : DmaStateText(mem.GetDmaState()),
        connection.processName.empty() ? "No target process" : connection.processName + " (PID " + std::to_string(connection.processId) + ")",
        dmaReady ? SupportHealthLevel::Good : mem.GetDmaState() == DmaConnectionState::Failed ? SupportHealthLevel::Error : SupportHealthLevel::Warning);

    AddHealthItem(
        snapshot,
        "Game world",
        worldReady ? "Ready" : inRaid ? "Missing" : "Waiting",
        mainGame.selectedLocation.empty() ? "No map selected" : mainGame.selectedLocation,
        worldReady ? SupportHealthLevel::Good : inRaid ? SupportHealthLevel::Error : SupportHealthLevel::Inactive);

    AddHealthItem(
        snapshot,
        "Local player",
        localReady ? "Ready" : inRaid ? "Missing" : "Waiting",
        localReady ? PointerText(local->instance) : "No published local-player pointer",
        localReady ? SupportHealthLevel::Good : inRaid ? SupportHealthLevel::Error : SupportHealthLevel::Inactive);

    AddHealthItem(
        snapshot,
        "Player roster",
        roster.possibleMissingEntities ? "Mismatch" : "Healthy",
        std::to_string(roster.cachedRegisteredCount) + " registered cached / " + std::to_string(roster.registeredCount) + " registered",
        roster.possibleMissingEntities ? SupportHealthLevel::Warning : inRaid ? SupportHealthLevel::Good : SupportHealthLevel::Inactive);

    AddHealthItem(
        snapshot,
        "Camera",
        camera && camera->valid ? "Ready" : inRaid ? "Invalid" : "Waiting",
        camera && !camera->cameraSampleFailure.empty() ? camera->cameraSampleFailure : camera && camera->valid ? PointerText(camera->activeCamera) : "No valid camera snapshot",
        camera && camera->valid ? SupportHealthLevel::Good : inRaid ? SupportHealthLevel::Warning : SupportHealthLevel::Inactive);

    AddHealthItem(
        snapshot,
        "Workers",
        workersRunning ? "Running" : "Idle",
        workersRunning ? "Scheduler lanes are active" : "No active raid workers",
        workersRunning ? SupportHealthLevel::Good : inRaid ? SupportHealthLevel::Error : SupportHealthLevel::Inactive);

    AddHealthItem(
        snapshot,
        "Aim input",
        inputReady ? inputDevice.GetConnectedName() : "Disconnected",
        aimGlobals::aimEnabled ? "Aim is enabled" : "Aim is disabled",
        inputReady ? SupportHealthLevel::Good : aimGlobals::aimEnabled ? SupportHealthLevel::Warning : SupportHealthLevel::Inactive);

    return snapshot;
}

std::filesystem::path GetSupportReportDirectory()
{
    std::filesystem::path base = LOGS.getErrorLogPath().parent_path();
    if (base.empty())
        base = std::filesystem::current_path() / "logs";

    return base / "support_reports";
}

SupportReportResult ExportSupportReport(const SupportReportOptions& options)
{
    SupportReportResult result;

    try
    {
        const std::string generatedAt = Timestamp(false);
        const std::string fileStamp = Timestamp(true);
        const SupportHealthSnapshot health = CaptureSupportHealthSnapshot();
        const MemoryConnectionStats connection = mem.GetConnectionStats();
        const MemoryTrafficStats traffic = mem.GetTrafficStats();
        const TarkovPointerSnapshot pointers = mem.GetTarkovPointerSnapshot();
        const PlayerSnapshot players = registeredPlayers.getCacheSnapshot();
        const RegisteredPlayerArraySnapshot registeredPlayerArray = MainGame::getRegisteredPlayerArraySnapshot();
        const PlayerSnapshotTelemetry playerTelemetry = registeredPlayers.getSnapshotTelemetry();
        const PlayerRosterStatus roster = registeredPlayers.getRosterStatus();
        const PlayerLocalStateSnapshot local = registeredPlayers.getLocalStateSnapshot();
        const CameraManagerSnapshot camera = cameraManagerTest.snapshot();
        const DxFuserPerformanceSnapshot fuser = g_DxWindow.GetPerformanceSnapshot();
        const std::optional<TargetResult> liveTarget = readOnlyAim.getLiveTarget();
        const std::optional<TargetResult> activeTarget = readOnlyAim.getActiveTarget();
        const AimReferencePoint aimReference = readOnlyAim.resolveAimReference();
        const FireportPose fireport = g_fireport.snapshot();
        const double fireportAgeMs = AgeMilliseconds(fireport.publishedAt);
        const std::vector<LootEntity> loot = Loot.getCacheLoot();
        const std::vector<QuestData> quests = GetQuestDataActiveSnapshot();
        const std::vector<PerfMetricSnapshot> metrics = PerfMonitor::Instance().GetTopMetrics({}, 4096);
        const std::vector<PerfSample> samples = PerfMonitor::Instance().GetRecent();
        const MakcuDiagnostics makcuDiagnostics = makcu.GetDiagnostics();
        const FerrumDiagnostics ferrumDiagnostics = ferrum.GetDiagnostics();

        json report;
        report["report"] = {
            { "format", "meaty_support_report" },
            { "version", 2 },
            { "generated_at_local", generatedAt },
            { "application_version", globals::appVersion },
            { "latest_known_version", globals::latestAppVersion },
            { "build", sizeof(void*) == 8 ? "x64" : "x86" },
#ifdef _DEBUG
            { "configuration", "debug" }
#else
            { "configuration", "release" }
#endif
        };

        report["privacy"] = {
            { "recent_logs_included", options.includeRecentLogs },
            { "player_identity_included", options.includePlayerIdentity },
            { "api_keys_included", false }
        };

        report["health"] = json::array();
        for (const SupportHealthItem& item : health.items)
        {
            report["health"].push_back({
                { "module", item.module },
                { "status", item.status },
                { "detail", item.detail },
                { "level", HealthLevelText(item.level) }
            });
        }

        report["connection"] = {
            { "dma_state", DmaStateText(mem.GetDmaState()) },
            { "dma_operational", mem.IsDmaOperational() },
            { "dma_initialised", connection.dmaInitialized },
            { "process_initialised", connection.processInitialized },
            { "vmm_handle_valid", connection.vmmHandleValid },
            { "vmm_handle", PointerText(connection.vmmHandleAddress) },
            { "process_name", connection.processName },
            { "process_id", connection.processId },
            { "target_base", PointerText(connection.targetBaseAddress) },
            { "target_size", connection.targetBaseSize },
            { "libraries", {
                { "vmm", connection.vmmLibraryLoaded },
                { "leechcore", connection.leechCoreLibraryLoaded },
                { "ftd3xx", connection.ftd3xxLibraryLoaded }
            } },
            { "fpga", {
                { "available", connection.fpgaInfoAvailable },
                { "fpga_id", PointerText(connection.fpgaId) },
                { "device_id", PointerText(connection.deviceId) },
                { "firmware_major", connection.firmwareMajor },
                { "firmware_minor", connection.firmwareMinor }
            } },
            { "cache_ticks", {
                { "available", connection.cacheInfoAvailable },
                { "process_partial", connection.processCachePartialTicks },
                { "process_total", connection.processCacheTotalTicks },
                { "read", connection.readCacheTicks },
                { "tlb", connection.tlbCacheTicks }
            } }
        };

        report["memory_traffic"] = {
            { "read_operations", traffic.readOperations },
            { "read_successes", traffic.readSuccesses },
            { "read_failures", traffic.readFailures },
            { "read_requests", traffic.readRequests },
            { "read_bytes_requested", traffic.readBytesRequested },
            { "read_operations_per_second", traffic.readOperationsPerSecond },
            { "read_requests_per_second", traffic.readRequestsPerSecond },
            { "read_bytes_per_second", traffic.readBytesRequestedPerSecond },
            { "write_operations", traffic.writeOperations },
            { "write_successes", traffic.writeSuccesses },
            { "write_failures", traffic.writeFailures },
            { "scatter_read_batches", traffic.scatterReadBatches },
            { "scatter_read_requests", traffic.scatterReadRequests },
            { "scatter_clear_failures", traffic.scatterClearFailures }
        };

        report["core_pointers"] = {
            { "unity_player_base", PointerText(pointers.unityPlayerBase) },
            { "unity_player_size", pointers.unityPlayerSize },
            { "game_assembly_base", PointerText(pointers.gameAssemblyBase) },
            { "game_object_manager_slot", PointerText(pointers.gameObjectManagerSlot) },
            { "game_object_manager", PointerText(pointers.gameObjectManager) },
            { "gom_signature_attempted", pointers.gameObjectManagerSignatureAttempted },
            { "gom_resolved_by_signature", pointers.gameObjectManagerResolvedBySignature },
            { "game_world", PointerText(mainGame.gameWorld) },
            { "local_game_world", PointerText(mainGame.localGameWorld) },
            { "registered_players", PointerText(mainGame.registeredPlayers) },
            { "registered_player_list", PointerText(mainGame.registeredPlayersList) }
        };

        report["world"] = {
            { "game_running", gameGlobals::gameRunning },
            { "online_raid", mainGame.onlineRaid },
            { "radar_running", appGlobals::runRadar.load(std::memory_order_acquire) },
            { "workers_running", appGlobals::runThreads.load(std::memory_order_acquire) },
            { "selected_location", mainGame.selectedLocation },
            { "registered_count", roster.registeredCount },
            { "cached_registered_count", roster.cachedRegisteredCount },
            { "possible_missing_entities", roster.possibleMissingEntities }
        };

        report["local_player"] = {
            { "instance", PointerText(local ? local->instance : 0) },
            { "hands_controller", PointerText(local ? local->handsController : 0) },
            { "profile", PointerText(local ? local->profile : 0) },
            { "position", Vec3Json(local ? local->location : glm::vec3{}) },
            { "rotation", Vec2Json(local ? local->rotation : glm::vec2{}) },
            { "is_scoped", local && local->isScoped },
            { "is_savage", local && local->isSavage }
        };

        report["player_snapshot"] = {
            { "version", playerTelemetry.snapshotVersion },
            { "motion_version", playerTelemetry.motionVersion },
            { "count", playerTelemetry.playerCount },
            { "snapshot_age_ms", playerTelemetry.snapshotAgeMs },
            { "motion_age_ms", playerTelemetry.motionAgeMs },
            { "average_motion_interval_ms", playerTelemetry.averageMotionIntervalMs }
        };

        std::unordered_map<std::uint64_t, std::size_t> cacheIndexes;
        cacheIndexes.reserve(players->size());
        for (std::size_t index = 0; index < players->size(); ++index)
            cacheIndexes.emplace((*players)[index].instance, index);

        std::unordered_set<std::uint64_t> registeredPointers;
        registeredPointers.reserve(registeredPlayerArray->size());
        for (const std::uint64_t pointer : *registeredPlayerArray)
        {
            if (Utils::valid_pointer(pointer))
                registeredPointers.insert(pointer);
        }

        report["registered_player_array"] = {
            { "container", PointerText(mainGame.registeredPlayers) },
            { "list", PointerText(mainGame.registeredPlayersList) },
            { "raw_slot_count", registeredPlayerArray->size() },
            { "accepted_unique_count", mainGame.registeredPlayersCount },
            { "entries", json::array() }
        };

        std::unordered_set<std::uint64_t> seenRosterPointers;
        seenRosterPointers.reserve(registeredPlayerArray->size());
        for (std::size_t index = 0; index < registeredPlayerArray->size(); ++index)
        {
            const std::uint64_t pointer = (*registeredPlayerArray)[index];
            const bool valid = Utils::valid_pointer(pointer);
            const bool duplicate = valid && !seenRosterPointers.insert(pointer).second;
            const auto cached = cacheIndexes.find(pointer);
            json entry = {
                { "index", index },
                { "pointer", PointerText(pointer) },
                { "valid_pointer", valid },
                { "duplicate", duplicate },
                { "accepted", valid && !duplicate },
                { "present_in_cache", cached != cacheIndexes.end() }
            };

            if (cached != cacheIndexes.end())
            {
                const Player& player = (*players)[cached->second];
                entry["cache_index"] = cached->second;
                entry["cache_entry_id"] = player.cacheEntryId;
                entry["dead"] = player.isDead;
                entry["exfiled"] = player.hasExfiled;
            }

            report["registered_player_array"]["entries"].push_back(std::move(entry));
        }

        report["players"] = json::array();
        for (const Player& player : *players)
            report["players"].push_back(PlayerJson(player, options.includePlayerIdentity, registeredPointers.contains(player.instance)));

        report["btr"] = {
            { "present", false },
            { "allocated_flag", mainGame.btrAllocated },
            { "vehicle_count", 0 },
            { "passenger_count", 0 },
            { "vehicles", json::array() },
            { "unassigned_passengers", json::array() }
        };

        std::size_t btrVehicleCount = 0;
        std::size_t btrPassengerCount = 0;
        for (const Player& vehicle : *players)
        {
            if (!vehicle.isBTR)
                continue;

            json vehicleEntry = {
                { "cache_entry", PlayerJson(vehicle, options.includePlayerIdentity, registeredPointers.contains(vehicle.instance)) },
                { "controller", PointerText(vehicle.btrController) },
                { "view", PointerText(vehicle.btrView) },
                { "turret", PointerText(vehicle.btrTurret) },
                { "operator", PointerText(vehicle.instance) },
                { "passengers", json::array() }
            };

            for (const Player& passenger : *players)
            {
                if (!passenger.isInBTR || passenger.btrPassengerVehicle != vehicle.instance)
                    continue;

                vehicleEntry["passengers"].push_back(PlayerJson(passenger, options.includePlayerIdentity, registeredPointers.contains(passenger.instance)));
                ++btrPassengerCount;
            }

            report["btr"]["vehicles"].push_back(std::move(vehicleEntry));
            ++btrVehicleCount;
        }

        for (const Player& passenger : *players)
        {
            if (!passenger.isInBTR)
                continue;

            const bool vehicleFound = std::any_of(players->begin(), players->end(), [&passenger](const Player& vehicle)
            {
                return vehicle.isBTR && vehicle.instance == passenger.btrPassengerVehicle;
            });
            if (!vehicleFound)
            {
                report["btr"]["unassigned_passengers"].push_back(PlayerJson(passenger, options.includePlayerIdentity, registeredPointers.contains(passenger.instance)));
                ++btrPassengerCount;
            }
        }

        report["btr"]["present"] = btrVehicleCount != 0 || btrPassengerCount != 0;
        report["btr"]["vehicle_count"] = btrVehicleCount;
        report["btr"]["passenger_count"] = btrPassengerCount;

        if (camera)
        {
            const double cameraAgeMs = camera->publishedAt == std::chrono::steady_clock::time_point{}
                ? -1.0
                : std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - camera->publishedAt).count();

            report["camera"] = {
                { "valid", camera->valid },
                { "version", camera->version },
                { "age_ms", cameraAgeMs },
                { "active_kind", camera->activeKind == ManagedCameraKind::Optic ? "optic" : "fps" },
                { "ads", camera->ads },
                { "scoped", camera->scoped },
                { "using_optic", camera->usingOptic },
                { "fps_matrix_valid", camera->fpsMatrixValid },
                { "optic_matrix_valid", camera->opticMatrixValid },
                { "magnification", camera->magnification },
                { "fps_fov", camera->fpsFov },
                { "fps_aspect", camera->fpsAspect },
                { "busy_read_skips", camera->busyReadSkips },
                { "sample_failure", camera->cameraSampleFailure },
                { "optic_failure", camera->opticCameraFailure },
                { "pointers", {
                    { "fps_camera", PointerText(camera->fpsCamera) },
                    { "optic_camera", PointerText(camera->opticCamera) },
                    { "active_camera", PointerText(camera->activeCamera) },
                    { "active_view_matrix", PointerText(camera->activeViewMatrixAddress) },
                    { "all_cameras_global", PointerText(camera->allCamerasGlobal) },
                    { "eft_camera_manager", PointerText(camera->eftCameraManager) },
                    { "optic_camera_manager", PointerText(camera->opticCameraManager) },
                    { "current_optic_sight", PointerText(camera->currentOpticSight) }
                } },
                { "fps_world_position", Vec3Json(camera->fpsCameraWorldPosition) },
                { "raw_view_matrix", MatrixJson(camera->rawViewMatrix) },
                { "view_matrix", MatrixJson(camera->viewMatrix) },
                { "main_view_projection", MatrixJson(camera->mainViewProjection) },
                { "optic_view_projection", MatrixJson(camera->opticViewProjection) }
            };
        }

        report["aim"] = {
            { "enabled", aimGlobals::aimEnabled },
            { "prediction_enabled", aimGlobals::predictionEnabled },
            { "target_lock", aimGlobals::targetLock },
            { "closest_bone_to_fireport", aimGlobals::aimClosestBoneToFireport },
            { "fov_pixels", aimGlobals::aimFOV },
            { "distance_metres", aimGlobals::aimDistance },
            { "smooth", aimGlobals::aimSmooth },
            { "speed_pixels_per_second", aimGlobals::aimSpeedPixelsPerSecond },
            { "deadzone_pixels", aimGlobals::aimDeadzonePixels },
            { "offset", { { "x", aimGlobals::aimOffsetX }, { "y", aimGlobals::aimOffsetY } } },
            { "reference", { { "valid", aimReference.valid }, { "position", Vec2Json(aimReference.pos) } } },
            { "fireport", {
                { "valid", fireport.valid },
                { "fresh_for_aim", IsFireportPoseFresh(fireport, kFireportAimMaxAge) },
                { "fresh_for_visual", IsFireportPoseFresh(fireport, kFireportVisualMaxAge) },
                { "age_ms", fireportAgeMs },
                { "path", fireport.pathUsed ? fireport.pathUsed : "" },
                { "version", fireport.version },
                { "camera_version", fireport.cameraVersion },
                { "screen_start_ok", fireport.screenStartOk },
                { "screen_end_ok", fireport.screenEndOk },
                { "aim_reference_ok_at_publish", fireport.aimRefOk },
                { "world_origin", Vec3Json(fireport.worldOrigin) },
                { "world_forward", Vec3Json(fireport.worldForward) },
                { "screen_start_at_publish", Vec2Json(fireport.screenStart) },
                { "screen_end_at_publish", Vec2Json(fireport.screenEnd) }
            } },
            { "live_target", TargetJson(liveTarget, options.includePlayerIdentity) },
            { "active_target", TargetJson(activeTarget, options.includePlayerIdentity) }
        };

        std::size_t lootPending = 0;
        std::size_t lootFailed = 0;
        std::size_t lootInvalidPosition = 0;
        for (const LootEntity& item : loot)
        {
            lootPending += item.pendingResolve ? 1u : 0u;
            lootFailed += item.failed ? 1u : 0u;
            lootInvalidPosition += !item.pendingResolve && !item.failed && !item.hasValidPosition ? 1u : 0u;
        }

        report["features"] = {
            { "loot", {
                { "list_pointer", PointerText(Loot.lootListP) },
                { "list_items_pointer", PointerText(Loot.lootListPtr) },
                { "reported_count", Loot.lootCount },
                { "cached_count", loot.size() },
                { "pending_count", lootPending },
                { "failed_count", lootFailed },
                { "invalid_position_count", lootInvalidPosition }
            } },
            { "explosives", {
                { "grenade_count", explosiveManager.getGrenadeCount() },
                { "tripwire_count", explosiveManager.getTripwireCount() },
                { "grenades_controller", PointerText(explosiveManager.getGrenadesController()) },
                { "grenades_list", PointerText(explosiveManager.getGrenadesListPointer()) },
                { "synchronizable_processor", PointerText(explosiveManager.getSynchronizableObjectLogicProcessor()) }
            } },
            { "active_quest_count", quests.size() }
        };

        report["input_device"] = {
            { "connected", inputDevice.IsConnected() },
            { "type", inputDevice.GetConnectedName() },
            { "makcu", {
                { "connected", makcuDiagnostics.connected },
                { "port", makcuDiagnostics.connectedPort },
                { "baud_rate", makcuDiagnostics.connectedBaudRate },
                { "firmware", makcuDiagnostics.firmwareVersion },
                { "last_error", makcuDiagnostics.lastError }
            } },
            { "ferrum", {
                { "connected", ferrumDiagnostics.connected },
                { "port", ferrumDiagnostics.connectedPort },
                { "baud_rate", ferrumDiagnostics.connectedBaudRate },
                { "firmware", ferrumDiagnostics.firmwareVersion },
                { "last_error", ferrumDiagnostics.lastError }
            } }
        };

        report["fuser"] = {
            { "running", g_DxWindow.IsRunning() },
            { "fps", fuser.presentedFPS },
            { "command_count", fuser.commandCount },
            { "frame_count", fuser.frameCount },
            { "dropped_commands", fuser.droppedCommandCount },
            { "frame_interval", TimingJson(fuser.frameInterval) },
            { "build", TimingJson(fuser.build) },
            { "draw", TimingJson(fuser.draw) },
            { "present", TimingJson(fuser.present) }
        };

        report["performance_metrics"] = json::array();
        for (const PerfMetricSnapshot& metric : metrics)
        {
            report["performance_metrics"].push_back({
                { "name", metric.name },
                { "detail", metric.detail },
                { "last_ms", metric.lastMs },
                { "average_ms", metric.averageMs },
                { "peak_ms", metric.peakMs },
                { "last_seen_seconds", metric.lastSeenSec },
                { "sample_count", metric.sampleCount }
            });
        }

        report["recent_performance_events"] = json::array();
        for (const PerfSample& sample : samples)
        {
            report["recent_performance_events"].push_back({
                { "name", sample.name },
                { "detail", sample.detail },
                { "duration_ms", sample.durationMs },
                { "timestamp_seconds", sample.timestampSec }
            });
        }

        if (options.includeRecentLogs)
        {
            const std::vector<Message> messages = LOGS.getMessages();
            report["recent_logs"] = json::array();
            for (const Message& message : messages)
            {
                report["recent_logs"].push_back({
                    { "timestamp_seconds", message.timestamp },
                    { "level", MessageLevelText(message.level) },
                    { "message", RedactSensitive(message.text) }
                });
            }
        }

        std::ostringstream text;
        text << "MEATY SUPPORT REPORT\n";
        text << "Generated: " << generatedAt << "\n";
        text << "Application: " << globals::appVersion << " | " << (sizeof(void*) == 8 ? "x64" : "x86") << "\n";
        text << "Privacy: player identity " << (options.includePlayerIdentity ? "included" : "excluded")
             << ", recent logs " << (options.includeRecentLogs ? "included" : "excluded") << ", API keys always excluded\n\n";

        text << "HEALTH SUMMARY\n";
        for (const SupportHealthItem& item : health.items)
            text << "[" << HealthLevelText(item.level) << "] " << item.module << ": " << item.status << " - " << item.detail << "\n";

        text << "\nCONNECTION\n";
        text << "DMA state: " << DmaStateText(mem.GetDmaState()) << " | operational: " << BoolText(mem.IsDmaOperational()) << "\n";
        text << "Target: " << (connection.processName.empty() ? "<none>" : connection.processName) << " | PID " << connection.processId
             << " | base " << PointerText(connection.targetBaseAddress) << "\n";
        text << "Libraries: VMM " << BoolText(connection.vmmLibraryLoaded) << " | LeechCore " << BoolText(connection.leechCoreLibraryLoaded)
             << " | FTD3XX " << BoolText(connection.ftd3xxLibraryLoaded) << "\n";
        text << "Reads: " << traffic.readOperations << " operations, " << traffic.readRequests << " requests, " << traffic.readFailures
             << " failures, " << static_cast<std::uint64_t>(traffic.readBytesRequestedPerSecond) << " bytes/s\n";

        text << "\nWORLD AND CORE POINTERS\n";
        text << "Map: " << (mainGame.selectedLocation.empty() ? "<none>" : mainGame.selectedLocation) << " | online raid: " << BoolText(mainGame.onlineRaid) << "\n";
        text << "UnityPlayer: " << PointerText(pointers.unityPlayerBase) << " | GameAssembly: " << PointerText(pointers.gameAssemblyBase) << "\n";
        text << "GOM slot: " << PointerText(pointers.gameObjectManagerSlot) << " | GOM: " << PointerText(pointers.gameObjectManager) << "\n";
        text << "GameWorld: " << PointerText(mainGame.gameWorld) << " | LocalGameWorld: " << PointerText(mainGame.localGameWorld) << "\n";
        text << "Registered list: " << PointerText(mainGame.registeredPlayersList) << " | " << roster.cachedRegisteredCount << "/" << roster.registeredCount
             << " cached/registered\n";

        text << "\nREGISTERED PLAYER ARRAY\n";
        text << "Container: " << PointerText(mainGame.registeredPlayers) << " | list: " << PointerText(mainGame.registeredPlayersList)
             << " | raw slots: " << registeredPlayerArray->size() << " | accepted unique: " << mainGame.registeredPlayersCount << "\n";
        seenRosterPointers.clear();
        for (std::size_t index = 0; index < registeredPlayerArray->size(); ++index)
        {
            const std::uint64_t pointer = (*registeredPlayerArray)[index];
            const bool valid = Utils::valid_pointer(pointer);
            const bool duplicate = valid && !seenRosterPointers.insert(pointer).second;
            const auto cached = cacheIndexes.find(pointer);
            text << "Slot " << index << ": " << PointerText(pointer) << " valid=" << BoolText(valid) << " duplicate=" << BoolText(duplicate)
                 << " cached=" << BoolText(cached != cacheIndexes.end());
            if (cached != cacheIndexes.end())
            {
                const Player& player = (*players)[cached->second];
                text << " cache_index=" << cached->second << " cache_id=" << player.cacheEntryId << " dead=" << BoolText(player.isDead)
                     << " exfiled=" << BoolText(player.hasExfiled);
            }
            text << "\n";
        }

        text << "\nLOCAL AND PLAYERS\n";
        text << "Local: " << PointerText(local ? local->instance : 0) << " | hands " << PointerText(local ? local->handsController : 0) << "\n";
        const std::size_t deadCachedCount = static_cast<std::size_t>(std::count_if(players->begin(), players->end(), [](const Player& player) { return player.isDead; }));
        const std::size_t exfiledCachedCount = static_cast<std::size_t>(std::count_if(players->begin(), players->end(), [](const Player& player) { return player.hasExfiled; }));
        text << "Snapshot: " << players->size() << " cached | " << deadCachedCount << " dead | " << exfiledCachedCount << " exfiled | age "
             << playerTelemetry.snapshotAgeMs << " ms | motion age " << playerTelemetry.motionAgeMs << " ms\n";
        for (std::size_t index = 0; index < players->size(); ++index)
        {
            const Player& player = (*players)[index];
            text << "Player " << index << " [" << PlayerKindText(player) << "] " << PointerText(player.instance);
            if (options.includePlayerIdentity)
                text << " name='" << player.name << "' profile='" << player.profileId << "' account='" << player.accountId << "'";
            text << " registered=" << BoolText(registeredPointers.contains(player.instance)) << " distance=" << player.distance
                 << " dead=" << BoolText(player.isDead) << " exfiled=" << BoolText(player.hasExfiled) << " in_btr=" << BoolText(player.isInBTR) << "\n";
            text << "  profile=" << PointerText(player.P_Profile) << " info=" << PointerText(player.P_Info) << " pwa=" << PointerText(player.P_PWA)
                 << " body=" << PointerText(player.P_Body) << " inventory=" << PointerText(player.P_InventoryControllerAddr)
                 << " hands_addr=" << PointerText(player.P_HandsControllerAddr) << " hands=" << PointerText(player.P_HandsController) << "\n";
            text << "  observed=" << PointerText(player.P_ObservedPlayerController) << " health=" << PointerText(player.P_ObservedHealthController)
                 << " movement=" << PointerText(player.P_MovementContext) << " rotation=" << PointerText(player.P_RotationAddress)
                 << " velocity=" << PointerText(player.P_VelocityAddress) << " bones=" << PointerText(player.playerBoneMatrixPtr) << "\n";
        }

        if (btrVehicleCount != 0 || btrPassengerCount != 0)
        {
            text << "\nBTR AND PASSENGERS\n";
            text << "Allocated flag: " << BoolText(mainGame.btrAllocated) << " | vehicles: " << btrVehicleCount << " | passengers: " << btrPassengerCount << "\n";
            for (const Player& vehicle : *players)
            {
                if (!vehicle.isBTR)
                    continue;

                text << "BTR operator=" << PointerText(vehicle.instance) << " controller=" << PointerText(vehicle.btrController)
                     << " view=" << PointerText(vehicle.btrView) << " turret=" << PointerText(vehicle.btrTurret)
                     << " position=" << vehicle.location.x << "," << vehicle.location.y << "," << vehicle.location.z << "\n";
                for (const Player& passenger : *players)
                {
                    if (!passenger.isInBTR || passenger.btrPassengerVehicle != vehicle.instance)
                        continue;

                    text << "  Passenger " << PointerText(passenger.instance);
                    if (options.includePlayerIdentity)
                        text << " name='" << passenger.name << "' profile='" << passenger.profileId << "'";
                    text << " dead=" << BoolText(passenger.isDead) << " exfiled=" << BoolText(passenger.hasExfiled) << "\n";
                }
            }

            for (const Player& passenger : *players)
            {
                if (!passenger.isInBTR)
                    continue;

                const bool vehicleFound = std::any_of(players->begin(), players->end(), [&passenger](const Player& vehicle)
                {
                    return vehicle.isBTR && vehicle.instance == passenger.btrPassengerVehicle;
                });
                if (!vehicleFound)
                    text << "Unassigned passenger " << PointerText(passenger.instance) << " vehicle=" << PointerText(passenger.btrPassengerVehicle) << "\n";
            }
        }

        text << "\nCAMERA AND AIM\n";
        text << "Camera: " << (camera && camera->valid ? "valid" : "invalid") << " | active " << PointerText(camera ? camera->activeCamera : 0)
             << " | FPS " << PointerText(camera ? camera->fpsCamera : 0) << " | optic " << PointerText(camera ? camera->opticCamera : 0) << "\n";
        if (camera && (!camera->cameraSampleFailure.empty() || !camera->opticCameraFailure.empty()))
            text << "Camera failures: " << camera->cameraSampleFailure << " | " << camera->opticCameraFailure << "\n";
        text << "Aim: enabled=" << BoolText(aimGlobals::aimEnabled) << " reference=" << BoolText(aimReference.valid)
             << " input=" << inputDevice.GetConnectedName() << " live_target=" << BoolText(liveTarget.has_value())
             << " active_target=" << BoolText(activeTarget.has_value()) << "\n";
        text << "Fireport: valid=" << BoolText(fireport.valid) << " aim_fresh=" << BoolText(IsFireportPoseFresh(fireport, kFireportAimMaxAge))
             << " visual_fresh=" << BoolText(IsFireportPoseFresh(fireport, kFireportVisualMaxAge)) << " age=" << fireportAgeMs
             << " ms path=" << (fireport.pathUsed ? fireport.pathUsed : "<none>") << " pose/camera versions=" << fireport.version << "/" << fireport.cameraVersion << "\n";

        text << "\nFEATURES\n";
        text << "Loot: " << loot.size() << " cached, " << lootPending << " pending, " << lootFailed << " failed, " << lootInvalidPosition << " invalid positions\n";
        text << "Grenades: " << explosiveManager.getGrenadeCount() << " | Tripwires: " << explosiveManager.getTripwireCount()
             << " | Active quests: " << quests.size() << "\n";

        text << "\nPERFORMANCE METRICS\n";
        for (const PerfMetricSnapshot& metric : metrics)
        {
            text << metric.name << " | last=" << metric.lastMs << " ms avg=" << metric.averageMs << " ms peak=" << metric.peakMs
                 << " ms samples=" << metric.sampleCount;
            if (!metric.detail.empty())
                text << " | " << metric.detail;
            text << "\n";
        }

        if (options.includeRecentLogs)
        {
            text << "\nRECENT LOGS\n";
            for (const Message& message : LOGS.getMessages())
                text << std::fixed << std::setprecision(2) << message.timestamp << "s [" << MessageLevelText(message.level) << "] "
                     << RedactSensitive(message.text) << "\n";
        }

        const std::filesystem::path directory = GetSupportReportDirectory();
        std::error_code directoryError;
        std::filesystem::create_directories(directory, directoryError);
        if (directoryError)
        {
            result.error = "Could not create support report directory: " + directoryError.message();
            return result;
        }

        result.textPath = directory / ("meaty_support_" + fileStamp + ".txt");
        result.jsonPath = directory / ("meaty_support_" + fileStamp + ".json");

        if (!WriteTextFile(result.textPath, text.str(), result.error))
            return result;

        if (!WriteTextFile(result.jsonPath, report.dump(2), result.error))
        {
            std::error_code removeError;
            std::filesystem::remove(result.textPath, removeError);
            return result;
        }

        std::ostringstream summary;
        summary << "Support report generated " << generatedAt << " with " << health.errorCount << " errors and " << health.warningCount
                << " warnings. Text: " << result.textPath.string() << " JSON: " << result.jsonPath.string();
        result.summary = summary.str();
        result.success = true;

        LOGS.logInfo("[SUPPORT] Exported diagnostic report: ", result.jsonPath.string());
    }
    catch (const std::exception& exception)
    {
        result.error = exception.what();
        LOGS.logError("[SUPPORT] Diagnostic export failed: ", result.error);
    }
    catch (...)
    {
        result.error = "Unknown support-report error";
        LOGS.logError("[SUPPORT] Diagnostic export failed with an unknown error");
    }

    return result;
}


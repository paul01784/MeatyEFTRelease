#include "../app/includes.h"
#include "headers/players.h"
#include "../app/globals.h"

#include "../memory/Memory.h"

#include "headers/maingame.h"
#include "../app/debug.h"
#include "headers/utils.h"
#include "headers/unityHelper.h"
#include "headers/unitysdk.h"
#include "headers/tarkovdevquery.h"
#include <cmath>
#include "headers/questManager.h"
#include "../app/market.h"
#include "headers/loot.h"
#include "headers/wishlist.h"
#include "headers/dogtag.h"
#include "../app/DogTagAPI.h"

std::mutex playerMutex;
Players players;

bool Players::groupIDSet = false;

static std::string CleanString(std::string str)
{
    const size_t nullPos = str.find('\0');
    if (nullPos != std::string::npos)
        str.erase(nullPos);

    while (!str.empty() && (str.back() == ' ' || str.back() == '\r' || str.back() == '\n' || str.back() == '\t'))
        str.pop_back();

    return str;
}

static std::string ReadString(uint64_t fieldAddr)
{
    if (!Utils::valid_pointer(fieldAddr))
        return "";

    uint64_t namePtr = mem.Read<uint64_t>(fieldAddr);
    if (!Utils::valid_pointer(namePtr))
        return "";

    int len = mem.Read<int>(static_cast<SIZE_T>(namePtr) + 0x10);
    if (len <= 0 || len > 256)
        return "";

    return CleanString(mem.readUnicodeString(namePtr + 0x14, len));
}

inline bool isValidBoneVector(const glm::vec3& v)
{
    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
        return false;

    if (std::abs(v.x) < 0.001f &&
        std::abs(v.y) < 0.001f &&
        std::abs(v.z) < 0.001f)
        return false;

    constexpr float LIMIT = 100000.f;
    if (std::abs(v.x) > LIMIT || std::abs(v.y) > LIMIT || std::abs(v.z) > LIMIT)
        return false;

    return true;
}

inline std::string SideToString(EPlayerSide side)
{
    switch (side)
    {
    case EPlayerSide::Usec:   return "Usec";
    case EPlayerSide::Bear:   return "Bear";
    case EPlayerSide::Savage: return "Savage";
    default:                  return "Unknown";
    }
}

int CalculateKD(uint32_t kills, uint32_t deaths)
{
    if (deaths == 0)
    {
        if (kills > 0)
            return kills;   // flawless → KD equals kills
        return 0;            // no combat
    }

    float kd = static_cast<float>(kills) / static_cast<float>(deaths);

    // round to nearest whole number
    return static_cast<int>(std::round(kd));
}

double CalculatePKD(uint32_t kills, uint32_t deaths)
{
    if (deaths == 0)
    {
        if (kills > 0)
            return static_cast<double>(kills);
        return 0.0;            // no combat
    }
    double pkd = static_cast<double>(kills) / static_cast<double>(deaths);
    // round to two decimal places
    return std::round(pkd * 100.0) / 100.0;
}

static int ConvertXpToLevel(int xp)
{
    const auto& t = LevelXpThresholds;

    for (int level = 1; level < static_cast<int>(t.size()); ++level)
    {
        if (xp < t[level])
            return level;
    }

    // If XP exceeds the highest value themn max level
    return static_cast<int>(t.size());
}

int Players::getDistance(glm::vec3 point1, glm::vec3 point2)
{
    float dx = point1.x - point2.x;
    float dy = point1.y - point2.y;
    float dz = point1.z - point2.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

std::string Players::voice2Name(std::string voiceName)
{
    if (voiceName.find("BossSanitar") != std::string::npos) {
        return "Sanitar";
    }
    else if (voiceName.find("BossBully") != std::string::npos) {
        return "BossBully";
    }
    else if (voiceName.find("BossGluhar") != std::string::npos) {
        return "Gluhar";
    }
    else if (voiceName.find("SectantPriest") != std::string::npos) {
        return "Priest";
    }
    else if (voiceName.find("SectantWarrior") != std::string::npos) {
        return "Warrior";
    }
    else if (voiceName.find("BossKilla") != std::string::npos) {
        return "Killa";
    }
    else if (voiceName.find("BossTagilla") != std::string::npos) {
        return "Tagilla";
    }
    else if (voiceName.find("Boss_Partizan") != std::string::npos) {
        return "Partizan";
    }
    else if (voiceName.find("BossBigPipe") != std::string::npos) {
        return "BigPipe";
    }
    else if (voiceName.find("BossBirdEye") != std::string::npos) {
        return "BirdEye";
    }
    else if (voiceName.find("BossKnight") != std::string::npos) {
        return "Knight";
    }
    else if (voiceName.find("Arena_Guard_1") != std::string::npos) {
        return "Arena Guard";
    }
    else if (voiceName.find("Arena_Guard_2") != std::string::npos) {
        return "Arena Guard";
    }
    else if (voiceName.find("Boss_Kaban") != std::string::npos) {
        return "Kaban";
    }
    else if (voiceName.find("Boss_Kollontay") != std::string::npos) {
        return "Kollontay";
    }
    else if (voiceName.find("Boss_Sturman") != std::string::npos) {
        return "Sturman";
    }
    else if (voiceName.find("Zombie_Generic") != std::string::npos) {
        return "Zombie";
    }
    else if (voiceName.find("BossZombieTagilla") != std::string::npos) {
        return "ZombieTagilla";
    }
    else if (voiceName.find("Zombie_Fast") != std::string::npos) {
        return "Zombie F";
    }
    else if (voiceName.find("Zombie_Medium") != std::string::npos) {
        return "Zombie M";
    }
    else
        return "Ai";
}

void Players::clearCache()
{
    std::lock_guard<std::mutex> lock(playerMutex);
    this->playerCache.clear();
    this->playerGroups.clear();
    players.groupIDSet = false;

    LOGS.logInfo("[PLAYER][CACHE] Data cleared");
}

void Players::softRestart()
{
    std::lock_guard<std::mutex> lock(playerMutex);

    players.groupIDSet = false;
    mainGame.localGroupId = "";
    this->playerCache.clear();
    this->playerGroups.clear();
}

std::vector<PlayerCache>& Players::getCache() {
    return playerCache;
}

std::vector<PlayerCache> Players::getCacheSnapshot()
{
    std::lock_guard<std::mutex> lock(playerMutex);
    return playerCache;
}

std::vector<PlayerGroups>& Players::getGroupCache() {
    return playerGroups;
}

PlayerCache Players::getBonePtrs(PlayerCache& players)
{

    int index = 0;
    for (auto& curBone : players.boneList)
    {
        players.bonePtrs[index] = mem.ReadChain(players.playerBoneMatrixPtr, { (0x20 + (static_cast<uint64_t>(curBone) * 0x8)), 0x10 });
        //std::cout << "BonePointer[" << std::to_string(index) << "] 0x" << std::hex << players.bonePtrs[index] << std::endl;
        index++;
    }

    return players;
}

void Players::readDogTagComponent(PlayerCache& player, bool force)
{
    if (!player.equipInited)
        return;

    if (player._slots.empty())
        return;

    if (!player.isPlayer)
        return;

    if (player.hasProfileData)
        return;

    for (auto& slot : player._slots)
    {
        std::string slotName = TrimEFT(slot.name);

        if (slotName != "Dogtag")
            continue;

        uint64_t dogtagItem = mem.Read<uint64_t>(slot.addr + sdk::Slot::ContainedItem);
        if (!Utils::valid_pointer(dogtagItem))
        {
            std::cout << "[DogTag] Fail: invalid dogtag item ptr\n";
            break;
        }

        uint64_t dogtagComp = mem.Read<uint64_t>(dogtagItem + sdk::BarterOtherOffsets::Dogtag);
        if (!Utils::valid_pointer(dogtagComp))
        {
            std::cout << "[DogTag] Fail: invalid dogtag component ptr\n";
            break;
        }

        std::cout << "[DogTag] Read Data:\n";
        std::cout << "  Nickname: " << player.DT_nickname << "\n";
        std::cout << "  ProfileID: " << player.DT_profileId << "\n";
        std::cout << "  AccountID: " << player.DT_accountId << "\n";
        std::cout << "  Level: " << player.DT_lvl << "\n";
        std::cout << "  Side: " << player.DT_Side << "\n";

        if (!player.DT_nickname.empty() ||
            player.DT_lvl > 0 ||
            player.DT_Side > 0 ||
            !player.DT_profileId.empty())
        {
            player.hasProfileData = true;

            std::cout << "[DogTag] SUCCESS: valid dogtag data\n";

            if (!player.DT_nickname.empty())
            {
                player.name = player.DT_nickname;
                std::cout << "[DogTag] Name updated from dogtag\n";
            }
        }
        else
        {
            std::cout << "[DogTag] Fail: all fields empty/invalid\n";
        }

        break;
    }
}

void Players::boneTask()
{
    try
    {
        if (!mem.vHandle) return;
        if (!Utils::valid_pointer(mainGame.localPlayerPtr)) return;

        std::lock_guard<std::mutex> lock(playerMutex);

        std::vector<PlayerCache>& cache = players.getCache();
        if (cache.empty()) return;

        struct ScatterGuard {
            Memory& mem;
            HANDLE handle;
            ScatterGuard(Memory& m) : mem(m), handle(m.CreateScatterHandle()) {}
            ~ScatterGuard() { if (handle) mem.CloseScatterHandle(handle); }
        } scatter(mem);

        if (!scatter.handle) return;

        
        constexpr int MAX_TRANSFORM_CHAIN = 512;

        // Read TransformAccessReadOnly
        for (auto& player : cache)
        {
            if (!Utils::valid_pointer(player.instance)) continue;
            if (player.isBTR) continue;
            if (player.isDead) continue;
            if (player.hasExfiled) continue;
            player.invalidBones = false;

            size_t count = std::min(player.bonePtrs.size(), player.boneTransforms.size());
            if (count == 0) continue;
            
            

            for (size_t i = 0; i < count; ++i)
            {
                

                uint64_t addr = player.bonePtrs[i] + UnityOffsets::TransformInternal_TransformAccessOffset;

                mem.AddScatterReadRequest(
                    scatter.handle,
                    addr,
                    &player.boneTransforms[i],
                    sizeof(TransformAccessReadOnly));
            }
        }
        mem.ExecuteReadScatter(scatter.handle);

        // Same filtering
        for (auto& player : cache)
        {

            if (!Utils::valid_pointer(player.instance)) continue;
            if (player.isBTR || player.invalidBones || player.isDead) continue;

            if (player.hasExfiled) continue;

            size_t count = std::min(player.bonePtrs.size(), player.boneTransforms.size());
            count = std::min(count, player.boneTransformsData.size());
            if (count == 0) continue;

            

            for (size_t i = 0; i < count; ++i)
            {
                

                auto& access = player.boneTransforms[i];
                auto& out = player.boneTransformsData[i];
                if (!access.pTransformData) { player.invalidBones = true; break; }

                uint64_t base = access.pTransformData;
                mem.AddScatterReadRequest(
                    scatter.handle,
                    base + UnityOffsets::Hierarchy_VerticesOffset,
                    &out.pTransformArray,
                    sizeof(uint64_t));
                mem.AddScatterReadRequest(
                    scatter.handle,
                    base + UnityOffsets::Hierarchy_IndicesOffset,
                    &out.pTransformIndices,
                    sizeof(uint64_t));
            }
        }
        mem.ExecuteReadScatter(scatter.handle);

        // Same filtering
        for (auto& player : cache)
        {

            if (!Utils::valid_pointer(player.instance)) continue;
            if (player.isBTR || player.invalidBones || player.isDead) continue;
            if (player.hasExfiled) continue;

            size_t count = std::min(player.bonePtrs.size(), player.boneTransforms.size());
            count = std::min(count, player.boneTransformsData.size());
            if (count == 0) continue;

            

            // Ensure vectors sized correctly
            if (player.pMatriciesBuffers.size() < count)
                player.pMatriciesBuffers.resize(count, nullptr);
            if (player.pIndicesBuffers.size() < count)
                player.pIndicesBuffers.resize(count, nullptr);
            if (player.matCap.size() < count)
                player.matCap.resize(count, 0);
            if (player.idxCap.size() < count)
                player.idxCap.resize(count, 0);

            for (size_t i = 0; i < count; ++i)
            {
                

                auto& access = player.boneTransforms[i];
                auto& data = player.boneTransformsData[i];

                if (!data.pTransformArray || !data.pTransformIndices)
                {
                    player.invalidBones = true;
                    break;
                }

                int tCount = access.index + 1;
                if (tCount <= 0 || tCount > MAX_TRANSFORM_CHAIN)
                {
                    player.invalidBones = true;
                    break;
                }

                SIZE_T matSize = tCount * sizeof(Matrix34);
                SIZE_T idxSize = tCount * sizeof(int32_t);

                // Grow / allocate matrices buffer if needed
                if (!player.pMatriciesBuffers[i] || player.matCap[i] < matSize)
                {
                    if (player.pMatriciesBuffers[i])
                        free(player.pMatriciesBuffers[i]);

                    player.pMatriciesBuffers[i] = malloc(matSize);
                    player.matCap[i] = matSize;
                }

                // Grow / allocate indices buffer if needed
                if (!player.pIndicesBuffers[i] || player.idxCap[i] < idxSize)
                {
                    if (player.pIndicesBuffers[i])
                        free(player.pIndicesBuffers[i]);

                    player.pIndicesBuffers[i] = malloc(idxSize);
                    player.idxCap[i] = idxSize;
                }

                if (!player.pMatriciesBuffers[i] || !player.pIndicesBuffers[i])
                {
                    player.invalidBones = true;
                    break;
                }

                mem.AddScatterReadRequest(
                    scatter.handle,
                    data.pTransformArray,
                    player.pMatriciesBuffers[i],
                    matSize);

                mem.AddScatterReadRequest(
                    scatter.handle,
                    data.pTransformIndices,
                    player.pIndicesBuffers[i],
                    idxSize);
            }
        }

        mem.ExecuteReadScatter(scatter.handle);

        // final bone positions
        for (auto& player : cache)
        {
            if (!Utils::valid_pointer(player.instance)) continue;
            if (!player.isBTR && !player.invalidBones && !player.isDead && !player.hasExfiled)
                player.UpdateBonePositions();
            //else
               // std::cout << "Failed boneposition checks" << std::endl;
        }
    }
    catch (const std::exception& e) {
        LOGS.logError("Exception caught in BoneTask: " + std::string(e.what()) + ". Retrying...");
        return;
    }
    catch (...) {
        LOGS.logError("Unknown exception caught in BoneTask. Retrying...");
        return;
    }
}


void PlayerCache::UpdateBonePositions()
{
    size_t count = std::min(bonePtrs.size(),
        std::min(boneTransforms.size(),
            std::min(boneTransformsData.size(),
                bonePositions.size())));

    for (size_t i = 0; i < count; ++i)
    {
        glm::vec3 newPos = GetTransformPosition((int)i);
        bonePositions[i] = newPos;
    }
}

glm::vec3 PlayerCache::GetTransformPosition(int boneIndex)
{
    // Basic safety
    if (boneIndex < 0 ||
        boneIndex >= static_cast<int>(bonePtrs.size()) ||
        boneIndex >= static_cast<int>(boneTransforms.size()) ||
        boneIndex >= static_cast<int>(boneTransformsData.size()) ||
        boneIndex >= static_cast<int>(pMatriciesBuffers.size()) ||
        boneIndex >= static_cast<int>(pIndicesBuffers.size()))
    {
        std::cout << "Failed transformpoisiton safety check" << std::endl;
        return glm::vec3(0.0f);

    }

    TransformAccessReadOnly pTransformAccessReadOnly = boneTransforms[boneIndex];
    TransformData           transformData = boneTransformsData[boneIndex];

    // Buffers filled in boneTask PASS 3
    Matrix34* matrices = static_cast<Matrix34*>(pMatriciesBuffers[boneIndex]);
    int32_t* indices = static_cast<int32_t*>(pIndicesBuffers[boneIndex]);

    if (!matrices || !indices)
        return glm::vec3(0.0f);

    // count = index + 1 (same logic as your GetTransformPosition3)
    int index = pTransformAccessReadOnly.index;
    if (index < 0)
        return glm::vec3(0.0f);

    int count = index + 1; // how many elements we expect to be valid
    if (count <= 0)
        return glm::vec3(0.0f);

    // Simple upper cap in case something goes crazy
    constexpr int MAX_TRANSFORM_CHAIN = 512;
    if (count > MAX_TRANSFORM_CHAIN)
        return glm::vec3(0.0f);

    // Bounds check against our buffer size: boneTask allocated exactly count elements
    // so we only allow transformIndex in [0, count)
    const SIZE_T sizeMatriciesBuf = sizeof(Matrix34) * count;

    if (index >= count)
        return glm::vec3(0.0f);

    // Initial value from matrices[index]
    __m128 result = *(__m128*)((uint8_t*)matrices + sizeof(Matrix34) * index);

    // Same constants as before
    const __m128 mulVec0 = { -2.0f,  2.0f, -2.0f, 0.0f };
    const __m128 mulVec1 = { 2.0f, -2.0f, -2.0f, 0.0f };
    const __m128 mulVec2 = { -2.0f, -2.0f,  2.0f, 0.0f };

    int transformIndex = indices[index];

    int safety = 0;
    while (transformIndex >= 0 && safety++ < 1000)
    {
        if (transformIndex < 0 || transformIndex >= count)
        {
            // out of range: bail out with current accumulated result
            // (no free() here, buffers are owned by PlayerCache)
            break;
        }

        const Matrix34& matrix34 = matrices[transformIndex];

        __m128 xxxx = _mm_castsi128_ps(_mm_shuffle_epi32(*(__m128i*)(&matrix34.vec1), 0x00)); // xxxx
        __m128 yyyy = _mm_castsi128_ps(_mm_shuffle_epi32(*(__m128i*)(&matrix34.vec1), 0x55)); // yyyy
        __m128 zwxy = _mm_castsi128_ps(_mm_shuffle_epi32(*(__m128i*)(&matrix34.vec1), 0x8E)); // zwxy
        __m128 wzyw = _mm_castsi128_ps(_mm_shuffle_epi32(*(__m128i*)(&matrix34.vec1), 0xDB)); // wzyw
        __m128 zzzz = _mm_castsi128_ps(_mm_shuffle_epi32(*(__m128i*)(&matrix34.vec1), 0xAA)); // zzzz
        __m128 yxwy = _mm_castsi128_ps(_mm_shuffle_epi32(*(__m128i*)(&matrix34.vec1), 0x71)); // yxwy

        __m128 tmp7 = _mm_mul_ps(*(__m128*)(&matrix34.vec2), result);

        result = _mm_add_ps(
            _mm_add_ps(
                _mm_add_ps(
                    _mm_mul_ps(
                        _mm_sub_ps(
                            _mm_mul_ps(_mm_mul_ps(xxxx, mulVec1), zwxy),
                            _mm_mul_ps(_mm_mul_ps(yyyy, mulVec2), wzyw)),
                        _mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(tmp7), 0xAA))),
                    _mm_mul_ps(
                        _mm_sub_ps(
                            _mm_mul_ps(_mm_mul_ps(zzzz, mulVec2), wzyw),
                            _mm_mul_ps(_mm_mul_ps(xxxx, mulVec0), yxwy)),
                        _mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(tmp7), 0x55)))),
                _mm_add_ps(
                    _mm_mul_ps(
                        _mm_sub_ps(
                            _mm_mul_ps(_mm_mul_ps(yyyy, mulVec0), yxwy),
                            _mm_mul_ps(_mm_mul_ps(zzzz, mulVec1), zwxy)),
                        _mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(tmp7), 0x00))),
                    tmp7)),
            *(__m128*)(&matrix34.vec0));

        int oldTransformIndex = transformIndex;
        transformIndex = indices[transformIndex];

        if (oldTransformIndex == transformIndex && transformIndex == 0)
        {
            // protection from infinite loops
            break;
        }
    }

    return glm::vec3(result.m128_f32[0], result.m128_f32[1], result.m128_f32[2]);
}

void Players::playersTask()
{
    static int failedPlayerListFrames = 0;

    try
    {
        if (!mem.vHandle)
            return;

        const bool playerListUpdated = mainGame.updatePlayerList();

        if (!playerListUpdated)
        {
            failedPlayerListFrames++;

            if (failedPlayerListFrames > 60)
            {
                std::lock_guard<std::mutex> lock(playerMutex);

                playerCache.clear();

                mainGame.localPlayerPtr = 0;
                mainGame.localPlayerHands = 0;
                mainGame.localplayerProfile = 0;
                mainGame.localGroupId.clear();
                mainGame.localIsScoped = false;

                groupIDSet = false;
                failedPlayerListFrames = 0;

                LOGS.logInfo("[PLAYERS] Cleared player cache after repeated player-list failures");
            }

            return;
        }

        failedPlayerListFrames = 0;

        // Copy current registered player list locally
        std::vector<uint64_t> registeredPlayers;
        registeredPlayers.reserve(mainGame.registeredPlayersCount);

        for (int i = 0; i < mainGame.registeredPlayersCount; i++)
        {
            const uint64_t currentPlayer = mainGame.player_buffer[i];

            if (Utils::valid_pointer(currentPlayer))
                registeredPlayers.emplace_back(currentPlayer);
        }

        if (registeredPlayers.empty())
            return;

        // Snapshot existing cache instances under short lock
        std::unordered_set<uint64_t> existingInstances;

        {
            std::lock_guard<std::mutex> lock(playerMutex);

            existingInstances.reserve(playerCache.size());

            for (const auto& cachedPlayer : playerCache)
            {
                if (Utils::valid_pointer(cachedPlayer.instance))
                    existingInstances.insert(cachedPlayer.instance);
            }
        }

        // Build new entities OUTSIDE the lock
        // This prevents boneTask from being blocked by addEntity.
        std::vector<PlayerCache> pendingNewEntities;

        for (const uint64_t currentPlayer : registeredPlayers)
        {
            if (existingInstances.find(currentPlayer) != existingInstances.end())
                continue;

            const bool isLocal = currentPlayer == mainGame.localPlayerPtr;

            auto builtEntity = buildEntity(currentPlayer, isLocal);

            if (builtEntity.has_value())
            {
                pendingNewEntities.emplace_back(std::move(*builtEntity));
            }
        }

        // Append new entities under short lock
        // Recheck duplicates because cache may have changed.
        if (!pendingNewEntities.empty())
        {
            std::lock_guard<std::mutex> lock(playerMutex);

            for (auto& entity : pendingNewEntities)
            {
                auto it = std::find_if(
                    playerCache.begin(),
                    playerCache.end(),
                    [&](const PlayerCache& cachedPlayer)
                    {
                        return cachedPlayer.instance == entity.instance;
                    });

                if (it != playerCache.end())
                    continue;

                std::ostringstream ss;
                ss << "[PLAYERS][INIT] Adding player : 0x"
                    << std::hex << entity.instance
                    << " className : " << entity.className
                    << " name : " << entity.name;

                LOGS.logInfo(ss.str());

                playerCache.emplace_back(std::move(entity));
            }
        }

        // Normal update pass
        {
            std::lock_guard<std::mutex> lock(playerMutex);

            tryFindBTR();
            updateEntity();
            checkExfil();
            checkGroupIDs();
        }
    }
    catch (const std::exception& e)
    {
        LOGS.logError("Exception caught in playersTask: " + std::string(e.what()) + ". Retrying...");
    }
    catch (...)
    {
        LOGS.logError("Unknown exception caught in playersTask. Retrying...");
    }
}

inline bool containsIgnoreCase(const std::string& str, const std::string& search)
{
    auto it = std::search(
        str.begin(), str.end(),
        search.begin(), search.end(),
        [](char ch1, char ch2) {
            return std::tolower(ch1) == std::tolower(ch2);
        }
    );
    return it != str.end();
}

AIRole GetAIRoleInfo(const std::string& voiceLine)
{
    if (containsIgnoreCase(voiceLine, "BossSanitar"))        return { "Sanitar", PlayerType::AIBoss };
    if (containsIgnoreCase(voiceLine, "BossBully"))          return { "Reshala", PlayerType::AIBoss };
    if (containsIgnoreCase(voiceLine, "BossGluhar"))         return { "Gluhar", PlayerType::AIBoss };
    if (containsIgnoreCase(voiceLine, "SectantPriest"))      return { "Priest", PlayerType::AIBoss };
    if (containsIgnoreCase(voiceLine, "SectantWarrior"))     return { "Cultist", PlayerType::AIRaider };
    if (containsIgnoreCase(voiceLine, "BossKilla"))          return { "Killa", PlayerType::AIBoss };
    if (containsIgnoreCase(voiceLine, "BossTagilla"))        return { "Tagilla", PlayerType::AIBoss };
    if (containsIgnoreCase(voiceLine, "Boss_Partizan"))      return { "Partisan", PlayerType::AIBoss };
    if (containsIgnoreCase(voiceLine, "BossBigPipe"))        return { "Big Pipe", PlayerType::AIBoss };
    if (containsIgnoreCase(voiceLine, "BossBirdEye"))        return { "Birdeye", PlayerType::AIBoss };
    if (containsIgnoreCase(voiceLine, "BossKnight"))         return { "Knight", PlayerType::AIBoss };
    if (containsIgnoreCase(voiceLine, "Boss_Kaban"))         return { "Kaban", PlayerType::AIBoss };
    if (containsIgnoreCase(voiceLine, "Boss_Kollontay"))     return { "Kollontay", PlayerType::AIBoss };
    if (containsIgnoreCase(voiceLine, "Boss_Sturman"))       return { "Shturman", PlayerType::AIBoss };
    if (containsIgnoreCase(voiceLine, "black_division"))       return { "Black Division", PlayerType::AIRaider };
    if (containsIgnoreCase(voiceLine, "vsrf"))       return { "VSRF", PlayerType::AIRaider };
    if (containsIgnoreCase(voiceLine, "civilian"))       return { "Civilian", PlayerType::AIScav };

    //  Arena guards 
    if (containsIgnoreCase(voiceLine, "Arena_Guard"))        return { "Arena Guard", PlayerType::AIScav };

    //  Zombies 
    if (containsIgnoreCase(voiceLine, "BossZombieTagilla"))  return { "Zombie Tagilla", PlayerType::AIBoss };
    if (containsIgnoreCase(voiceLine, "Zombie"))             return { "Zombie", PlayerType::AIScav };

    //  Faction fallbacks 
    if (containsIgnoreCase(voiceLine, "usec"))               return { "Usec", PlayerType::AIRaider };
    if (containsIgnoreCase(voiceLine, "bear"))               return { "Bear", PlayerType::AIRaider };
    if (containsIgnoreCase(voiceLine, "scav"))               return { "Scav", PlayerType::AIScav };

    //  Final fallback 
    return { voiceLine, PlayerType::AIBoss };
}

std::optional<PlayerCache> Players::buildEntity(const uint64_t instance, bool isLocal)
{
    if (!mem.vHandle)
        return std::nullopt;

    if (!Utils::valid_pointer(instance))
        return std::nullopt;

    // Safer memory helpers
    auto TryReadValue = [&](uint64_t address, auto& out) -> bool
        {
            using T = std::decay_t<decltype(out)>;

            out = {};

            if (!Utils::valid_pointer(address))
                return false;

            try
            {
                return mem.Read(address, &out, sizeof(T));
            }
            catch (...)
            {
                return false;
            }
        };

    auto TryReadPtr = [&](uint64_t address, uint64_t& out) -> bool
        {
            out = 0;

            if (!TryReadValue(address, out))
                return false;

            return Utils::valid_pointer(out);
        };

    auto TryReadChain = [&](uint64_t base, std::initializer_list<uint64_t> offsets, uint64_t& out) -> bool
        {
            out = 0;

            if (!Utils::valid_pointer(base))
                return false;

            uint64_t current = base;

            for (uint64_t offset : offsets)
            {
                uint64_t next = 0;

                if (!Utils::valid_pointer(current))
                    return false;

                if (!TryReadPtr(current + offset, next))
                    return false;

                current = next;
            }

            out = current;
            return Utils::valid_pointer(out);
        };

    auto AddFailure = [](std::string& failed, const char* name)
        {
            if (!failed.empty())
                failed += ", ";

            failed += name;
        };

    auto ValidatePtr = [&](std::string& failed, uint64_t ptr, const char* name)
        {
            if (!Utils::valid_pointer(ptr))
                AddFailure(failed, name);
        };

    auto ValidateAddr = [&](std::string& failed, uint64_t addr, const char* name)
        {
            if (!Utils::valid_pointer(addr))
                AddFailure(failed, name);
        };

    auto ReadUnityStringSafe = [&](uint64_t stringPtr, int maxLen = 128) -> std::string
        {
            if (!Utils::valid_pointer(stringPtr))
                return {};

            int len = 0;

            if (!TryReadValue(stringPtr + 0x10, len))
                return {};

            if (len <= 0 || len > maxLen)
                return {};

            try
            {
                return mem.readUnicodeString(stringPtr + 0x14, len);
            }
            catch (...)
            {
                return {};
            }
        };

    auto LogInitFail = [&](const std::string& reason)
        {
            std::ostringstream ss;
            ss << "[PLAYER][INIT] Failed 0x"
                << std::hex << instance
                << " | " << reason;

            //LOGS.logError(ss.str());
        };

    // Start building entity
    PlayerCache newEntity{};

    try
    {
        newEntity.className = ReadName(instance, 64);
    }
    catch (...)
    {
        LogInitFail("ReadName threw exception");
        return std::nullopt;
    }

    if (newEntity.className.empty())
        return std::nullopt;

    newEntity.instance = instance;

    const bool isOfflineClass =
        newEntity.className == "LocalPlayer" ||
        newEntity.className == "ClientPlayer";

    // Offline / local style player
    if (isOfflineClass)
    {
        if (isLocal)
        {
            newEntity.isLocal = true;
            newEntity.name = "LocalPlayer";
        }
        else
        {
            newEntity.isAi = true;
            newEntity.name = "Ai";
        }

        std::string failed;

        if (!TryReadChain(instance, { sdk::Player::_playerBody, 0x30, 0x30, 0x10 }, newEntity.playerBoneMatrixPtr))
            AddFailure(failed, "BoneMatrixPtr");

        newEntity.P_CorpseAddr = instance + sdk::Player::Corpse;

        if (!TryReadPtr(instance + sdk::Player::Profile, newEntity.P_Profile))
            AddFailure(failed, "Profile");

        if (Utils::valid_pointer(newEntity.P_Profile))
        {
            if (!TryReadPtr(newEntity.P_Profile + sdk::Profile::Info, newEntity.P_Info))
                AddFailure(failed, "ProfileInfo");
        }

        TryReadPtr(instance + sdk::Player::ProceduralWeaponAnimation, newEntity.P_PWA);

        if (!TryReadPtr(instance + sdk::Player::_playerBody, newEntity.P_Body))
            AddFailure(failed, "PlayerBody");

        newEntity.P_InventoryControllerAddr = instance + sdk::Player::_inventoryController;
        newEntity.P_HandsControllerAddr = instance + sdk::Player::_handsController;

        if (Utils::valid_pointer(newEntity.P_Info))
        {
            if (!TryReadValue(newEntity.P_Info + sdk::PlayerInfo::Side, newEntity.playerSide))
                AddFailure(failed, "PlayerSide");
        }

        if (!TryReadPtr(instance + sdk::Player::MovementContext, newEntity.P_MovementContext))
            AddFailure(failed, "MovementContext");

        if (Utils::valid_pointer(newEntity.P_MovementContext))
            newEntity.P_RotationAddress = newEntity.P_MovementContext + sdk::MovementContext::_rotation;

        ValidateAddr(failed, newEntity.P_CorpseAddr, "CorpseAddr");
        ValidateAddr(failed, newEntity.P_InventoryControllerAddr, "InventoryControllerAddr");
        ValidateAddr(failed, newEntity.P_HandsControllerAddr, "HandsControllerAddr");
        ValidateAddr(failed, newEntity.P_RotationAddress, "RotationAddress");

        if (!failed.empty())
        {
            LogInitFail(failed);
            return std::nullopt;
        }

        if (newEntity.isLocal)
        {
            const bool isSavage =
                (static_cast<uint32_t>(newEntity.playerSide) &
                    static_cast<uint32_t>(EPlayerSide::Savage)) != 0;

            mainGame.localIsSavage = isSavage;

            if (isSavage)
            {
                newEntity.isPlayer = false;
                newEntity.isPlayerScav = true;
            }
            else
            {
                newEntity.isPlayer = true;
                newEntity.isPlayerScav = false;
            }

            mainGame.localplayerProfile = newEntity.P_Profile;

            try
            {
                questManager.initQuestManager();
            }
            catch (...)
            {
                LOGS.logError("[PLAYER][INIT] questManager.initQuestManager failed");
            }
        }
    }

    // Online observed player
    else
    {
        std::string failed;

        if (!TryReadChain(instance, { sdk::ObservedPlayerView::PlayerBody, 0x30, 0x30, 0x10 }, newEntity.playerBoneMatrixPtr))
            AddFailure(failed, "BoneMatrixPtr");

        if (!TryReadPtr(instance + sdk::ObservedPlayerView::ObservedPlayerController, newEntity.P_ObservedPlayerController))
            AddFailure(failed, "ObservedPlayerController");

        if (Utils::valid_pointer(newEntity.P_ObservedPlayerController))
        {
            if (!TryReadPtr(newEntity.P_ObservedPlayerController + sdk::ObservedPlayerController::HealthController, newEntity.P_ObservedHealthController))
                AddFailure(failed, "HealthController");

            newEntity.P_InventoryControllerAddr =
                newEntity.P_ObservedPlayerController + sdk::ObservedPlayerController::InventoryController;

            newEntity.P_HandsControllerAddr =
                newEntity.P_ObservedPlayerController + sdk::ObservedPlayerController::HandsController;

            if (!TryReadChain(
                newEntity.P_ObservedPlayerController,
                {
                    sdk::ObservedPlayerController::MovementController,
                    sdk::ObservedMovementController::ObservedPlayerStateContext
                },
                newEntity.P_MovementContext))
            {
                AddFailure(failed, "MovementContext");
            }
        }

        if (Utils::valid_pointer(newEntity.P_ObservedHealthController))
            newEntity.P_CorpseAddr = newEntity.P_ObservedHealthController + sdk::ObservedHealthController::PlayerCorpse;

        if (Utils::valid_pointer(newEntity.P_MovementContext))
            newEntity.P_RotationAddress = newEntity.P_MovementContext + sdk::ObservedPlayerStateContext::Rotation;

        ValidatePtr(failed, newEntity.playerBoneMatrixPtr, "BoneMatrixPtr");
        ValidatePtr(failed, newEntity.P_ObservedPlayerController, "ObservedPlayerController");
        ValidatePtr(failed, newEntity.P_ObservedHealthController, "HealthController");

        ValidateAddr(failed, newEntity.P_CorpseAddr, "CorpseAddr");
        ValidateAddr(failed, newEntity.P_InventoryControllerAddr, "InventoryControllerAddr");
        ValidateAddr(failed, newEntity.P_HandsControllerAddr, "HandsControllerAddr");
        ValidatePtr(failed, newEntity.P_MovementContext, "MovementContext");
        ValidateAddr(failed, newEntity.P_RotationAddress, "RotationAddress");

        if (!failed.empty())
        {
            LogInitFail(failed);
            return std::nullopt;
        }

        if (!TryReadValue(instance + sdk::ObservedPlayerView::IsAI, newEntity.isAi))
        {
            LogInitFail("IsAI read failed");
            return std::nullopt;
        }

        newEntity.isPlayer = !newEntity.isAi;

        if (!TryReadValue(instance + sdk::ObservedPlayerView::Side, newEntity.playerSide))
        {
            LogInitFail("Side read failed");
            return std::nullopt;
        }

        newEntity.side = SideToString(newEntity.playerSide);

        const bool isSavage =
            (static_cast<uint32_t>(newEntity.playerSide) &
                static_cast<uint32_t>(EPlayerSide::Savage)) != 0;

        if (isSavage)
        {
            if (newEntity.isAi)
            {
                uint64_t voicePtr = 0;
                TryReadPtr(instance + sdk::ObservedPlayerView::Voice, voicePtr);

                const std::string voice = ReadUnityStringSafe(voicePtr, 128);

                AIRole role = GetAIRoleInfo(voice);

                if (!role.Name.empty())
                    newEntity.name = role.Name;
                else
                    newEntity.name = "Ai";

                if (role.Type == PlayerType::AIBoss)
                    newEntity.isBoss = true;

                newEntity.isPlayerScav = false;
                newEntity.isAi = true;
                newEntity.isPlayer = false;
            }
            else
            {
                newEntity.name = "PScav " + std::to_string(mainGame.pmcNumber);
                mainGame.pmcNumber++;

                newEntity.isPlayerScav = true;
                newEntity.isAi = false;

                newEntity.isPlayer = true;
            }
        }
        else
        {
            newEntity.name = "PMC " + std::to_string(mainGame.pmcNumber);
            mainGame.pmcNumber++;

            newEntity.isPlayerScav = false;
            newEntity.isAi = false;
            newEntity.isPlayer = true;
        }
    }

    // Bone pointer setup
    if (!newEntity.isBTR)
    {
        try
        {
            newEntity = getBonePtrs(newEntity);
        }
        catch (...)
        {
            LogInitFail("getBonePtrs threw exception");
            return std::nullopt;
        }
    }

    // Final sanity check.
    if (!Utils::valid_pointer(newEntity.instance))
        return std::nullopt;

    return newEntity;
}

glm::vec3 GetBestPlayerBasePosition(const PlayerCache& cachePlayer)
{
    const glm::vec3 zero(0.0f);

    const glm::vec3 root = cachePlayer.bonePositions[boneListIndexes::Base];
    const glm::vec3 lFoot = cachePlayer.bonePositions[boneListIndexes::LFoot];
    const glm::vec3 rFoot = cachePlayer.bonePositions[boneListIndexes::RFoot];

    auto isNearlyZeroVec = [](const glm::vec3& v) -> bool
        {
            constexpr float eps = 0.001f;
            return std::fabs(v.x) < eps &&
                std::fabs(v.y) < eps &&
                std::fabs(v.z) < eps;
        };

    auto isValidVec = [&](const glm::vec3& v) -> bool
        {
            if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
                return false;

            if (isNearlyZeroVec(v))
                return false;

            return true;
        };

    auto distanceXZ = [](const glm::vec3& a, const glm::vec3& b) -> float
        {
            const float dx = a.x - b.x;
            const float dz = a.z - b.z;
            return std::sqrt((dx * dx) + (dz * dz));
        };

    const bool rootValid = isValidVec(root);
    const bool lFootValid = isValidVec(lFoot);
    const bool rFootValid = isValidVec(rFoot);

    glm::vec3 feetMid = zero;
    bool feetValid = false;

    if (lFootValid && rFootValid)
    {
        //Feet should not be miles apart
        const float footSeparation = glm::distance(lFoot, rFoot);
        if (footSeparation > 0.01f && footSeparation <= 5.5f)
        {
            feetMid = (lFoot + rFoot) * 0.5f;
            feetValid = true;
        }
    }

    if (!rootValid && !feetValid)
        return zero;

    if (!rootValid && feetValid)
        return feetMid;

    if (rootValid && !feetValid)
        return root;

    //Compare horizontal distance only
    const float rootToFeetXZ = distanceXZ(root, feetMid);

    //If root is close horizontally, trust it
    if (rootToFeetXZ <= 5.0f)
        return root;

    //Otherwise root is probably stale/broken
    return feetMid;
}

void Players::tryFindBTR()
{
    

    if (!mem.vHandle)
        return;

    std::string selectedMap = TrimEFT(mainGame.selectedLocation);

    std::transform(
        selectedMap.begin(),
        selectedMap.end(),
        selectedMap.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });

    if (selectedMap != "tarkovstreets" && selectedMap != "woods")
        return;

    if (!Utils::valid_pointer(mainGame.localGameWorld))
        return;

    // Safe read helpers
    auto TryReadValue = [&](uint64_t address, auto& out) -> bool
        {
            using T = std::decay_t<decltype(out)>;

            out = {};

            if (!Utils::valid_pointer(address))
                return false;

            try
            {
                return mem.Read(address, &out, sizeof(T));
            }
            catch (...)
            {
                return false;
            }
        };

    auto TryReadPtr = [&](uint64_t address, uint64_t& out) -> bool
        {
            out = 0;

            if (!TryReadValue(address, out))
                return false;

            return Utils::valid_pointer(out);
        };

    // localGameWorld -> btrController -> btrView -> turret -> attachedBot
    uint64_t btrController = 0;
    uint64_t btrView = 0;
    uint64_t btrTurret = 0;
    uint64_t btrOper = 0;

    if (!TryReadPtr(
        mainGame.localGameWorld + sdk::ClientLocalGameWorld::btrController,
        btrController))
    {
        return;
    }

    if (!TryReadPtr(
        btrController + sdk::BtrController::BtrView,
        btrView))
    {
        return;
    }

    if (!TryReadPtr(
        btrView + sdk::BTRView::turret,
        btrTurret))
    {
        return;
    }

    if (!TryReadPtr(
        btrTurret + sdk::BTRTurretView::attachedBot,
        btrOper))
    {
        return;
    }

    std::vector<PlayerCache>& cache = players.getCache();

    if (cache.empty())
        return;

    // Find the AI/player cache entry matching attachedBot
    for (auto& cachePlayer : cache)
    {
        if (!Utils::valid_pointer(cachePlayer.instance))
            continue;

        if (cachePlayer.instance != btrOper)
            continue;

        
        if (cachePlayer.isLocal || cachePlayer.isPlayer || cachePlayer.isPlayerScav)
            return;

        const bool wasAlreadyBTR = cachePlayer.isBTR;
        const uint64_t oldBtrView = cachePlayer.btrView;

        cachePlayer.isBTR = true;
        cachePlayer.isAi = true;
        cachePlayer.isBoss = false;
        cachePlayer.isPlayer = false;
        cachePlayer.isPlayerScav = false;

        cachePlayer.colour = coloursGlobals::aiBTR;
        cachePlayer.btrView = btrView;
        cachePlayer.name = "BTR";

        glm::vec3 btrPosition{};

        if (TryReadValue(btrView + sdk::BTRView::previousPosition, btrPosition))
        {
            cachePlayer.location = btrPosition;
            cachePlayer.distance = getDistance(cachePlayer.location, mainGame.localLocation);
        }

        if (!mainGame.btrAllocated || !wasAlreadyBTR || oldBtrView != btrView)
        {
            mainGame.btrAllocated = true;

            std::ostringstream ss;
            ss << "[BTR] BTR Allocated | operator: 0x"
                << std::hex << btrOper
                << " view: 0x"
                << btrView;

            LOGS.logInfo(ss.str());
        }

        return;
    }
}

void Players::updateEntity()
{
    if (!mem.vHandle)
        return;

    std::vector<PlayerCache>& cache = players.getCache();

    if (cache.empty())
        return;

    // Safer memory helpers
    auto TryReadValue = [&](uint64_t address, auto& out) -> bool
        {
            using T = std::decay_t<decltype(out)>;

            out = {};

            if (!Utils::valid_pointer(address))
                return false;

            try
            {
                return mem.Read(address, &out, sizeof(T));
            }
            catch (...)
            {
                return false;
            }
        };

    auto AddSafeScatter = [&](auto handle, uint64_t address, void* out, size_t size) -> bool
        {
            if (!handle)
                return false;

            if (!Utils::valid_pointer(address))
                return false;

            if (!out || size == 0)
                return false;

            try
            {
                mem.AddScatterReadRequest(handle, address, out, size);
                return true;
            }
            catch (...)
            {
                return false;
            }
        };

    auto ExecuteAndClose = [&](auto& handle, const char* name)
        {
            if (!handle)
                return;

            try
            {
                mem.ExecuteReadScatter(handle);
            }
            catch (...)
            {
                LOGS.logError(std::string("[PLAYERS][UPDATE] Scatter execute failed: ") + name);
            }

            try
            {
                mem.CloseScatterHandle(handle);
            }
            catch (...)
            {
                LOGS.logError(std::string("[PLAYERS][UPDATE] Scatter close failed: ") + name);
            }

            handle = {};
        };

    auto ApplyPlayerColour = [&](PlayerCache& cachePlayer)
        {
            cachePlayer.colour = { 1, 1, 1, 1 };

            if (cachePlayer.isDead)
            {
                cachePlayer.colour = coloursGlobals::playerCorpse;
                return;
            }

            if (cachePlayer.isAi && !cachePlayer.isPlayerScav && !cachePlayer.isPlayer)
            {
                cachePlayer.colour = coloursGlobals::playerAI;
            }

            if (cachePlayer.isPlayerScav && !cachePlayer.isAi && cachePlayer.isPlayer)
            {
                cachePlayer.colour = coloursGlobals::playerScav;
            }

            if (cachePlayer.isBoss)
            {
                cachePlayer.colour = coloursGlobals::playerBoss;
            }

            if (cachePlayer.isPlayer && !cachePlayer.isPlayerScav && !cachePlayer.isAi)
            {
                cachePlayer.colour = coloursGlobals::playerPMC;
            }

            if (cachePlayer.isWatched)
            {
                cachePlayer.colour = coloursGlobals::playerWatched;
            }

            if (!mainGame.localGroupId.empty() &&
                cachePlayer.groupId == mainGame.localGroupId)
            {
                cachePlayer.colour = coloursGlobals::playerFriendly;
            }

            if (cachePlayer.isLocal &&
                Utils::valid_pointer(cachePlayer.instance) &&
                mainGame.localPlayerPtr == cachePlayer.instance)
            {
                cachePlayer.colour = coloursGlobals::playerLocal;
            }
        };

    // Create scatter handles
    auto handleRotation = mem.CreateScatterHandle();
    auto handleCorpse = mem.CreateScatterHandle();
    auto handleHealth = mem.CreateScatterHandle();
    auto handleHands = mem.CreateScatterHandle();
    auto handleIsAiming = mem.CreateScatterHandle();

    //queue memory reads
    for (auto& cachePlayer : cache)
    {
        if (cachePlayer.isBTR)
        {
            if (Utils::valid_pointer(cachePlayer.btrView))
            {
                glm::vec3 btrPos{};

                if (TryReadValue(cachePlayer.btrView + sdk::BTRView::previousPosition, btrPos))
                {
                    cachePlayer.location = btrPos;
                }
            }

            continue;
        }

        if (cachePlayer.isDead || cachePlayer.hasExfiled)
            continue;

        if (!Utils::valid_pointer(cachePlayer.instance))
            continue;

        cachePlayer.P_CorpseClass = 0;

        if (cachePlayer.isLocal)
        {
            mainGame.localGroupId = cachePlayer.groupId;
            mainGame.localPlayerHands = cachePlayer.P_HandsController;
            mainGame.localplayerProfile = cachePlayer.P_Profile;
            mainGame.localPlayerPWA = cachePlayer.P_PWA;
        }

        const bool isOfflinePlayer =
            cachePlayer.className == "LocalPlayer" ||
            cachePlayer.className == "ClientPlayer";

        AddSafeScatter(
            handleRotation,
            cachePlayer.P_RotationAddress,
            &cachePlayer.rotationRAW,
            sizeof(glm::vec2)
        );

        AddSafeScatter(
            handleCorpse,
            cachePlayer.P_CorpseAddr,
            &cachePlayer.P_CorpseClass,
            sizeof(uint64_t)
        );

        AddSafeScatter(
            handleHands,
            cachePlayer.P_HandsControllerAddr,
            &cachePlayer.P_HandsController,
            sizeof(uint64_t)
        );

        if (isOfflinePlayer)
        {
            cachePlayer.isAiming = false;

            if (Utils::valid_pointer(cachePlayer.P_PWA))
            {
                AddSafeScatter(
                    handleIsAiming,
                    cachePlayer.P_PWA + sdk::ProceduralWeaponAnimation::_isAiming,
                    &cachePlayer.isAiming,
                    sizeof(bool)
                );
            }
        }
        else
        {
            if (Utils::valid_pointer(cachePlayer.P_ObservedHealthController))
            {
                AddSafeScatter(
                    handleHealth,
                    cachePlayer.P_ObservedHealthController + sdk::ObservedHealthController::HealthStatus,
                    &cachePlayer.healthETAG,
                    sizeof(ETagStatus)
                );
            }
        }
    }

    // Execute scatters
    ExecuteAndClose(handleRotation, "Rotation");
    ExecuteAndClose(handleCorpse, "Corpse");
    ExecuteAndClose(handleHealth, "Health");
    ExecuteAndClose(handleHands, "Hands");
    ExecuteAndClose(handleIsAiming, "IsAiming");

    //process updated values
    for (auto& cachePlayer : cache)
    {
        // BTR only
        if (cachePlayer.isBTR)
        {
            cachePlayer.colour = coloursGlobals::aiBTR;
            cachePlayer.distance = getDistance(cachePlayer.location, mainGame.localLocation);
            continue;
        }

        // Already dead / exfiled
        if (cachePlayer.isDead || cachePlayer.hasExfiled)
        {
            cachePlayer.distance = getDistance(cachePlayer.location, mainGame.localLocation);
            ApplyPlayerColour(cachePlayer);
            continue;
        }

        // Corpse check early
        if (Utils::valid_pointer(cachePlayer.P_CorpseClass))
        {
            cachePlayer.isDead = true;
            cachePlayer.instance = 0x0;

            cachePlayer.distance = getDistance(cachePlayer.location, mainGame.localLocation);
            ApplyPlayerColour(cachePlayer);
            continue;
        }

        if (!Utils::valid_pointer(cachePlayer.instance))
            continue;

        // Position
        try
        {
            cachePlayer.location = GetBestPlayerBasePosition(cachePlayer);
        }
        catch (...)
        {
            LOGS.logError("[PLAYERS][UPDATE] GetBestPlayerBasePosition failed");
        }

        if (cachePlayer.isLocal)
            mainGame.localLocation = cachePlayer.location;

        cachePlayer.distance = getDistance(cachePlayer.location, mainGame.localLocation);

        // Held item
        try
        {
            

            if (Utils::valid_pointer(cachePlayer.P_HandsController))
            {
                cachePlayer.observedHandsInfo.update(cachePlayer);
            }
            else
            {
                cachePlayer.itemInHand.clear();
            }
        }
        catch (...)
        {
            cachePlayer.itemInHand.clear();
            LOGS.logError("[PLAYERS][UPDATE] heldItemName failed");
        }

        // Rotation
        try
        {
            cachePlayer.rotation =
                Utils::Player::Rotation::correctRotation2d(cachePlayer.rotationRAW);
        }
        catch (...)
        {
            cachePlayer.rotation = {};
            LOGS.logError("[PLAYERS][UPDATE] Rotation correction failed");
        }

        // Dogtag cache / profile data
        if (cachePlayer.isPlayer && !cachePlayer.profileId.empty())
        {
            auto now = std::chrono::steady_clock::now();

            if (!cachePlayer.foundDogTagCache &&
                cachePlayer.name.contains("PMC") &&
                now - cachePlayer.lastDogTagLookup > std::chrono::seconds(5))
            {
                cachePlayer.lastDogTagLookup = now;

                try
                {
                    auto result = g_dogTagCache.GetByProfileId(cachePlayer.profileId);

                    if (result.has_value())
                    {
                        if (!result->nickname.empty())
                            cachePlayer.name = result->nickname;

                        cachePlayer.accountId = result->accountId;
                        cachePlayer.foundDogTagCache = true;
                    }
                }
                catch (...)
                {
                    LOGS.logError("[PLAYERS][UPDATE] Dogtag cache lookup failed");
                }
            }

            if (!cachePlayer.hasProfileData &&
                !cachePlayer.accountId.empty() &&
                radarGlobals::getPlayerStats == TRUE &&
                !cachePlayer.triedprofileonce)
            {
                cachePlayer.triedprofileonce = true;

                try
                {
                    auto profile =
                        TarkovDevProfileClient::GetProfileForAccountId(cachePlayer.accountId);

                    if (profile)
                    {
                        cachePlayer.profileStats = *profile;
                        cachePlayer.hasProfileData = true;

                        cachePlayer.DT_lvl = ConvertXpToLevel(profile->experience);
                        cachePlayer.kd = CalculateKD(profile->Kills, profile->deathsPMC);
                        cachePlayer.pkd = CalculatePKD(profile->killedPMC, profile->deathsPMC);
                        cachePlayer.hours = profile->hoursPlayed;
                    }
                }
                catch (...)
                {
                    LOGS.logError("[PLAYERS][UPDATE] Tarkov profile lookup failed");
                }
            }
        }

        // Colours
        ApplyPlayerColour(cachePlayer);

        // Local player final update
        if (cachePlayer.isLocal &&
            Utils::valid_pointer(cachePlayer.instance) &&
            mainGame.localPlayerPtr == cachePlayer.instance)
        {
            mainGame.localLocation = cachePlayer.location;
            mainGame.localRotation = cachePlayer.rotation;
            mainGame.localGroupId = cachePlayer.groupId;
            mainGame.localPlayerHands = cachePlayer.P_HandsController;
            mainGame.localIsScoped = cachePlayer.isAiming;
            mainGame.localPlayerPWA = cachePlayer.P_PWA;

            cachePlayer.colour = coloursGlobals::playerLocal;
        }
    }
}

void Players::checkGroupIDs()
{

    if (groupIDSet)
        return;

    if (!mem.vHandle)
        return;

    // Do not group until local has actually spawned in.
    // localPlayerHands must not be ClientEmptyHandsController.
    if (!mainGame.checkIfRaidStarted())
        return;

    auto& cache = players.getCache();

    if (cache.empty())
        return;

    constexpr float GROUP_DISTANCE_METERS = 15.0f;

    auto hasValidBone = [&](const PlayerCache& player) -> bool
        {
            return isValidBoneVector(player.bonePositions[allPlayerBones::HumanBase]);
        };

    auto isValidGroupId = [](const std::string& groupId) -> bool
        {
            // Treat blank and 0 as unassigned.
            return !groupId.empty() && groupId != "0";
        };

    auto isValidGroupingTarget = [&](const PlayerCache& player) -> bool
        {
            if (player.isBTR)
                return false;

            if (player.isDead || player.hasExfiled)
                return false;

            if (!Utils::valid_pointer(player.instance))
                return false;

            if (!(player.isPlayer || player.isPlayerScav))
                return false;

            if (!hasValidBone(player))
                return false;

            return true;
        };

    // Build list of valid player indexes
    std::vector<size_t> targets;
    targets.reserve(cache.size());

    int localTargetIndex = -1;

    for (size_t i = 0; i < cache.size(); ++i)
    {
        PlayerCache& player = cache[i];

        if (!isValidGroupingTarget(player))
            continue;

        const int targetIndex = static_cast<int>(targets.size());
        targets.emplace_back(i);

        if (player.isLocal && player.instance == mainGame.localPlayerPtr)
            localTargetIndex = targetIndex;
    }

    // Need local to be fully valid before doing the one-time spawn grouping.
    if (localTargetIndex < 0)
        return;

    // Need at least 2 valid players to create any group.
    if (targets.size() < 2)
        return;

    // Unionfind setup
    std::vector<int> parent(targets.size());

    for (int i = 0; i < static_cast<int>(parent.size()); ++i)
        parent[i] = i;

    auto Find = [&](int x) -> int
        {
            while (parent[x] != x)
            {
                parent[x] = parent[parent[x]];
                x = parent[x];
            }

            return x;
        };

    auto Union = [&](int a, int b)
        {
            int rootA = Find(a);
            int rootB = Find(b);

            if (rootA == rootB)
                return;

            const int localRoot = Find(localTargetIndex);

            // If one side is locals component, keep local as the root.
            if (rootA == localRoot)
            {
                parent[rootB] = rootA;
            }
            else if (rootB == localRoot)
            {
                parent[rootA] = rootB;
            }
            else
            {
                parent[rootB] = rootA;
            }
        };

    // ---------------------------------------------------------
    // Join players that are within group distance
    // This groups local and non-local teams.
    // It also handles chained grouping:
    // A close to B, B close to C => A/B/C same group.
    // ---------------------------------------------------------
    bool foundAnyPair = false;

    for (int a = 0; a < static_cast<int>(targets.size()); ++a)
    {
        PlayerCache& playerA = cache[targets[a]];

        const glm::vec3 posA =
            playerA.bonePositions[allPlayerBones::HumanBase];

        for (int b = a + 1; b < static_cast<int>(targets.size()); ++b)
        {
            PlayerCache& playerB = cache[targets[b]];

            if (playerA.instance == playerB.instance)
                continue;

            const glm::vec3 posB =
                playerB.bonePositions[allPlayerBones::HumanBase];

            const float dist = players.getDistance(posA, posB);

            if (!std::isfinite(dist))
                continue;

            if (dist > GROUP_DISTANCE_METERS)
                continue;

            Union(a, b);
            foundAnyPair = true;
        }
    }

    if (!foundAnyPair)
        return;

    // Count how many players are in each connected component
    std::unordered_map<int, int> componentCounts;

    for (int i = 0; i < static_cast<int>(targets.size()); ++i)
    {
        const int root = Find(i);
        componentCounts[root]++;
    }

    const int localRoot = Find(localTargetIndex);

    // Build component -> group id map
    std::unordered_map<int, std::string> componentGroupIds;

    int nextGroupId = 1;

    auto getNextGroupId = [&]() -> std::string
        {
            return std::to_string(nextGroupId++);
        };

    PlayerCache& localPlayer = cache[targets[localTargetIndex]];

    if (componentCounts[localRoot] >= 2)
    {
        if (!isValidGroupId(mainGame.localGroupId))
        {
            if (isValidGroupId(localPlayer.groupId))
                mainGame.localGroupId = localPlayer.groupId;
            else
                mainGame.localGroupId = getNextGroupId();
        }

        componentGroupIds[localRoot] = mainGame.localGroupId;
    }

    // Preserve valid existing group IDs for non-local grouped components.
    for (int i = 0; i < static_cast<int>(targets.size()); ++i)
    {
        const int root = Find(i);

        // Do not assign group ids to solo players.
        if (componentCounts[root] < 2)
            continue;

        if (root == localRoot)
            continue;

        PlayerCache& player = cache[targets[i]];

        if (isValidGroupId(player.groupId))
        {
            if (componentGroupIds.find(root) == componentGroupIds.end())
                componentGroupIds[root] = player.groupId;
        }
    }

    // Assign new group IDs to grouped components that do not have one.
    for (int i = 0; i < static_cast<int>(targets.size()); ++i)
    {
        const int root = Find(i);

        // Do not assign group ids to solo players.
        if (componentCounts[root] < 2)
            continue;

        if (componentGroupIds.find(root) == componentGroupIds.end())
            componentGroupIds[root] = getNextGroupId();
    }

    // Apply group IDs
    bool assignedAnyGroup = false;

    for (int i = 0; i < static_cast<int>(targets.size()); ++i)
    {
        const int root = Find(i);

        // Do not assign fake group ids to solo players.
        if (componentCounts[root] < 2)
            continue;

        PlayerCache& player = cache[targets[i]];

        auto groupIt = componentGroupIds.find(root);
        if (groupIt == componentGroupIds.end())
            continue;

        const std::string& newGroupId = groupIt->second;

        if (player.groupId != newGroupId)
        {
            player.groupId = newGroupId;
            assignedAnyGroup = true;
        }
    }

    // Final sync for local player if local has a real group
    if (componentCounts[localRoot] >= 2)
    {
        auto localGroupIt = componentGroupIds.find(localRoot);

        if (localGroupIt != componentGroupIds.end())
        {
            mainGame.localGroupId = localGroupIt->second;

            if (localPlayer.groupId != mainGame.localGroupId)
            {
                localPlayer.groupId = mainGame.localGroupId;
                assignedAnyGroup = true;
            }
        }
    }

    if (assignedAnyGroup)
    {
        groupIDSet = true;

        std::ostringstream ss;
        ss << "[PLAYERS][GROUP] Group IDs assigned. "
            << "LocalGroupId: "
            << (mainGame.localGroupId.empty() ? "none" : mainGame.localGroupId)
            << " | validTargets: "
            << targets.size();

        LOGS.logInfo(ss.str());
    }
}

static const std::unordered_set<std::string> skipNames =
{
    "Compass",
    "ArmBand",
    "Eyewear",
    "Pockets"
};

void Players::playerEquipment()
{
    if (!radarGlobals::getPlayerEquip)
        return;

    using Clock = std::chrono::steady_clock;
    using SlotVec = std::remove_reference_t<decltype(std::declval<PlayerCache&>()._slots)>;
    using SlotEntry = typename SlotVec::value_type;
    using PlayerValueT = std::remove_reference_t<decltype(std::declval<PlayerCache&>().playerValue)>;

    struct InitJob
    {
        uint64_t instance = 0;
        uint64_t inventoryControllerAddr = 0;

        uint64_t inventoryController = 0;
        uint64_t inventory = 0;
        uint64_t equipment = 0;
        uint64_t slotsPtr = 0;
    };

    struct InitResult
    {
        uint64_t instance = 0;
        SlotVec slots;
        bool success = false;
    };

    struct ScanJob
    {
        uint64_t instance = 0;
        bool isPlayer = false;
        std::string profileId;
        SlotVec slots;
        Clock::time_point updateTime{};
    };

    struct SlotRead
    {
        size_t jobIndex = 0;
        size_t slotIndex = 0;

        uint64_t containedItem = 0;
        uint64_t itemTemplate = 0;
        MongoID mongoId{};
    };

    struct ScanResult
    {
        uint64_t instance = 0;
        SlotVec slots;
        PlayerValueT playerValue{};
        Clock::time_point updateTime{};

        bool hasProfileUpdate = false;
        std::string profileId;
        std::string accountId;
        std::string nickname;
    };

    auto findPlayerByInstance = [](std::vector<PlayerCache>& cache, uint64_t instance) -> PlayerCache*
        {
            for (auto& player : cache)
            {
                if (player.instance == instance)
                    return &player;
            }

            return nullptr;
        };

    auto executeScatter = [](auto&& queueReads)
        {
            auto handle = mem.CreateScatterHandle();

            if (!handle)
                return false;

            queueReads(handle);

            const bool ok = mem.ExecuteReadScatter(handle);
            mem.CloseScatterHandle(handle);

            return ok;
        };

    try
    {
        // ------------------------------------------------------------
        // 1) Build init jobs under one short lock
        // ------------------------------------------------------------
        std::vector<InitJob> initJobs;

        {
            std::lock_guard<std::mutex> lock(playerMutex);

            std::vector<PlayerCache>& cache = players.getCache();

            initJobs.reserve(cache.size());

            for (const auto& player : cache)
            {
                if (!Utils::valid_pointer(player.instance))
                    continue;

                if (player.isDead || player.hasExfiled)
                    continue;

                if (player.equipInited)
                    continue;

                if (!Utils::valid_pointer(player.P_InventoryControllerAddr))
                    continue;

                InitJob job{};
                job.instance = player.instance;
                job.inventoryControllerAddr = player.P_InventoryControllerAddr;

                initJobs.push_back(job);
            }
        }

        // ------------------------------------------------------------
        // 2) Scatter inventoryController
        // ------------------------------------------------------------
        if (!initJobs.empty())
        {
            executeScatter([&](auto handle)
                {
                    for (auto& job : initJobs)
                    {
                        if (!Utils::valid_pointer(job.inventoryControllerAddr))
                            continue;

                        mem.AddScatterReadRequest(
                            handle,
                            job.inventoryControllerAddr,
                            &job.inventoryController,
                            sizeof(job.inventoryController)
                        );
                    }
                });

            // --------------------------------------------------------
            // 3) Scatter inventory
            // --------------------------------------------------------
            executeScatter([&](auto handle)
                {
                    for (auto& job : initJobs)
                    {
                        if (!Utils::valid_pointer(job.inventoryController))
                            continue;

                        mem.AddScatterReadRequest(
                            handle,
                            job.inventoryController + sdk::InventoryController::Inventory,
                            &job.inventory,
                            sizeof(job.inventory)
                        );
                    }
                });

            // --------------------------------------------------------
            // 4) Scatter equipment
            // --------------------------------------------------------
            executeScatter([&](auto handle)
                {
                    for (auto& job : initJobs)
                    {
                        if (!Utils::valid_pointer(job.inventory))
                            continue;

                        mem.AddScatterReadRequest(
                            handle,
                            job.inventory + sdk::Inventory::Equipment,
                            &job.equipment,
                            sizeof(job.equipment)
                        );
                    }
                });

            // --------------------------------------------------------
            // 5) Scatter slots array pointer
            // --------------------------------------------------------
            executeScatter([&](auto handle)
                {
                    for (auto& job : initJobs)
                    {
                        if (!Utils::valid_pointer(job.equipment))
                            continue;

                        mem.AddScatterReadRequest(
                            handle,
                            job.equipment + sdk::InventoryEquipment::_cachedSlots,
                            &job.slotsPtr,
                            sizeof(job.slotsPtr)
                        );
                    }
                });
        }

        // ------------------------------------------------------------
        // 6) Build slot init results
        //    This still uses normal string reads because slot name strings
        //    are variable-length. You can scatter this too later, but this
        //    keeps it much simpler and safer.
        // ------------------------------------------------------------
        std::vector<InitResult> initResults;
        initResults.reserve(initJobs.size());

        for (const auto& job : initJobs)
        {
            InitResult result{};
            result.instance = job.instance;

            if (!Utils::valid_pointer(job.slotsPtr))
                continue;

            UnityArray<uint64_t> slotsArray(job.slotsPtr);

            if (slotsArray.count < 1 || slotsArray.count > 128)
                continue;

            for (auto& slotPtr : slotsArray)
            {
                if (!Utils::valid_pointer(slotPtr))
                    continue;

                uint64_t namePtr = mem.Read<uint64_t>(slotPtr + sdk::Slot::ID);

                if (!Utils::valid_pointer(namePtr))
                    continue;

                int nameLen = mem.Read<int>(namePtr + 0x10);

                if (nameLen <= 0 || nameLen > 128)
                    continue;

                std::string name = mem.readUnicodeString(namePtr + 0x14, nameLen);

                if (name.empty())
                    continue;

                if (skipNames.contains(name))
                    continue;

                result.slots.push_back({ name, slotPtr });
            }

            if (!result.slots.empty())
            {
                result.success = true;
                initResults.push_back(std::move(result));
            }
        }

        // ------------------------------------------------------------
        // 7) Apply init results under one short lock
        // ------------------------------------------------------------
        {
            std::lock_guard<std::mutex> lock(playerMutex);

            std::vector<PlayerCache>& cache = players.getCache();

            for (auto& result : initResults)
            {
                PlayerCache* player = findPlayerByInstance(cache, result.instance);

                if (!player)
                    continue;

                if (!Utils::valid_pointer(player->instance))
                    continue;

                if (player->isDead || player->hasExfiled)
                    continue;

                if (player->equipInited)
                    continue;

                if (!result.success || result.slots.empty())
                    continue;

                player->_slots = std::move(result.slots);
                player->equipInited = true;
            }
        }

        // ------------------------------------------------------------
        // 8) Build scan jobs under one short lock
        // ------------------------------------------------------------
        std::vector<ScanJob> scanJobs;

        {
            std::lock_guard<std::mutex> lock(playerMutex);

            std::vector<PlayerCache>& cache = players.getCache();

            scanJobs.reserve(cache.size());

            const auto now = Clock::now();

            for (const auto& player : cache)
            {
                if (!Utils::valid_pointer(player.instance))
                    continue;

                if (player.isDead || player.hasExfiled)
                    continue;

                if (!player.equipInited)
                    continue;

                if (player._slots.empty())
                    continue;

                if (now - player.lastEquipmentUpdate < player.equipmentUpdateInterval)
                    continue;

                ScanJob job{};
                job.instance = player.instance;
                job.isPlayer = player.isPlayer;
                job.profileId = player.profileId;
                job.slots = player._slots;
                job.updateTime = now;

                scanJobs.push_back(std::move(job));
            }
        }

        if (scanJobs.empty())
            return;

        // ------------------------------------------------------------
        // 9) Flatten slots for scatter reading
        // ------------------------------------------------------------
        std::vector<SlotRead> slotReads;

        for (size_t jobIndex = 0; jobIndex < scanJobs.size(); ++jobIndex)
        {
            auto& job = scanJobs[jobIndex];

            for (size_t slotIndex = 0; slotIndex < job.slots.size(); ++slotIndex)
            {
                const auto& slot = job.slots[slotIndex];

                if (job.isPlayer && slot.name == "Scabbard")
                    continue;

                if (!Utils::valid_pointer(slot.addr))
                    continue;

                SlotRead sr{};
                sr.jobIndex = jobIndex;
                sr.slotIndex = slotIndex;

                slotReads.push_back(sr);
            }
        }

        // ------------------------------------------------------------
        // 10) Scatter ContainedItem for every slot
        // ------------------------------------------------------------
        executeScatter([&](auto handle)
            {
                for (auto& sr : slotReads)
                {
                    const auto& job = scanJobs[sr.jobIndex];
                    const auto& slot = job.slots[sr.slotIndex];

                    if (!Utils::valid_pointer(slot.addr))
                        continue;

                    mem.AddScatterReadRequest(
                        handle,
                        slot.addr + sdk::Slot::ContainedItem,
                        &sr.containedItem,
                        sizeof(sr.containedItem)
                    );
                }
            });

        // ------------------------------------------------------------
        // 11) Scatter Template pointer for every contained item
        // ------------------------------------------------------------
        executeScatter([&](auto handle)
            {
                for (auto& sr : slotReads)
                {
                    if (!Utils::valid_pointer(sr.containedItem))
                        continue;

                    mem.AddScatterReadRequest(
                        handle,
                        sr.containedItem + sdk::LootItem::Template,
                        &sr.itemTemplate,
                        sizeof(sr.itemTemplate)
                    );
                }
            });

        // ------------------------------------------------------------
        // 12) Scatter MongoID for every item template
        // ------------------------------------------------------------
        executeScatter([&](auto handle)
            {
                for (auto& sr : slotReads)
                {
                    if (!Utils::valid_pointer(sr.itemTemplate))
                        continue;

                    mem.AddScatterReadRequest(
                        handle,
                        sr.itemTemplate + sdk::ItemTemplate::_id,
                        &sr.mongoId,
                        sizeof(sr.mongoId)
                    );
                }
            });

        // ------------------------------------------------------------
        // 13) Process results outside lock
        // ------------------------------------------------------------
        std::vector<ScanResult> scanResults;
        scanResults.reserve(scanJobs.size());

        for (auto& job : scanJobs)
        {
            ScanResult result{};
            result.instance = job.instance;
            result.updateTime = job.updateTime;
            result.slots = std::move(job.slots);
            result.playerValue = 0;

            scanResults.push_back(std::move(result));
        }

        for (auto& sr : slotReads)
        {
            if (sr.jobIndex >= scanJobs.size())
                continue;

            if (sr.jobIndex >= scanResults.size())
                continue;

            ScanJob& job = scanJobs[sr.jobIndex];
            ScanResult& result = scanResults[sr.jobIndex];

            if (sr.slotIndex >= result.slots.size())
                continue;

            SlotEntry& slot = result.slots[sr.slotIndex];

            slot.wanted = false;
            slot.price = 0;
            slot.equipName.clear();

            if (!Utils::valid_pointer(sr.containedItem))
                continue;

            // --------------------------------------------------------
            // Dogtag profile ID check
            // Kept mostly non-scatter because ReadName / ReadString are
            // string-heavy and conditional.
            // --------------------------------------------------------
            if (job.isPlayer && job.profileId.empty() && !result.hasProfileUpdate)
            {
                std::string className = ReadName(sr.containedItem);

                if (className == "BarterOther")
                {
                    uint64_t dogtag = mem.Read<uint64_t>(
                        sr.containedItem + sdk::BarterOtherOffsets::Dogtag
                    );

                    if (!Utils::valid_pointer(dogtag))
                    {
                        LOGS.logError("[DOGTAG] pointer to dogtag in equipment scan failed!");
                    }
                    else
                    {
                        uint64_t profileIdPtr = dogtag + sdk::DogtagComponent::ProfileId;

                        if (!Utils::valid_pointer(profileIdPtr))
                        {
                            LOGS.logError("[DOGTAG] pointer to dogtag string failed!");
                        }
                        else
                        {
                            std::string readString = ReadString(profileIdPtr);

                            if (!readString.empty())
                            {
                                result.hasProfileUpdate = true;
                                result.profileId = readString;

                                LOGS.logInfo("[PLAYER] Set PID to Player : ", result.profileId);

                                if (g_DogTagAPI.hasApiKey())
                                {
                                    auto apiResult = g_DogTagAPI.getByProfile(result.profileId);

                                    if (apiResult)
                                    {
                                        if (!apiResult->accountId.empty())
                                            result.accountId = apiResult->accountId;

                                        if (!apiResult->nickname.empty())
                                            result.nickname = apiResult->nickname;
                                    }
                                }
                            }
                            else
                            {
                                LOGS.logInfo("[PLAYER] DogTag profileid is empty");
                            }
                        }
                    }
                }
            }

            if (!Utils::valid_pointer(sr.itemTemplate))
                continue;

            std::string id = TrimEFT(sr.mongoId.ReadString(mem));

            if (id.empty())
                continue;

            // --------------------------------------------------------
            // Market lookup
            // --------------------------------------------------------
            for (const auto& ml : marketList)
            {
                if (ml.bsgid != id)
                    continue;

                slot.equipName = ml.shortName;
                slot.price = (ml.marketPrice == 0) ? ml.traderPrice : ml.marketPrice;
                break;
            }

            // --------------------------------------------------------
            // Loot filters
            // --------------------------------------------------------
            for (const auto& filter : lootFilters)
            {
                if (!filter.active)
                    continue;

                bool found = false;

                for (size_t i = 0; i < filter.lootItems.size(); ++i)
                {
                    if (id == filter.lootItems[i].bsgid)
                    {
                        slot.wanted = true;
                        found = true;
                        break;
                    }
                }

                if (found)
                    break;
            }

            // --------------------------------------------------------
            // Quest loot
            // --------------------------------------------------------
            if (!slot.wanted)
            {
                for (const auto& quest : masterItems)
                {
                    if (quest == id)
                    {
                        slot.wanted = true;
                        break;
                    }
                }
            }

            // --------------------------------------------------------
            // Wishlist
            // --------------------------------------------------------
            if (!slot.wanted)
            {
                for (const auto& wishlist : wishListData)
                {
                    if (wishlist.bsgId == id)
                    {
                        slot.wanted = true;
                        break;
                    }
                }
            }

            // --------------------------------------------------------
            // Value loot
            // --------------------------------------------------------
            if (!slot.wanted)
            {
                if (slot.price > lootGlobals::valueLootFrom && lootGlobals::enableValueLoot)
                    slot.wanted = true;
            }
        }

        // ------------------------------------------------------------
        // 14) Calculate player values outside lock
        // ------------------------------------------------------------
        for (auto& result : scanResults)
        {
            result.playerValue = 0;

            for (const auto& slot : result.slots)
            {
                std::string slotName = TrimEFT(slot.name);

                if (slotName == "SecuredContainer" || slotName == "Dogtag" || slotName == "Scabbard")
                    continue;

                if (slot.price <= 0)
                    continue;

                result.playerValue += static_cast<PlayerValueT>(slot.price);
            }
        }

        // ------------------------------------------------------------
        // 15) Apply scan results under one short lock
        // ------------------------------------------------------------
        {
            std::lock_guard<std::mutex> lock(playerMutex);

            std::vector<PlayerCache>& cache = players.getCache();

            for (auto& result : scanResults)
            {
                PlayerCache* player = findPlayerByInstance(cache, result.instance);

                if (!player)
                    continue;

                if (!Utils::valid_pointer(player->instance))
                    continue;

                if (player->isDead || player->hasExfiled)
                    continue;

                player->_slots = std::move(result.slots);
                player->playerValue = result.playerValue;
                player->lastEquipmentUpdate = result.updateTime;

                if (result.hasProfileUpdate && player->profileId.empty())
                {
                    player->profileId = result.profileId;

                    if (!result.accountId.empty())
                        player->accountId = result.accountId;

                    if (!result.nickname.empty())
                        player->name = result.nickname;
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        LOGS.logError("[PLAYER][EQUIP] Exception: ", e.what());
    }
    catch (...)
    {
        LOGS.logError("[PLAYER][EQUIP] Unknown exception.");
    }
}

std::string Players::heldItemName(PlayerCache& player)
{
    try
    {
        std::string ItemHand = player.itemInHand;
        
        auto now = std::chrono::steady_clock::now();
        if (now - player.lastWeaponUpdate < player.weaponUpdateInterval)
            return ItemHand;

        player.lastWeaponUpdate = now;


        if (!Utils::valid_pointer(player.P_MovementContext))
        {
            //std::cout << "[ITEMINHAND] observedhands memory read 1\n";
            return ItemHand;
        }

        uint64_t observedHands = mem.Read<uint64_t>(player.P_MovementContext + sdk::ObservedMovementState::ObservedPlayerHands);
        if (!Utils::valid_pointer(observedHands))
        {
            //std::cout << "[ITEMINHAND] observedhands memory read 2\n";
            return ItemHand;
        }

        uint64_t itemBase = mem.Read<uint64_t>(observedHands + sdk::ObservedPlayerHands::Item);
        if (!Utils::valid_pointer(itemBase))
        {
            ItemHand = "--";
            return ItemHand;
        }

        if (itemBase != player._lastObservedHands)
        {
            player._lastObservedHands = itemBase;

            // query template for item
            ItemHand = this->ReadNameFromHandsItem(itemBase);
            
        }
        if (ItemHand == "--ERR--")
            player._lastObservedHands = NULL;

        return ItemHand;


    }
    catch (...)
    {
        return "--EX--";
    }

    

}

std::string Players::ReadNameFromHandsItem(uint64_t itemBase)
{

    uint64_t itemTemp = 0x0;

    if (!mem.TryRead<uint64_t>(itemBase + sdk::LootItem::Template,itemTemp))
		return "--ERR--";

    auto mongoId = mem.Read<MongoID>(itemTemp + sdk::ItemTemplate::_id);
    auto itemId = TrimEFT(mongoId.ReadString(mem, 64));
    
    if (itemId.empty())
        return "--ERR2--";

    for (auto& ml : marketList)
    {
        if (ml.bsgid != itemId.c_str())
            continue;

        return ml.shortName;
    }

}

static inline bool TryReadAmmoTemplateFromRound(uint64_t roundPtr, uint64_t& ammoTemplate)
{
    ammoTemplate = 0;

    if (!Utils::valid_pointer(roundPtr))
        return false;

    if (!mem.TryRead<uint64_t>(roundPtr + sdk::LootItem::Template, ammoTemplate))
        return false;

    return Utils::valid_pointer(ammoTemplate);
}

static inline bool CountLoadedChamberArray(
    uint64_t chambersPtr,
    uint64_t& firstRound,
    int& currentAmmoCount,
    int& maxAmmoCount
)
{
    if (!Utils::valid_pointer(chambersPtr))
        return false;

    UnityArray<Chamber> chambers(chambersPtr);

    if (chambers.count <= 0)
        return false;

    maxAmmoCount += chambers.count;

    for (int i = 0; i < chambers.count; ++i)
    {
        Chamber chamber = chambers[i];

        if (!chamber.HasBullet(true))
            continue;

        ++currentAmmoCount;

        // Keep first valid round for ammo type
        if (!Utils::valid_pointer(firstRound))
        {
            const uint64_t chamberPtr = static_cast<uint64_t>(chamber);

            if (!Utils::valid_pointer(chamberPtr))
                continue;

            uint64_t containedItem = 0;

            if (mem.TryRead<uint64_t>(
                chamberPtr + sdk::Slot::ContainedItem,
                containedItem) &&
                Utils::valid_pointer(containedItem))
            {
                firstRound = containedItem;
            }
        }
    }

    return true;
}


static inline bool TryGetAmmoTemplateFromWeapon(
    uint64_t itemBase,
    uint64_t& ammoTemplate,
    int& chamberCount,
    int& magazineCount
)
{
    ammoTemplate = 0;

    int currentAmmoCount = 0;
    int maxAmmoCount = 0;

    uint64_t firstRound = 0;

    // ----------------------------------------------------
    // 1. Weapon chamber path
    // Count chamber ammo, but DO NOT return here.
    // Normal guns still need the magazine counted after this.
    // ----------------------------------------------------
    uint64_t chambersPtr = 0;

    if (mem.TryRead<uint64_t>(itemBase + sdk::LootItemWeapon::Chambers, chambersPtr) &&
        Utils::valid_pointer(chambersPtr))
    {
        CountLoadedChamberArray(
            chambersPtr,
            firstRound,
            currentAmmoCount,
            maxAmmoCount
        );
    }

    // ----------------------------------------------------
    // 2. Magazine path
    // ----------------------------------------------------
    uint64_t magSlot = 0;
    uint64_t magItemPtr = 0;

    if (!mem.TryRead<uint64_t>(itemBase + sdk::LootItemWeapon::magSlotCache, magSlot) ||
        !Utils::valid_pointer(magSlot))
    {
        return false;
    }

    if (!mem.TryRead<uint64_t>(magSlot + sdk::Slot::ContainedItem, magItemPtr) ||
        !Utils::valid_pointer(magItemPtr))
    {
        return false;
    }

    // ----------------------------------------------------
    // 3. Magazine chambers path
    // Revolvers, etc.
    // ----------------------------------------------------
    uint64_t magChambersPtr = 0;

    if (mem.TryRead<uint64_t>(magItemPtr + sdk::LootItemMod::Slots, magChambersPtr) &&
        Utils::valid_pointer(magChambersPtr))
    {
        UnityArray<Chamber> magChambers(magChambersPtr);

        if (magChambers.count > 0)
        {
            CountLoadedChamberArray(
                magChambersPtr,
                firstRound,
                currentAmmoCount,
                maxAmmoCount
            );

            chamberCount = currentAmmoCount;
            magazineCount = maxAmmoCount;

            return TryReadAmmoTemplateFromRound(firstRound, ammoTemplate);
        }
    }

    // ----------------------------------------------------
    // 4. Regular magazine stack path
    // ----------------------------------------------------
    uint64_t cartridges = 0;
    uint64_t magStackPtr = 0;

    if (!mem.TryRead<uint64_t>(magItemPtr + 0xA8, cartridges) ||
        !Utils::valid_pointer(cartridges))
    {
        return false;
    }

    if (!mem.TryRead<uint64_t>(cartridges + sdk::StackSlot::items, magStackPtr) ||
        !Utils::valid_pointer(magStackPtr))
    {
        return false;
    }

    int magMaxCount = 0;

    if (!mem.TryRead<int>(cartridges + sdk::StackSlot::MaxCount, magMaxCount))
        magMaxCount = 0;

    if (magMaxCount < 0)
        magMaxCount = 0;

    maxAmmoCount += magMaxCount;

    UnityList<uint64_t> magStack(magStackPtr);

    if (magStack.count() > 0)
    {
        for (const auto& stack : magStack)
        {
            if (!Utils::valid_pointer(stack))
                continue;

            int stackNumber = 0;

            if (!mem.TryRead<int>(stack + 0x24, stackNumber))
                continue;

            if (stackNumber < 0)
                continue;

            currentAmmoCount += stackNumber;

            // If no chamber round was found, use the first mag round for ammo type
            if (!Utils::valid_pointer(firstRound))
                firstRound = stack;
        }
    }

    chamberCount = currentAmmoCount;
    magazineCount = maxAmmoCount;

    return TryReadAmmoTemplateFromRound(firstRound, ammoTemplate);
}

inline bool HandsInfo::update(const PlayerCache& playerCache)
{
    if (playerCache.isDead || playerCache.hasExfiled)
    {
        reset();
        cachedItem = 0;
        cachedIsWeapon = false;
        return false;
    }

    if (!Utils::valid_pointer(playerCache.P_HandsController))
    {
        reset();
        cachedItem = 0;
        cachedIsWeapon = false;
        return false;
    }

    uint64_t itemBase = 0;

    if (playerCache.isLocal ||
        playerCache.className.find("LocalPlayer") != std::string::npos ||
        playerCache.className.find("ClientPlayer") != std::string::npos)
    {
        itemBase = mem.Read<uint64_t>(
            playerCache.P_HandsController + sdk::ItemHandsController::Item
        );
    }
    else
    {
        itemBase = mem.Read<uint64_t>(
            playerCache.P_HandsController + sdk::ObservedPlayerHands::Item
        );
    }

    if (!Utils::valid_pointer(itemBase) || itemName == "Unknown")
    {
        reset();
        cachedItem = 0;
        cachedIsWeapon = false;
        return false;
    }

    bool isWeapon = cachedIsWeapon;

    // Only refresh item identity when the held item pointer changes
    bool itemChanged = (itemBase != cachedItem);

    if (itemChanged)
    {
        itemName.clear();
        ammoName.clear();

        chamberCount = 0;
        magazineCount = 0;

        cachedIsWeapon = false;
        isWeapon = false;

        uint64_t itemTemp = 0;

        if (!mem.TryRead<uint64_t>(itemBase + sdk::LootItem::Template, itemTemp) ||
            !Utils::valid_pointer(itemTemp))
        {
            itemName = "Unknown";
            cachedItem = itemBase;
            return true;
        }

        MongoID mongoId{};

        if (!mem.TryRead<MongoID>(itemTemp + sdk::ItemTemplate::_id, mongoId))
        {
            itemName = "Unknown";
            cachedItem = itemBase;
            return true;
        }

        std::string itemId = TrimEFT(mongoId.ReadString(mem, 64));

        std::string itemMarketName;

        if (!itemId.empty())
        {
            for (const auto& ml : marketList)
            {
                if (ml.bsgid != itemId)
                    continue;

                itemMarketName = ml.shortName;

                //Check if we have a weapon category
                const bool hasWeaponCategory =
                    std::find(ml.bsgCategory.begin(), ml.bsgCategory.end(), "Weapon") != ml.bsgCategory.end();

                if (hasWeaponCategory)
                {
                    isWeapon = true;
                    cachedIsWeapon = true;
                }

                break;
            }
        }

        if (!itemMarketName.empty())
        {
            itemName = itemMarketName;
        }
        else
        {
            uint64_t itemNamePointer = 0;

            if (mem.TryRead<uint64_t>(itemTemp + sdk::ItemTemplate::ShortName, itemNamePointer) &&
                Utils::valid_pointer(itemNamePointer))
            {
                std::string shortNameMem = TrimEFT(
                    mem.readUnityString(itemNamePointer, 32)
                );

                if (!shortNameMem.empty())
                    itemName = shortNameMem;
                else
                    itemName = "Unknown";
            }
            else
            {
                itemName = "Unknown";
            }

            if (itemName.find("nsv_utes") != std::string::npos)
            {
                itemName = "NSV Utyos";
            }
            else if (itemName.find("ags30_30") != std::string::npos)
            {
                itemName = "AGS-30";
                ammoName = "VOG-30";
            }
            else if (itemName.find("izhmash_rpk16") != std::string::npos)
            {
                itemName = "RPK-16";
            }
        }

        cachedItem = itemBase;
    }

    // Use cached weapon state after item identity refresh
    isWeapon = cachedIsWeapon;

    if (isWeapon && (playerCache.isLocal || itemChanged))
    {
        uint64_t ammoTemplate = 0;

        int newChamberCount = chamberCount;
        int newMagazineCount = magazineCount;

        const bool gotAmmoTemplate = TryGetAmmoTemplateFromWeapon(
            itemBase,
            ammoTemplate,
            newChamberCount,
            newMagazineCount
        );

        
        if (newMagazineCount > 0 || newChamberCount > 0)
        {
            chamberCount = newChamberCount;
            magazineCount = newMagazineCount;
        }

        
        if (gotAmmoTemplate || Utils::valid_pointer(ammoTemplate))
        { 

            MongoID ammoMongoId{};

            if (mem.TryRead<MongoID>(ammoTemplate + sdk::ItemTemplate::_id, ammoMongoId))
            {
                std::string ammoId = TrimEFT(ammoMongoId.ReadString(mem, 64));

                if (ammoId.empty())
                    return true;

                for (const auto& ml : marketList)
                {
                    if (ml.bsgid != ammoId)
                        continue;

                    ammoName = ml.shortName;
                    break;
                }
            }
        }
    }


    if (!isWeapon)
    {
        chamberCount = 0;
        magazineCount = 0;
        ammoName = "";
    }

    return true;
}

void Players::checkExfil()
{
    // IMPORTANT:
    // This function assumes playerMutex is already locked by playersTask().
    // Do not lock playerMutex again in here.

    if (!mem.vHandle)
        return;

    std::vector<PlayerCache>& cache = players.getCache();

    if (cache.empty())
        return;

    // If updatePlayerList failed or returned a bad count, do not mark everyone exfiled.
    // Adjust this max if your registered player buffer supports more.
    constexpr int MAX_REGISTERED_PLAYERS_SAFE = 512;

    const int registeredCount = mainGame.registeredPlayersCount;

    if (registeredCount <= 0 || registeredCount > MAX_REGISTERED_PLAYERS_SAFE)
    {
        LOGS.logError("[PLAYERS][EXFIL] Invalid registeredPlayersCount, skipping exfil check");
        return;
    }

    // Build a fast lookup of currently registered player instances.
    std::unordered_set<uint64_t> alivePlayers;
    alivePlayers.reserve(static_cast<size_t>(registeredCount));

    for (int i = 0; i < registeredCount; i++)
    {
        const uint64_t playerInstance = mainGame.player_buffer[i];

        if (!Utils::valid_pointer(playerInstance))
            continue;

        alivePlayers.insert(playerInstance);
    }

    // If the buffer had no valid player pointers, skip.
    // This avoids falsely marking everyone exfiled during a bad read/frame.
    if (alivePlayers.empty())
    {
        LOGS.logError("[PLAYERS][EXFIL] No valid registered players found, skipping exfil check");
        return;
    }

    for (auto& cachedPlayer : cache)
    {
        if (cachedPlayer.isBTR)
            continue;

        if (cachedPlayer.isDead || cachedPlayer.hasExfiled)
            continue;

        if (!Utils::valid_pointer(cachedPlayer.instance))
            continue;

        const bool stillRegistered =
            alivePlayers.find(cachedPlayer.instance) != alivePlayers.end();

        if (!stillRegistered)
        {
            cachedPlayer.hasExfiled = true;
            cachedPlayer.instance = 0x0; // avoid unnecessary future reads

            if (cachedPlayer.isLocal)
            {
                LOGS.logInfo("[PLAYERS][EXFIL] Local player no longer registered");
            }
            else
            {
                std::ostringstream ss;
                ss << "[PLAYERS][EXFIL] Player exfiled/removed: "
                    << cachedPlayer.name
                    << " old instance: 0x"
                    << std::hex << cachedPlayer.instance;

                LOGS.logInfo(ss.str());
            }
        }
    }
}

uint64_t Players::getPlayerBoneMatrixPtr(const uint64_t instance)
{
    if (mainGame.onlineRaid)
        return mem.ReadChain(instance, { sdk::ObservedPlayerView::PlayerBody, 0x30, 0x30, 0x10 });
    else
        return mem.ReadChain(instance, { sdk::Player::_playerBody, 0x30, 0x30, 0x10 });
}

uint64_t Players::getPlayerHealthControllerPtr(const uint64_t instance)
{
    if (mainGame.onlineRaid)
    {
        return mem.ReadChain(instance, { sdk::ObservedPlayerView::ObservedPlayerController, sdk::ObservedPlayerController::HealthController });
    }
    else
        return instance; // offline corpse ptr in eft.player
}
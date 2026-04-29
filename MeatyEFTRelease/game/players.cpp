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
        auto& cache = players.getCache();
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
    try
    {
        if (!mem.vHandle) {
            return;
        }
        std::lock_guard<std::mutex> lock(playerMutex);

        mainGame.updatePlayerList();

        // Proccess registered raw player buffer fetched every run
        for (int i = 0; i < mainGame.registeredPlayersCount; i++)
        {
            uint64_t currentPlayer = mainGame.player_buffer[i];

            if (!Utils::valid_pointer(currentPlayer))
                continue;

            bool isLocal = false;
            //local player
            if (currentPlayer == mainGame.localPlayerPtr) {
                isLocal = true;
            }

            // Check playerCache for entry and add if not found
            
            std::vector<PlayerCache>& cache = players.getCache();

            auto it = std::find_if(cache.begin(), cache.end(),
                [currentPlayer](const PlayerCache& cachePlayer) {
                    return cachePlayer.instance == currentPlayer;
                });

            if (it == cache.end()) { 
                addEntity(currentPlayer, isLocal);
            }
        }

        // Try find BTR
        tryFindBTR();

        // Update all cachePlayer data
        updateEntity();

        //check exfil players
        checkExfil();

        //check and assign groups based on distance between players that DONT have a group ID set ( Solo people will have GROUP 0, unset players will be blank!)
        checkGroupIDs();

        //equipment scanning
        playerEquipment();
    }
    catch (const std::exception& e) {
        LOGS.logError("Exception caught in playersTask: " + std::string(e.what()) + ". Retrying...");
    }
    catch (...) {
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

void Players::addEntity(const uint64_t instance, bool isLocal = false) {

    if (!Utils::valid_pointer(instance))
        return;

    PlayerCache newEntity;

    newEntity.className = ReadName(instance, 64);

    if (newEntity.className == "")
        return;


    //cache to set
    newEntity.instance = instance;

    // // offline class
    if (newEntity.className == "LocalPlayer" || newEntity.className == "ClientPlayer")
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
        
        newEntity.playerBoneMatrixPtr = mem.ReadChain(instance, { sdk::Player::_playerBody, 0x30, 0x30, 0x10 });

        newEntity.P_CorpseAddr = instance + sdk::Player::Corpse;
        newEntity.P_Profile = mem.Read<uint64_t>(instance + sdk::Player::Profile);
        newEntity.P_Info = mem.Read<uint64_t>(newEntity.P_Profile + sdk::Profile::Info);
        newEntity.P_PWA = mem.Read<uint64_t>(instance + sdk::Player::ProceduralWeaponAnimation);
        newEntity.P_Body = mem.Read<uint64_t>(instance + sdk::Player::_playerBody);

        newEntity.P_InventoryControllerAddr = instance + sdk::Player::_inventoryController;
        newEntity.P_HandsControllerAddr = instance + sdk::Player::_handsController;

        //get entity side
        newEntity.playerSide = mem.Read<EPlayerSide>(newEntity.P_Info + sdk::PlayerInfo::Side);

        //movement and rotation
        newEntity.P_MovementContext = mem.Read<uint64_t>(instance + sdk::Player::MovementContext);
        newEntity.P_RotationAddress = newEntity.P_MovementContext + sdk::MovementContext::_rotation;


        //if we are localplayer is it as a scav>????
        if (newEntity.isLocal)
        {
            if ((static_cast<uint32_t>(newEntity.playerSide) &
                static_cast<uint32_t>(EPlayerSide::Savage)) == 0)
            {
                mainGame.localIsSavage = false;
                newEntity.isPlayer = true;
                newEntity.isPlayerScav = false;
            }
            else
            {
                mainGame.localIsSavage = true;
                newEntity.isPlayer = false;
                newEntity.isPlayerScav = true;
            }

            

            //do quest data
            mainGame.localplayerProfile = newEntity.P_Profile;
            questManager.initQuestManager();
        }
        
        

    }
    else {
        // online
        newEntity.playerBoneMatrixPtr = mem.ReadChain(instance, { sdk::ObservedPlayerView::PlayerBody, 0x30, 0x30, 0x10 });
        newEntity.P_ObservedPlayerController = mem.Read<uint64_t>(instance + sdk::ObservedPlayerView::ObservedPlayerController);
        newEntity.P_ObservedHealthController = mem.Read<uint64_t>(newEntity.P_ObservedPlayerController + sdk::ObservedPlayerController::HealthController);
        newEntity.P_CorpseAddr = newEntity.P_ObservedHealthController + sdk::ObservedHealthController::PlayerCorpse;
        newEntity.P_InventoryControllerAddr = newEntity.P_ObservedPlayerController + sdk::ObservedPlayerController::InventoryController;
        newEntity.P_HandsControllerAddr = newEntity.P_ObservedPlayerController + sdk::ObservedPlayerController::HandsController;

        //movement & rotation
        newEntity.P_MovementContext = mem.ReadChain(newEntity.P_ObservedPlayerController, { sdk::ObservedPlayerController::MovementController, sdk::ObservedMovementController::ObservedPlayerStateContext });
        newEntity.P_RotationAddress = newEntity.P_MovementContext + sdk::ObservedPlayerStateContext::Rotation;

        // Build list of failures
        std::string failed;
        auto test = [&](uint64_t ptr, const char* name) {
            if (!Utils::valid_pointer(ptr)) {
                if (!failed.empty()) failed += ", ";
                failed += name;
            }
            };

        test(newEntity.playerBoneMatrixPtr, "BoneMatrixPtr");
        test(newEntity.P_ObservedPlayerController, "PlayerController");
        test(newEntity.P_ObservedHealthController, "HealthController");
        test(newEntity.P_CorpseAddr, "CorpseAddr");
        test(newEntity.P_InventoryControllerAddr, "InventoryControllerAddr");
        test(newEntity.P_HandsControllerAddr, "HandsControllerAddr");
        test(newEntity.P_MovementContext, "MovementContext");
        test(newEntity.P_RotationAddress, "RotationAddress");

        // If any invalid pointers log once and skip entity
        if (!failed.empty())
        {
            std::ostringstream ss;
            ss << "[PLAYER] Init Failed 0x"
                << std::hex << instance
                << " | " << failed;

            //LOGS.logError(ss.str());
            return; 
        }

        //AI / Human stuff
        newEntity.isAi = mem.Read<bool>(instance + sdk::ObservedPlayerView::IsAI);
        newEntity.isPlayer = !newEntity.isAi;

        //profileid
        //handled in player dogtag equipment 

        //side
        uint64_t sideAddrs = instance + sdk::ObservedPlayerView::Side;
        newEntity.playerSide = mem.Read<EPlayerSide>(sideAddrs);
        newEntity.side = SideToString(newEntity.playerSide);

        
        

        if ((static_cast<uint32_t>(newEntity.playerSide) &
            static_cast<uint32_t>(EPlayerSide::Savage)) != 0)
        {
            if (newEntity.isAi)
            {
                uint64_t voicePtr = mem.Read<uint64_t>(instance + sdk::ObservedPlayerView::Voice);
                std::string voice = mem.readUnicodeString(voicePtr + 0x14, mem.Read<int>(static_cast<SIZE_T>(voicePtr) + 0x10));
                AIRole role = GetAIRoleInfo(voice);
                newEntity.name = role.Name;
                if (role.Type == PlayerType::AIBoss)
                    newEntity.isBoss = TRUE;

                newEntity.isPlayerScav = FALSE;
                newEntity.isAi = TRUE;
                newEntity.isPlayer = FALSE;
            }
            else
            {
                newEntity.name = "PScav " + std::to_string(mainGame.pmcNumber);
                mainGame.pmcNumber++;

                newEntity.isPlayerScav = TRUE;
                newEntity.isAi = FALSE;
                newEntity.isPlayer = FALSE;
            }
        }
        else if (newEntity.isPlayer)
        {
            //player pmc
            newEntity.name = "PMC " + std::to_string(mainGame.pmcNumber);
            mainGame.pmcNumber++;
            
            //Try query tarkov dev for data
            if (!newEntity.accountId.empty() && radarGlobals::getPlayerStats == TRUE)
            {
                auto profile = TarkovDevProfileClient::GetProfileForAccountId(newEntity.accountId);
                if (profile)
                {
                    newEntity.profileStats = *profile;
                    newEntity.hasProfileData = true;

                    newEntity.name = profile->nickname;
                    newEntity.DT_lvl = ConvertXpToLevel(profile->experience);
                    newEntity.kd = CalculateKD(profile->killsPMC, profile->deathsPMC);
                }
            }
            newEntity.isPlayerScav = FALSE;
            newEntity.isAi = FALSE;
            newEntity.isPlayer = TRUE;
        }
       


    }

    //get bone ptrs
    if (!newEntity.isBTR)
        newEntity = getBonePtrs(newEntity);

    
    playerCache.emplace_back(newEntity);
    LOGS.logInfo("[PLAYERS][INIT] Adding player : 0x", std::hex, newEntity.instance, " className : ", newEntity.className.c_str());

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
        if (footSeparation > 0.01f && footSeparation <= 1.5f)
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
    const std::string selectedMap = TrimEFT(mainGame.selectedLocation);

    if (selectedMap != "TarkovStreets" && selectedMap != "Woods")
        return;

    if (!Utils::valid_pointer(mainGame.localGameWorld)) return;

    uint64_t btrController = mem.Read<uint64_t>(mainGame.localGameWorld + sdk::ClientLocalGameWorld::btrController); if (!Utils::valid_pointer(btrController)) return;
    uint64_t btrView = mem.Read<uint64_t>(btrController + sdk::BtrController::BtrView); if (!Utils::valid_pointer(btrView)) return;
    uint64_t btrTurret = mem.Read<uint64_t>(btrView + sdk::BTRView::turret); if (!Utils::valid_pointer(btrTurret)) return;
    uint64_t btrOper = mem.Read<uint64_t>(btrTurret + sdk::BTRTurretView::attachedBot); if (!Utils::valid_pointer(btrOper)) return;

    std::vector<PlayerCache>& cache = players.getCache();

    if (cache.empty())
        return;

    for (auto& playerList : cache)
    {
        if (playerList.instance != btrOper)
            continue;

        if (playerList.isPlayer || playerList.isPlayerScav || playerList.isLocal)
            continue;

        playerList.isBTR = true;
        playerList.colour = coloursGlobals::aiBTR;
        playerList.btrView = btrView;
        playerList.name = "BTR";

        if (!mainGame.btrAllocated)
        {
            mainGame.btrAllocated = true;
            LOGS.logInfo("[BTR] BTR Allocated");
        }

        break;
    }
}

void Players::updateEntity()
{

    std::vector<PlayerCache>& cache = players.getCache();

    //ScatterRead Handles
    
    auto handleRotation = mem.CreateScatterHandle();
    auto handleCorpse = mem.CreateScatterHandle();
    auto handleHealth = mem.CreateScatterHandle();
    auto handleHands = mem.CreateScatterHandle();
    auto handleIsAiming = mem.CreateScatterHandle();

    for (auto& cachePlayer : cache)
    {
        if (!Utils::valid_pointer(cachePlayer.instance))
            continue;

        if (cachePlayer.isDead || cachePlayer.hasExfiled)
            continue;

        //BTR only
        if (cachePlayer.isBTR)
        {
            // We just want to update BTR position and move on
            cachePlayer.location = mem.Read<glm::vec3>(cachePlayer.btrView + sdk::BTRView::previousPosition);

            continue;
        }

        if (cachePlayer.isLocal)
        {
            mainGame.localGroupId = cachePlayer.groupId;
            mainGame.localPlayerHands = cachePlayer.P_HandsController;
            mainGame.localplayerProfile = cachePlayer.P_Profile;
        }

        

        //offline
        if (cachePlayer.className == "LocalPlayer" || cachePlayer.className == "ClientPlayer")
        {
            mem.AddScatterReadRequest(handleRotation, cachePlayer.P_RotationAddress, &cachePlayer.rotationRAW, sizeof(glm::vec2));
            mem.AddScatterReadRequest(handleCorpse, cachePlayer.P_CorpseAddr, &cachePlayer.P_CorpseClass, sizeof(uint64_t));
            mem.AddScatterReadRequest(handleHands, cachePlayer.P_HandsControllerAddr, &cachePlayer.P_HandsController, sizeof(uint64_t));
            mem.AddScatterReadRequest(handleIsAiming, cachePlayer.P_PWA + sdk::ProceduralWeaponAnimation::_isAiming , &cachePlayer.isAiming, sizeof(bool));
        }
        else
        {
            mem.AddScatterReadRequest(handleRotation, cachePlayer.P_RotationAddress, &cachePlayer.rotationRAW, sizeof(glm::vec2));
            mem.AddScatterReadRequest(handleCorpse, cachePlayer.P_CorpseAddr, &cachePlayer.P_CorpseClass, sizeof(uint64_t));
            mem.AddScatterReadRequest(handleHealth, cachePlayer.P_ObservedHealthController + sdk::ObservedHealthController::HealthStatus, &cachePlayer.healthETAG, sizeof(ETagStatus));
            mem.AddScatterReadRequest(handleHands, cachePlayer.P_HandsControllerAddr, &cachePlayer.P_HandsController, sizeof(uint64_t));
        }

    }

    //execute scatters > close
    mem.ExecuteReadScatter(handleRotation); mem.CloseScatterHandle(handleRotation);
    mem.ExecuteReadScatter(handleCorpse); mem.CloseScatterHandle(handleCorpse);
    mem.ExecuteReadScatter(handleHealth); mem.CloseScatterHandle(handleHealth);
    mem.ExecuteReadScatter(handleHands); mem.CloseScatterHandle(handleHands);
    mem.ExecuteReadScatter(handleIsAiming); mem.CloseScatterHandle(handleIsAiming);

    //loop our list again and update raw values and other data
    for (auto& cachePlayer : cache)
    {
        //BTR Only
        if (cachePlayer.isBTR)
        {
            cachePlayer.colour = coloursGlobals::aiBTR;

            continue;
        }

        //location checks
        cachePlayer.location = GetBestPlayerBasePosition(cachePlayer);
        if (cachePlayer.isLocal)
            mainGame.localLocation = cachePlayer.location;

        //always update distances regardless
        cachePlayer.distance = getDistance(cachePlayer.location, mainGame.localLocation);

        if (cachePlayer.isDead || cachePlayer.hasExfiled)
            continue;

        // Held item
        cachePlayer.itemInHand = heldItemName(cachePlayer);

        //correct rotation
        cachePlayer.rotation = Utils::Player::Rotation::correctRotation2d(cachePlayer.rotationRAW);

        //update player info if possible and not done THIS IS ONLY LOCAL MEMORY CACHE NOT API!! TO AVOID FLOODING OUR API SERVER. API CALLS ONLY ON ADDING
        if (cachePlayer.isPlayer && !cachePlayer.profileId.empty())
        {
            auto now = std::chrono::steady_clock::now();

            if (!cachePlayer.foundDogTagCache &&
                cachePlayer.name.contains("PMC") &&
                now - cachePlayer.lastDogTagLookup > std::chrono::seconds(5))
            {
                cachePlayer.lastDogTagLookup = now;

                auto result = g_dogTagCache.GetByProfileId(cachePlayer.profileId);
                if (result.has_value())
                {
                    cachePlayer.name = result->nickname;
                    cachePlayer.accountId = result->accountId;
                    cachePlayer.foundDogTagCache = true;
                }
            }

            if (!cachePlayer.hasProfileData &&
                !cachePlayer.accountId.empty() &&
                radarGlobals::getPlayerStats == TRUE)
            {
                if (!cachePlayer.triedprofileonce)
                {
                    cachePlayer.triedprofileonce = true;

                    auto profile = TarkovDevProfileClient::GetProfileForAccountId(cachePlayer.accountId);
                    if (profile)
                    {
                        cachePlayer.profileStats = *profile;
                        cachePlayer.hasProfileData = true;

                        cachePlayer.name = profile->nickname;
                        cachePlayer.DT_lvl = ConvertXpToLevel(profile->experience);
                        cachePlayer.kd = CalculateKD(profile->killsPMC, profile->deathsPMC);
                        cachePlayer.hours = profile->hoursPlayed;
                    }
                }
            }
        }
        
        //colours
        cachePlayer.colour = { 1,1,1,1 };

        //corpse?
        if (Utils::valid_pointer(cachePlayer.P_CorpseClass))
        {
            cachePlayer.isDead = true;

        }

        //set colours
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
        //friendly
        if ((cachePlayer.groupId == mainGame.localGroupId) && mainGame.localGroupId != "")
        {
            cachePlayer.colour = coloursGlobals::playerFriendly;
        }

        if (cachePlayer.isDead || cachePlayer.hasExfiled)
        {
            if (cachePlayer.isDead)
                cachePlayer.colour = coloursGlobals::playerCorpse;
        }

        if (cachePlayer.isLocal && mainGame.localPlayerPtr == cachePlayer.instance)
        {
            //update localplayer cache
            mainGame.localLocation = GetBestPlayerBasePosition(cachePlayer);
            mainGame.localRotation = cachePlayer.rotation;
            mainGame.localGroupId = cachePlayer.groupId;
            cachePlayer.colour = coloursGlobals::playerLocal;

            mainGame.localGroupId = cachePlayer.groupId;
            mainGame.localPlayerHands = cachePlayer.P_HandsController;
            mainGame.localIsScoped = cachePlayer.isAiming;
            mainGame.localPlayerPWA = cachePlayer.P_PWA;

        }
        
    }

    

}

void Players::checkGroupIDs()
{
    if (groupIDSet)
        return;

    if (!mainGame.checkIfRaidStarted())
        return;

    auto& cache = players.getCache();
    int nextGroupId = 1;
    bool changed = true;
    bool assignedAnyGroup = false;

    auto hasValidBone = [&](const PlayerCache& player) -> bool
        {
            return isValidBoneVector(player.bonePositions[allPlayerBones::HumanBase]);
        };

    auto isValidGroupingTarget = [&](const PlayerCache& player) -> bool
        {
            return (player.isPlayer || player.isPlayerScav) && hasValidBone(player);
        };

    auto getNewGroupId = [&]() -> std::string
        {
            return std::to_string(nextGroupId++);
        };

    while (changed)
    {
        changed = false;

        for (auto& playerA : cache)
        {
            if (!isValidGroupingTarget(playerA))
                continue;

            if (playerA.hasExfiled || playerA.isDead)
                continue;

            for (auto& playerB : cache)
            {
                if (playerB.hasExfiled || playerB.isDead)
                    continue;

                if (playerA.instance == playerB.instance)
                    continue;

                if (!isValidGroupingTarget(playerB))
                    continue;

                float dist = players.getDistance(
                    playerA.bonePositions[allPlayerBones::HumanBase],
                    playerB.bonePositions[allPlayerBones::HumanBase]);

                if (dist > 15.0f)
                    continue;

                // If one of them is local, force both into the local group
                if (playerA.isLocal || playerB.isLocal)
                {
                    if (mainGame.localGroupId.empty())
                        mainGame.localGroupId = getNewGroupId();

                    if (playerA.groupId != mainGame.localGroupId)
                    {
                        playerA.groupId = mainGame.localGroupId;
                        changed = true;
                        assignedAnyGroup = true;
                    }

                    if (playerB.groupId != mainGame.localGroupId)
                    {
                        playerB.groupId = mainGame.localGroupId;
                        changed = true;
                        assignedAnyGroup = true;
                    }

                    continue;
                }

                // Neither has a group yet
                if (playerA.groupId.empty() && playerB.groupId.empty())
                {
                    std::string newGroupId = getNewGroupId();
                    playerA.groupId = newGroupId;
                    playerB.groupId = newGroupId;
                    changed = true;
                    assignedAnyGroup = true;
                    continue;
                }

                // Copy A to B
                if (!playerA.groupId.empty() && playerB.groupId.empty())
                {
                    playerB.groupId = playerA.groupId;
                    changed = true;
                    assignedAnyGroup = true;
                    continue;
                }

                // Copy B to A
                if (playerA.groupId.empty() && !playerB.groupId.empty())
                {
                    playerA.groupId = playerB.groupId;
                    changed = true;
                    assignedAnyGroup = true;
                    continue;
                }

                // Merge two different existing groups
                if (!playerA.groupId.empty() &&
                    !playerB.groupId.empty() &&
                    playerA.groupId != playerB.groupId)
                {
                    std::string fromGroup = playerB.groupId;
                    std::string toGroup = playerA.groupId;

                    // Prefer the local group if one side already belongs to it
                    if (!mainGame.localGroupId.empty())
                    {
                        if (playerA.groupId == mainGame.localGroupId)
                        {
                            fromGroup = playerB.groupId;
                            toGroup = playerA.groupId;
                        }
                        else if (playerB.groupId == mainGame.localGroupId)
                        {
                            fromGroup = playerA.groupId;
                            toGroup = playerB.groupId;
                        }
                    }

                    for (auto& player : cache)
                    {
                        if (player.groupId == fromGroup)
                            player.groupId = toGroup;
                    }

                    changed = true;
                    assignedAnyGroup = true;
                    continue;
                }
            }
        }
    }

    if (assignedAnyGroup)
        groupIDSet = true;
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

    try
    {

        // init all players
        std::vector<PlayerCache>& cache = players.getCache();
        for (auto& player : cache)
        {
            if (player.isDead || player.hasExfiled)
                continue;

            if (player.equipInited)
                continue;

            if (!Utils::valid_pointer(player.P_InventoryControllerAddr))
                continue;

            uint64_t inventoryController = mem.Read<uint64_t>(player.P_InventoryControllerAddr); if (!Utils::valid_pointer(inventoryController)) continue;
            uint64_t inventory = mem.Read<uint64_t>(inventoryController + sdk::InventoryController::Inventory); if (!Utils::valid_pointer(inventory)) continue;
            uint64_t equipment = mem.Read<uint64_t>(inventory + sdk::Inventory::Equipment); if (!Utils::valid_pointer(equipment)) continue;
            uint64_t slotsPtr = mem.Read<uint64_t>(equipment + sdk::InventoryEquipment::_cachedSlots); if (!Utils::valid_pointer(slotsPtr)) continue;

            
            auto slotsArray = UnityArray<uint64_t>(slotsPtr);

            if (slotsArray.count < 1)
                continue;

            player._slots.clear();

            for (auto& slotPtr : slotsArray)
            {
                uint64_t namePtr = mem.Read<uint64_t>(slotPtr + sdk::Slot::ID);
                auto name = mem.readUnicodeString(namePtr + 0x14, mem.Read<int>(static_cast<SIZE_T>(namePtr) + 0x10));

                if (skipNames.contains(name))
                    continue;

                //add slot to the vector for later scanning
                player._slots.push_back({ name, slotPtr });
            }

            if (!player._slots.empty())
                player.equipInited = true;

        }

        // update slots in players equipment scanned slots

        for (auto& player : cache)
        {
            if (player.isDead || player.hasExfiled)
                continue;

            auto now = std::chrono::steady_clock::now();
            if (now - player.lastEquipmentUpdate < player.equipmentUpdateInterval)
                continue;
            player.lastEquipmentUpdate = now;


            for (auto& slot : player._slots)
            {
                slot.wanted = false;
                slot.price = 0;
                slot.equipName.clear();

                if (player.isPlayer && slot.name == "Scabbard")
                    continue;

                uint64_t containedItem = mem.Read<uint64_t>(slot.addr + sdk::Slot::ContainedItem);
                if (!Utils::valid_pointer(containedItem))
                    continue;

                //check if we have dogtag and proccess profileid if not set
                if (player.isPlayer && player.profileId.empty())
                {
                    std::string className = "";
                    className = ReadName(containedItem);

                    if (className == "BarterOther")
                    {
                        uint64_t dogtag = mem.Read<uint64_t>(containedItem + sdk::BarterOtherOffsets::Dogtag);
                        if (!Utils::valid_pointer(dogtag))
                        {
                            LOGS.logError("[DOGTAG] pointer to dogtag in equipment scan failed!");
                            continue;
                        }

                        uint64_t profileIdPtr = dogtag + sdk::DogtagComponent::ProfileId;
                        if (!Utils::valid_pointer(profileIdPtr))
                        {
                            LOGS.logError("[DOGTAG] pointer to dogtag string failed!");
                            continue;
                        }

                        std::string readString = ReadString(profileIdPtr);

                        if (!readString.empty())
                        {
                            player.profileId = readString;
                            LOGS.logInfo("[PLAYER] Set PID to Player : ", player.profileId);
                            

                            //call API try get data we might have
                            auto result = g_DogTagAPI.getByProfile(player.profileId);
                            if (result)
                            {
                                player.accountId = result->accountId;
                                player.name = result->nickname;
                            }
                        }
                        else
                        {
                            LOGS.logInfo("[PLAYER] DogTag profileid is empty");
                        }

                    }
                }

                uint64_t inventorytemplate = mem.Read<uint64_t>(containedItem + sdk::LootItem::Template);
                if (!Utils::valid_pointer(inventorytemplate))
                    continue;

                auto mongoId = mem.Read<MongoID>(inventorytemplate + sdk::ItemTemplate::_id);
                auto id = TrimEFT(mongoId.ReadString(mem));
                if (id.empty())
                    continue;

                // price/name lookup
                for (auto& ml : marketList)
                {
                    if (ml.bsgid != id)
                        continue;

                    slot.equipName = ml.shortName;
                    slot.price = (ml.marketPrice == 0) ? ml.traderPrice : ml.marketPrice;
                    break;
                }

                // loot filters
                for (auto& filter : lootFilters)
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

                // quest loot
                if (!slot.wanted)
                {
                    for (auto& quest : masterItems)
                    {
                        if (quest == id)
                        {
                            slot.wanted = true;
                            break;
                        }
                    }
                }

                // wishlist
                if (!slot.wanted)
                {
                    for (auto& wishlist : wishListData)
                    {
                        if (wishlist.bsgId == id)
                        {
                            slot.wanted = true;
                            break;
                        }
                    }
                }

                //value loot
                if (!slot.wanted)
                {
                    if (slot.price > lootGlobals::valueLootFrom && lootGlobals::enableValueLoot)
                    {
                        slot.wanted = true;
                    }
                }


            }

            //reset value first
            player.playerValue = 0;
            //update player value
            for (const auto& slot : player._slots)
            {
                std::string slotn = TrimEFT(slot.name);

                if (slotn == "SecuredContainer" || slotn == "Dogtag" || slotn == "Scabbard")
                    continue;
                if (slot.price <= 0)
                    continue;

                player.playerValue += slot.price;
            }
        }
    }
    catch (...)
    {
        LOGS.logError("[PLAYER][EQUIP] Exception..");
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

    uint64_t itemTemp = mem.Read<uint64_t>(itemBase + sdk::LootItem::Template);
    if (!Utils::valid_pointer(itemTemp))
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

void Players::checkExfil()
{

    std::vector<PlayerCache>& cache = players.getCache();

    // Iterate over the cache
    for (auto& cachedPlayer : cache) {
        if (!Utils::valid_pointer(cachedPlayer.instance))
            continue;
        if (cachedPlayer.isDead || cachedPlayer.hasExfiled)
            continue;
        if (cachedPlayer.isBTR)
            continue;

        bool isExfiled = true; // Assume player has exfiled unless proven otherwise
        for (int i = 0; i < mainGame.registeredPlayersCount; i++) {
            if (mainGame.player_buffer[i] == cachedPlayer.instance) {
                isExfiled = false; // Player still in the game
                break;
            }
        }

        if (isExfiled) {
            cachedPlayer.hasExfiled = TRUE;
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
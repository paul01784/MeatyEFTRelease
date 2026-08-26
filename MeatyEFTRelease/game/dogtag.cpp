#include "headers/dogtag.h"

#include "headers/utils.h"
#include "headers/unityHelper.h"
#include "headers/sdk.h"
#include "../memory/Memory.h"
#include "../app/DogTagAPI.h"

DogTagCache g_dogTagCache;

DogTagCache::DogTagCache()
{
}

bool DogTagCache::IsValidEntry(const Entry& entry) const
{
    return !entry.profileId.empty() &&
        !entry.accountId.empty() &&
        !entry.nickname.empty();
}

bool DogTagCache::WasCorpseProcessed(uint64_t corpseInteractiveClass) const
{
    std::lock_guard<std::mutex> lock(m_processedMutex);
    return m_processedCorpses.find(corpseInteractiveClass) != m_processedCorpses.end();
}

void DogTagCache::MarkCorpseProcessed(uint64_t corpseInteractiveClass)
{
    std::lock_guard<std::mutex> lock(m_processedMutex);
    m_processedCorpses.insert(corpseInteractiveClass);
}

void DogTagCache::ClearProcessedCorpses()
{
    std::lock_guard<std::mutex> lock(m_processedMutex);
    m_processedCorpses.clear();
}

bool DogTagCache::IsDuplicate(const Entry& entry) const
{
    if (!entry.profileId.empty() &&
        m_profileIndex.find(entry.profileId) != m_profileIndex.end())
        return true;

    if (!entry.accountId.empty() &&
        m_accountIndex.find(entry.accountId) != m_accountIndex.end())
        return true;

    return false;
}

void DogTagCache::RebuildIndexes()
{
    m_profileIndex.clear();
    m_accountIndex.clear();

    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        const auto& entry = m_entries[i];

        if (!entry.profileId.empty())
            m_profileIndex[entry.profileId] = i;

        if (!entry.accountId.empty())
            m_accountIndex[entry.accountId] = i;
    }
}

bool DogTagCache::AddEntryIfMissing(const Entry& entry)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!IsValidEntry(entry))
        return false;

    if (IsDuplicate(entry))
        return false;

    m_entries.push_back(entry);
    const size_t index = m_entries.size() - 1;

    m_profileIndex[entry.profileId] = index;
    m_accountIndex[entry.accountId] = index;

    
    if (entry.lvl > 0 &&
        entry.lvl <= 100 &&
        m_sentProfiles.insert(entry.profileId).second)
    {
        if (!g_DogTagAPI.post(
            entry.profileId,
            entry.accountId,
            entry.nickname,
            entry.lvl))
        {
            std::cout << "[DogTagCache] API POST failed\n";
        }
    }

    return true;
}

bool DogTagCache::HasEntryByProfileId(const std::string& profileId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_profileIndex.find(profileId) != m_profileIndex.end();
}

bool DogTagCache::HasEntryByAccountId(const std::string& accountId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_accountIndex.find(accountId) != m_accountIndex.end();
}

std::optional<DogTagCache::Entry> DogTagCache::GetByProfileId(const std::string& profileId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_profileIndex.find(profileId);
    if (it == m_profileIndex.end())
        return std::nullopt;

    return m_entries[it->second];
}

std::optional<DogTagCache::Entry> DogTagCache::GetByAccountId(const std::string& accountId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_accountIndex.find(accountId);
    if (it == m_accountIndex.end())
        return std::nullopt;

    return m_entries[it->second];
}

const std::vector<DogTagCache::Entry>& DogTagCache::GetAllEntries() const
{
    return m_entries;
}

void DogTagCache::ReadFromCorpse(uint64_t corpseInteractiveClass)
{
    if (!Utils::valid_pointer(corpseInteractiveClass))
        return;

    if (WasCorpseProcessed(corpseInteractiveClass))
        return;

    bool processedSuccessfully = false;

    try
    {
        uint64_t itemBase = mem.Read<uint64_t>(corpseInteractiveClass + sdk::InteractiveLootItem::Item);
        if (!Utils::valid_pointer(itemBase))
            return;

        uint64_t slotsPtr = mem.Read<uint64_t>(itemBase + sdk::LootItemMod::Slots);
        if (!Utils::valid_pointer(slotsPtr))
            return;

        constexpr int kMaxDogtagSlots = 128;
        auto slotsRead = UnityArray<uint64_t>(
            slotsPtr,
            "Dogtag slots",
            kMaxDogtagSlots);
        if (slotsRead.count == 0)
            return;

        for (auto& slotPtr : slotsRead)
        {
            if (!Utils::valid_pointer(slotPtr))
                continue;

            uint64_t namePtr = mem.Read<uint64_t>(slotPtr + sdk::Slot::ID);
            if (!Utils::valid_pointer(namePtr))
                continue;

            const int nameLen = mem.Read<int>(static_cast<SIZE_T>(namePtr) + 0x10);
            auto name = mem.readUnicodeString(namePtr + 0x14, nameLen);
            std::string slotName = TrimEFT(name);

            if (slotName != "Dogtag")
                continue;

            uint64_t dogtagItem = mem.Read<uint64_t>(slotPtr + sdk::Slot::ContainedItem);
            if (!Utils::valid_pointer(dogtagItem))
                break;

            uint64_t dogtagComp = mem.Read<uint64_t>(dogtagItem + sdk::BarterOtherOffsets::Dogtag);
            if (!Utils::valid_pointer(dogtagComp))
                break;

            Entry victim;
            victim.profileId = mem.readUnityStringField(dogtagComp + sdk::DogtagComponent::ProfileId, 256);
            victim.accountId = mem.readUnityStringField(dogtagComp + sdk::DogtagComponent::AccountId, 256);
            victim.nickname = mem.readUnityStringField(dogtagComp + sdk::DogtagComponent::Nickname, 256);
            victim.lvl = mem.Read<int>(dogtagComp + sdk::DogtagComponent::Level);

            Entry killer;
            killer.profileId = mem.readUnityStringField(dogtagComp + sdk::DogtagComponent::KillerProfileId, 256);
            killer.accountId = mem.readUnityStringField(dogtagComp + sdk::DogtagComponent::KillerAccountId, 256);
            killer.nickname = mem.readUnityStringField(dogtagComp + sdk::DogtagComponent::KillerName, 256);

            AddEntryIfMissing(victim);

            if (killer.accountId != "0") // skip scavs or maybe bad data
                AddEntryIfMissing(killer);

            processedSuccessfully = true;
            break;
        }

        if (processedSuccessfully)
            MarkCorpseProcessed(corpseInteractiveClass);
    }
    catch (...)
    {
        std::cout << "[DogTagCache] exception while processing corpse dogtag\n";
    }
}

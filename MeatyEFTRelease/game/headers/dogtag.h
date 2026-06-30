#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <filesystem>
#include <mutex>
#include <Windows.h>

#include <nlohmann/json.hpp>
#include <unordered_set>

class DogTagCache
{
public:
    struct Entry
    {
        std::string profileId;
        std::string accountId;
        std::string nickname;
    };

    DogTagCache();

    void ReadFromCorpse(uint64_t corpseInteractiveClass);

    bool HasEntryByProfileId(const std::string& profileId) const;
    bool HasEntryByAccountId(const std::string& accountId) const;

    std::optional<Entry> GetByProfileId(const std::string& profileId) const;
    std::optional<Entry> GetByAccountId(const std::string& accountId) const;

    const std::vector<Entry>& GetAllEntries() const;

    void ClearProcessedCorpses();

private:
    bool IsValidEntry(const Entry& entry) const;
    bool IsDuplicate(const Entry& entry) const;
    bool AddEntryIfMissing(const Entry& entry);
    void RebuildIndexes();

    bool WasCorpseProcessed(uint64_t corpseInteractiveClass) const;
    void MarkCorpseProcessed(uint64_t corpseInteractiveClass);
    

private:
    mutable std::mutex m_mutex;
    mutable std::mutex m_processedMutex;

    std::vector<Entry> m_entries;

    std::unordered_map<std::string, size_t> m_profileIndex;
    std::unordered_map<std::string, size_t> m_accountIndex;

    std::unordered_set<uint64_t> m_processedCorpses;

    std::unordered_set<std::string> m_sentProfiles;
};

extern DogTagCache g_dogTagCache;
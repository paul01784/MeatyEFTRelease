#include "EftOffsetService.h"
#include "UnityOffsetService.h"

#include "../../Tarkov/SDK/EftOffsets.h"
#include "../../UI/globals.h"
#include "../../external/nlohmann/json.hpp"
#include "../../libs/vmmdll.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace
{
    constexpr char kProcessName[] = "EscapeFromTarkov.exe";
    constexpr char kModuleName[] = "GameAssembly.dll";
    constexpr std::size_t kMaximumClasses = 80'000;
    constexpr std::size_t kClassChunkSize = 2'048;
    constexpr std::size_t kMaximumNameLength = 160;

    enum class OffsetKind : std::uint8_t
    {
        Field,
        TypeIndex
    };

    struct OffsetDescriptor
    {
        const char* group;
        const char* name;
        const char* className;
        const char* fieldName;
        std::uint64_t* value;
        std::uint64_t fallback;
        OffsetKind kind;
        bool staticField;
        bool required;
    };

    std::vector<OffsetDescriptor>& Descriptors()
    {
        static std::vector<OffsetDescriptor> descriptors = {
#define EFT_FIELD(group, name, className, fieldName, fallback, isStatic, required) \
            {#group, #name, className, fieldName, &sdk::group::name, fallback, OffsetKind::Field, isStatic, required},
#define EFT_TYPE(group, name, className, fallback, required) \
            {#group, #name, className, "", &sdk::group::name, fallback, OffsetKind::TypeIndex, false, required},
#define EFT_ALIAS(group, name, targetGroup, targetName)
#include "../../Tarkov/SDK/EftOffsetDefinitions.inc"
#undef EFT_ALIAS
#undef EFT_TYPE
#undef EFT_FIELD
        };
        return descriptors;
    }

    std::string Hex(std::uint64_t value)
    {
        std::ostringstream stream;
        stream << "0x" << std::uppercase << std::hex << value;
        return stream.str();
    }

    bool ParseValue(const nlohmann::json& value, std::uint64_t& result)
    {
        try
        {
            if (value.is_number_unsigned())
            {
                result = value.get<std::uint64_t>();
                return true;
            }
            if (value.is_number_integer())
            {
                const auto signedValue = value.get<std::int64_t>();
                if (signedValue < 0)
                    return false;
                result = static_cast<std::uint64_t>(signedValue);
                return true;
            }
            if (!value.is_string())
                return false;

            const std::string text = value.get<std::string>();
            std::size_t consumed = 0;
            result = std::stoull(text, &consumed, 0);
            return consumed == text.size();
        }
        catch (...)
        {
            return false;
        }
    }

    std::string UtcNow()
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t time = std::chrono::system_clock::to_time_t(now);
        std::tm utc{};
        gmtime_s(&utc, &time);
        std::ostringstream stream;
        stream << std::put_time(&utc, "%d-%m-%YT%H:%M:%SZ");
        return stream.str();
    }

    bool IsPointer(std::uint64_t value)
    {
        return value >= 0x10000 && value <= 0x00007FFFFFFFFFFF;
    }

    struct ScatterRead
    {
        std::uint64_t address{};
        void* buffer{};
        DWORD size{};
    };

    class ResolverSession
    {
    public:
        ~ResolverSession()
        {
            if (handle_)
                VMMDLL_Close(handle_);
        }

        bool Open(std::string& error)
        {
            std::vector<std::string> storage = {"", "-device", "fpga://algo=0", "-waitinitialize"};
            const auto baseArgumentCount = storage.size();
            std::error_code pathError;
            const auto mapPath = std::filesystem::current_path(pathError) / "mmap.txt";
            if (!pathError && std::filesystem::is_regular_file(mapPath, pathError))
            {
                storage.emplace_back("-memmap");
                storage.emplace_back(mapPath.string());
            }

            const auto initialise = [](std::vector<std::string>& values)
            {
                std::vector<LPCSTR> arguments;
                arguments.reserve(values.size());
                for (auto& value : values)
                    arguments.push_back(value.c_str());
                return VMMDLL_Initialize(static_cast<DWORD>(arguments.size()), arguments.data());
            };

            handle_ = initialise(storage);
            if (!handle_ && storage.size() > baseArgumentCount)
            {
                storage.resize(baseArgumentCount);
                handle_ = initialise(storage);
            }
            if (!handle_)
            {
                error = "Could not open a temporary DMA session";
                return false;
            }

            if (!RefreshAll())
            {
                error = "The temporary DMA session could not complete its initial full cache refresh";
                return false;
            }

            for (int attempt = 0; attempt < 20 && !pid_; ++attempt)
            {
                (void)VMMDLL_PidGetFromName(handle_, const_cast<LPSTR>(kProcessName), &pid_);
                if (!pid_)
                    Sleep(250);
            }
            if (!pid_)
            {
                error = "EscapeFromTarkov.exe was not found. Open the game before resolving";
                return false;
            }

            if (!RefreshAll())
            {
                error = "The temporary DMA session could not refresh memory, translations and process information";
                return false;
            }
            pid_ = 0;
            (void)VMMDLL_PidGetFromName(handle_, const_cast<LPSTR>(kProcessName), &pid_);
            if (!pid_)
            {
                error = "EscapeFromTarkov.exe disappeared during the full refresh";
                return false;
            }

            PVMMDLL_MAP_MODULEENTRY module = nullptr;
            const bool found = VMMDLL_Map_GetModuleFromNameU(handle_, pid_, const_cast<LPSTR>(kModuleName), &module, VMMDLL_MODULE_FLAG_NORMAL) != FALSE;
            if (!found || !module)
            {
                error = "GameAssembly.dll was not available in the target process";
                return false;
            }
            moduleBase_ = module->vaBase;
            moduleSize_ = module->cbImageSize;
            VMMDLL_MemFree(module);
            if (!IsPointer(moduleBase_) || !moduleSize_)
            {
                error = "GameAssembly.dll returned invalid module information";
                return false;
            }
            return true;
        }

        bool RefreshAll() const
        {
            return handle_ && VMMDLL_ConfigSet(handle_, VMMDLL_OPT_REFRESH_ALL, 1) != FALSE;
        }

        bool Read(std::uint64_t address, void* buffer, std::size_t size) const
        {
            if (!handle_ || !pid_ || !IsPointer(address) || !buffer || !size || size > MAXDWORD)
                return false;
            DWORD bytesRead = 0;
            return VMMDLL_MemReadEx(handle_, pid_, address, static_cast<PBYTE>(buffer), static_cast<DWORD>(size), &bytesRead,
                                    VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_NOCACHEPUT | VMMDLL_FLAG_ZEROPAD_ON_FAIL) != FALSE && bytesRead == size;
        }

        template <typename T> bool Read(std::uint64_t address, T& value) const
        {
            value = {};
            return Read(address, &value, sizeof(value));
        }

        bool ReadScatter(const std::vector<ScatterRead>& reads) const
        {
            if (reads.empty())
                return true;
            const auto scatter = VMMDLL_Scatter_Initialize(handle_, pid_, VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_NOCACHEPUT);
            if (!scatter)
                return false;
            std::vector<DWORD> lengths(reads.size());
            bool prepared = false;
            for (std::size_t i = 0; i < reads.size(); ++i)
            {
                const auto& read = reads[i];
                if (!IsPointer(read.address) || !read.buffer || !read.size)
                    continue;
                prepared = VMMDLL_Scatter_PrepareEx(scatter, read.address, read.size, static_cast<PBYTE>(read.buffer), &lengths[i]) != FALSE || prepared;
            }
            const bool result = prepared && VMMDLL_Scatter_ExecuteRead(scatter) != FALSE;
            VMMDLL_Scatter_CloseHandle(scatter);
            return result;
        }

        [[nodiscard]] std::uint64_t ModuleBase() const { return moduleBase_; }
        [[nodiscard]] std::uint64_t ModuleSize() const { return moduleSize_; }

    private:
        VMM_HANDLE handle_{};
        DWORD pid_{};
        std::uint64_t moduleBase_{};
        std::uint64_t moduleSize_{};
    };

    std::string ReadString(const ResolverSession& session, std::uint64_t address)
    {
        if (!IsPointer(address))
            return {};
        std::array<char, kMaximumNameLength> buffer{};
        if (!session.Read(address, buffer.data(), buffer.size()))
            return {};
        const auto end = std::find(buffer.begin(), buffer.end(), '\0');
        if (end == buffer.begin() || end == buffer.end())
            return {};
        for (auto iterator = buffer.begin(); iterator != end; ++iterator)
        {
            const auto character = static_cast<unsigned char>(*iterator);
            if (character < 0x20 || character > 0x7E)
                return {};
        }
        return std::string(buffer.begin(), end);
    }

    struct PatternByte
    {
        std::uint8_t value{};
        bool wildcard{};
    };

    std::vector<PatternByte> ParsePattern(const char* pattern)
    {
        std::vector<PatternByte> result;
        std::istringstream stream(pattern ? pattern : "");
        std::string token;
        while (stream >> token)
        {
            if (token == "?" || token == "??")
                result.push_back({0, true});
            else
            {
                try
                {
                    result.push_back({static_cast<std::uint8_t>(std::stoul(token, nullptr, 16)), false});
                }
                catch (...)
                {
                    return {};
                }
            }
        }
        return result;
    }

    bool Matches(const std::uint8_t* data, const std::vector<PatternByte>& pattern)
    {
        for (std::size_t i = 0; i < pattern.size(); ++i)
            if (!pattern[i].wildcard && data[i] != pattern[i].value)
                return false;
        return true;
    }

    struct Signature
    {
        const char* pattern;
        std::uint32_t displacementOffset;
        std::uint32_t instructionLength;
        std::size_t limit;
    };

    std::vector<std::pair<std::uint64_t, const Signature*>> FindSignatures(const ResolverSession& session, const Signature* signatures, std::size_t signatureCount)
    {
        struct ParsedSignature
        {
            const Signature* source{};
            std::vector<PatternByte> bytes;
            std::size_t matches{};
        };
        std::vector<ParsedSignature> parsed;
        for (std::size_t i = 0; i < signatureCount; ++i)
            parsed.push_back({&signatures[i], ParsePattern(signatures[i].pattern), 0});

        struct Region { std::uint64_t address{}; std::size_t size{}; };
        std::vector<Region> regions;
        IMAGE_DOS_HEADER dos{};
        IMAGE_NT_HEADERS64 nt{};
        const auto base = session.ModuleBase();
        const auto imageSize = session.ModuleSize();
        if (session.Read(base, dos) && dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0 &&
            session.Read(base + static_cast<std::uint32_t>(dos.e_lfanew), nt) && nt.Signature == IMAGE_NT_SIGNATURE)
        {
            const auto sectionAddress = base + static_cast<std::uint32_t>(dos.e_lfanew) + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader;
            const auto sectionCount = (std::min)(nt.FileHeader.NumberOfSections, static_cast<WORD>(96));
            std::vector<IMAGE_SECTION_HEADER> sections(sectionCount);
            if (sectionCount && session.Read(sectionAddress, sections.data(), sections.size() * sizeof(IMAGE_SECTION_HEADER)))
            {
                for (const auto& section : sections)
                {
                    if ((section.Characteristics & (IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE)) == 0)
                        continue;
                    const auto size = static_cast<std::size_t>((std::max)(section.Misc.VirtualSize, section.SizeOfRawData));
                    if (size && static_cast<std::uint64_t>(section.VirtualAddress) + size <= imageSize)
                        regions.push_back({base + section.VirtualAddress, size});
                }
            }
        }
        if (regions.empty())
            regions.push_back({base, static_cast<std::size_t>(imageSize)});

        std::vector<std::pair<std::uint64_t, const Signature*>> results;
        constexpr std::size_t chunkSize = 1024 * 1024;
        std::size_t longest = 1;
        for (const auto& value : parsed)
            longest = (std::max)(longest, value.bytes.size());
        const auto overlap = longest - 1;
        for (const auto& region : regions)
        {
            for (std::size_t offset = 0; offset < region.size;)
            {
                const auto readSize = (std::min)(chunkSize + overlap, region.size - offset);
                std::vector<std::uint8_t> buffer(readSize);
                (void)session.Read(region.address + offset, buffer.data(), buffer.size());
                for (auto& value : parsed)
                {
                    if (value.bytes.empty() || value.matches >= value.source->limit)
                        continue;
                    for (std::size_t i = 0; i + value.bytes.size() <= buffer.size() && value.matches < value.source->limit; ++i)
                    {
                        if (!Matches(buffer.data() + i, value.bytes))
                            continue;
                        results.emplace_back(region.address + offset + i, value.source);
                        ++value.matches;
                    }
                }
                if (readSize <= overlap)
                    break;
                offset += readSize - overlap;
            }
        }
        return results;
    }

    bool IsValidClass(const ResolverSession& session, std::uint64_t klass)
    {
        std::uint64_t namePointer = 0;
        return IsPointer(klass) && session.Read(klass + 0x10, namePointer) && !ReadString(session, namePointer).empty();
    }

    bool ProbeTable(const ResolverSession& session, std::uint64_t table, std::size_t start, std::size_t count, std::size_t required)
    {
        std::vector<std::uint64_t> pointers(count);
        if (!session.Read(table + start * sizeof(std::uint64_t), pointers.data(), pointers.size() * sizeof(std::uint64_t)))
            return false;
        std::size_t valid = 0;
        for (const auto pointer : pointers)
            if (IsValidClass(session, pointer) && ++valid >= required)
                return true;
        return false;
    }

    bool ValidateTypeInfoTable(const ResolverSession& session, std::uint64_t rva)
    {
        if (!rva || rva >= session.ModuleSize())
            return false;
        std::uint64_t table = 0;
        return session.Read(session.ModuleBase() + rva, table) && IsPointer(table) &&
               ProbeTable(session, table, 0, 16, 8) && ProbeTable(session, table, 5000, 8, 3);
    }

    std::uint64_t ResolveTypeInfoTable(const ResolverSession& session)
    {
        const auto fallbackRva = sdk::Runtime::TypeInfoTableRva;

        constexpr Signature primary[] = {
            {"48 8B 05 ? ? ? ? ? ? ? ? ? ? ? 90 48 85 DB 75 ? 48 8D 2D ? ? ? ? 48 89 6C 24 ? 48 8B CD E8 ? ? ? ? 90 ? ? ? 48 85 DB 75 ? 8B CF", 3, 7, 64},
            {"48 8B 0D ? ? ? ? 48 63 D0 ? ? ? ? 48 85 C9 74", 3, 7, 64},
            {"48 89 05 ? ? ? ? 4C 8B 05 ? ? ? ? 48 8B 05 ? ? ? ? 48 63 48 ? BA ? ? ? ? 41 FF D0 48 89 05 ? ? ? ?", 3, 7, 64},
            {"48 89 05 ? ? ? ? 48 8B 05 ? ? ? ? 8B 48", 3, 7, 64},
            {"48 C1 E9 04 BA 08 00 00 00 ? ? ? 48 89 05 ? ? ? ? 48 8B 05 ? ? ? ?", 15, 19, 64},
        };
        constexpr Signature broad[] = {
            {"48 8B 05 ? ? ? ?", 3, 7, 20'000}, {"48 8B 0D ? ? ? ?", 3, 7, 20'000},
            {"48 8B 15 ? ? ? ?", 3, 7, 20'000}, {"4C 8B 05 ? ? ? ?", 3, 7, 20'000},
            {"4C 8B 0D ? ? ? ?", 3, 7, 20'000}, {"48 89 05 ? ? ? ?", 3, 7, 20'000},
            {"48 89 0D ? ? ? ?", 3, 7, 20'000}, {"4C 89 05 ? ? ? ?", 3, 7, 20'000},
        };

        std::unordered_set<std::uint64_t> seen;
        std::vector<std::uint64_t> candidates;
        const auto collectCandidates = [&](const Signature* values, std::size_t count)
        {
            for (const auto& [address, signature] : FindSignatures(session, values, count))
            {
                std::int32_t displacement = 0;
                if (!session.Read(address + signature->displacementOffset, displacement))
                    continue;
                const auto target = static_cast<std::uint64_t>(static_cast<std::int64_t>(address + signature->instructionLength) + displacement);
                if (target <= session.ModuleBase() || target >= session.ModuleBase() + session.ModuleSize())
                    continue;
                const auto rva = target - session.ModuleBase();
                if (seen.insert(rva).second)
                    candidates.push_back(rva);
            }
        };
        const auto validateCandidates = [&]() -> std::uint64_t
        {
            for (const auto rva : candidates)
                if (ValidateTypeInfoTable(session, rva))
                    return rva;
            return 0;
        };

        collectCandidates(primary, std::size(primary));
        for (int attempt = 0; !candidates.empty() && attempt < 20; ++attempt)
        {
            if (const auto rva = validateCandidates())
                return rva;
            if (attempt != 19)
                Sleep(250);
        }

        collectCandidates(broad, std::size(broad));
        if (const auto rva = validateCandidates())
            return rva;

        // Stored/configured values are deliberately last
        return ValidateTypeInfoTable(session, fallbackRva) ? fallbackRva : 0;
    }

    struct ClassRecord
    {
        std::uint64_t address{};
        std::uint64_t typeIndex{};
    };
    using ClassMap = std::unordered_map<std::string, ClassRecord>;

    std::string ShortName(std::string_view fullName)
    {
        const auto separator = fullName.rfind('.');
        return std::string(separator == std::string_view::npos ? fullName : fullName.substr(separator + 1));
    }

    ClassMap ReadTargetClasses(const ResolverSession& session, std::uint64_t table)
    {
        std::unordered_set<std::string> targetNames;
        for (const auto& descriptor : Descriptors())
            targetNames.insert(ShortName(descriptor.className));

        ClassMap classes;
        for (std::size_t offset = 0; offset < kMaximumClasses; offset += kClassChunkSize)
        {
            const auto count = (std::min)(kClassChunkSize, kMaximumClasses - offset);
            std::vector<std::uint64_t> pointers(count);
            if (!session.Read(table + offset * sizeof(std::uint64_t), pointers.data(), pointers.size() * sizeof(std::uint64_t)))
                break;

            struct NamePointers { std::uint64_t name{}, nameSpace{}; };
            std::vector<NamePointers> headers(count);
            std::vector<ScatterRead> headerReads;
            for (std::size_t i = 0; i < count; ++i)
                if (IsPointer(pointers[i]))
                    headerReads.push_back({pointers[i] + 0x10, &headers[i], sizeof(NamePointers)});
            if (headerReads.empty())
                break;
            (void)session.ReadScatter(headerReads);

            std::vector<std::array<char, kMaximumNameLength>> names(count), namespaces(count);
            std::vector<ScatterRead> stringReads;
            for (std::size_t i = 0; i < count; ++i)
            {
                if (IsPointer(headers[i].name))
                    stringReads.push_back({headers[i].name, names[i].data(), static_cast<DWORD>(names[i].size())});
                if (IsPointer(headers[i].nameSpace))
                    stringReads.push_back({headers[i].nameSpace, namespaces[i].data(), static_cast<DWORD>(namespaces[i].size())});
            }
            (void)session.ReadScatter(stringReads);

            for (std::size_t i = 0; i < count; ++i)
            {
                const auto nameEnd = std::find(names[i].begin(), names[i].end(), '\0');
                if (nameEnd == names[i].begin() || nameEnd == names[i].end())
                    continue;
                const std::string name(names[i].begin(), nameEnd);
                if (!targetNames.contains(name))
                    continue;
                const auto namespaceEnd = std::find(namespaces[i].begin(), namespaces[i].end(), '\0');
                const std::string nameSpace = namespaceEnd == namespaces[i].end() ? std::string{} : std::string(namespaces[i].begin(), namespaceEnd);
                const ClassRecord record{pointers[i], offset + i};
                classes.try_emplace(name, record);
                if (!nameSpace.empty())
                    classes.try_emplace(nameSpace + "." + name, record);
            }
        }
        return classes;
    }

#pragma pack(push, 8)
    struct RawFieldInfo
    {
        std::uint64_t name{};
        std::uint64_t type{};
        std::uint64_t parent{};
        std::int32_t offset{};
        std::uint32_t token{};
    };
#pragma pack(pop)
    static_assert(sizeof(RawFieldInfo) == 0x20);

    using FieldMap = std::unordered_map<std::string, std::uint64_t>;

    FieldMap ReadFields(const ResolverSession& session, std::uint64_t originalClass)
    {
        FieldMap fields;
        std::unordered_set<std::uint64_t> visited;
        std::uint64_t klass = originalClass;
        for (int depth = 0; depth < 32 && IsPointer(klass) && visited.insert(klass).second; ++depth)
        {
            std::uint16_t count = 0;
            std::uint64_t fieldPointer = 0;
            (void)session.Read(klass + 0x124, count);
            (void)session.Read(klass + 0x80, fieldPointer);
            if (count && count <= 4096 && IsPointer(fieldPointer))
            {
                std::vector<RawFieldInfo> raw(count);
                if (session.Read(fieldPointer, raw.data(), raw.size() * sizeof(RawFieldInfo)))
                {
                    std::vector<std::array<char, kMaximumNameLength>> names(count);
                    std::vector<ScatterRead> reads;
                    for (std::size_t i = 0; i < raw.size(); ++i)
                        if (IsPointer(raw[i].name))
                            reads.push_back({raw[i].name, names[i].data(), static_cast<DWORD>(names[i].size())});
                    (void)session.ReadScatter(reads);
                    for (std::size_t i = 0; i < raw.size(); ++i)
                    {
                        if (raw[i].offset < 0 || raw[i].offset >= 0x4000)
                            continue;
                        const auto end = std::find(names[i].begin(), names[i].end(), '\0');
                        if (end != names[i].begin() && end != names[i].end())
                            fields.try_emplace(std::string(names[i].begin(), end), static_cast<std::uint64_t>(raw[i].offset));
                    }
                }
            }
            std::uint64_t parent = 0;
            if (!session.Read(klass + 0x58, parent) || !IsPointer(parent))
                break;
            klass = parent;
        }
        return fields;
    }

    std::vector<std::string> FieldCandidates(const char* name)
    {
        std::vector<std::string> values;
        values.emplace_back(name ? name : "");
        const std::string source = values.front();
        if (source.size() > 18 && source.front() == '<' && source.ends_with(">k__BackingField"))
        {
            const std::string property = source.substr(1, source.size() - 17);
            values.push_back("_" + property + "_k__BackingField");
            values.push_back("_" + property);
            values.push_back(property);
        }
        else if (source.size() > 1 && source.front() == '_')
        {
            const std::string property = source.substr(1);
            values.push_back(property);
            values.push_back("<" + property + ">k__BackingField");
        }
        return values;
    }

    bool ResolveValues(const ResolverSession& session, const ClassMap& classes, std::vector<std::uint64_t>& values, std::vector<std::string>& missing)
    {
        std::unordered_map<std::uint64_t, FieldMap> fieldCache;
        const auto& descriptors = Descriptors();
        values.assign(descriptors.size(), 0);
        for (std::size_t i = 0; i < descriptors.size(); ++i)
        {
            const auto& descriptor = descriptors[i];
            const auto classIterator = classes.find(descriptor.className);
            if (classIterator == classes.end())
            {
                missing.push_back(std::string(descriptor.group) + "." + descriptor.name + " (class " + descriptor.className + ")");
                continue;
            }
            if (descriptor.kind == OffsetKind::TypeIndex)
            {
                values[i] = classIterator->second.typeIndex;
                continue;
            }

            const auto [fieldIterator, inserted] = fieldCache.try_emplace(classIterator->second.address);
            if (inserted)
                fieldIterator->second = ReadFields(session, classIterator->second.address);
            bool found = false;
            for (const auto& candidate : FieldCandidates(descriptor.fieldName))
            {
                const auto value = fieldIterator->second.find(candidate);
                if (value == fieldIterator->second.end())
                    continue;
                values[i] = value->second;
                found = true;
                break;
            }
            if (!found)
                missing.push_back(std::string(descriptor.group) + "." + descriptor.name + " (field " + descriptor.fieldName + ")");
        }

        if (missing.empty())
            return true;
        for (std::size_t i = 0; i < descriptors.size(); ++i)
            if (!descriptors[i].required && !values[i])
                values[i] = *descriptors[i].value;
        return std::none_of(missing.begin(), missing.end(), [&](const std::string& item)
        {
            for (const auto& descriptor : descriptors)
                if (descriptor.required && item.starts_with(std::string(descriptor.group) + "." + descriptor.name + " "))
                    return true;
            return false;
        });
    }

    nlohmann::ordered_json BuildJson(const std::vector<std::uint64_t>& values, std::uint64_t typeInfoRva,
                                     std::uint64_t moduleSize, std::uint32_t peTimestamp)
    {
        nlohmann::ordered_json existing;
        try
        {
            std::error_code pathError;
            const auto path = std::filesystem::current_path(pathError) / "configs" / "offsets.json";
            std::ifstream input(path, std::ios::binary);
            if (!pathError && input.is_open())
                existing = nlohmann::ordered_json::parse(input);
        }
        catch (...)
        {
            existing = nlohmann::ordered_json{};
        }

        nlohmann::ordered_json document;
        const bool hasUnity = existing.contains("unity") && existing["unity"].is_object();
        document["schemaVersion"] = hasUnity ? 2 : 1;
        document["generatedAtUtc"] = UtcNow();
        document["target"] = {{"process", kProcessName}, {"module", kModuleName}, {"imageSize", Hex(moduleSize)}, {"peTimestamp", Hex(peTimestamp)}};
        if (existing.contains("target") && existing["target"].is_object() && existing["target"].contains("modules"))
            document["target"]["modules"] = existing["target"]["modules"];
        if (hasUnity)
            document["unity"] = existing["unity"];
        document["bootstrap"] = {{"typeInfoTableRva", Hex(typeInfoRva)}, {"il2CppClassStaticFields", Hex(sdk::Runtime::Il2CppClassStaticFields)}};
        nlohmann::ordered_json offsets = nlohmann::ordered_json::object();
        const auto& descriptors = Descriptors();
        for (std::size_t i = 0; i < descriptors.size(); ++i)
            offsets[descriptors[i].group][descriptors[i].name] = Hex(values[i]);
        document["offsets"] = std::move(offsets);
        return document;
    }

    bool WriteAtomically(const std::filesystem::path& path, const std::string& contents, std::string& error)
    {
        std::error_code fileError;
        std::filesystem::create_directories(path.parent_path(), fileError);
        if (fileError)
        {
            error = "Could not create the configs folder: " + fileError.message();
            return false;
        }
        auto temporary = path;
        temporary += ".tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
            output.flush();
            if (!output.good())
            {
                error = "Could not write the temporary offsets file";
                return false;
            }
        }
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            error = "Could not replace offsets.json (Windows error " + std::to_string(GetLastError()) + ")";
            std::filesystem::remove(temporary, fileError);
            return false;
        }
        return true;
    }
}

EftOffsetService& EftOffsetService::Instance()
{
    static EftOffsetService instance;
    return instance;
}

EftOffsetService::~EftOffsetService()
{
    if (worker_.joinable())
        worker_.join();
}

std::filesystem::path EftOffsetService::FilePath() const
{
    std::error_code error;
    const auto current = std::filesystem::current_path(error);
    return (error ? std::filesystem::path{} : current) / "configs" / "offsets.json";
}

bool EftOffsetService::Load()
{
    const auto path = FilePath();
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open())
    {
        SetStatus("Using built-in offsets (offsets.json not found)");
        return false;
    }

    try
    {
        const nlohmann::json document = nlohmann::json::parse(input);
        const int schemaVersion = document.value("schemaVersion", 0);
        if ((schemaVersion != 1 && schemaVersion != 2) || !document.contains("bootstrap") || !document.contains("offsets"))
            throw std::runtime_error("unsupported or incomplete schema");

        std::uint64_t tableRva = 0, staticFields = 0;
        if (!ParseValue(document.at("bootstrap").at("typeInfoTableRva"), tableRva) ||
            !ParseValue(document.at("bootstrap").at("il2CppClassStaticFields"), staticFields) || !tableRva || !staticFields)
            throw std::runtime_error("invalid bootstrap values");

        const auto& descriptors = Descriptors();
        std::vector<std::uint64_t> values(descriptors.size());
        for (std::size_t i = 0; i < descriptors.size(); ++i)
        {
            const auto& descriptor = descriptors[i];
            const auto& value = document.at("offsets").at(descriptor.group).at(descriptor.name);
            if (!ParseValue(value, values[i]) || (descriptor.kind == OffsetKind::TypeIndex && !values[i]) ||
                (!descriptor.staticField && descriptor.kind == OffsetKind::Field && !values[i]))
                throw std::runtime_error(std::string("invalid value for ") + descriptor.group + "." + descriptor.name);
        }

        const auto normalized = nlohmann::ordered_json(document).dump(2) + "\n";
        {
            std::lock_guard lock(mutex_);
            for (std::size_t i = 0; i < descriptors.size(); ++i)
                *descriptors[i].value = values[i];
            sdk::Runtime::TypeInfoTableRva = tableRva;
            sdk::Runtime::Il2CppClassStaticFields = staticFields;
            jsonText_ = normalized;
        }
        SetStatus("Loaded configs/offsets.json");
        return true;
    }
    catch (const std::exception& exception)
    {
        SetStatus("Using built-in offsets", std::string("offsets.json was ignored: ") + exception.what());
        return false;
    }
}

void EftOffsetService::RefreshJsonText()
{
    std::ifstream input(FilePath(), std::ios::binary);
    if (!input.is_open())
        return;
    try
    {
        const auto normalized = nlohmann::ordered_json::parse(input).dump(2) + "\n";
        std::lock_guard lock(mutex_);
        jsonText_ = normalized;
    }
    catch (...)
    {
    }
}

bool EftOffsetService::StartResolve()
{
    if (UnityOffsetService::Instance().IsResolving())
    {
        SetStatus("Resolver unavailable", "Wait for the active-raid resolver to finish");
        return false;
    }
    if (memoryGlobals::dmaConnected.load(std::memory_order_acquire) || memoryGlobals::processFound.load(std::memory_order_acquire))
    {
        SetStatus("Resolver unavailable while DMA is connected", "Disconnect DMA before resolving offsets");
        return false;
    }

    bool expected = false;
    if (!resolving_.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return false;
    if (worker_.joinable())
        worker_.join();
    SetStatus("Starting full offset scan with the Unity stage...");
    worker_ = std::thread([this] { ResolveWorker(); });
    return true;
}

void EftOffsetService::ResolveWorker()
{
    struct Finish
    {
        std::atomic_bool& resolving;
        ~Finish() { resolving.store(false, std::memory_order_release); }
    } finish{resolving_};

    SetStatus("Stage 1/2: resolving and validating active-raid Unity offsets...");
    auto& unityService = UnityOffsetService::Instance();
    if (!unityService.ResolveForFullScan())
    {
        std::string unityError = unityService.LastError();
        if (unityError.empty())
            unityError = unityService.Status();
        SetStatus("Full scan failed during the Unity stage; existing managed offsets were kept", unityError);
        return;
    }

    SetStatus("Stage 2/2: opening a fresh managed resolver session...");
    ResolverSession session;
    std::string error;
    if (!session.Open(error))
    {
        SetStatus("Full scan failed during the managed stage", error);
        return;
    }

    SetStatus("Stage 2/2: signature-scanning the IL2CPP type table...");
    const auto tableRva = ResolveTypeInfoTable(session);
    if (!tableRva)
    {
        SetStatus("Full scan failed during the managed stage", "The GameAssembly type information table could not be located or validated by signatures or the stored fallback");
        return;
    }

    std::uint64_t table = 0;
    if (!session.Read(session.ModuleBase() + tableRva, table) || !IsPointer(table))
    {
        SetStatus("Full scan failed during the managed stage", "The resolved type table pointer could not be read");
        return;
    }

    SetStatus("Stage 2/2: reading target IL2CPP classes and fields...");
    const auto classes = ReadTargetClasses(session, table);
    std::vector<std::uint64_t> values;
    std::vector<std::string> missing;
    if (!ResolveValues(session, classes, values, missing))
    {
        std::ostringstream message;
        message << "Required offsets were not resolved (" << missing.size() << "): ";
        const auto shown = (std::min)(missing.size(), static_cast<std::size_t>(8));
        for (std::size_t i = 0; i < shown; ++i)
            message << (i ? ", " : "") << missing[i];
        if (missing.size() > shown)
            message << ", ...";
        SetStatus("Full scan failed during the managed stage; existing managed offsets were kept", message.str());
        return;
    }

    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    std::uint32_t timestamp = 0;
    if (session.Read(session.ModuleBase(), dos) && dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0 &&
        session.Read(session.ModuleBase() + static_cast<std::uint32_t>(dos.e_lfanew), nt) && nt.Signature == IMAGE_NT_SIGNATURE)
        timestamp = nt.FileHeader.TimeDateStamp;

    const auto document = BuildJson(values, tableRva, session.ModuleSize(), timestamp);
    const std::string jsonText = document.dump(2) + "\n";
    if (!WriteAtomically(FilePath(), jsonText, error))
    {
        SetStatus("Full scan failed while saving; existing managed offsets were kept", error);
        return;
    }

    {
        std::lock_guard lock(mutex_);
        const auto& descriptors = Descriptors();
        for (std::size_t i = 0; i < descriptors.size(); ++i)
            *descriptors[i].value = values[i];
        sdk::Runtime::TypeInfoTableRva = tableRva;
        jsonText_ = jsonText;
    }
    SetStatus("Full scan complete: loaded 7 Unity and " + std::to_string(values.size()) + " EFT offsets");
}

bool EftOffsetService::IsResolving() const noexcept
{
    return resolving_.load(std::memory_order_acquire);
}

void EftOffsetService::SetStatus(std::string status, std::string error)
{
    std::lock_guard lock(mutex_);
    status_ = std::move(status);
    lastError_ = std::move(error);
}

std::string EftOffsetService::Status() const
{
    std::lock_guard lock(mutex_);
    return status_;
}

std::string EftOffsetService::LastError() const
{
    std::lock_guard lock(mutex_);
    return lastError_;
}

std::string EftOffsetService::JsonText() const
{
    std::lock_guard lock(mutex_);
    return jsonText_;
}

std::vector<EftOffsetView> EftOffsetService::Entries() const
{
    std::lock_guard lock(mutex_);
    std::vector<EftOffsetView> values;
    values.reserve(Descriptors().size());
    for (const auto& descriptor : Descriptors())
        values.push_back({descriptor.group, descriptor.name, descriptor.className, descriptor.fieldName, *descriptor.value,
                          descriptor.kind == OffsetKind::TypeIndex, descriptor.staticField});
    return values;
}


#include "UnityOffsetService.h"

#include "EftOffsetService.h"
#include "../../Tarkov/Unity/UnityOffsets.h"
#include "../../UI/globals.h"
#include "../../external/nlohmann/json.hpp"
#include "../../libs/vmmdll.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <unordered_set>

namespace
{
    constexpr char kProcessName[] = "EscapeFromTarkov.exe";
    constexpr char kUnityPlayerName[] = "UnityPlayer.dll";
    constexpr std::size_t kMaximumCameraCount = 4096;

    enum class UnityValue : std::size_t
    {
        GameObjectManager,
        AllCameras,
        LastActiveNode,
        ActiveNodes,
        ViewMatrix,
        Fov,
        AspectRatio,
        Count
    };

    constexpr std::size_t kUnityValueCount = static_cast<std::size_t>(UnityValue::Count);

    struct UnityDescriptor
    {
        const char* group;
        const char* name;
        const char* module;
        const char* kind;
        const char* jsonCategory;
        const char* jsonGroup;
        const char* jsonName;
        std::uint64_t* runtimeValue;
    };

    std::array<UnityDescriptor, kUnityValueCount>& Descriptors()
    {
        static std::array<UnityDescriptor, kUnityValueCount> descriptors = {{
            {"Unity RVA / UnityPlayer.dll", "GameObjectManager", kUnityPlayerName, "RVA", "rvas", kUnityPlayerName, "gameObjectManager", &UnityOffsets::GameObjectManager},
            {"Unity RVA / UnityPlayer.dll", "AllCameras", kUnityPlayerName, "RVA", "rvas", kUnityPlayerName, "allCameras", &UnityOffsets::AllCamera},
            {"Unity layout / GameObjectManager", "LastActiveNode", "", "Member", "layouts", "GameObjectManager", "lastActiveNode", &UnityOffsets::GameObjectManager_LastActiveNodeOffset},
            {"Unity layout / GameObjectManager", "ActiveNodes", "", "Member", "layouts", "GameObjectManager", "activeNodes", &UnityOffsets::GameObjectManager_ActiveNodesOffset},
            {"Unity layout / Camera", "ViewMatrix", "", "Member", "layouts", "Camera", "viewMatrix", &UnityOffsets::Camera_ViewMatrixOffset},
            {"Unity layout / Camera", "FOV", "", "Member", "layouts", "Camera", "fov", &UnityOffsets::Camera_FOVOffset},
            {"Unity layout / Camera", "AspectRatio", "", "Member", "layouts", "Camera", "aspectRatio", &UnityOffsets::Camera_AspectRatioOffset}
        }};
        return descriptors;
    }

    constexpr std::size_t Index(UnityValue value)
    {
        return static_cast<std::size_t>(value);
    }

    bool IsPointer(std::uint64_t value)
    {
        return value >= 0x10000 && value <= 0x00007FFFFFFFFFFF;
    }

    std::string Hex(std::uint64_t value)
    {
        std::ostringstream stream;
        stream << "0x" << std::uppercase << std::hex << value;
        return stream.str();
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

    class ResolverSession
    {
    public:
        struct ScanChunk
        {
            std::uint64_t address{};
            std::vector<std::uint8_t> bytes;
        };

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
                error = "EscapeFromTarkov.exe was not found. Open the game and enter an active raid before resolving";
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
                error = "EscapeFromTarkov.exe disappeared during the full refresh.";
                return false;
            }

            PVMMDLL_MAP_MODULEENTRY module = nullptr;
            const bool found = VMMDLL_Map_GetModuleFromNameU(handle_, pid_, const_cast<LPSTR>(kUnityPlayerName), &module, VMMDLL_MODULE_FLAG_NORMAL) != FALSE;
            if (!found || !module)
            {
                error = "UnityPlayer.dll was not available in the target process";
                return false;
            }
            moduleBase_ = module->vaBase;
            moduleSize_ = module->cbImageSize;
            VMMDLL_MemFree(module);
            if (!IsPointer(moduleBase_) || !moduleSize_)
            {
                error = "UnityPlayer.dll returned invalid module information";
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

        [[nodiscard]] std::uint64_t ModuleBase() const { return moduleBase_; }
        [[nodiscard]] std::uint64_t ModuleSize() const { return moduleSize_; }

        const std::vector<ScanChunk>& ScanChunks() const
        {
            if (scanChunksLoaded_)
                return scanChunks_;
            scanChunksLoaded_ = true;

            struct Region { std::uint64_t address{}; std::size_t size{}; };
            std::vector<Region> regions;
            IMAGE_DOS_HEADER dos{};
            IMAGE_NT_HEADERS64 nt{};
            if (Read(moduleBase_, dos) && dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0 &&
                Read(moduleBase_ + static_cast<std::uint32_t>(dos.e_lfanew), nt) && nt.Signature == IMAGE_NT_SIGNATURE)
            {
                const auto sectionAddress = moduleBase_ + static_cast<std::uint32_t>(dos.e_lfanew) + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader;
                const auto sectionCount = (std::min)(nt.FileHeader.NumberOfSections, static_cast<WORD>(96));
                std::vector<IMAGE_SECTION_HEADER> sections(sectionCount);
                if (sectionCount && Read(sectionAddress, sections.data(), sections.size() * sizeof(IMAGE_SECTION_HEADER)))
                {
                    for (const auto& section : sections)
                    {
                        if ((section.Characteristics & (IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE)) == 0)
                            continue;
                        const auto size = static_cast<std::size_t>((std::max)(section.Misc.VirtualSize, section.SizeOfRawData));
                        if (size && static_cast<std::uint64_t>(section.VirtualAddress) + size <= moduleSize_)
                            regions.push_back({moduleBase_ + section.VirtualAddress, size});
                    }
                }
            }
            if (regions.empty())
                regions.push_back({moduleBase_, static_cast<std::size_t>(moduleSize_)});

            constexpr std::size_t chunkSize = 1024 * 1024;
            constexpr std::size_t overlap = 128;
            for (const auto& region : regions)
            {
                for (std::size_t offset = 0; offset < region.size;)
                {
                    const auto readSize = (std::min)(chunkSize + overlap, region.size - offset);
                    ScanChunk chunk;
                    chunk.address = region.address + offset;
                    chunk.bytes.resize(readSize);
                    (void)Read(chunk.address, chunk.bytes.data(), chunk.bytes.size());
                    scanChunks_.push_back(std::move(chunk));
                    if (readSize <= overlap)
                        break;
                    offset += readSize - overlap;
                }
            }
            return scanChunks_;
        }

    private:
        VMM_HANDLE handle_{};
        DWORD pid_{};
        std::uint64_t moduleBase_{};
        std::uint64_t moduleSize_{};
        mutable bool scanChunksLoaded_{};
        mutable std::vector<ScanChunk> scanChunks_;
    };

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
        const char* name;
        const char* pattern;
        std::uint32_t displacementOffset;
        std::uint32_t instructionLength;
    };

    std::vector<std::uint64_t> FindPattern(const ResolverSession& session, const char* text, std::size_t limit = 64)
    {
        const auto pattern = ParsePattern(text);
        if (pattern.empty())
            return {};

        std::vector<std::uint64_t> matches;
        std::unordered_set<std::uint64_t> seen;
        for (const auto& chunk : session.ScanChunks())
        {
            for (std::size_t i = 0; i + pattern.size() <= chunk.bytes.size() && matches.size() < limit; ++i)
            {
                const auto address = chunk.address + i;
                if (Matches(chunk.bytes.data() + i, pattern) && seen.insert(address).second)
                    matches.push_back(address);
            }
            if (matches.size() >= limit)
                break;
        }
        return matches;
    }

    std::uint64_t DecodeRelative(const ResolverSession& session, std::uint64_t instruction, std::uint32_t displacementOffset, std::uint32_t instructionLength)
    {
        std::int32_t displacement = 0;
        if (!session.Read(instruction + displacementOffset, displacement))
            return 0;
        return static_cast<std::uint64_t>(static_cast<std::int64_t>(instruction + instructionLength) + displacement);
    }

    bool InModule(const ResolverSession& session, std::uint64_t address)
    {
        return address >= session.ModuleBase() && address - session.ModuleBase() < session.ModuleSize();
    }

    struct GomValidation
    {
        bool valid{};
        std::uint64_t lastActiveOffset{};
        std::uint64_t activeNodesOffset{};
    };

    GomValidation ValidateGom(const ResolverSession& session, std::uint64_t slot)
    {
        std::uint64_t manager = 0;
        if (!session.Read(slot, manager) || !IsPointer(manager))
            return {};
        constexpr std::array<std::pair<std::uint64_t, std::uint64_t>, 2> layouts = {{{0x20, 0x28}, {0x28, 0x30}}};
        for (const auto& [lastOffset, activeOffset] : layouts)
        {
            std::uint64_t last = 0, active = 0, firstObject = 0, lastObject = 0;
            if (session.Read(manager + lastOffset, last) && session.Read(manager + activeOffset, active) && IsPointer(last) && IsPointer(active) &&
                session.Read(active + 0x10, firstObject) && session.Read(last + 0x10, lastObject) && IsPointer(firstObject) && IsPointer(lastObject))
                return {true, lastOffset, activeOffset};
        }
        return {};
    }

    struct CameraContainer
    {
        bool valid{};
        std::uint64_t firstCamera{};
    };

    CameraContainer ValidateAllCameras(const ResolverSession& session, std::uint64_t slot)
    {
        std::uint64_t container = 0;
        if (!session.Read(slot, container) || !IsPointer(container))
            return {};

        struct Layout { std::uint64_t items, count, capacity; bool hasCapacity; };
        constexpr std::array<Layout, 2> layouts = {{{0x0, 0x8, 0x0, false}, {0x0, 0x10, 0x18, true}}};
        for (const auto& layout : layouts)
        {
            std::uint64_t items = 0, capacity = 0, firstCamera = 0;
            std::uint32_t count = 0;
            if (!session.Read(container + layout.items, items) || !session.Read(container + layout.count, count) || count == 0 || count > kMaximumCameraCount)
                continue;
            if (layout.hasCapacity && (!session.Read(container + layout.capacity, capacity) || capacity < count || capacity > kMaximumCameraCount * 16))
                continue;
            if (IsPointer(items) && session.Read(items, firstCamera) && IsPointer(firstCamera))
                return {true, firstCamera};
        }
        return {};
    }

    struct GlobalResult
    {
        std::uint64_t rva{};
        std::string method;
        GomValidation gom;
        CameraContainer cameras;
    };

    std::optional<GlobalResult> ResolveGom(const ResolverSession& session)
    {
        constexpr Signature direct[] = {
            {"GOM init store", "48 89 05 ? ? ? ? 48 83 C4 ? C3 33 C9", 3, 7},
            {"GOM rbp store", "48 89 2D ? ? ? ? 48 8B 6C 24 ? 48 83 C4 ? 5E C3 33 ED", 3, 7},
            {"GOM rsi read", "48 8B 35 ? ? ? ? 48 85 F6 0F 84 ? ? ? ? 8B 46", 3, 7},
            {"GOM rdx read", "48 8B 15 ? ? ? ? 48 83 C2 ? 48 3B DA", 3, 7},
            {"GOM rcx read", "48 8B 0D ? ? ? ? 4C 8D 4C 24 ? 4C 8D 44 24 ? 89 44 24", 3, 7}
        };
        constexpr Signature callSites[] = {
            {"GOM getter call 1", "E8 ? ? ? ? 4C 8D 45 ? 89 5D ? 48 8D 55", 1, 5},
            {"GOM getter call 2", "E8 ? ? ? ? 8B 48 ? ? ? ? ? ? ? ? 48 8D 77", 1, 5},
            {"GOM getter call 3", "E8 ? ? ? ? 48 8B 58 ? 48 8D 78 ? 48 3B DF 74 ? ? ? ? 48 8B 53", 1, 5}
        };

        struct Candidate { std::set<std::string> families; GomValidation validation; };
        std::map<std::uint64_t, Candidate> candidates;
        for (const auto& signature : direct)
        {
            for (const auto match : FindPattern(session, signature.pattern))
            {
                const auto slot = DecodeRelative(session, match, signature.displacementOffset, signature.instructionLength);
                if (InModule(session, slot))
                    candidates[slot].families.insert(signature.name);
            }
        }
        for (const auto& signature : callSites)
        {
            for (const auto match : FindPattern(session, signature.pattern))
            {
                const auto getter = DecodeRelative(session, match, signature.displacementOffset, signature.instructionLength);
                std::array<std::uint8_t, 3> prefix{};
                if (!InModule(session, getter) || !session.Read(getter, prefix.data(), prefix.size()) || prefix != std::array<std::uint8_t, 3>{0x48, 0x8B, 0x05})
                    continue;
                const auto slot = DecodeRelative(session, getter, 3, 7);
                if (InModule(session, slot))
                    candidates[slot].families.insert(signature.name);
            }
        }

        std::uint64_t bestSlot = 0;
        Candidate* best = nullptr;
        bool ambiguous = false;
        for (auto& [slot, candidate] : candidates)
        {
            candidate.validation = ValidateGom(session, slot);
            if (candidate.validation.valid && (!best || candidate.families.size() > best->families.size()))
            {
                bestSlot = slot;
                best = &candidate;
                ambiguous = false;
            }
            else if (candidate.validation.valid && best && candidate.families.size() == best->families.size())
                ambiguous = true;
        }
        if (!best || ambiguous)
            return std::nullopt;
        return GlobalResult{bestSlot - session.ModuleBase(), *best->families.begin() + " + live GOM validation", best->validation, {}};
    }

    std::optional<GlobalResult> ResolveAllCameras(const ResolverSession& session)
    {
        constexpr Signature signatures[] = {
            {"AllCameras iterator", "4C 8B 05 ? ? ? ? 33 D2 49 8B 48 10 48 85 C9 74 ? 4D 8B 08", 3, 7},
            {"AllCameras indexed lookup", "48 8B 05 ? ? ? ? 48 8B 38 48 8B 3C 3E 8B 87 74 03 00 00", 3, 7},
            {"AllCameras count check", "48 8B 05 ? ? ? ? 49 C7 C6 FF FF FF FF 8B 48 10 85 C9 0F 84 ? ? ? ?", 3, 7},
            {"AllCameras rbx iterator", "48 8B 1D ? ? ? ? 48 8B 73 ? 48 8B 43 ? 48 FF C6", 3, 7}
        };
        struct Candidate { std::set<std::string> families; CameraContainer validation; };
        std::map<std::uint64_t, Candidate> candidates;
        for (const auto& signature : signatures)
        {
            for (const auto match : FindPattern(session, signature.pattern))
            {
                const auto slot = DecodeRelative(session, match, signature.displacementOffset, signature.instructionLength);
                if (InModule(session, slot))
                    candidates[slot].families.insert(signature.name);
            }
        }

        std::uint64_t bestSlot = 0;
        Candidate* best = nullptr;
        bool ambiguous = false;
        for (auto& [slot, candidate] : candidates)
        {
            candidate.validation = ValidateAllCameras(session, slot);
            if (candidate.validation.valid && (!best || candidate.families.size() > best->families.size()))
            {
                bestSlot = slot;
                best = &candidate;
                ambiguous = false;
            }
            else if (candidate.validation.valid && best && candidate.families.size() == best->families.size())
                ambiguous = true;
        }
        if (!best || ambiguous)
            return std::nullopt;
        return GlobalResult{bestSlot - session.ModuleBase(), *best->families.begin() + " + live camera-list validation", {}, best->validation};
    }

    bool ValidateMatrix(const ResolverSession& session, std::uint64_t camera, std::uint64_t offset)
    {
        std::array<float, 16> matrix{};
        if (!IsPointer(camera) || !session.Read(camera + offset, matrix.data(), sizeof(matrix)))
            return false;
        bool anyNonZero = false;
        for (const auto value : matrix)
        {
            if (!std::isfinite(value) || std::abs(value) > 1.0e7f)
                return false;
            anyNonZero = anyNonZero || std::abs(value) > 0.000001f;
        }
        return anyNonZero;
    }

    bool ValidateFloat(const ResolverSession& session, std::uint64_t camera, std::uint64_t offset, float minimum, float maximum)
    {
        float value = 0.0f;
        return IsPointer(camera) && session.Read(camera + offset, value) && std::isfinite(value) && value > minimum && value < maximum;
    }

    struct MemberPattern
    {
        const char* name;
        const char* pattern;
        int valueOffset;
        bool callTarget;
        int targetValueOffset;
    };

    struct MemberResult
    {
        std::uint64_t value{};
        std::string method;
    };

    std::optional<MemberResult> ResolveMember(const ResolverSession& session, std::uint64_t camera, const MemberPattern& pattern,
                                               const std::function<bool(std::uint64_t)>& validator)
    {
        struct Candidate { std::size_t matches{}; std::uint64_t firstMatch{}; };
        std::map<std::uint64_t, Candidate> candidates;
        for (const auto match : FindPattern(session, pattern.pattern))
        {
            std::uint64_t source = match;
            int valueOffset = pattern.valueOffset;
            if (pattern.callTarget)
            {
                source = DecodeRelative(session, match + pattern.valueOffset, 1, 5);
                valueOffset = pattern.targetValueOffset;
                if (!InModule(session, source))
                    continue;
                if (valueOffset == 3)
                {
                    std::array<std::uint8_t, 3> prefix{};
                    if (!session.Read(source, prefix.data(), prefix.size()) || prefix != std::array<std::uint8_t, 3>{0x48, 0x8D, 0x81})
                        continue;
                }
                else if (valueOffset == 4)
                {
                    std::array<std::uint8_t, 4> prefix{};
                    if (!session.Read(source, prefix.data(), prefix.size()) || prefix != std::array<std::uint8_t, 4>{0xF3, 0x0F, 0x10, 0x81})
                        continue;
                }
            }

            std::uint32_t value = 0;
            if (!session.Read(source + valueOffset, value) || value < 0x10 || value >= 0x1000 || !validator(value))
                continue;
            auto& candidate = candidates[value];
            ++candidate.matches;
            if (!candidate.firstMatch)
                candidate.firstMatch = match;
        }

        if (candidates.empty())
            return std::nullopt;
        const auto best = std::max_element(candidates.begin(), candidates.end(), [](const auto& left, const auto& right)
        {
            return left.second.matches < right.second.matches;
        });
        if (std::count_if(candidates.begin(), candidates.end(), [&](const auto& candidate) { return candidate.second.matches == best->second.matches; }) != 1)
            return std::nullopt;
        return MemberResult{best->first, std::string(pattern.name) + " signature extraction + live camera validation"};
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
                error = "Could not write the temporary offsets file.";
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

    void ApplyValues(const std::array<std::uint64_t, kUnityValueCount>& values)
    {
        const auto& descriptors = Descriptors();
        for (std::size_t i = 0; i < descriptors.size(); ++i)
            *descriptors[i].runtimeValue = values[i];
        UnityOffsets::Camera_CullingMatrixOffset = values[Index(UnityValue::ViewMatrix)];
    }

    std::vector<UnityOffsetView> MakeEntries(const std::array<std::uint64_t, kUnityValueCount>& values,
                                             const std::array<std::string, kUnityValueCount>& methods,
                                             const std::array<bool, kUnityValueCount>& validated)
    {
        std::vector<UnityOffsetView> entries;
        const auto& descriptors = Descriptors();
        entries.reserve(descriptors.size());
        for (std::size_t i = 0; i < descriptors.size(); ++i)
            entries.push_back({descriptors[i].group, descriptors[i].name, descriptors[i].module, descriptors[i].kind, methods[i], values[i], validated[i]});
        return entries;
    }
}

UnityOffsetService& UnityOffsetService::Instance()
{
    static UnityOffsetService instance;
    return instance;
}

UnityOffsetService::~UnityOffsetService()
{
    if (worker_.joinable())
        worker_.join();
}

std::filesystem::path UnityOffsetService::FilePath() const
{
    std::error_code error;
    const auto current = std::filesystem::current_path(error);
    return (error ? std::filesystem::path{} : current) / "configs" / "offsets.json";
}

bool UnityOffsetService::Load()
{
    std::ifstream input(FilePath(), std::ios::binary);
    if (!input.is_open())
    {
        SetStatus("Using built-in Unity offsets (offsets.json not found)");
        return false;
    }

    try
    {
        const nlohmann::json document = nlohmann::json::parse(input);
        if (!document.contains("unity"))
        {
            SetStatus("Using built-in Unity offsets (no Unity section in offsets.json)");
            return false;
        }

        const auto& unity = document.at("unity");
        const auto& descriptors = Descriptors();
        std::array<std::uint64_t, kUnityValueCount> values{};
        std::array<std::string, kUnityValueCount> methods;
        std::array<bool, kUnityValueCount> validated{};
        for (std::size_t i = 0; i < descriptors.size(); ++i)
        {
            const auto& descriptor = descriptors[i];
            if (!ParseValue(unity.at(descriptor.jsonCategory).at(descriptor.jsonGroup).at(descriptor.jsonName), values[i]) || !values[i])
                throw std::runtime_error(std::string("invalid Unity value: ") + descriptor.name);
            methods[i] = "Loaded from offsets.json";
            if (unity.contains("resolution") && unity["resolution"].contains(descriptor.name))
            {
                const auto& resolution = unity["resolution"][descriptor.name];
                methods[i] = resolution.value("method", methods[i]);
                validated[i] = resolution.value("runtimeValidated", false);
            }
        }
        ApplyValues(values);
        {
            std::lock_guard lock(mutex_);
            entries_ = MakeEntries(values, methods, validated);
        }
        EftOffsetService::Instance().RefreshJsonText();
        SetStatus("Loaded active-raid offsets from configs/offsets.json");
        return true;
    }
    catch (const std::exception& exception)
    {
        SetStatus("Using built-in Unity offsets", std::string("The Unity section was ignored: ") + exception.what());
        return false;
    }
}

bool UnityOffsetService::StartResolve()
{
    if (EftOffsetService::Instance().IsResolving())
    {
        SetStatus("Resolver unavailable", "Wait for the EFT SDK resolver to finish.");
        return false;
    }
    if (memoryGlobals::dmaConnected.load(std::memory_order_acquire) || memoryGlobals::processFound.load(std::memory_order_acquire))
    {
        SetStatus("Resolver unavailable while DMA is connected", "Disconnect DMA before resolving active-raid offsets");
        return false;
    }
    bool expected = false;
    if (!resolving_.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return false;
    if (worker_.joinable())
        worker_.join();
    SetStatus("Opening an isolated DMA session and performing a full refresh...");
    worker_ = std::thread([this] { ResolveWorker(); });
    return true;
}

void UnityOffsetService::ResolveWorker()
{
    struct Finish
    {
        std::atomic_bool& resolving;
        ~Finish() { resolving.store(false, std::memory_order_release); }
    } finish{resolving_};

    (void)ResolveNow();
}

bool UnityOffsetService::ResolveForFullScan()
{
    if (resolving_.load(std::memory_order_acquire))
        return false;
    if (worker_.joinable())
        worker_.join();
    return ResolveNow();
}

bool UnityOffsetService::ResolveNow()
{
    ResolverSession session;
    std::string error;
    if (!session.Open(error))
    {
        SetStatus("Active-raid resolve failed", error);
        return false;
    }

    SetStatus("Resolving UnityPlayer globals from signatures...");
    const auto gom = ResolveGom(session);
    if (!gom)
    {
        SetStatus("Active-raid resolve failed; existing offsets were kept", "GameObjectManager could not be signature-resolved and validated against the live raid");
        return false;
    }
    const auto allCameras = ResolveAllCameras(session);
    if (!allCameras)
    {
        SetStatus("Active-raid resolve failed; existing offsets were kept", "AllCameras could not be signature-resolved and validated. Make sure a raid is fully loaded");
        return false;
    }

    SetStatus("Extracting and validating live camera member offsets...");
    const MemberPattern viewPattern{"Camera view-matrix getter", "E8 ? ? ? ? 48 3B 58 ? 0F 83 ? ? ? ? ? ? ? 48 8D 0C 5D ? ? ? ? 48 03 CB ? ? ? ? E8 ? ? ? ? 4C 8B C7 49 FF C0 ? ? ? ? ? 75", 0, true, 3};
    const MemberPattern fovPattern{"Camera FOV getter", "83 B9 ? ? ? ? 02 75 ? F3 0F 10 81 ? ? ? ? C3 F3 0F 10 81 ? ? ? ? C3", 22, false, 0};
    const MemberPattern aspectPattern{"Camera aspect getter", "E8 ? ? ? ? F3 44 0F 59 05 ? ? ? ? F3 0F 59 C6", 0, true, 4};
    const auto camera = allCameras->cameras.firstCamera;
    const auto view = ResolveMember(session, camera, viewPattern, [&](const auto value) { return ValidateMatrix(session, camera, value); });
    const auto fov = ResolveMember(session, camera, fovPattern, [&](const auto value) { return ValidateFloat(session, camera, value, 1.0f, 180.0f); });
    const auto aspect = ResolveMember(session, camera, aspectPattern, [&](const auto value) { return ValidateFloat(session, camera, value, 0.1f, 5.0f); });
    if (!view || !fov || !aspect)
    {
        std::string missing;
        if (!view) missing += "ViewMatrix";
        if (!fov) missing += (missing.empty() ? "" : ", ") + std::string("FOV");
        if (!aspect) missing += (missing.empty() ? "" : ", ") + std::string("AspectRatio");
        SetStatus("Active-raid resolve failed; existing offsets were kept", "Camera offsets not resolved and live-validated: " + missing + ".");
        return false;
    }

    std::array<std::uint64_t, kUnityValueCount> values{};
    values[Index(UnityValue::GameObjectManager)] = gom->rva;
    values[Index(UnityValue::AllCameras)] = allCameras->rva;
    values[Index(UnityValue::LastActiveNode)] = gom->gom.lastActiveOffset;
    values[Index(UnityValue::ActiveNodes)] = gom->gom.activeNodesOffset;
    values[Index(UnityValue::ViewMatrix)] = view->value;
    values[Index(UnityValue::Fov)] = fov->value;
    values[Index(UnityValue::AspectRatio)] = aspect->value;

    std::array<std::string, kUnityValueCount> methods{};
    methods[Index(UnityValue::GameObjectManager)] = gom->method;
    methods[Index(UnityValue::AllCameras)] = allCameras->method;
    methods[Index(UnityValue::LastActiveNode)] = "Detected from live GOM layout";
    methods[Index(UnityValue::ActiveNodes)] = "Detected from live GOM layout";
    methods[Index(UnityValue::ViewMatrix)] = view->method;
    methods[Index(UnityValue::Fov)] = fov->method;
    methods[Index(UnityValue::AspectRatio)] = aspect->method;
    std::array<bool, kUnityValueCount> validated{};
    validated.fill(true);

    nlohmann::ordered_json existing;
    try
    {
        std::ifstream input(FilePath(), std::ios::binary);
        if (input.is_open())
            existing = nlohmann::ordered_json::parse(input);
    }
    catch (...)
    {
        existing = nlohmann::ordered_json{};
    }
    nlohmann::ordered_json target = existing.contains("target") && existing["target"].is_object()
        ? existing["target"] : nlohmann::ordered_json::object();
    target["process"] = kProcessName;

    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    std::uint32_t timestamp = 0;
    if (session.Read(session.ModuleBase(), dos) && dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0 &&
        session.Read(session.ModuleBase() + static_cast<std::uint32_t>(dos.e_lfanew), nt) && nt.Signature == IMAGE_NT_SIGNATURE)
        timestamp = nt.FileHeader.TimeDateStamp;
    target["modules"][kUnityPlayerName] = {{"imageSize", Hex(session.ModuleSize())}, {"peTimestamp", Hex(timestamp)}};

    nlohmann::ordered_json unity;
    const auto& descriptors = Descriptors();
    for (std::size_t i = 0; i < descriptors.size(); ++i)
    {
        const auto& descriptor = descriptors[i];
        unity[descriptor.jsonCategory][descriptor.jsonGroup][descriptor.jsonName] = Hex(values[i]);
        unity["resolution"][descriptor.name] = {{"method", methods[i]}, {"runtimeValidated", true}};
    }
    nlohmann::ordered_json document;
    document["schemaVersion"] = 2;
    document["generatedAtUtc"] = UtcNow();
    document["target"] = std::move(target);
    document["unity"] = std::move(unity);
    if (existing.contains("bootstrap"))
        document["bootstrap"] = existing["bootstrap"];
    if (existing.contains("offsets"))
        document["offsets"] = existing["offsets"];

    const std::string jsonText = document.dump(2) + "\n";
    if (!WriteAtomically(FilePath(), jsonText, error))
    {
        SetStatus("Active-raid resolve failed; existing offsets were kept", error);
        return false;
    }

    ApplyValues(values);
    {
        std::lock_guard lock(mutex_);
        entries_ = MakeEntries(values, methods, validated);
    }
    EftOffsetService::Instance().RefreshJsonText();
    SetStatus("Resolved and loaded 7 active-raid offsets");
    return true;
}

bool UnityOffsetService::IsResolving() const noexcept
{
    return resolving_.load(std::memory_order_acquire);
}

void UnityOffsetService::SetStatus(std::string status, std::string error)
{
    std::lock_guard lock(mutex_);
    status_ = std::move(status);
    lastError_ = std::move(error);
}

std::string UnityOffsetService::Status() const
{
    std::lock_guard lock(mutex_);
    return status_;
}

std::string UnityOffsetService::LastError() const
{
    std::lock_guard lock(mutex_);
    return lastError_;
}

std::vector<UnityOffsetView> UnityOffsetService::Entries() const
{
    std::lock_guard lock(mutex_);
    return entries_;
}

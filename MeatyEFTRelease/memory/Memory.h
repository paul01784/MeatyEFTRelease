#pragma once
#include "pch.h"
#include "InputManager.h"
#include "Registry.h"
#include "structs.h"
#include "DmaScheduler.h"

#include <atomic>
#include <array>
#include <chrono>
#include <optional>
#include <type_traits>
#include <limits>
#include <thread>
#include <mutex>
#include <source_location>
#include <span>
#include <string_view>

struct MemoryTrafficStats
{
    // Lifetime totals since construction or ResetTrafficStats()
    uint64_t readOperations = 0;          // Direct reads + scatter execute-read batches.
    uint64_t readSuccesses = 0;

    uint64_t readFailures = 0;

    uint64_t readRequests = 0;

    uint64_t readBytesRequested = 0;

    uint64_t directReadBytesReturned = 0;

    uint64_t scatterReadBatches = 0;
    uint64_t scatterReadRequests = 0;
    uint64_t scatterHandleInitializations = 0;
    uint64_t scatterHandleReuses = 0;

    uint64_t scatterClearFailures = 0;

    double sampleWindowSeconds = 0.0;

    double readOperationsPerSecond = 0.0;
    double readRequestsPerSecond = 0.0;

    double readBytesRequestedPerSecond = 0.0;
};

struct MemoryConnectionStats
{
    bool vmmHandleValid = false;
    bool dmaInitialized = false;
    bool processInitialized = false;

    bool vmmLibraryLoaded = false;
    bool leechCoreLibraryLoaded = false;
    bool ftd3xxLibraryLoaded = false;

    uint64_t vmmHandleAddress = 0;

    uint32_t processId = 0;
    std::string processName;

    uint64_t targetBaseAddress = 0;
    uint64_t targetBaseSize = 0;

    bool fpgaInfoAvailable = false;
    uint64_t fpgaId = 0;
    uint64_t deviceId = 0;
    uint64_t firmwareMajor = 0;
    uint64_t firmwareMinor = 0;

    bool cacheInfoAvailable = false;
    uint64_t processCachePartialTicks = 0;
    uint64_t processCacheTotalTicks = 0;
    uint64_t readCacheTicks = 0;
    uint64_t tlbCacheTicks = 0;
};

struct TarkovPointerSnapshot
{
    uint64_t unityPlayerBase = 0;
    uint64_t unityPlayerSize = 0;
    uint64_t gameAssemblyBase = 0;
    uint64_t gameObjectManagerSlot = 0;
    uint64_t gameObjectManager = 0;
    bool gameObjectManagerSignatureAttempted = false;
    bool gameObjectManagerResolvedBySignature = false;
};

enum class DmaConnectionState : uint8_t
{
    Disconnected,
    Connecting,
    WaitingForProcess,
    Connected,
    Failed
};

enum class DmaCacheMode : uint8_t
{
    Cached,
    Uncached
};

enum class DmaPagingMode : uint8_t
{
    NoPaging,
    AllowPaging
};

enum class DmaLockMode : uint8_t
{
    Try,
    Wait
};

enum class DmaHealthSource : uint8_t
{
    GameWorldSingleton,
    CameraChain,
    OpticMesh,
    LocalPlayerPose,
    Count
};

class Memory
{
private:
    struct CurrentProcessInformation
    {
        DWORD PID = 0;
        uintptr_t base_address = 0;
        size_t base_size = 0;
        std::string process_name{};
    };

    static inline CurrentProcessInformation current_process{};

    static inline std::atomic_bool DMA_INITIALIZED{ false };
    static inline std::atomic_bool PROCESS_INITIALIZED{ false };

    bool DumpMemoryMap(bool debug = false);
    bool SetFPGA();
    void CloseScatterHandleLocked() noexcept;

    std::thread dmaThread;

    std::atomic_bool initRunning{ false };
    std::atomic_bool cancelInit{ false };
    std::atomic<DmaConnectionState> dmaState{ DmaConnectionState::Disconnected };

    std::shared_ptr<c_keys> key;
    c_registry registry;
    
private:
    struct TrafficCounterSnapshot
    {
        uint64_t readOperations = 0;
        uint64_t readSuccesses = 0;

        uint64_t readFailures = 0;

        uint64_t readRequests = 0;

        uint64_t readBytesRequested = 0;

        uint64_t directReadBytesReturned = 0;

        uint64_t scatterReadBatches = 0;
        uint64_t scatterReadRequests = 0;
        uint64_t scatterHandleInitializations = 0;
        uint64_t scatterHandleReuses = 0;

        uint64_t scatterClearFailures = 0;
    };

    struct TrafficRateSnapshot
    {
        double sampleWindowSeconds = 0.0;

        double readOperationsPerSecond = 0.0;
        double readRequestsPerSecond = 0.0;

        double readBytesRequestedPerSecond = 0.0;
    };

    TrafficCounterSnapshot LoadTrafficCounters() const;

    void RecordDirectRead(
        size_t requestedBytes,
        size_t returnedBytes,
        bool success
    ) const;

    void RecordScatterReadRequest(size_t requestedBytes) const;

    void RecordScatterReadExecute(bool success) const;

    void RecordScatterClearFailure() const;

    // Atomic counters: no locking in normal DMA read paths.
    mutable std::atomic<uint64_t> statsReadOperations{ 0 };

    mutable std::atomic<uint64_t> statsReadSuccesses{ 0 };

    mutable std::atomic<uint64_t> statsReadFailures{ 0 };

    mutable std::atomic<uint64_t> statsReadRequests{ 0 };

    mutable std::atomic<uint64_t> statsReadBytesRequested{ 0 };

    mutable std::atomic<uint64_t> statsDirectReadBytesReturned{ 0 };

    mutable std::atomic<uint64_t> statsScatterReadBatches{ 0 };
    mutable std::atomic<uint64_t> statsScatterReadRequests{ 0 };
    mutable std::atomic<uint64_t> statsScatterHandleInitializations{ 0 };
    mutable std::atomic<uint64_t> statsScatterHandleReuses{ 0 };

    mutable std::atomic<uint64_t> statsScatterClearFailures{ 0 };

    // stats from the debug UI.
    mutable std::mutex trafficStatsMutex;
    mutable bool trafficStatsBaselineValid = false;

    mutable TrafficCounterSnapshot trafficStatsBaseline{};
    mutable TrafficRateSnapshot trafficRateSnapshot{};

    mutable std::chrono::steady_clock::time_point trafficStatsLastSample{};

    mutable std::mutex handleMutex;
    mutable PriorityDmaMutex dmaOpsMutex;
    VMMDLL_SCATTER_HANDLE reusableScatterHandle = nullptr;

    mutable std::mutex tarkovPointersMutex;
    TarkovPointerSnapshot tarkovPointers{};

    [[nodiscard]] bool TryGetModuleInfo(const std::string& moduleName, uintptr_t& moduleBase, size_t& moduleSize) const;
    void ClearTarkovPointerSnapshot();

    std::chrono::steady_clock::time_point lastMemoryCacheRefresh{};
    std::chrono::steady_clock::time_point lastTlbCacheRefresh{};
    bool preferTlbCacheRefresh = true;

    struct DmaHealthFailureState
    {
        uint64_t fingerprint = 0;
        uint32_t consecutiveFailures = 0;
        std::chrono::steady_clock::time_point firstFailure{};
        bool recoveryQueued = false;
    };

    mutable std::mutex dmaHealthMutex;
    std::array<DmaHealthFailureState,
        static_cast<size_t>(DmaHealthSource::Count)> dmaHealthFailures{};
    std::atomic_bool dmaHealthRecoveryPending{ false };
    std::atomic<uint64_t> dmaRecoveryEpoch{ 0 };
    std::chrono::steady_clock::time_point lastDmaHealthRecovery{};

private:
    [[nodiscard]] static DWORD BuildReadFlags(DmaCacheMode cacheMode);
    [[nodiscard]] static DWORD BuildScatterFlags(DmaCacheMode cacheMode, DmaPagingMode pagingMode = DmaPagingMode::NoPaging);

public:
    // 64 KiB remains the hard device limit. Live work yields every 32 KiB;
    // background work may use the full window for throughput.
    static constexpr size_t PreferredDmaTransferBytes = 32 * 1024;
    static constexpr size_t MaxDmaTransferBytes = 64 * 1024;
    static constexpr size_t MaxDmaScatterRequests = 256;

    [[nodiscard]] static size_t GetDmaTransferLimit() noexcept
    {
        return GetCurrentDmaPriority() == DmaPriority::Background
            ? MaxDmaTransferBytes
            : PreferredDmaTransferBytes;
    }

    struct ScatterReadRequest
    {
        uint64_t address = 0;
        void* buffer = nullptr;
        size_t size = 0;
        uint64_t diagnosticBase = 0;
        uint64_t diagnosticOffset = 0;
        const char* diagnosticExpression = nullptr;
    };

    enum class TryScatterReadResult : uint8_t
    {
        Success,
        Incomplete,
        Busy,
        Failed
    };

    Memory();
    ~Memory();

    Memory(const Memory&) = delete;
    Memory& operator=(const Memory&) = delete;

    c_registry& GetRegistry() { return registry; }
    const c_registry& GetRegistry() const { return registry; }

    c_keys* GetKeyboard() { return key.get(); }
    const c_keys* GetKeyboard() const { return key.get(); }

    bool Init(bool memMap = true, bool debug = false);
    bool WaitForRetryOrCancel(DWORD milliseconds);
    void CloseAndReset();
    void doDMAConnect();

    DmaConnectionState GetDmaState() const;

    bool IsInitRunning() const;
    bool IsDmaOperational() const;
    [[nodiscard]] bool ShouldDeferDmaWork(DmaPriority priority) const
    {
        return dmaOpsMutex.ShouldDefer(priority);
    }

    DWORD GetPidFromName(const std::string& process_name);
    std::vector<int> GetPidListFromName(const std::string& process_name);
    std::vector<std::string> GetModuleList(const std::string& process_name);

    VMMDLL_PROCESS_INFORMATION GetProcessInformation();
    PEB GetProcessPeb();

    uintptr_t GetBaseDaddy(const std::string& module_name);
    size_t GetBaseSize(const std::string& module_name);
    [[nodiscard]] bool GetModuleInfo(const std::string& moduleName, uintptr_t& moduleBase, size_t& moduleSize) const;

    void PreloadTarkovPointerSnapshot();
    void RefreshTarkovPointerSnapshot();
    [[nodiscard]] TarkovPointerSnapshot GetTarkovPointerSnapshot() const;

    uintptr_t GetExportTableAddress(std::string import, std::string process, std::string module);
    uintptr_t GetImportTableAddress(std::string import, std::string process, std::string module);

    bool FixCr3();
    bool DumpMemory(uintptr_t address, std::string path);

    MemoryTrafficStats GetTrafficStats() const;
    std::string GetTrafficStatsString() const;
    void ResetTrafficStats();

    MemoryConnectionStats GetConnectionStats() const;
    std::string GetConnectionStatsString() const;

    uint64_t FindSignature(
        const char* signature,
        uint64_t range_start,
        uint64_t range_end,
        int PID = 0,
        DmaCacheMode cacheMode = DmaCacheMode::Uncached
    );

    bool RefreshProcessInformationNow();
    bool RefreshAllCaches();
    void RunCacheMaintenance();
    void ReportDmaHealthFailure(DmaHealthSource source, uint64_t fingerprint = 0);
    void ReportDmaHealthSuccess(DmaHealthSource source);
    [[nodiscard]] uint64_t GetDmaRecoveryEpoch() const noexcept;


    bool Read(
        uintptr_t address,
        void* buffer,
        size_t size,
        DmaCacheMode cacheMode = DmaCacheMode::Uncached,
        std::string_view callingFunc = {},
        std::source_location caller = std::source_location::current()) const;

    bool Read(
        uintptr_t address,
        void* buffer,
        size_t size,
        int pid,
        DmaCacheMode cacheMode = DmaCacheMode::Uncached,
        std::string_view callingFunc = {},
        std::source_location caller = std::source_location::current()) const;

    template <typename T>
    [[nodiscard]] bool TryRead(
        uint64_t address,
        T& out,
        DmaCacheMode cacheMode = DmaCacheMode::Uncached,
        std::source_location caller = std::source_location::current()) const
    {
        static_assert(std::is_trivially_copyable_v<T>, "TryRead<T> requires trivially copyable type");

        out = {};

        if (!address)
            return false;

        return Read(address, &out, sizeof(T), cacheMode, {}, caller);
    }

    template <typename T>
    [[nodiscard]] std::optional<T> ReadOpt(
        uint64_t address,
        DmaCacheMode cacheMode = DmaCacheMode::Uncached,
        std::source_location caller = std::source_location::current()) const
    {
        T value{};

        if (!TryRead(address, value, cacheMode, caller))
            return std::nullopt;

        return value;
    }

    template <typename T>
    [[nodiscard]] T ReadOr(
        uint64_t address,
        T fallback,
        DmaCacheMode cacheMode = DmaCacheMode::Uncached,
        std::source_location caller = std::source_location::current()) const
    {
        T value{};

        if (!TryRead(address, value, cacheMode, caller))
            return fallback;

        return value;
    }

    template <typename T>
    [[nodiscard]] T Read(
        uint64_t address,
        DmaCacheMode cacheMode = DmaCacheMode::Uncached,
        std::source_location caller = std::source_location::current()) const
    {
        static_assert(std::is_trivially_copyable_v<T>, "Read<T> requires trivially copyable type");

        T value;
        std::memset(&value, 0, sizeof(T));

        Read(address, &value, sizeof(T), cacheMode, {}, caller);

        return value;
    }

    template <typename T>
    [[nodiscard]] T Read(
        void* address,
        DmaCacheMode cacheMode = DmaCacheMode::Uncached,
        std::source_location caller = std::source_location::current()) const
    {
        return Read<T>(reinterpret_cast<uint64_t>(address), cacheMode, caller);
    }

    template <typename T>
    [[nodiscard]] std::vector<T> ReadVector(
        uint64_t address,
        size_t count,
        DmaCacheMode cacheMode = DmaCacheMode::Uncached,
        std::source_location caller = std::source_location::current()) const
    {
        static_assert(std::is_trivially_copyable_v<T>, "ReadVector<T> requires trivially copyable type");

        if (!address || count == 0)
            return {};

        constexpr size_t MAX_VECTOR_COUNT = 1'000'000;

        if (count > MAX_VECTOR_COUNT)
            return {};

        if (count > (std::numeric_limits<size_t>::max)() / sizeof(T))
            return {};

        std::vector<T> buffer(count);
        const size_t bytes = count * sizeof(T);

        if (!Read(address, buffer.data(), bytes, cacheMode, {}, caller))
            return {};

        return buffer;
    }

    template <typename T>
    [[nodiscard]] std::vector<T> read_vec_new(
        uint64_t address,
        size_t count,
        DmaCacheMode cacheMode = DmaCacheMode::Uncached,
        std::source_location caller = std::source_location::current()) const
    {
        return ReadVector<T>(address, count, cacheMode, caller);
    }

    template <typename T>
    [[nodiscard]] std::vector<T> read_vec(
        uint64_t address,
        size_t count,
        DmaCacheMode cacheMode = DmaCacheMode::Uncached,
        std::source_location caller = std::source_location::current()) const
    {
        return ReadVector<T>(address, count, cacheMode, caller);
    }

    [[nodiscard]] bool ReadChain(
        uint64_t base,
        const std::vector<uint64_t>& offsets,
        uint64_t& out,
        DmaCacheMode cacheMode = DmaCacheMode::Uncached
    ) const;

    [[nodiscard]] uint64_t ReadChain(
        uint64_t base,
        const std::vector<uint64_t>& offsets,
        DmaCacheMode cacheMode = DmaCacheMode::Uncached
    ) const
    {
        uint64_t out = 0;
        ReadChain(base, offsets, out, cacheMode);
        return out;
    }

    std::string readUnityString(uintptr_t address, SIZE_T maxChars = 128, DmaCacheMode cacheMode = DmaCacheMode::Uncached);
    std::string readUnityStringField(uintptr_t fieldAddress, SIZE_T maxChars = 128, DmaCacheMode cacheMode = DmaCacheMode::Uncached);
    std::string readUTF8String(uint64_t address, SIZE_T size, DmaCacheMode cacheMode = DmaCacheMode::Uncached);
    std::string readString(uint64_t address, size_t size, DmaCacheMode cacheMode = DmaCacheMode::Uncached);
    std::wstring readWideString(uintptr_t address, SIZE_T size, DmaCacheMode cacheMode = DmaCacheMode::Uncached);
    std::string readUnicodeString(uintptr_t address, SIZE_T size, DmaCacheMode cacheMode = DmaCacheMode::Uncached);

    bool TryReadLootIdFallback(uint64_t fieldAddress, std::string& output);
    bool TryReadLootTransformFallback(uint64_t gameObject, uint64_t& output);
    bool ReadScatter(const ScatterReadRequest* requests, size_t requestCount, DmaCacheMode cacheMode = DmaCacheMode::Uncached, std::string_view callingFunc = {}, int pid = 0);

    TryScatterReadResult TryReadScatter(const ScatterReadRequest* requests, size_t requestCount, DmaCacheMode cacheMode = DmaCacheMode::Uncached, std::string_view callingFunc = {}, std::span<DWORD> bytesRead = {}, DmaPagingMode pagingMode = DmaPagingMode::NoPaging, DmaLockMode lockMode = DmaLockMode::Try, int pid = 0);

    [[nodiscard]] static bool IsValidPointer(uintptr_t pointer)
    {
        return pointer > 0xFFFFFF && pointer < 0x0000800000000000;
    }

    void doDMADisconnect();

    bool IsDisconnectRequested() const;


    ULONG64 GET_MonoModuleAddress(char* module_name);

    int FindSignatureOffset(
        const std::vector<uint8_t>& array,
        const std::vector<uint8_t>& signature,
        const std::string& mask = ""
    );

    VMM_HANDLE vHandle = nullptr;

    uint64_t base = 0;
    uint64_t baseSize = 0;

};

inline Memory mem;

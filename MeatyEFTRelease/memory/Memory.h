#pragma once
#include "pch.h"
#include "InputManager.h"
#include "Registry.h"
#include "Shellcode.h"
#include "structs.h"

#include <atomic>
#include <optional>
#include <type_traits>
#include <limits>
#include <thread>
#include <mutex>

struct MemoryTrafficStats
{
    // Lifetime totals since construction or ResetTrafficStats()
    uint64_t readOperations = 0;          // Direct reads + scatter execute-read batches.
    uint64_t writeOperations = 0;         // Direct writes + scatter execute-write batches.

    uint64_t readSuccesses = 0;
    uint64_t writeSuccesses = 0;

    uint64_t readFailures = 0;
    uint64_t writeFailures = 0;

    uint64_t readRequests = 0;
    uint64_t writeRequests = 0;

    uint64_t readBytesRequested = 0;
    uint64_t writeBytesRequested = 0;

    uint64_t directReadBytesReturned = 0;

    uint64_t scatterReadBatches = 0;
    uint64_t scatterWriteBatches = 0;

    uint64_t scatterReadRequests = 0;
    uint64_t scatterWriteRequests = 0;

    uint64_t scatterClearFailures = 0;

    double sampleWindowSeconds = 0.0;

    double readOperationsPerSecond = 0.0;
    double writeOperationsPerSecond = 0.0;

    double readRequestsPerSecond = 0.0;
    double writeRequestsPerSecond = 0.0;

    double readBytesRequestedPerSecond = 0.0;
    double writeBytesRequestedPerSecond = 0.0;
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

enum class DmaConnectionState : uint8_t
{
    Disconnected,
    Connecting,
    WaitingForProcess,
    Connected,
    Failed
};

class Memory
{
private:
    struct LibModules
    {
        HMODULE VMM = nullptr;
        HMODULE FTD3XX = nullptr;
        HMODULE LEECHCORE = nullptr;
    };

    static inline LibModules modules{};

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
    void setCustomRefreshData();

    std::thread dmaThread;

    std::atomic_bool initRunning{ false };
    std::atomic_bool cancelInit{ false };
    std::atomic<DmaConnectionState> dmaState{ DmaConnectionState::Disconnected };

    std::shared_ptr<c_keys> key;
    c_registry registry;
    c_shellcode shellcode;
    
private:
    struct TrafficCounterSnapshot
    {
        uint64_t readOperations = 0;
        uint64_t writeOperations = 0;

        uint64_t readSuccesses = 0;
        uint64_t writeSuccesses = 0;

        uint64_t readFailures = 0;
        uint64_t writeFailures = 0;

        uint64_t readRequests = 0;
        uint64_t writeRequests = 0;

        uint64_t readBytesRequested = 0;
        uint64_t writeBytesRequested = 0;

        uint64_t directReadBytesReturned = 0;

        uint64_t scatterReadBatches = 0;
        uint64_t scatterWriteBatches = 0;

        uint64_t scatterReadRequests = 0;
        uint64_t scatterWriteRequests = 0;

        uint64_t scatterClearFailures = 0;
    };

    struct TrafficRateSnapshot
    {
        double sampleWindowSeconds = 0.0;

        double readOperationsPerSecond = 0.0;
        double writeOperationsPerSecond = 0.0;

        double readRequestsPerSecond = 0.0;
        double writeRequestsPerSecond = 0.0;

        double readBytesRequestedPerSecond = 0.0;
        double writeBytesRequestedPerSecond = 0.0;
    };

    TrafficCounterSnapshot LoadTrafficCounters() const;

    void RecordDirectRead(
        size_t requestedBytes,
        size_t returnedBytes,
        bool success
    ) const;

    void RecordDirectWrite(
        size_t requestedBytes,
        bool success
    ) const;

    void RecordScatterReadRequest(size_t requestedBytes) const;
    void RecordScatterWriteRequest(size_t requestedBytes) const;

    void RecordScatterReadExecute(bool success) const;
    void RecordScatterWriteExecute(bool success) const;

    void RecordScatterClearFailure() const;

    // Atomic counters: no locking in normal DMA read/write paths.
    mutable std::atomic<uint64_t> statsReadOperations{ 0 };
    mutable std::atomic<uint64_t> statsWriteOperations{ 0 };

    mutable std::atomic<uint64_t> statsReadSuccesses{ 0 };
    mutable std::atomic<uint64_t> statsWriteSuccesses{ 0 };

    mutable std::atomic<uint64_t> statsReadFailures{ 0 };
    mutable std::atomic<uint64_t> statsWriteFailures{ 0 };

    mutable std::atomic<uint64_t> statsReadRequests{ 0 };
    mutable std::atomic<uint64_t> statsWriteRequests{ 0 };

    mutable std::atomic<uint64_t> statsReadBytesRequested{ 0 };
    mutable std::atomic<uint64_t> statsWriteBytesRequested{ 0 };

    mutable std::atomic<uint64_t> statsDirectReadBytesReturned{ 0 };

    mutable std::atomic<uint64_t> statsScatterReadBatches{ 0 };
    mutable std::atomic<uint64_t> statsScatterWriteBatches{ 0 };

    mutable std::atomic<uint64_t> statsScatterReadRequests{ 0 };
    mutable std::atomic<uint64_t> statsScatterWriteRequests{ 0 };

    mutable std::atomic<uint64_t> statsScatterClearFailures{ 0 };

    // stats from the debug UI.
    mutable std::mutex trafficStatsMutex;
    mutable bool trafficStatsBaselineValid = false;

    mutable TrafficCounterSnapshot trafficStatsBaseline{};
    mutable TrafficRateSnapshot trafficRateSnapshot{};

    mutable std::chrono::steady_clock::time_point trafficStatsLastSample{};

    mutable std::mutex handleMutex;

private:
    [[nodiscard]] static DWORD BuildReadFlags(bool useCache);
    [[nodiscard]] static DWORD BuildScatterFlags(bool useCache);

public:
    Memory();
    ~Memory();

    Memory(const Memory&) = delete;
    Memory& operator=(const Memory&) = delete;

    c_registry& GetRegistry() { return registry; }
    const c_registry& GetRegistry() const { return registry; }

    c_keys* GetKeyboard() { return key.get(); }
    const c_keys* GetKeyboard() const { return key.get(); }

    c_shellcode& GetShellcode() { return shellcode; }
    const c_shellcode& GetShellcode() const { return shellcode; }

    bool Init(bool memMap = true, bool debug = false);
    bool WaitForRetryOrCancel(DWORD milliseconds);
    void CloseAndReset();
    void doDMAConnect();

    DmaConnectionState GetDmaState() const;

    bool IsInitRunning() const;

    DWORD GetPidFromName(const std::string& process_name);
    std::vector<int> GetPidListFromName(const std::string& process_name);
    std::vector<std::string> GetModuleList(const std::string& process_name);

    VMMDLL_PROCESS_INFORMATION GetProcessInformation();
    PEB GetProcessPeb();

    uintptr_t GetBaseDaddy(const std::string& module_name);
    size_t GetBaseSize(const std::string& module_name);

    uintptr_t GetExportTableAddress(std::string import, std::string process, std::string module);
    uintptr_t GetImportTableAddress(std::string import, std::string process, std::string module);

    bool FixCr3();
    bool DumpMemory(uintptr_t address, std::string path);

    void RefreshLight();

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
        bool useCache = false
    );

    bool WriteBufferEnsure(
        uintptr_t address,
        const void* buffer,
        size_t size,
        bool useCache = false
    ) const;

    bool Write(uintptr_t address, const void* buffer, size_t size) const;
    bool Write(uintptr_t address, const void* buffer, size_t size, int pid) const;

    template <typename T>
    bool WriteValue(uintptr_t address, const T& value) const
    {
        static_assert(std::is_trivially_copyable_v<T>, "WriteValue<T> requires trivially copyable type");

        if (!address)
            return false;

        return Write(address, &value, sizeof(T));
    }

    bool Read(uintptr_t address, void* buffer, size_t size, bool useCache = false) const;
    bool Read(uintptr_t address, void* buffer, size_t size, int pid, bool useCache = false) const;

    template <typename T>
    [[nodiscard]] bool TryRead(uint64_t address, T& out, bool useCache = false) const
    {
        static_assert(std::is_trivially_copyable_v<T>, "TryRead<T> requires trivially copyable type");

        out = {};

        if (!address)
            return false;

        return Read(address, &out, sizeof(T), useCache);
    }

    template <typename T>
    [[nodiscard]] std::optional<T> ReadOpt(uint64_t address, bool useCache = false) const
    {
        T value{};

        if (!TryRead(address, value, useCache))
            return std::nullopt;

        return value;
    }

    template <typename T>
    [[nodiscard]] T ReadOr(uint64_t address, T fallback, bool useCache = false) const
    {
        T value{};

        if (!TryRead(address, value, useCache))
            return fallback;

        return value;
    }

    template <typename T>
    [[nodiscard]] T Read(uint64_t address, bool useCache = false) const
    {
        static_assert(std::is_trivially_copyable_v<T>, "Read<T> requires trivially copyable type");

        T value;
        std::memset(&value, 0, sizeof(T));

        Read(address, &value, sizeof(T), useCache);

        return value;
    }

    template <typename T>
    [[nodiscard]] T Read(void* address, bool useCache = false) const
    {
        return Read<T>(reinterpret_cast<uint64_t>(address), useCache);
    }

    template <typename T>
    [[nodiscard]] std::vector<T> ReadVector(uint64_t address, size_t count, bool useCache = false) const
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

        if (!Read(address, buffer.data(), bytes, useCache))
            return {};

        return buffer;
    }

    template <typename T>
    [[nodiscard]] std::vector<T> read_vec_new(uint64_t address, size_t count, bool useCache = false) const
    {
        return ReadVector<T>(address, count, useCache);
    }

    template <typename T>
    [[nodiscard]] std::vector<T> read_vec(uint64_t address, size_t count, bool useCache = false) const
    {
        return ReadVector<T>(address, count, useCache);
    }

    [[nodiscard]] bool ReadChain(
        uint64_t base,
        const std::vector<uint64_t>& offsets,
        uint64_t& out,
        bool useCache = false
    ) const;

    [[nodiscard]] uint64_t ReadChain(
        uint64_t base,
        const std::vector<uint64_t>& offsets,
        bool useCache = false
    ) const
    {
        uint64_t out = 0;
        ReadChain(base, offsets, out, useCache);
        return out;
    }

    std::string readUnityString(uintptr_t address, SIZE_T maxChars = 128, bool useCache = false);
    std::string readUTF8String(uint64_t address, SIZE_T size, bool useCache = false);
    std::string readString(uint64_t address, size_t size, bool useCache = false);
    std::wstring readWideString(uintptr_t address, SIZE_T size, bool useCache = false);
    std::string readUnicodeString(uintptr_t address, SIZE_T size, bool useCache = false);

    VMMDLL_SCATTER_HANDLE CreateScatterHandle(bool useCache = false) const;
    VMMDLL_SCATTER_HANDLE CreateScatterHandle(int pid, bool useCache = false) const;

    void CloseScatterHandle(VMMDLL_SCATTER_HANDLE handle);

    bool AddScatterReadRequest(
        VMMDLL_SCATTER_HANDLE handle,
        uint64_t address,
        void* buffer,
        size_t size
    );

    bool AddScatterWriteRequest(
        VMMDLL_SCATTER_HANDLE handle,
        uint64_t address,
        const void* buffer,
        size_t size
    );

    bool ExecuteReadScatter(VMMDLL_SCATTER_HANDLE handle, int pid = 0, bool useCache = false);
    bool ExecuteWriteScatter(VMMDLL_SCATTER_HANDLE handle, int pid = 0, bool useCache = false);

    [[nodiscard]] static bool IsValidPointer(uintptr_t pointer)
    {
        return pointer > 0x10000 && pointer < 0x0000800000000000;
    }

    bool fullRefresh();

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
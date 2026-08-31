#include "pch.h"
#include "InputManager.h"
#include "Registry.h"
#include "Memory.h"
#include <iostream>
#include "../UI/debug.h"

namespace
{
	void MemoryLogError(const std::string& message)
	{
		LOGS.logError("[Memory] " + message);
	}

	void MemoryLogInfo(const std::string& message)
	{
		LOGS.logInfo("[Memory] " + message);
	}

    class KeyboardInitOutcome
    {
    public:
        ~KeyboardInitOutcome()
        {
            if (succeeded_)
            {
                MemoryLogInfo(
                    "[KEYS] Keyboard initialised successfully via " +
                    successPath_);
                return;
            }

            MemoryLogError(
                "[KEYS] Keyboard initialisation failed: " +
                lastFailure_ +
                " | Hotkeys disabled");
        }

        void SetFailure(std::string reason)
        {
            lastFailure_ = std::move(reason);
        }

        void MarkSuccess(std::string path)
        {
            succeeded_ = true;
            successPath_ = std::move(path);
        }

    private:
        bool succeeded_ = false;
        std::string successPath_;
        std::string lastFailure_ = "initialisation did not complete";
    };
}

bool c_keys::InitKeyboard()
{
    constexpr uintptr_t kKernelAddressThreshold = 0x7FFFFFFFFFFFULL;
    KeyboardInitOutcome outcome;

    auto Log = [&outcome](bool isError, const char* format, ...)
        {
            if (!isError)
                return;

            char buffer[1024]{};

            va_list args;
            va_start(args, format);
            _vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
            va_end(args);

            outcome.SetFailure(buffer);
        };

    auto IsKernelPointer = [](uintptr_t address) -> bool
        {
            return address > kKernelAddressThreshold;
        };

    try
    {
        Log(false, "[KEYS][INIT] ===== InitKeyboard started =====");

        gafAsyncKeyStateExport = 0;
        this->win_logon_pid = 0;

        if (!mem.vHandle)
        {
            Log(true, "[KEYS][INIT] mem.vHandle is null");
            return false;
        }

        Log(
            false,
            "[KEYS][INIT] mem.vHandle = 0x%llX",
            static_cast<unsigned long long>(
                reinterpret_cast<uintptr_t>(mem.vHandle)
                )
        );

        // Registry: Windows build
        const std::string win = registry.QueryValue(
            "HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\CurrentBuild",
            e_registry_type::sz
        );

        Log(false, "[KEYS][REGISTRY] CurrentBuild raw value: '%s'", win.c_str());

        if (win.empty())
        {
            Log(true, "[KEYS][REGISTRY] CurrentBuild was empty");
            return false;
        }

        int Winver = 0;

        try
        {
            Winver = std::stoi(win);
        }
        catch (const std::exception& ex)
        {
            Log(
                true,
                "[KEYS][REGISTRY] Failed parsing CurrentBuild '%s': %s",
                win.c_str(),
                ex.what()
            );
            return false;
        }

        Log(false, "[KEYS][REGISTRY] Windows build parsed: %d", Winver);

        // Registry: UBR
        const std::string ubr = registry.QueryValue(
            "HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\UBR",
            e_registry_type::dword
        );

        Log(false, "[KEYS][REGISTRY] UBR raw value: '%s'", ubr.c_str());

        if (ubr.empty())
        {
            Log(true, "[KEYS][REGISTRY] UBR was empty");
            return false;
        }

        int Ubr = 0;

        try
        {
            Ubr = std::stoi(ubr);
        }
        catch (const std::exception& ex)
        {
            Log(
                true,
                "[KEYS][REGISTRY] Failed parsing UBR '%s': %s",
                ubr.c_str(),
                ex.what()
            );
            return false;
        }

        Log(false, "[KEYS][REGISTRY] UBR parsed: %d", Ubr);

        // winlogon
        this->win_logon_pid = mem.GetPidFromName("winlogon.exe");

        Log(
            false,
            "[KEYS][INIT] winlogon.exe PID: %u",
            static_cast<unsigned int>(this->win_logon_pid)
        );

        // Windows 11 / newer
        if (Winver > 22000)
        {
            Log(false, "[KEYS][WIN11] Windows build > 22000. Using CSRSS session-state path.");

            static constexpr uint8_t kWin32ksgdSessionSignature[] =
            {
                0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00,
                0x48, 0x8B, 0x04, 0xC8
            };
            static constexpr uint8_t kWin32kSessionSignature[] =
            {
                0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00,
                0xFF, 0xC9
            };
            static constexpr uint8_t kGafAsyncKeyStateSignature[] =
            {
                0x48, 0x8D, 0x90, 0x00, 0x00, 0x00, 0x00,
                0xE8, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x57, 0xC0
            };

            auto TryResolveWindows11Keyboard = [&]() -> bool
            {
                const auto pids = mem.GetPidListFromName("csrss.exe");

                Log(false, "[KEYS][WIN11] csrss.exe PID count: %zu", pids.size());

                if (pids.empty())
                {
                    Log(true, "[KEYS][WIN11] No csrss.exe processes found");
                    return false;
                }

                for (size_t index = 0; index < pids.size(); ++index)
                {
                    const auto pid = pids[index];

                    const DWORD kernelPid =
                        static_cast<DWORD>(pid) |
                        VMMDLL_PID_PROCESS_WITH_KERNELMEMORY;

                auto ReadKernelExact = [&](uintptr_t address, void* outBuffer, DWORD size, const char* label) -> bool
                    {
                        if (!address || !outBuffer || !size)
                        {
                            Log(
                                true,
                                "[KEYS][READ] %s invalid arguments | addr=0x%llX size=%u",
                                label,
                                static_cast<unsigned long long>(address),
                                size
                            );
                            return false;
                        }

                        DWORD bytesRead = 0;

                        const BOOL ok = VMMDLL_MemReadEx(
                            mem.vHandle,
                            kernelPid,
                            address,
                            reinterpret_cast<PBYTE>(outBuffer),
                            size,
                            &bytesRead,
                            VMMDLL_FLAG_NOCACHE
                        );

                        Log(
                            false,
                            "[KEYS][READ] %s | pid=0x%X addr=0x%llX size=%u ok=%s bytesRead=%u",
                            label,
                            kernelPid,
                            static_cast<unsigned long long>(address),
                            size,
                            ok ? "true" : "false",
                            bytesRead
                        );

                        return ok && bytesRead == size;
                    };

                auto FindKernelSignatureExact = [&] (
                    uintptr_t rangeStart,
                    uintptr_t rangeEnd,
                    const uint8_t* pattern,
                    const char* mask,
                    size_t patternSize,
                    const char* label) -> uintptr_t
                    {
                        if (!rangeStart ||
                            rangeStart >= rangeEnd ||
                            !pattern ||
                            !mask ||
                            patternSize == 0 ||
                            std::strlen(mask) != patternSize)
                        {
                            return 0;
                        }

                        // Scan one page at a time so a single unavailable
                        // kernel page cannot discard a much larger module
                        // region. The overlap catches cross-page signatures.
                        constexpr size_t kChunkSize = 4 * 1024;
                        std::vector<uint8_t> buffer(
                            kChunkSize + patternSize - 1);

                        for (uintptr_t address = rangeStart;
                            address < rangeEnd;
                            address += kChunkSize)
                        {
                            const size_t remaining = static_cast<size_t>(
                                rangeEnd - address);
                            const size_t bytesToRead = (std::min)(
                                kChunkSize + patternSize - 1,
                                remaining);

                            if (bytesToRead < patternSize)
                                break;

                            size_t readableBytes = bytesToRead;

                            if (!ReadKernelExact(
                                address,
                                buffer.data(),
                                static_cast<DWORD>(bytesToRead),
                                label))
                            {
                                // The overlap enters the following page. If
                                // only that page is unavailable, retain the
                                // current page instead of dropping both.
                                if (bytesToRead <= kChunkSize ||
                                    !ReadKernelExact(
                                        address,
                                        buffer.data(),
                                        static_cast<DWORD>(kChunkSize),
                                        label))
                                {
                                    continue;
                                }

                                readableBytes = kChunkSize;
                            }

                            const size_t lastStart =
                                readableBytes - patternSize;

                            for (size_t offset = 0;
                                offset <= lastStart;
                                ++offset)
                            {
                                bool matches = true;

                                for (size_t byteIndex = 0;
                                    byteIndex < patternSize;
                                    ++byteIndex)
                                {
                                    if (mask[byteIndex] != '?' &&
                                        buffer[offset + byteIndex] !=
                                        pattern[byteIndex])
                                    {
                                        matches = false;
                                        break;
                                    }
                                }

                                if (matches)
                                    return address + offset;
                            }
                        }

                        return 0;
                    };

                Log(false, "[KEYS][WIN11] ----------------------------------------");
                Log(
                    false,
                    "[KEYS][WIN11] Processing csrss index %zu | PID=%u | kernelPid=0x%X",
                    index,
                    static_cast<unsigned int>(pid),
                    kernelPid
                );

                // Resolve win32ksgd.sys or win32k.sys
                PVMMDLL_MAP_MODULEENTRY win32k_module_info = nullptr;
                bool usingWin32ksgd = false;

                Log(false, "[KEYS][WIN11] Looking for win32ksgd.sys");

                if (VMMDLL_Map_GetModuleFromNameW(
                    mem.vHandle,
                    kernelPid,
                    const_cast<LPWSTR>(L"win32ksgd.sys"),
                    &win32k_module_info,
                    VMMDLL_MODULE_FLAG_NORMAL))
                {
                    usingWin32ksgd = true;
                    Log(false, "[KEYS][WIN11] Found win32ksgd.sys");
                }
                else
                {
                    Log(false, "[KEYS][WIN11] win32ksgd.sys not found. Trying win32k.sys");

                    if (!VMMDLL_Map_GetModuleFromNameW(
                        mem.vHandle,
                        kernelPid,
                        const_cast<LPWSTR>(L"win32k.sys"),
                        &win32k_module_info,
                        VMMDLL_MODULE_FLAG_NORMAL))
                    {
                        Log(true, "[KEYS][WIN11] Failed to resolve win32ksgd.sys or win32k.sys");
                        continue;
                    }

                    Log(false, "[KEYS][WIN11] Found win32k.sys");
                }

                if (!win32k_module_info)
                {
                    Log(true, "[KEYS][WIN11] win32k module info pointer was null");
                    continue;
                }

                const uintptr_t win32k_base = win32k_module_info->vaBase;
                const size_t win32k_size = win32k_module_info->cbImageSize;

                VMMDLL_MemFree(win32k_module_info);
                win32k_module_info = nullptr;

                Log(
                    false,
                    "[KEYS][WIN11] win32k module=%s base=0x%llX size=0x%llX (%zu bytes)",
                    usingWin32ksgd ? "win32ksgd.sys" : "win32k.sys",
                    static_cast<unsigned long long>(win32k_base),
                    static_cast<unsigned long long>(win32k_size),
                    win32k_size
                );

                if (!win32k_base || !win32k_size)
                {
                    Log(true, "[KEYS][WIN11] Invalid win32k module range");
                    continue;
                }

                // Find g_session_global_slots instruction
                uintptr_t g_session_ptr = 0;

                if (usingWin32ksgd)
                {
                    Log(false, "[KEYS][WIN11] Searching win32ksgd signature");

                    g_session_ptr = FindKernelSignatureExact(
                        win32k_base,
                        win32k_base + win32k_size,
                        kWin32ksgdSessionSignature,
                        "xxx????xxxx",
                        std::size(kWin32ksgdSessionSignature),
                        "win32ksgd signature scan"
                    );
                }
                else
                {
                    Log(false, "[KEYS][WIN11] Searching win32k signature");

                    g_session_ptr = FindKernelSignatureExact(
                        win32k_base,
                        win32k_base + win32k_size,
                        kWin32kSessionSignature,
                        "xxx????xx",
                        std::size(kWin32kSessionSignature),
                        "win32k signature scan"
                    );
                }

                Log(
                    false,
                    "[KEYS][WIN11] g_session signature result: 0x%llX",
                    static_cast<unsigned long long>(g_session_ptr)
                );

                if (!g_session_ptr)
                {
                    Log(true, "[KEYS][WIN11] Failed to find g_session_global_slots signature");
                    continue;
                }

                uint8_t gSessionBytes[16]{};

                if (!ReadKernelExact(
                    g_session_ptr,
                    gSessionBytes,
                    sizeof(gSessionBytes),
                    "g_session signature bytes"))
                {
                    Log(true, "[KEYS][WIN11] Could not read g_session instruction bytes");
                    continue;
                }

                int32_t relative = 0;

                if (!ReadKernelExact(
                    g_session_ptr + 3,
                    &relative,
                    sizeof(relative),
                    "g_session RIP displacement"))
                {
                    Log(true, "[KEYS][WIN11] Could not read g_session RIP displacement");
                    continue;
                }

                Log(
                    false,
                    "[KEYS][WIN11] g_session relative displacement: 0x%X (%d)",
                    static_cast<uint32_t>(relative),
                    relative
                );

                const uintptr_t g_session_global_slots = static_cast<uintptr_t>(
                    static_cast<intptr_t>(g_session_ptr + 7) +
                    static_cast<intptr_t>(relative)
                    );

                Log(
                    false,
                    "[KEYS][WIN11] g_session_global_slots: 0x%llX",
                    static_cast<unsigned long long>(g_session_global_slots)
                );

                if (!IsKernelPointer(g_session_global_slots))
                {
                    Log(true, "[KEYS][WIN11] g_session_global_slots was not a kernel address");
                    continue;
                }

                // ----------------------------------------------------
                // Pointer chain:
                // sessionSlotsTable = *g_session_global_slots
                // slotPointer       = *(sessionSlotsTable + slot * 8)
                // userSessionState  = *slotPointer
                // ----------------------------------------------------
                uintptr_t sessionSlotsTable = 0;

                if (!ReadKernelExact(
                    g_session_global_slots,
                    &sessionSlotsTable,
                    sizeof(sessionSlotsTable),
                    "g_session_global_slots dereference"))
                {
                    Log(true, "[KEYS][WIN11] Failed reading sessionSlotsTable");
                    continue;
                }

                Log(
                    false,
                    "[KEYS][WIN11] sessionSlotsTable: 0x%llX",
                    static_cast<unsigned long long>(sessionSlotsTable)
                );

                if (!IsKernelPointer(sessionSlotsTable))
                {
                    Log(true, "[KEYS][WIN11] sessionSlotsTable was not a valid kernel pointer");
                    continue;
                }

                uintptr_t user_session_state = 0;

                for (int slot = 0; slot < 4; ++slot)
                {
                    const uintptr_t slotAddress =
                        sessionSlotsTable +
                        (static_cast<uintptr_t>(slot) * sizeof(uintptr_t));

                    uintptr_t slotPointer = 0;

                    if (!ReadKernelExact(
                        slotAddress,
                        &slotPointer,
                        sizeof(slotPointer),
                        "session slot pointer"))
                    {
                        Log(true, "[KEYS][WIN11] Failed reading slot %d pointer", slot);
                        continue;
                    }

                    Log(
                        false,
                        "[KEYS][WIN11] slot=%d slotAddress=0x%llX slotPointer=0x%llX",
                        slot,
                        static_cast<unsigned long long>(slotAddress),
                        static_cast<unsigned long long>(slotPointer)
                    );

                    if (!IsKernelPointer(slotPointer))
                    {
                        Log(false, "[KEYS][WIN11] slot=%d pointer is not kernel-space", slot);
                        continue;
                    }

                    uintptr_t candidateSessionState = 0;

                    if (!ReadKernelExact(
                        slotPointer,
                        &candidateSessionState,
                        sizeof(candidateSessionState),
                        "user_session_state dereference"))
                    {
                        Log(true, "[KEYS][WIN11] Failed reading session state for slot %d", slot);
                        continue;
                    }

                    Log(
                        false,
                        "[KEYS][WIN11] slot=%d candidate user_session_state=0x%llX",
                        slot,
                        static_cast<unsigned long long>(candidateSessionState)
                    );

                    if (IsKernelPointer(candidateSessionState))
                    {
                        user_session_state = candidateSessionState;

                        Log(
                            false,
                            "[KEYS][WIN11] Valid user_session_state found in slot %d",
                            slot
                        );

                        break;
                    }
                }

                if (!IsKernelPointer(user_session_state))
                {
                    Log(true, "[KEYS][WIN11] Failed to resolve a valid user_session_state");
                    continue;
                }

                // Resolve win32kbase.sys
                PVMMDLL_MAP_MODULEENTRY win32kbase_module_info = nullptr;

                Log(false, "[KEYS][WIN11] Looking for win32kbase.sys");

                if (!VMMDLL_Map_GetModuleFromNameW(
                    mem.vHandle,
                    kernelPid,
                    const_cast<LPWSTR>(L"win32kbase.sys"),
                    &win32kbase_module_info,
                    VMMDLL_MODULE_FLAG_NORMAL))
                {
                    Log(true, "[KEYS][WIN11] Failed to get win32kbase.sys module info");
                    continue;
                }

                if (!win32kbase_module_info)
                {
                    Log(true, "[KEYS][WIN11] win32kbase module info pointer was null");
                    continue;
                }

                const uintptr_t win32kbase_base = win32kbase_module_info->vaBase;
                const size_t win32kbase_size = win32kbase_module_info->cbImageSize;

                VMMDLL_MemFree(win32kbase_module_info);
                win32kbase_module_info = nullptr;

                Log(
                    false,
                    "[KEYS][WIN11] win32kbase base=0x%llX size=0x%llX (%zu bytes)",
                    static_cast<unsigned long long>(win32kbase_base),
                    static_cast<unsigned long long>(win32kbase_size),
                    win32kbase_size
                );

                if (!win32kbase_base || !win32kbase_size)
                {
                    Log(true, "[KEYS][WIN11] Invalid win32kbase module range");
                    continue;
                }

                // Resolve gafAsyncKeyState offset from user_session_state
                Log(false, "[KEYS][WIN11] Searching gafAsyncKeyStateExport signature");

                const uintptr_t ptr = FindKernelSignatureExact(
                    win32kbase_base,
                    win32kbase_base + win32kbase_size,
                    kGafAsyncKeyStateSignature,
                    "xxx????x????xxx",
                    std::size(kGafAsyncKeyStateSignature),
                    "gafAsyncKeyState signature scan"
                );

                Log(
                    false,
                    "[KEYS][WIN11] gafAsyncKeyStateExport signature result: 0x%llX",
                    static_cast<unsigned long long>(ptr)
                );

                if (!ptr)
                {
                    Log(true, "[KEYS][WIN11] Failed to find gafAsyncKeyStateExport signature");
                    continue;
                }

                uint8_t gafBytes[16]{};

                if (!ReadKernelExact(
                    ptr,
                    gafBytes,
                    sizeof(gafBytes),
                    "gafAsyncKeyState signature bytes"))
                {
                    Log(true, "[KEYS][WIN11] Could not read gafAsyncKeyState instruction bytes");
                    continue;
                }

                int32_t session_offset = 0;

                if (!ReadKernelExact(
                    ptr + 3,
                    &session_offset,
                    sizeof(session_offset),
                    "gafAsyncKeyState session offset"))
                {
                    Log(true, "[KEYS][WIN11] Could not read gafAsyncKeyState session offset");
                    continue;
                }

                Log(
                    false,
                    "[KEYS][WIN11] session_offset: 0x%X (%d)",
                    static_cast<uint32_t>(session_offset),
                    session_offset
                );

                gafAsyncKeyStateExport = static_cast<uintptr_t>(
                    static_cast<intptr_t>(user_session_state) +
                    static_cast<intptr_t>(session_offset)
                    );

                Log(
                    false,
                    "[KEYS][WIN11] candidate gafAsyncKeyStateExport: 0x%llX",
                    static_cast<unsigned long long>(gafAsyncKeyStateExport)
                );

                if (IsKernelPointer(gafAsyncKeyStateExport))
                {
                    outcome.MarkSuccess("Windows 11 session-state lookup");
                    return true;
                }

                Log(true, "[KEYS][WIN11] Candidate gafAsyncKeyStateExport was not kernel-space");
                }

                return false;
            };

            constexpr int kMaximumResolveAttempts = 3;

            for (int attempt = 1;
                attempt <= kMaximumResolveAttempts;
                ++attempt)
            {
                if (TryResolveWindows11Keyboard())
                    return true;

                if (attempt == kMaximumResolveAttempts)
                    break;

                // Automatic VMM refresh is disabled. Rebuild process/module
                // and virtual-translation state before retrying the CSRSS
                // session mappings.
                if (!mem.RefreshProcessInformationNow())
                {
                    Log(true, "[KEYS][WIN11] VMM refresh before retry failed");
                }

                std::this_thread::sleep_for(
                    std::chrono::milliseconds(200));
            }

            return false;
        }

        // Windows 10 / older
        Log(false, "[KEYS][LEGACY] Windows build <= 22000. Using EAT/PDB path.");

        if (!this->win_logon_pid)
        {
            Log(true, "[KEYS][LEGACY] winlogon.exe PID was zero");
            return false;
        }

        const DWORD winlogonKernelPid =
            static_cast<DWORD>(this->win_logon_pid) |
            VMMDLL_PID_PROCESS_WITH_KERNELMEMORY;

        Log(
            false,
            "[KEYS][LEGACY] winlogon PID=%u kernelPid=0x%X",
            static_cast<unsigned int>(this->win_logon_pid),
            winlogonKernelPid
        );

        PVMMDLL_MAP_EAT eat_map = nullptr;

        const bool eatResult = VMMDLL_Map_GetEATU(
            mem.vHandle,
            winlogonKernelPid,
            const_cast<LPSTR>("win32kbase.sys"),
            &eat_map
        );

        Log(
            false,
            "[KEYS][LEGACY] VMMDLL_Map_GetEATU result=%s map=0x%llX",
            eatResult ? "true" : "false",
            static_cast<unsigned long long>(
                reinterpret_cast<uintptr_t>(eat_map)
                )
        );

        if (!eatResult || !eat_map)
        {
            Log(true, "[KEYS][LEGACY] Failed getting win32kbase.sys EAT map");
            return false;
        }

        Log(
            false,
            "[KEYS][LEGACY] EAT version=%u expected=%u entries=%u",
            eat_map->dwVersion,
            VMMDLL_MAP_EAT_VERSION,
            eat_map->cMap
        );

        if (eat_map->dwVersion != VMMDLL_MAP_EAT_VERSION)
        {
            Log(true, "[KEYS][LEGACY] EAT version mismatch");

            VMMDLL_MemFree(eat_map);
            return false;
        }

        bool exportFound = false;

        for (DWORD i = 0; i < eat_map->cMap; ++i)
        {
            PVMMDLL_MAP_EATENTRY entry = eat_map->pMap + i;

            if (!entry || !entry->uszFunction)
                continue;

            if (strcmp(entry->uszFunction, "gafAsyncKeyState") != 0)
                continue;

            gafAsyncKeyStateExport = entry->vaFunction;
            exportFound = true;

            Log(
                false,
                "[KEYS][LEGACY] Found gafAsyncKeyState in EAT index=%u address=0x%llX",
                i,
                static_cast<unsigned long long>(gafAsyncKeyStateExport)
            );

            break;
        }

        VMMDLL_MemFree(eat_map);
        eat_map = nullptr;

        if (!exportFound)
        {
            Log(true, "[KEYS][LEGACY] gafAsyncKeyState was not present in EAT");
            return false;
        }

        if (IsKernelPointer(gafAsyncKeyStateExport))
        {
            outcome.MarkSuccess("legacy EAT lookup");
            return true;
        }

        Log(
            false,
            "[KEYS][LEGACY] EAT address was not kernel-space. Trying PDB lookup."
        );

        PVMMDLL_MAP_MODULEENTRY module_info = nullptr;

        const bool moduleResult = VMMDLL_Map_GetModuleFromNameW(
            mem.vHandle,
            winlogonKernelPid,
            const_cast<LPWSTR>(L"win32kbase.sys"),
            &module_info,
            VMMDLL_MODULE_FLAG_NORMAL
        );

        Log(
            false,
            "[KEYS][LEGACY] GetModuleFromNameW result=%s module=0x%llX",
            moduleResult ? "true" : "false",
            static_cast<unsigned long long>(
                reinterpret_cast<uintptr_t>(module_info)
                )
        );

        if (!moduleResult || !module_info)
        {
            Log(true, "[KEYS][LEGACY] Failed to resolve win32kbase.sys module info");
            return false;
        }

        Log(
            false,
            "[KEYS][LEGACY] win32kbase base=0x%llX",
            static_cast<unsigned long long>(module_info->vaBase)
        );

        char pdbName[260]{};

        const bool pdbLoadResult = VMMDLL_PdbLoad(
            mem.vHandle,
            winlogonKernelPid,
            module_info->vaBase,
            pdbName
        );

        Log(
            false,
            "[KEYS][LEGACY] PDB load result=%s pdb='%s'",
            pdbLoadResult ? "true" : "false",
            pdbName
        );

        if (!pdbLoadResult)
        {
            Log(true, "[KEYS][LEGACY] Failed to load PDB");
            return false;
        }

        uintptr_t gafAsyncKeyState = 0;

        const bool symbolResult = VMMDLL_PdbSymbolAddress(
            mem.vHandle,
            pdbName,
            const_cast<LPSTR>("gafAsyncKeyState"),
            &gafAsyncKeyState
        );

        Log(
            false,
            "[KEYS][LEGACY] PDB symbol lookup result=%s address=0x%llX",
            symbolResult ? "true" : "false",
            static_cast<unsigned long long>(gafAsyncKeyState)
        );

        if (!symbolResult)
        {
            Log(true, "[KEYS][LEGACY] Failed to resolve gafAsyncKeyState through PDB");
            return false;
        }

        gafAsyncKeyStateExport = gafAsyncKeyState;

        if (IsKernelPointer(gafAsyncKeyStateExport))
        {
            outcome.MarkSuccess("legacy PDB lookup");
            return true;
        }

        Log(
            true,
            "[KEYS][LEGACY] ===== InitKeyboard failed: final address=0x%llX =====",
            static_cast<unsigned long long>(gafAsyncKeyStateExport)
        );

        return false;
    }
    catch (const std::exception& ex)
    {
        Log(true, "[KEYS][EXCEPTION] InitKeyboard std::exception: %s", ex.what());
        return false;
    }
    catch (...)
    {
        Log(true, "[KEYS][EXCEPTION] InitKeyboard unknown exception");
        return false;
    }
}

void c_keys::UpdateKeysUnlocked()
{
	if (gafAsyncKeyStateExport < 0x7FFFFFFFFFFF)
		return;

	uint8_t previous_key_state_bitmap[64] = { 0 };
	memcpy(previous_key_state_bitmap, state_bitmap, 64);

	const int keyboardPid = static_cast<int>(
		static_cast<DWORD>(this->win_logon_pid) |
		VMMDLL_PID_PROCESS_WITH_KERNELMEMORY
	);

	mem.Read(
		gafAsyncKeyStateExport,
		state_bitmap,
		sizeof(state_bitmap),
		keyboardPid,
		DmaCacheMode::Uncached,
		"Keyboard"
	);

	for (int vk = 0; vk < 256; ++vk)
		if ((state_bitmap[(vk * 2 / 8)] & 1 << vk % 4 * 2) && !(previous_key_state_bitmap[(vk * 2 / 8)] & 1 << vk % 4 * 2))
			previous_state_bitmap[vk / 8] |= 1 << vk % 8;
}

void c_keys::UpdateKeys()
{
	std::lock_guard<std::mutex> stateLock(state_mutex);
	UpdateKeysUnlocked();
}

bool c_keys::IsKeyDown(uint32_t virtual_key_code)
{
	if (virtual_key_code >= 256)
		return false;

	std::lock_guard<std::mutex> stateLock(state_mutex);

	if (gafAsyncKeyStateExport < 0x7FFFFFFFFFFF)
		return false;
	if (std::chrono::system_clock::now() - start > std::chrono::milliseconds(100))
	{
		UpdateKeysUnlocked();
		start = std::chrono::system_clock::now();
	}
	return state_bitmap[(virtual_key_code * 2 / 8)] & 1 << virtual_key_code % 4 * 2;
}

bool c_keys::IsReady() const
{
	std::lock_guard<std::mutex> stateLock(state_mutex);
	return gafAsyncKeyStateExport >= 0x7FFFFFFFFFFF;
}

void c_keys::BeginKeyCapture()
{
	std::lock_guard<std::mutex> stateLock(state_mutex);

	if (gafAsyncKeyStateExport < 0x7FFFFFFFFFFF)
		return;

	// Snapshot the current state before clearing pending edges. A key that was
	// already held when capture began must be released and pressed again.
	UpdateKeysUnlocked();
	memset(previous_state_bitmap, 0, sizeof(previous_state_bitmap));
	start = std::chrono::system_clock::now();
}

uint32_t c_keys::GetFirstPressedKey()
{
	std::lock_guard<std::mutex> stateLock(state_mutex);

	if (gafAsyncKeyStateExport < 0x7FFFFFFFFFFF)
		return 0;

	const auto now = std::chrono::system_clock::now();
	if (now - start > std::chrono::milliseconds(20))
	{
		UpdateKeysUnlocked();
		start = now;
	}

	auto consumeKeyPress = [this](uint32_t virtualKeyCode)
		{
			const uint8_t mask = static_cast<uint8_t>(1u << (virtualKeyCode % 8));
			uint8_t& state = previous_state_bitmap[virtualKeyCode / 8];
			if ((state & mask) == 0)
				return false;

			state &= static_cast<uint8_t>(~mask);
			return true;
		};

	// Windows raises generic modifier VKs as well. Prefer the physical side.
	constexpr uint32_t preferredModifierKeys[] =
	{
		VK_LSHIFT,
		VK_RSHIFT,
		VK_LCONTROL,
		VK_RCONTROL,
		VK_LMENU,
		VK_RMENU
	};

	for (const uint32_t virtualKeyCode : preferredModifierKeys)
	{
		if (consumeKeyPress(virtualKeyCode))
			return virtualKeyCode;
	}

	for (uint32_t virtualKeyCode = 1; virtualKeyCode < 256; ++virtualKeyCode)
	{
		if (virtualKeyCode == VK_SHIFT ||
			virtualKeyCode == VK_CONTROL ||
			virtualKeyCode == VK_MENU)
		{
			continue;
		}

		bool isPreferredModifier = false;
		for (const uint32_t modifierKey : preferredModifierKeys)
		{
			if (virtualKeyCode == modifierKey)
			{
				isPreferredModifier = true;
				break;
			}
		}

		if (!isPreferredModifier && consumeKeyPress(virtualKeyCode))
			return virtualKeyCode;
	}

	// Some sources expose only the generic modifier virtual key.
	constexpr uint32_t genericModifierKeys[] = { VK_SHIFT, VK_CONTROL, VK_MENU };
	for (const uint32_t virtualKeyCode : genericModifierKeys)
	{
		if (consumeKeyPress(virtualKeyCode))
			return virtualKeyCode;
	}

	return 0;
}

#include "pch.h"
#include "Memory.h"
#include "../app/globals.h"
#include "../app/debug.h"

#include <thread>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <chrono>

uint64_t cbSize = 0x80000;
//callback for VfsFileListU
VOID cbAddFile(_Inout_ HANDLE h, _In_ LPCSTR uszName, _In_ ULONG64 cb, _In_opt_ PVMMDLL_VFS_FILELIST_EXINFO pExInfo)
{
	if (strcmp(uszName, "dtb.txt") == 0)
		cbSize = cb;
}

struct Info
{
	uint32_t index;
	uint32_t process_id;
	uint64_t dtb;
	uint64_t kernelAddr;
	std::string name;
};

namespace
{
	std::string Hex64(uint64_t value)
	{
		std::ostringstream oss;
		oss << "0x" << std::hex << std::uppercase << value;
		return oss.str();
	}

	void MemoryLogError(const std::string& message)
	{
		LOGS.logError("[Memory] " + message);
	}

	void MemoryLogInfo(const char* message)
	{
		LOGS.logInfo(
			std::string("[Memory] ") +
			(message ? message : "")
		);
	}

	void MemoryLogInfo(const std::string& message)
	{
		LOGS.logInfo("[Memory] " + message);
	}
}

std::string FormatCount(double value)
{
	if (value < 0.0)
		value = 0.0;

	const uint64_t roundedValue =
		static_cast<uint64_t>(value + 0.5);

	std::string text = std::to_string(roundedValue);

	for (int position = static_cast<int>(text.length()) - 3;
		position > 0;
		position -= 3)
	{
		text.insert(static_cast<size_t>(position), ",");
	}

	return text;
}

std::string FormatBytes(double bytes)
{
	static constexpr const char* units[] =
	{
		"B",
		"KB",
		"MB",
		"GB"
	};

	int unitIndex = 0;

	while (bytes >= 1024.0 && unitIndex < 3)
	{
		bytes /= 1024.0;
		++unitIndex;
	}

	std::ostringstream oss;

	if (unitIndex == 0)
		oss << std::fixed << std::setprecision(0);
	else
		oss << std::fixed << std::setprecision(2);

	oss << bytes << ' ' << units[unitIndex];
	return oss.str();
}

std::string FormatRate(double value)
{
	std::ostringstream oss;

	if (value < 100.0)
		oss << std::fixed << std::setprecision(1);
	else
		oss << std::fixed << std::setprecision(0);

	oss << value;
	return oss.str();
}

std::string BuildTrafficStatsString(const MemoryTrafficStats& stats)
{
	std::ostringstream oss;

	oss
		<< "DMA I/O | "
		<< "R: " << FormatRate(stats.readOperationsPerSecond) << " ops/s"
		<< " | " << FormatRate(stats.readRequestsPerSecond) << " req/s"
		<< " | " << FormatBytes(stats.readBytesRequestedPerSecond) << "/s"
		<< " || W: " << FormatRate(stats.writeOperationsPerSecond) << " ops/s"
		<< " | " << FormatRate(stats.writeRequestsPerSecond) << " req/s"
		<< " | " << FormatBytes(stats.writeBytesRequestedPerSecond) << "/s"
		<< " || Failures R/W: "
		<< stats.readFailures << "/" << stats.writeFailures;

	return oss.str();
}

// ------------------------------------------------------------
// Flags
// ------------------------------------------------------------

DWORD Memory::BuildReadFlags(bool useCache)
{
	DWORD flags = VMMDLL_FLAG_ZEROPAD_ON_FAIL;

	if (!useCache)
	{
		flags |= VMMDLL_FLAG_NOCACHE;
		flags |= VMMDLL_FLAG_NOCACHEPUT;
	}

	return flags;
}

DWORD Memory::BuildScatterFlags(bool useCache)
{
	DWORD flags =
		VMMDLL_FLAG_ZEROPAD_ON_FAIL |
		VMMDLL_FLAG_NOPAGING |
		VMMDLL_FLAG_NOPAGING_IO |
		VMMDLL_FLAG_SCATTER_PREPAREEX_NOMEMZERO;

	if (!useCache)
	{
		flags |= VMMDLL_FLAG_NOCACHE;
		flags |= VMMDLL_FLAG_NOCACHEPUT;
	}

	return flags;
}

// ------------------------------------------------------------
// Construction / destruction
// ------------------------------------------------------------

Memory::Memory()
{
	MemoryLogInfo("Loading libraries");

	modules.VMM = LoadLibraryA("vmm.dll");
	modules.FTD3XX = LoadLibraryA("FTD3XX.dll");
	modules.LEECHCORE = LoadLibraryA("leechcore.dll");

	if (!modules.VMM || !modules.FTD3XX || !modules.LEECHCORE)
	{
		std::ostringstream oss;
		oss << "Could not load one or more libraries. "
			<< "vmm=" << modules.VMM
			<< " ftd=" << modules.FTD3XX
			<< " leechcore=" << modules.LEECHCORE;

		MemoryLogError(oss.str());
		THROW("[!] Could not load a library\n");
	}

	key = std::make_shared<c_keys>();

	MemoryLogInfo("Successfully loaded libraries");
}

Memory::~Memory()
{
	if (dmaThread.joinable())
		dmaThread.join();

	std::lock_guard<std::mutex> lock(handleMutex);

	if (vHandle)
	{
		VMMDLL_Close(vHandle);
		vHandle = nullptr;
	}

	DMA_INITIALIZED = false;
	PROCESS_INITIALIZED = false;

	memoryGlobals::dmaConnected = FALSE;
	memoryGlobals::processFound = FALSE;
}

void Memory::doDMAConnect()
{
	bool expected = false;

	if (!initRunning.compare_exchange_strong(
		expected,
		true,
		std::memory_order_acq_rel
	))
	{
		return; // Already connecting / waiting for process.
	}

	// Previous worker completed but its std::thread object still exists.
	// It is safe to join here because initRunning was false before CAS succeeded.
	if (dmaThread.joinable())
		dmaThread.join();

	cancelInit.store(false, std::memory_order_release);

	dmaThread = std::thread([this]()
		{
			bool success = false;

			try
			{
				success = Init(true, false);
			}
			catch (const std::exception& e)
			{
				MemoryLogInfo(std::string("[!] DMA worker exception: ") + e.what());
			}
			catch (...)
			{
				MemoryLogInfo("[!] Unknown DMA worker exception");
			}

			// Init returns false when cancelled or when DMA setup genuinely fails.
			if (!success || cancelInit.load(std::memory_order_acquire))
			{
				CloseAndReset();
			}

			initRunning.store(false, std::memory_order_release);
		});
}

DmaConnectionState Memory::GetDmaState() const
{
	return dmaState.load(std::memory_order_acquire);
}

bool Memory::IsInitRunning() const
{
	return initRunning.load(std::memory_order_acquire);
}

// ------------------------------------------------------------
// Restricted / unsupported routines
// ------------------------------------------------------------

std::filesystem::path GetMemMapPath()
{
	std::error_code ec;
	const auto currentDir = std::filesystem::current_path(ec);
	return ec
		? std::filesystem::path("mmap.txt")
		: currentDir / "mmap.txt";
}

bool IsMemMapFileValid(const std::filesystem::path& mapPath)
{
	std::error_code ec;

	if (!std::filesystem::is_regular_file(mapPath, ec) || ec)
		return false;

	const auto fileSize = std::filesystem::file_size(mapPath, ec);
	if (ec || fileSize == 0)
		return false;

	std::ifstream file(mapPath);
	if (!file.is_open())
		return false;

	std::string line;
	bool foundValidRange = false;

	while (std::getline(file, line))
	{
		if (line.find_first_not_of(" \t\r\n") == std::string::npos)
			continue;

		std::istringstream stream(line);

		std::uint64_t start = 0;
		std::uint64_t end = 0;
		std::string trailing;

		if (!(stream >> std::hex >> start >> end))
			return false;

		if (start > end)
			return false;

		if (stream >> trailing)
			return false;

		foundValidRange = true;
	}

	return foundValidRange;
}

bool Memory::DumpMemoryMap(bool debug)
{
	const std::filesystem::path mapPath = GetMemMapPath();
	std::filesystem::path tempMapPath = mapPath;
	tempMapPath += ".tmp";

	std::vector<LPCSTR> args;
	args.reserve(8);

	args.push_back("");
	args.push_back("-device");
	args.push_back("fpga://algo=0");

	if (debug)
	{
		args.push_back("-v");
		args.push_back("-printf");
	}

	VMM_HANDLE tempHandle = VMMDLL_Initialize(
		static_cast<DWORD>(args.size()),
		args.data()
	);

	if (!tempHandle)
	{
		MemoryLogInfo("[!] Failed to open temporary VMM handle for mmap dump");
		return false;
	}

	PVMMDLL_MAP_PHYSMEM pPhysMemMap = nullptr;

	if (!VMMDLL_Map_GetPhysMem(tempHandle, &pPhysMemMap))
	{
		MemoryLogInfo("[!] Failed to get physical memory map");

		VMMDLL_Close(tempHandle);
		return false;
	}

	if (!pPhysMemMap ||
		pPhysMemMap->dwVersion != VMMDLL_MAP_PHYSMEM_VERSION ||
		pPhysMemMap->cMap == 0)
	{
		MemoryLogInfo("[!] Invalid or empty physical memory map");

		VMMDLL_MemFree(pPhysMemMap);
		VMMDLL_Close(tempHandle);
		return false;
	}

	std::error_code ec;
	std::filesystem::remove(tempMapPath, ec);

	std::ofstream file(tempMapPath, std::ios::out | std::ios::trunc);
	if (!file.is_open())
	{
		MemoryLogInfo("[!] Failed to create temporary mmap file");

		VMMDLL_MemFree(pPhysMemMap);
		VMMDLL_Close(tempHandle);
		return false;
	}

	bool writeOk = true;
	DWORD writtenRanges = 0;

	for (DWORD i = 0; i < pPhysMemMap->cMap; ++i)
	{
		const ULONG64 start = pPhysMemMap->pMap[i].pa;
		const ULONG64 length = pPhysMemMap->pMap[i].cb;

		if (length == 0)
			continue;

		if (start > (std::numeric_limits<ULONG64>::max() - (length - 1)))
		{
			MemoryLogInfo("[!] Invalid physical memory range while building mmap");
			writeOk = false;
			break;
		}

		const ULONG64 end = start + length - 1;

		file << std::hex << start << ' ' << end << '\n';

		if (!file.good())
		{
			MemoryLogInfo("[!] Failed while writing mmap file");
			writeOk = false;
			break;
		}

		++writtenRanges;
	}

	file.flush();
	writeOk = writeOk && file.good() && writtenRanges > 0;

	file.close();

	VMMDLL_MemFree(pPhysMemMap);
	VMMDLL_Close(tempHandle);

	if (!writeOk)
	{
		std::filesystem::remove(tempMapPath, ec);
		return false;
	}

	std::filesystem::remove(mapPath, ec);

	ec.clear();
	std::filesystem::rename(tempMapPath, mapPath, ec);

	if (ec)
	{
		MemoryLogInfo("[!] Failed to move temporary mmap into place");

		std::filesystem::remove(tempMapPath, ec);
		return false;
	}

	MemoryLogInfo("Successfully dumped memory map to mmap.txt");
	return true;
}

unsigned char abort2[4] = { 0x10, 0x00, 0x10, 0x00 };
bool Memory::SetFPGA()
{
	ULONG64 qwID = 0, qwVersionMajor = 0, qwVersionMinor = 0;
	if (!VMMDLL_ConfigGet(this->vHandle, LC_OPT_FPGA_FPGA_ID, &qwID) && VMMDLL_ConfigGet(this->vHandle, LC_OPT_FPGA_VERSION_MAJOR, &qwVersionMajor) && VMMDLL_ConfigGet(this->vHandle, LC_OPT_FPGA_VERSION_MINOR, &qwVersionMinor))
	{
		MemoryLogInfo("[!] Failed to lookup FPGA device, Attempting to proceed");
		return false;
	}

	if ((qwVersionMajor >= 4) && ((qwVersionMajor >= 5) || (qwVersionMinor >= 7)))
	{
		HANDLE handle;
		LC_CONFIG config = { .dwVersion = LC_CONFIG_VERSION, .szDevice = "existing" };
		handle = LcCreate(&config);
		if (!handle)
		{
			MemoryLogInfo("[!] Failed to create FPGA device");
			return false;
		}

		LcCommand(handle, LC_CMD_FPGA_CFGREGPCIE_MARKWR | 0x002, 4, reinterpret_cast<PBYTE>(&abort2), NULL, NULL);
		MemoryLogInfo("[-] Register auto cleared");
		LcClose(handle);
	}

	return true;
}

bool Memory::FixCr3()
{
	PVMMDLL_MAP_MODULEENTRY module_entry = NULL;
	bool result = VMMDLL_Map_GetModuleFromNameU(this->vHandle, current_process.PID, const_cast<LPSTR>(current_process.process_name.c_str()), &module_entry, NULL);
	if (result)
	{
		
		return true;
	}
	if (!VMMDLL_InitializePlugins(this->vHandle))
	{
		MemoryLogInfo("[-] Failed VMMDLL_InitializePlugins call");
		return false;
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(500));

	while (true)
	{
		BYTE bytes[4] = { 0 };
		DWORD i = 0;
		auto nt = VMMDLL_VfsReadW(this->vHandle, const_cast<LPWSTR>(L"\\misc\\procinfo\\progress_percent.txt"), bytes, 3, &i, 0);
		if (nt == VMMDLL_STATUS_SUCCESS && atoi(reinterpret_cast<LPSTR>(bytes)) == 100)
			break;

		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	VMMDLL_VFS_FILELIST2 VfsFileList;
	VfsFileList.dwVersion = VMMDLL_VFS_FILELIST_VERSION;
	VfsFileList.h = 0;
	VfsFileList.pfnAddDirectory = 0;
	VfsFileList.pfnAddFile = cbAddFile; //dumb af callback who made this system

	result = VMMDLL_VfsListU(this->vHandle, const_cast<LPSTR>("\\misc\\procinfo\\"), &VfsFileList);
	if (!result)
		return false;

	//read the data from the txt and parse it
	const size_t buffer_size = cbSize;
	std::unique_ptr<BYTE[]> bytes(new BYTE[buffer_size]);
	DWORD j = 0;
	auto nt = VMMDLL_VfsReadW(this->vHandle, const_cast<LPWSTR>(L"\\misc\\procinfo\\dtb.txt"), bytes.get(), buffer_size - 1, &j, 0);
	if (nt != VMMDLL_STATUS_SUCCESS)
		return false;

	std::vector<uint64_t> possible_dtbs = { };
	std::string lines(reinterpret_cast<char*>(bytes.get()));
	std::istringstream iss(lines);
	std::string line = "";

	while (std::getline(iss, line))
	{
		Info info = { };

		std::istringstream info_ss(line);
		if (info_ss >> std::hex >> info.index >> std::dec >> info.process_id >> std::hex >> info.dtb >> info.kernelAddr >> info.name)
		{
			if (info.process_id == 0) //parts that lack a name or have a NULL pid are suspects
				possible_dtbs.push_back(info.dtb);
			if (current_process.process_name.find(info.name) != std::string::npos)
				possible_dtbs.push_back(info.dtb);
		}
	}

	//loop over possible dtbs and set the config to use it til we find the correct one
	for (size_t i = 0; i < possible_dtbs.size(); i++)
	{
		auto dtb = possible_dtbs[i];
		VMMDLL_ConfigSet(this->vHandle, VMMDLL_OPT_PROCESS_DTB | current_process.PID, dtb);
		result = VMMDLL_Map_GetModuleFromNameU(this->vHandle, current_process.PID, const_cast<LPSTR>(current_process.process_name.c_str()), &module_entry, NULL);
		if (result)
		{
			MemoryLogInfo("[+] Patched DTB");
			return true;
		}
	}

	MemoryLogInfo("[-] Failed to patch module");
	return false;
}

bool Memory::DumpMemory(uintptr_t address, std::string path)
{
	MemoryLogInfo("[!] Memory dumping currently does not rebuild the IAT table, imports will be missing from the dump.\n");
	IMAGE_DOS_HEADER dos{ };
	Read(address, &dos, sizeof(IMAGE_DOS_HEADER));

	//Check if memory has a PE 
	if (dos.e_magic != 0x5A4D) //Check if it starts with MZ
	{
		MemoryLogInfo("[-] Invalid PE Header\n");
		return false;
	}

	IMAGE_NT_HEADERS64 nt;
	Read(address + dos.e_lfanew, &nt, sizeof(IMAGE_NT_HEADERS64));

	//Sanity check
	if (nt.Signature != IMAGE_NT_SIGNATURE || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
	{
		MemoryLogInfo("[-] Failed signature check\n");
		return false;
	}
	//Shouldn't change ever. so const 
	const size_t target_size = nt.OptionalHeader.SizeOfImage;
	//Crashes if we don't make it a ptr :(
	auto target = std::unique_ptr<uint8_t[]>(new uint8_t[target_size]);

	//Read whole modules memory
	Read(address, target.get(), target_size);
	auto nt_header = (PIMAGE_NT_HEADERS64)(target.get() + dos.e_lfanew);
	auto sections = (PIMAGE_SECTION_HEADER)(target.get() + dos.e_lfanew + FIELD_OFFSET(IMAGE_NT_HEADERS, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader);

	for (size_t i = 0; i < nt.FileHeader.NumberOfSections; i++, sections++)
	{
		
		sections->PointerToRawData = sections->VirtualAddress;
		sections->SizeOfRawData = sections->Misc.VirtualSize;
	}

	//Find all modules used by this process
	//auto descriptor = Read<IMAGE_IMPORT_DESCRIPTOR>(address + ntHeader->OptionalHeader.DataDirectory[1].VirtualAddress);

	//int descriptor_count = 0;
	//int thunk_count = 0;

	/*std::vector<ModuleData> modulelist;
	while (descriptor.Name) {
		auto first_thunk = Read<IMAGE_THUNK_DATA>(moduleAddr + descriptor.FirstThunk);
		auto original_first_thunk = Read<IMAGE_THUNK_DATA>(moduleAddr + descriptor.OriginalFirstThunk);
		thunk_count = 0;

		char ModuleName[256];
		ReadMemory(moduleAddr + descriptor.Name, (void*)&ModuleName, 256);

		std::string DllName = ModuleName;

		ModuleData tmpModuleData;

		//if(std::find(modulelist.begin(), modulelist.end(), tmpModuleData) == modulelist.end())
		//	modulelist.push_back(tmpModuleData);
		while (original_first_thunk.u1.AddressOfData) {
			char name[256];
			ReadMemory(moduleAddr + original_first_thunk.u1.AddressOfData + 0x2, (void*)&name, 256);

			std::string str_name = name;
			auto thunk_offset{ thunk_count * sizeof(uintptr_t) };

			//if (str_name.length() > 0)
			//	imports[str_name] = moduleAddr + descriptor.FirstThunk + thunk_offset;

			++thunk_count;
			first_thunk = Read<IMAGE_THUNK_DATA>(moduleAddr + descriptor.FirstThunk + sizeof(IMAGE_THUNK_DATA) * thunk_count);
			original_first_thunk = Read<IMAGE_THUNK_DATA>(moduleAddr + descriptor.OriginalFirstThunk + sizeof(IMAGE_THUNK_DATA) * thunk_count);
		}

		++descriptor_count;
		descriptor = Read<IMAGE_IMPORT_DESCRIPTOR>(moduleAddr + ntHeader->OptionalHeader.DataDirectory[1].VirtualAddress + sizeof(IMAGE_IMPORT_DESCRIPTOR) * descriptor_count);
	}*/

	//Rebuild import table

	//LOG("[!] Creating new import section\n");

	//Create New Import Section

	//Build new import Table

	//Dump file
	const auto dumped_file = CreateFileW(std::wstring(path.begin(), path.end()).c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_COMPRESSED, NULL);
	if (dumped_file == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	if (!WriteFile(dumped_file, target.get(), static_cast<DWORD>(target_size), NULL, NULL))
	{
		CloseHandle(dumped_file);
		return false;
	}

	CloseHandle(dumped_file);
	return true;
}

static const char* hexdigits =
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\001\002\003\004\005\006\007\010\011\000\000\000\000\000\000"
"\000\012\013\014\015\016\017\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\012\013\014\015\016\017\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000";

static uint8_t GetByte(const char* hex)
{
	return static_cast<uint8_t>((hexdigits[hex[0]] << 4) | (hexdigits[hex[1]]));
}

uint64_t Memory::FindSignature(
	const char* signature,
	uint64_t range_start,
	uint64_t range_end,
	int PID,
	bool useCache
)
{
	if (!signature || !*signature)
	{
		LOG("[FindSignature] Invalid signature string\n");
		return 0;
	}

	if (range_start >= range_end)
	{
		LOG(
			"[FindSignature] Invalid scan range: start=0x%llX end=0x%llX\n",
			range_start,
			range_end
		);
		return 0;
	}

	if (!vHandle)
	{
		LOG("[FindSignature] vHandle is null\n");
		return 0;
	}

	if (PID == 0)
		PID = current_process.PID;

	if (PID == 0)
	{
		LOG("[FindSignature] PID is zero\n");
		return 0;
	}

	// ------------------------------------------------------------
	// Parse signature such as:
	// "48 8B ?? ?? 89"
	// ------------------------------------------------------------
	std::vector<uint8_t> pattern;
	std::vector<bool> mask;

	for (const char* cur = signature; *cur;)
	{
		if (*cur == ' ')
		{
			++cur;
			continue;
		}

		if (*cur == '?')
		{
			pattern.push_back(0);
			mask.push_back(false);

			++cur;

			if (*cur == '?')
				++cur;
		}
		else
		{
			// Need two valid hex characters for a normal byte.
			if (!cur[0] || !cur[1])
			{
				LOG("[FindSignature] Invalid signature byte\n");
				return 0;
			}

			pattern.push_back(GetByte(cur));
			mask.push_back(true);

			cur += 2;
		}
	}

	if (pattern.empty())
	{
		LOG("[FindSignature] Parsed pattern is empty\n");
		return 0;
	}

	const uint64_t scanSize = range_end - range_start;

	if (scanSize < pattern.size())
	{
		LOG("[FindSignature] Scan range is smaller than signature\n");
		return 0;
	}

	constexpr size_t CHUNK_SIZE = 0x10000;

	std::vector<uint8_t> buffer(
		CHUNK_SIZE + pattern.size() - 1
	);

	for (uint64_t addr = range_start; addr < range_end; addr += CHUNK_SIZE)
	{
		const uint64_t remainingBytes = range_end - addr;

		const size_t bytesToRead = static_cast<size_t>(
			std::min<uint64_t>(
				CHUNK_SIZE + pattern.size() - 1,
				remainingBytes
			)
			);

		if (bytesToRead < pattern.size())
			break;

		DWORD readSize = 0;

		const bool ok = VMMDLL_MemReadEx(
			vHandle,
			PID,
			addr,
			buffer.data(),
			static_cast<DWORD>(bytesToRead),
			&readSize,
			BuildReadFlags(useCache)
		);

		const bool completeRead = ok && readSize == bytesToRead;

		// Includes direct signature scanning in traffic statistics.
		RecordDirectRead(
			bytesToRead,
			readSize,
			completeRead
		);

		if (!completeRead)
		{
			LOG(
				"[FindSignature] MemRead failed at 0x%llX "
				"(requested=0x%zX read=0x%X)\n",
				addr,
				bytesToRead,
				readSize
			);

			continue;
		}

		const size_t lastStartOffset = bytesToRead - pattern.size();

		for (size_t i = 0; i <= lastStartOffset; ++i)
		{
			bool match = true;

			for (size_t j = 0; j < pattern.size(); ++j)
			{
				if (mask[j] && buffer[i + j] != pattern[j])
				{
					match = false;
					break;
				}
			}

			if (match)
			{
				const uint64_t foundAt = addr + i;

				LOG(
					"[FindSignature] Match found at 0x%llX\n",
					foundAt
				);

				return foundAt;
			}
		}
	}

	LOG(
		"[FindSignature] Signature not found in range 0x%llX-0x%llX\n",
		range_start,
		range_end
	);

	return 0;
}

// ------------------------------------------------------------
// Config
// ------------------------------------------------------------

void Memory::setCustomRefreshData()
{
	if (!vHandle)
	{
		MemoryLogError("setCustomRefreshData failed: vHandle is null");
		return;
	}

	VMMDLL_ConfigSet(vHandle, VMMDLL_OPT_CONFIG_PROCCACHE_TICKS_PARTIAL, 200);
	VMMDLL_ConfigSet(vHandle, VMMDLL_OPT_CONFIG_PROCCACHE_TICKS_TOTAL, 2000);
	VMMDLL_ConfigSet(vHandle, VMMDLL_OPT_CONFIG_READCACHE_TICKS, 50);
	VMMDLL_ConfigSet(vHandle, VMMDLL_OPT_CONFIG_TLBCACHE_TICKS, 100);
}

// ------------------------------------------------------------
// Refresh
// ------------------------------------------------------------

void Memory::RefreshLight()
{
	if (!vHandle)
		return;

	// Light-ish refresh: memory/read cache + TLB.
	VMMDLL_ConfigSet(vHandle, VMMDLL_OPT_REFRESH_FREQ_MEM, 1);
	VMMDLL_ConfigSet(vHandle, VMMDLL_OPT_REFRESH_FREQ_TLB, 1);
}

bool Memory::fullRefresh()
{
	if (!vHandle)
	{
		MemoryLogError("quickRefresh failed: vHandle is null");
		return false;
	}

	if (!VMMDLL_ConfigSet(vHandle, VMMDLL_OPT_REFRESH_ALL, 1))
	{
		MemoryLogError("quickRefresh failed");
		return false;
	}

	return true;
}

// ------------------------------------------------------------
// Disconnect from dma device
// ------------------------------------------------------------

void Memory::doDMADisconnect()
{
	cancelInit.store(true, std::memory_order_release);

	// If Init is still running, its worker will see cancelInit,
	// leave its retry loop, then call CloseAndReset().
	if (initRunning.load(std::memory_order_acquire))
		return;

	// Already connected; start cleanup on a worker too.
	bool expected = false;

	if (!initRunning.compare_exchange_strong(
		expected,
		true,
		std::memory_order_acq_rel
	))
	{
		return;
	}

	if (dmaThread.joinable())
		dmaThread.join();

	dmaThread = std::thread([this]()
		{
			CloseAndReset();

			cancelInit.store(false, std::memory_order_release);
			initRunning.store(false, std::memory_order_release);
		});
}

bool Memory::IsDisconnectRequested() const
{
	return cancelInit.load(std::memory_order_acquire);
}

// ------------------------------------------------------------
// Init
// ------------------------------------------------------------

bool Memory::Init(bool memMap, bool debug)
{
	constexpr LPCSTR processName = "EscapeFromTarkov.exe";
	constexpr LPCSTR targetModule = "UnityPlayer.dll";

	auto clearProcessState = [this]()
		{
			PROCESS_INITIALIZED = FALSE;

			memoryGlobals::processFound.store(false, std::memory_order_release);

			current_process.PID = 0;
			current_process.base_address = 0;
			current_process.base_size = 0;
			current_process.process_name.clear();
		};

	auto closeAndReset = [this, &clearProcessState]()
		{
			clearProcessState();

			{
				std::lock_guard<std::mutex> lock(handleMutex);

				if (vHandle)
				{
					VMMDLL_Close(vHandle);
					vHandle = nullptr;
				}

				DMA_INITIALIZED = FALSE;
			}

			memoryGlobals::dmaConnected.store(false, std::memory_order_release);
		};

	// Disconnect was pressed before the worker properly started.
	if (cancelInit.load(std::memory_order_acquire))
	{
		closeAndReset();
		return false;
	}

	//INITIALISATION

	bool dmaReady = false;

	{
		std::lock_guard<std::mutex> lock(handleMutex);

		// Protect against a bad partial state.
		if (DMA_INITIALIZED && !vHandle)
		{
			MemoryLogInfo("[!]DMA was marked initialised but VMM handle was null");

			DMA_INITIALIZED = FALSE;
			memoryGlobals::dmaConnected.store(false, std::memory_order_release);
		}

		dmaReady = (DMA_INITIALIZED && vHandle);
	}

	if (!dmaReady)
	{
		MemoryLogInfo("Initialising DMA...");

		std::filesystem::path memMapPath;
		std::error_code fsError;

		memMapPath = std::filesystem::current_path(fsError);

		if (fsError)
		{
			MemoryLogInfo("[!] Could not resolve current path; using local mmap.txt");
			memMapPath = "mmap.txt";
		}
		else
		{
			memMapPath /= "mmap.txt";
		}

		bool useMemMap = false;

		if (memMap)
		{
			fsError.clear();

			bool mapExists =
				std::filesystem::is_regular_file(memMapPath, fsError) &&
				!fsError;

			if (mapExists)
			{
				const auto mapSize = std::filesystem::file_size(memMapPath, fsError);

				mapExists = !fsError && mapSize > 0;
			}

			if (!mapExists)
			{
				MemoryLogInfo("No valid mmap.txt found; generating memory map...");

				fsError.clear();
				std::filesystem::remove(memMapPath, fsError);

				if (DumpMemoryMap(debug))
				{
					fsError.clear();

					useMemMap =
						std::filesystem::is_regular_file(memMapPath, fsError) &&
						!fsError;

					if (useMemMap)
					{
						const auto mapSize =
							std::filesystem::file_size(memMapPath, fsError);

						useMemMap = !fsError && mapSize > 0;
					}

					if (useMemMap)
						MemoryLogInfo("Memory map created successfully");
					else
						MemoryLogInfo("[!] mmap dump reported success but mmap.txt was invalid");
				}
				else
				{
					MemoryLogInfo("[!] Could not create mmap.txt; continuing without memory map");
				}
			}
			else
			{
				useMemMap = true;
				MemoryLogInfo("Using existing mmap.txt");
			}
		}

		if (cancelInit.load(std::memory_order_acquire))
		{
			closeAndReset();
			return false;
		}

		std::vector<LPCSTR> args;
		args.reserve(10);

		args.push_back("");
		args.push_back("-device");
		args.push_back("fpga://algo=0");

		if (debug)
		{
			args.push_back("-v");
			args.push_back("-printf");
		}

		std::string memMapPathString;

		if (useMemMap)
		{
			memMapPathString = memMapPath.string();

			args.push_back("-memmap");
			args.push_back(memMapPathString.c_str());
		}

		VMM_HANDLE newHandle = VMMDLL_Initialize(
			static_cast<DWORD>(args.size()),
			args.data()
		);

		if (!newHandle)
		{
			MemoryLogInfo("[!] DMA initialisation failed. Is the DMA disconnected or already in use?");

			closeAndReset();
			return false;
		}

		bool fpgaConfigured = false;

		{
			std::lock_guard<std::mutex> lock(handleMutex);

			// A stale handle should not survive a reconnect attempt.
			if (vHandle)
			{
				VMMDLL_Close(vHandle);
				vHandle = nullptr;
			}

			if (cancelInit.load(std::memory_order_acquire))
			{
				VMMDLL_Close(newHandle);
				newHandle = nullptr;
			}
			else
			{
				vHandle = newHandle;
				newHandle = nullptr;

				ULONG64 FPGA_ID = 0;
				ULONG64 DEVICE_ID = 0;

				VMMDLL_ConfigGet(vHandle, LC_OPT_FPGA_FPGA_ID, &FPGA_ID);
				VMMDLL_ConfigGet(vHandle, LC_OPT_FPGA_DEVICE_ID, &DEVICE_ID);

				if (SetFPGA())
				{
					DMA_INITIALIZED = TRUE;
					fpgaConfigured = true;
				}
				else
				{
					MemoryLogInfo("[!] Could not configure FPGA");

					VMMDLL_Close(vHandle);
					vHandle = nullptr;

					DMA_INITIALIZED = FALSE;
				}
			}
		}

		if (newHandle)
		{
			VMMDLL_Close(newHandle);
			newHandle = nullptr;
		}

		if (!fpgaConfigured)
		{
			closeAndReset();
			return false;
		}

		memoryGlobals::dmaConnected.store(true, std::memory_order_release);

		MemoryLogInfo("DMA connected successfully");
	}
	else
	{
		memoryGlobals::dmaConnected.store(true, std::memory_order_release);
		MemoryLogInfo("DMA already initialised");
	}

	
	// INITIALISATION

	if (cancelInit.load(std::memory_order_acquire))
	{
		closeAndReset();
		return false;
	}

	// Check whether an existing cached process still matches
	if (PROCESS_INITIALIZED)
	{
		DWORD livePid = 0;
		bool handleValid = false;

		{
			std::lock_guard<std::mutex> lock(handleMutex);

			handleValid = (vHandle != nullptr);

			if (handleValid)
				livePid = GetPidFromName(processName);
		}

		if (handleValid &&
			livePid != 0 &&
			livePid == current_process.PID)
		{
			MemoryLogInfo("Process already initialised");
			return true;
		}

		MemoryLogInfo("Cached process state is stale; resolving process again");
		clearProcessState();
	}
	else
	{
		clearProcessState();
	}

	/*
		Outer loop:
		- waits for game
		- then waits for UnityPlayer.dll
		- restarts PID lookup if the game closes/restarts mid-attach
	*/
	while (!cancelInit.load(std::memory_order_acquire))
	{
		DWORD foundPid = 0;
		bool handleValid = false;

		{
			std::lock_guard<std::mutex> lock(handleMutex);

			handleValid = (vHandle != nullptr);

			if (handleValid)
				foundPid = GetPidFromName(processName);
		}

		if (!handleValid)
		{
			MemoryLogInfo("[!] VMM handle became invalid while waiting for process");

			closeAndReset();
			return false;
		}

		if (!foundPid)
		{
			MemoryLogInfo("Waiting for target process...");

			if (!WaitForRetryOrCancel(5000))
				break;

			continue;
		}

		current_process.PID = foundPid;
		current_process.process_name = processName;
		current_process.base_address = 0;
		current_process.base_size = 0;

		MemoryLogInfo("Target process found; waiting for UnityPlayer.dll...");

		bool processRestarted = false;

		while (!cancelInit.load(std::memory_order_acquire))
		{
			uint64_t moduleBase = 0;
			DWORD livePid = 0;
			bool innerHandleValid = false;

			{
				std::lock_guard<std::mutex> lock(handleMutex);

				innerHandleValid = (vHandle != nullptr);

				if (innerHandleValid)
				{
					moduleBase = GetBaseDaddy(targetModule);

					if (!moduleBase)
						livePid = GetPidFromName(processName);
				}
			}

			if (!innerHandleValid)
			{
				MemoryLogInfo("[!] VMM handle became invalid while resolving UnityPlayer.dll");

				closeAndReset();
				return false;
			}

			if (moduleBase)
			{
				current_process.base_address = moduleBase;

				{
					std::lock_guard<std::mutex> lock(handleMutex);

					if (vHandle)
						current_process.base_size = GetBaseSize(targetModule);
				}

				if (!current_process.base_size)
					MemoryLogInfo("[!] Could not get UnityPlayer.dll module size");

				PROCESS_INITIALIZED = TRUE;

				memoryGlobals::processFound.store(
					true,
					std::memory_order_release
				);

				MemoryLogInfo("Target process initialised successfully");
				return true;
			}

			// Game closed or restarted while we were waiting for UnityPlayer.dll.
			if (livePid == 0 || livePid != current_process.PID)
			{
				MemoryLogInfo("Target process restarted while attaching; resolving PID again");

				clearProcessState();
				processRestarted = true;
				break;
			}

			if (!WaitForRetryOrCancel(1000))
				break;
		}

		if (!processRestarted &&
			cancelInit.load(std::memory_order_acquire))
		{
			break;
		}
	}

	// Only reached when Disconnect was requested.
	closeAndReset();
	return false;
}

bool Memory::WaitForRetryOrCancel(DWORD totalMilliseconds)
{
	constexpr DWORD sliceMilliseconds = 100;

	for (DWORD elapsed = 0; elapsed < totalMilliseconds; elapsed += sliceMilliseconds)
	{
		if (cancelInit.load(std::memory_order_acquire))
			return false;

		const DWORD remaining = totalMilliseconds - elapsed;

		Sleep((remaining < sliceMilliseconds)
			? remaining
			: sliceMilliseconds);
	}

	return !cancelInit.load(std::memory_order_acquire);
}

void Memory::CloseAndReset()
{
	// Stop other code
	memoryGlobals::processFound.store(false, std::memory_order_release);
	PROCESS_INITIALIZED = FALSE;

	current_process.PID = 0;
	current_process.base_address = 0;
	current_process.base_size = 0;
	current_process.process_name.clear();

	{
		std::lock_guard<std::mutex> lock(handleMutex);

		if (vHandle)
		{
			VMMDLL_Close(vHandle);
			vHandle = nullptr;
		}

		DMA_INITIALIZED = FALSE;
	}

	memoryGlobals::dmaConnected.store(false, std::memory_order_release);

	MemoryLogInfo("DMA disconnected");
}

// ------------------------------------------------------------
// Process / module helpers
// ------------------------------------------------------------

DWORD Memory::GetPidFromName(const std::string& process_name)
{
	if (!vHandle)
	{
		MemoryLogError("GetPidFromName failed: vHandle is null");
		return 0;
	}

	if (process_name.empty())
	{
		MemoryLogError("GetPidFromName failed: process name is empty");
		return 0;
	}

	DWORD pid = 0;

	if (!VMMDLL_PidGetFromName(vHandle, const_cast<LPSTR>(process_name.c_str()), &pid))
	{
		MemoryLogError("Failed to get PID from process name: " + process_name);
		return 0;
	}

	return pid;
}

std::vector<int> Memory::GetPidListFromName(const std::string& name)
{
	std::vector<int> list;

	if (!vHandle)
	{
		MemoryLogError("GetPidListFromName failed: vHandle is null");
		return list;
	}

	if (name.empty())
	{
		MemoryLogError("GetPidListFromName failed: name is empty");
		return list;
	}

	PVMMDLL_PROCESS_INFORMATION processInfo = nullptr;
	DWORD totalProcesses = 0;

	if (!VMMDLL_ProcessGetInformationAll(vHandle, &processInfo, &totalProcesses))
	{
		MemoryLogError("Failed to get process list");
		return list;
	}

	for (DWORD i = 0; i < totalProcesses; ++i)
	{
		const auto& process = processInfo[i];

		if (strstr(process.szNameLong, name.c_str()))
			list.push_back(static_cast<int>(process.dwPID));
	}

	VMMDLL_MemFree(processInfo);
	return list;
}

std::vector<std::string> Memory::GetModuleList(const std::string& process_name)
{
	std::vector<std::string> list;

	if (!vHandle)
	{
		MemoryLogError("GetModuleList failed: vHandle is null");
		return list;
	}

	if (!current_process.PID)
	{
		MemoryLogError("GetModuleList failed: PID is zero");
		return list;
	}

	PVMMDLL_MAP_MODULE moduleInfo = nullptr;

	if (!VMMDLL_Map_GetModuleU(vHandle, current_process.PID, &moduleInfo, VMMDLL_MODULE_FLAG_NORMAL))
	{
		MemoryLogError("Failed to get module list for: " + process_name);
		return list;
	}

	for (DWORD i = 0; i < moduleInfo->cMap; ++i)
	{
		list.emplace_back(moduleInfo->pMap[i].uszText);
	}

	VMMDLL_MemFree(moduleInfo);
	return list;
}

VMMDLL_PROCESS_INFORMATION Memory::GetProcessInformation()
{
	VMMDLL_PROCESS_INFORMATION info{};
	SIZE_T processInformationSize = sizeof(VMMDLL_PROCESS_INFORMATION);

	info.magic = VMMDLL_PROCESS_INFORMATION_MAGIC;
	info.wVersion = VMMDLL_PROCESS_INFORMATION_VERSION;

	if (!vHandle || !current_process.PID)
	{
		MemoryLogError("GetProcessInformation failed: invalid handle or PID");
		return {};
	}

	if (!VMMDLL_ProcessGetInformation(vHandle, current_process.PID, &info, &processInformationSize))
	{
		MemoryLogError("Failed to get process information");
		return {};
	}

	return info;
}

PEB Memory::GetProcessPeb()
{
	const auto info = GetProcessInformation();

	if (!info.win.vaPEB)
	{
		MemoryLogError("Failed to find process PEB");
		return {};
	}

	PEB peb{};

	if (!TryRead(info.win.vaPEB, peb))
	{
		MemoryLogError("Failed to read process PEB at " + Hex64(info.win.vaPEB));
		return {};
	}

	return peb;
}

uintptr_t Memory::GetBaseDaddy(const std::string& module_name)
{
	if (!vHandle || !current_process.PID || module_name.empty())
	{
		MemoryLogError("GetBaseDaddy failed: invalid args");
		return 0;
	}

	std::wstring wideName(module_name.begin(), module_name.end());

	PVMMDLL_MAP_MODULEENTRY moduleInfo = nullptr;

	if (!VMMDLL_Map_GetModuleFromNameW(
		vHandle,
		current_process.PID,
		const_cast<LPWSTR>(wideName.c_str()),
		&moduleInfo,
		VMMDLL_MODULE_FLAG_NORMAL
	))
	{
		MemoryLogError("Could not find base address for module: " + module_name);
		return 0;
	}

	base = moduleInfo->vaBase;
	baseSize = moduleInfo->cbImageSize;

	return moduleInfo->vaBase;
}

size_t Memory::GetBaseSize(const std::string& module_name)
{
	if (!vHandle || !current_process.PID || module_name.empty())
	{
		MemoryLogError("GetBaseSize failed: invalid args");
		return 0;
	}

	std::wstring wideName(module_name.begin(), module_name.end());

	PVMMDLL_MAP_MODULEENTRY moduleInfo = nullptr;

	if (!VMMDLL_Map_GetModuleFromNameW(
		vHandle,
		current_process.PID,
		const_cast<LPWSTR>(wideName.c_str()),
		&moduleInfo,
		VMMDLL_MODULE_FLAG_NORMAL
	))
	{
		MemoryLogError("Could not find base size for module: " + module_name);
		return 0;
	}

	return moduleInfo->cbImageSize;
}

uintptr_t Memory::GetExportTableAddress(std::string import, std::string process, std::string module)
{
	if (!vHandle || import.empty() || process.empty() || module.empty())
	{
		MemoryLogError("GetExportTableAddress failed: invalid args");
		return 0;
	}

	PVMMDLL_MAP_EAT eatMap = nullptr;
	PVMMDLL_MAP_EATENTRY exportEntry = nullptr;

	const DWORD pid = GetPidFromName(process);

	if (!pid)
	{
		MemoryLogError("GetExportTableAddress failed: PID not found for " + process);
		return 0;
	}

	if (!VMMDLL_Map_GetEATU(vHandle, pid, const_cast<LPSTR>(module.c_str()), &eatMap))
	{
		MemoryLogError("Failed to get export table for module: " + module);
		return 0;
	}

	if (eatMap->dwVersion != VMMDLL_MAP_EAT_VERSION)
	{
		VMMDLL_MemFree(eatMap);
		MemoryLogError("Invalid EAT map version");
		return 0;
	}

	uintptr_t address = 0;

	for (DWORD i = 0; i < eatMap->cMap; ++i)
	{
		exportEntry = eatMap->pMap + i;

		if (strcmp(exportEntry->uszFunction, import.c_str()) == 0)
		{
			address = exportEntry->vaFunction;
			break;
		}
	}

	VMMDLL_MemFree(eatMap);
	return address;
}

uintptr_t Memory::GetImportTableAddress(std::string import, std::string process, std::string module)
{
	if (!vHandle || import.empty() || process.empty() || module.empty())
	{
		MemoryLogError("GetImportTableAddress failed: invalid args");
		return 0;
	}

	PVMMDLL_MAP_IAT iatMap = nullptr;
	PVMMDLL_MAP_IATENTRY importEntry = nullptr;

	const DWORD pid = GetPidFromName(process);

	if (!pid)
	{
		MemoryLogError("GetImportTableAddress failed: PID not found for " + process);
		return 0;
	}

	if (!VMMDLL_Map_GetIATU(vHandle, pid, const_cast<LPSTR>(module.c_str()), &iatMap))
	{
		MemoryLogError("Failed to get import table for module: " + module);
		return 0;
	}

	if (iatMap->dwVersion != VMMDLL_MAP_IAT_VERSION)
	{
		VMMDLL_MemFree(iatMap);
		MemoryLogError("Invalid IAT map version");
		return 0;
	}

	uintptr_t address = 0;

	for (DWORD i = 0; i < iatMap->cMap; ++i)
	{
		importEntry = iatMap->pMap + i;

		if (strcmp(importEntry->uszFunction, import.c_str()) == 0)
		{
			address = importEntry->vaFunction;
			break;
		}
	}

	VMMDLL_MemFree(iatMap);
	return address;
}

// Stats Section

Memory::TrafficCounterSnapshot Memory::LoadTrafficCounters() const
{
	TrafficCounterSnapshot snapshot{};

	snapshot.readOperations =
		statsReadOperations.load(std::memory_order_relaxed);

	snapshot.writeOperations =
		statsWriteOperations.load(std::memory_order_relaxed);

	snapshot.readSuccesses =
		statsReadSuccesses.load(std::memory_order_relaxed);

	snapshot.writeSuccesses =
		statsWriteSuccesses.load(std::memory_order_relaxed);

	snapshot.readFailures =
		statsReadFailures.load(std::memory_order_relaxed);

	snapshot.writeFailures =
		statsWriteFailures.load(std::memory_order_relaxed);

	snapshot.readRequests =
		statsReadRequests.load(std::memory_order_relaxed);

	snapshot.writeRequests =
		statsWriteRequests.load(std::memory_order_relaxed);

	snapshot.readBytesRequested =
		statsReadBytesRequested.load(std::memory_order_relaxed);

	snapshot.writeBytesRequested =
		statsWriteBytesRequested.load(std::memory_order_relaxed);

	snapshot.directReadBytesReturned =
		statsDirectReadBytesReturned.load(std::memory_order_relaxed);

	snapshot.scatterReadBatches =
		statsScatterReadBatches.load(std::memory_order_relaxed);

	snapshot.scatterWriteBatches =
		statsScatterWriteBatches.load(std::memory_order_relaxed);

	snapshot.scatterReadRequests =
		statsScatterReadRequests.load(std::memory_order_relaxed);

	snapshot.scatterWriteRequests =
		statsScatterWriteRequests.load(std::memory_order_relaxed);

	snapshot.scatterClearFailures =
		statsScatterClearFailures.load(std::memory_order_relaxed);

	return snapshot;
}

void Memory::RecordDirectRead(
	size_t requestedBytes,
	size_t returnedBytes,
	bool success
) const
{
	statsReadOperations.fetch_add(1, std::memory_order_relaxed);
	statsReadRequests.fetch_add(1, std::memory_order_relaxed);

	statsReadBytesRequested.fetch_add(
		static_cast<uint64_t>(requestedBytes),
		std::memory_order_relaxed
	);

	statsDirectReadBytesReturned.fetch_add(
		static_cast<uint64_t>(returnedBytes),
		std::memory_order_relaxed
	);

	if (success)
		statsReadSuccesses.fetch_add(1, std::memory_order_relaxed);
	else
		statsReadFailures.fetch_add(1, std::memory_order_relaxed);
}

void Memory::RecordDirectWrite(
	size_t requestedBytes,
	bool success
) const
{
	statsWriteOperations.fetch_add(1, std::memory_order_relaxed);
	statsWriteRequests.fetch_add(1, std::memory_order_relaxed);

	statsWriteBytesRequested.fetch_add(
		static_cast<uint64_t>(requestedBytes),
		std::memory_order_relaxed
	);

	if (success)
		statsWriteSuccesses.fetch_add(1, std::memory_order_relaxed);
	else
		statsWriteFailures.fetch_add(1, std::memory_order_relaxed);
}

void Memory::RecordScatterReadRequest(size_t requestedBytes) const
{
	statsReadRequests.fetch_add(1, std::memory_order_relaxed);
	statsScatterReadRequests.fetch_add(1, std::memory_order_relaxed);

	statsReadBytesRequested.fetch_add(
		static_cast<uint64_t>(requestedBytes),
		std::memory_order_relaxed
	);
}

void Memory::RecordScatterWriteRequest(size_t requestedBytes) const
{
	statsWriteRequests.fetch_add(1, std::memory_order_relaxed);
	statsScatterWriteRequests.fetch_add(1, std::memory_order_relaxed);

	statsWriteBytesRequested.fetch_add(
		static_cast<uint64_t>(requestedBytes),
		std::memory_order_relaxed
	);
}

void Memory::RecordScatterReadExecute(bool success) const
{
	statsReadOperations.fetch_add(1, std::memory_order_relaxed);
	statsScatterReadBatches.fetch_add(1, std::memory_order_relaxed);

	if (success)
		statsReadSuccesses.fetch_add(1, std::memory_order_relaxed);
	else
		statsReadFailures.fetch_add(1, std::memory_order_relaxed);
}

void Memory::RecordScatterWriteExecute(bool success) const
{
	statsWriteOperations.fetch_add(1, std::memory_order_relaxed);
	statsScatterWriteBatches.fetch_add(1, std::memory_order_relaxed);

	if (success)
		statsWriteSuccesses.fetch_add(1, std::memory_order_relaxed);
	else
		statsWriteFailures.fetch_add(1, std::memory_order_relaxed);
}

void Memory::RecordScatterClearFailure() const
{
	statsScatterClearFailures.fetch_add(1, std::memory_order_relaxed);
}

MemoryTrafficStats Memory::GetTrafficStats() const
{
	const TrafficCounterSnapshot current = LoadTrafficCounters();
	const auto now = std::chrono::steady_clock::now();

	std::lock_guard<std::mutex> lock(trafficStatsMutex);

	if (!trafficStatsBaselineValid)
	{
		trafficStatsBaseline = current;
		trafficStatsLastSample = now;
		trafficStatsBaselineValid = true;
	}
	else
	{
		const double elapsedSeconds =
			std::chrono::duration<double>(
				now - trafficStatsLastSample
			).count();

		if (elapsedSeconds >= 1.0)
		{
			trafficRateSnapshot.sampleWindowSeconds = elapsedSeconds;

			trafficRateSnapshot.readOperationsPerSecond =
				static_cast<double>(
					current.readOperations - trafficStatsBaseline.readOperations
					) / elapsedSeconds;

			trafficRateSnapshot.writeOperationsPerSecond =
				static_cast<double>(
					current.writeOperations - trafficStatsBaseline.writeOperations
					) / elapsedSeconds;

			trafficRateSnapshot.readRequestsPerSecond =
				static_cast<double>(
					current.readRequests - trafficStatsBaseline.readRequests
					) / elapsedSeconds;

			trafficRateSnapshot.writeRequestsPerSecond =
				static_cast<double>(
					current.writeRequests - trafficStatsBaseline.writeRequests
					) / elapsedSeconds;

			trafficRateSnapshot.readBytesRequestedPerSecond =
				static_cast<double>(
					current.readBytesRequested -
					trafficStatsBaseline.readBytesRequested
					) / elapsedSeconds;

			trafficRateSnapshot.writeBytesRequestedPerSecond =
				static_cast<double>(
					current.writeBytesRequested -
					trafficStatsBaseline.writeBytesRequested
					) / elapsedSeconds;

			trafficStatsBaseline = current;
			trafficStatsLastSample = now;
		}
	}

	MemoryTrafficStats result{};

	result.readOperations = current.readOperations;
	result.writeOperations = current.writeOperations;

	result.readSuccesses = current.readSuccesses;
	result.writeSuccesses = current.writeSuccesses;

	result.readFailures = current.readFailures;
	result.writeFailures = current.writeFailures;

	result.readRequests = current.readRequests;
	result.writeRequests = current.writeRequests;

	result.readBytesRequested = current.readBytesRequested;
	result.writeBytesRequested = current.writeBytesRequested;

	result.directReadBytesReturned =
		current.directReadBytesReturned;

	result.scatterReadBatches = current.scatterReadBatches;
	result.scatterWriteBatches = current.scatterWriteBatches;

	result.scatterReadRequests = current.scatterReadRequests;
	result.scatterWriteRequests = current.scatterWriteRequests;

	result.scatterClearFailures = current.scatterClearFailures;

	result.sampleWindowSeconds =
		trafficRateSnapshot.sampleWindowSeconds;

	result.readOperationsPerSecond =
		trafficRateSnapshot.readOperationsPerSecond;

	result.writeOperationsPerSecond =
		trafficRateSnapshot.writeOperationsPerSecond;

	result.readRequestsPerSecond =
		trafficRateSnapshot.readRequestsPerSecond;

	result.writeRequestsPerSecond =
		trafficRateSnapshot.writeRequestsPerSecond;

	result.readBytesRequestedPerSecond =
		trafficRateSnapshot.readBytesRequestedPerSecond;

	result.writeBytesRequestedPerSecond =
		trafficRateSnapshot.writeBytesRequestedPerSecond;

	return result;
}

std::string Memory::GetTrafficStatsString() const
{
	bool connected = false;

	{
		std::lock_guard<std::mutex> lock(handleMutex);

		connected =
			(vHandle != nullptr) &&
			DMA_INITIALIZED;
	}

	if (!connected)
		return "DMA: Disconnected";

	const MemoryTrafficStats stats = GetTrafficStats();

	std::ostringstream oss;

	oss
		<< "DMA: Connected - "
		<< "Reads: "
		<< FormatCount(stats.readOperationsPerSecond)
		<< "/s ("
		<< FormatBytes(stats.readBytesRequestedPerSecond)
		<< "/s)"
		<< " | Writes: "
		<< FormatCount(stats.writeOperationsPerSecond)
		<< "/s ("
		<< FormatBytes(stats.writeBytesRequestedPerSecond)
		<< "/s)";

	return oss.str();
}

void Memory::ResetTrafficStats()
{
	statsReadOperations.store(0, std::memory_order_relaxed);
	statsWriteOperations.store(0, std::memory_order_relaxed);

	statsReadSuccesses.store(0, std::memory_order_relaxed);
	statsWriteSuccesses.store(0, std::memory_order_relaxed);

	statsReadFailures.store(0, std::memory_order_relaxed);
	statsWriteFailures.store(0, std::memory_order_relaxed);

	statsReadRequests.store(0, std::memory_order_relaxed);
	statsWriteRequests.store(0, std::memory_order_relaxed);

	statsReadBytesRequested.store(0, std::memory_order_relaxed);
	statsWriteBytesRequested.store(0, std::memory_order_relaxed);

	statsDirectReadBytesReturned.store(0, std::memory_order_relaxed);

	statsScatterReadBatches.store(0, std::memory_order_relaxed);
	statsScatterWriteBatches.store(0, std::memory_order_relaxed);

	statsScatterReadRequests.store(0, std::memory_order_relaxed);
	statsScatterWriteRequests.store(0, std::memory_order_relaxed);

	statsScatterClearFailures.store(0, std::memory_order_relaxed);

	std::lock_guard<std::mutex> lock(trafficStatsMutex);

	trafficStatsBaselineValid = false;
	trafficStatsBaseline = {};
	trafficRateSnapshot = {};
	trafficStatsLastSample = {};
}

// ------------------------------------------------------------
// Read / Write
// ------------------------------------------------------------

bool Memory::Read(uintptr_t address, void* buffer, size_t size, bool useCache) const
{
	if (!vHandle)
	{
		MemoryLogError("Read failed: vHandle is null");
		return false;
	}

	if (!current_process.PID)
	{
		MemoryLogError("Read failed: process PID is zero");
		return false;
	}

	if (!address)
	{
		//MemoryLogError("Read failed: address is null"); // spams log files allot
		return false;
	}

	if (!buffer)
	{
		MemoryLogError("Read failed: output buffer is null at " + Hex64(address));
		return false;
	}

	if (size == 0)
		return false;

	DWORD readSize = 0;

	const bool ok = VMMDLL_MemReadEx(
		vHandle,
		current_process.PID,
		address,
		static_cast<PBYTE>(buffer),
		size,
		&readSize,
		BuildReadFlags(useCache)
	);

	const bool completeRead = ok && readSize == size;

	RecordDirectRead(
		size,
		readSize,
		completeRead
	);

	return true;
}

bool Memory::Read(uintptr_t address, void* buffer, size_t size, int pid, bool useCache) const
{
	if (!vHandle)
	{
		MemoryLogError("Read(pid) failed: vHandle is null");
		return false;
	}

	if (!pid)
	{
		MemoryLogError("Read(pid) failed: pid is zero");
		return false;
	}

	if (!buffer)
	{
		MemoryLogError("Read(pid) failed: output buffer is null at " + Hex64(address));
		return false;
	}

	if (size == 0)
		return false;

	DWORD readSize = 0;

	const bool ok = VMMDLL_MemReadEx(
		vHandle,
		pid,
		address,
		static_cast<PBYTE>(buffer),
		size,
		&readSize,
		BuildReadFlags(useCache)
	);

	const bool completeRead = ok && readSize == size;

	RecordDirectRead(
		size,
		readSize,
		completeRead
	);

	return true;
}

bool Memory::Write(uintptr_t address, const void* buffer, size_t size) const
{
	if (!vHandle)
	{
		MemoryLogError("Write failed: vHandle is null");
		return false;
	}

	if (!current_process.PID)
	{
		MemoryLogError("Write failed: process PID is zero");
		return false;
	}

	if (!address || !buffer || size == 0)
	{
		MemoryLogError("Write failed: invalid args at " + Hex64(address));
		return false;
	}

	const bool ok = VMMDLL_MemWrite(
		vHandle,
		current_process.PID,
		address,
		reinterpret_cast<PBYTE>(const_cast<void*>(buffer)),
		size
	);

	RecordDirectWrite(size, ok);

	if (!ok)
	{
		MemoryLogError("Write failed at " + Hex64(address));
		return false;
	}

	return true;
}

bool Memory::Write(uintptr_t address, const void* buffer, size_t size, int pid) const
{
	if (!vHandle)
	{
		MemoryLogError("Write(pid) failed: vHandle is null");
		return false;
	}

	if (!pid)
	{
		MemoryLogError("Write(pid) failed: pid is zero");
		return false;
	}

	if (!address || !buffer || size == 0)
	{
		MemoryLogError("Write(pid) failed: invalid args at " + Hex64(address));
		return false;
	}

	const bool ok = VMMDLL_MemWrite(
		vHandle,
		pid,
		address,
		reinterpret_cast<PBYTE>(const_cast<void*>(buffer)),
		size
	);

	RecordDirectWrite(size, ok);

	if (!ok)
	{
		MemoryLogError("Write failed at " + Hex64(address));
		return false;
	}

	return true;
}

bool Memory::WriteBufferEnsure(
	uintptr_t address,
	const void* buffer,
	size_t size,
	bool useCache
) const
{
	if (!address || !buffer || size == 0)
	{
		MemoryLogError("WriteBufferEnsure failed: invalid args");
		return false;
	}

	constexpr int retryCount = 3;
	std::vector<uint8_t> verify(size);

	for (int attempt = 1; attempt <= retryCount; ++attempt)
	{
		if (!Write(address, buffer, size))
		{
			MemoryLogError(
				"WriteBufferEnsure write failed at " + Hex64(address) +
				" attempt=" + std::to_string(attempt)
			);
			continue;
		}

		for (int i = 0; i < 5; ++i)
			std::this_thread::yield();

		std::fill(verify.begin(), verify.end(), 0);

		if (!Read(address, verify.data(), size, useCache))
		{
			MemoryLogError(
				"WriteBufferEnsure readback failed at " + Hex64(address) +
				" attempt=" + std::to_string(attempt)
			);
			continue;
		}

		if (std::memcmp(verify.data(), buffer, size) == 0)
			return true;
	}

	MemoryLogError("WriteBufferEnsure verification failed at " + Hex64(address));
	return false;
}

bool Memory::ReadChain(
	uint64_t baseAddress,
	const std::vector<uint64_t>& offsets,
	uint64_t& out,
	bool useCache
) const
{
	out = 0;

	if (!baseAddress || offsets.empty())
		return false;

	uint64_t current = baseAddress;

	for (size_t i = 0; i < offsets.size(); ++i)
	{
		const uint64_t readAddress = current + offsets[i];

		if (!IsValidPointer(readAddress))
		{
			//MemoryLogError(
			//	"ReadChain invalid read address " + Hex64(readAddress) +
			//	" index=" + std::to_string(i)
			//);
			return false;
		}

		if (!TryRead(readAddress, current, useCache))
		{
			//MemoryLogError(
			//	"ReadChain failed at " + Hex64(readAddress) +
			//	" index=" + std::to_string(i)
			//);
			return false;
		}

		if (i + 1 < offsets.size() && !IsValidPointer(current))
		{
			//MemoryLogError(
			//	"ReadChain invalid pointer result " + Hex64(current) +
			//	" index=" + std::to_string(i)
			//);
			return false;
		}
	}

	out = current;
	return true;
}

// ------------------------------------------------------------
// String helpers
// ------------------------------------------------------------

std::string Memory::readUnityString(uintptr_t address, SIZE_T maxChars, bool useCache)
{
	if (!address)
		return "";

	constexpr SIZE_T HARD_MAX_CHARS = 4096;
	maxChars = std::min(maxChars, HARD_MAX_CHARS);

	int charCount = 0;

	if (!TryRead(address + 0x10, charCount, useCache))
		return "";

	if (charCount <= 0 || static_cast<SIZE_T>(charCount) > maxChars)
		return "";

	const SIZE_T byteCount = static_cast<SIZE_T>(charCount) * sizeof(wchar_t);

	std::vector<wchar_t> wideBuffer(static_cast<size_t>(charCount) + 1, L'\0');

	if (!Read(address + 0x14, wideBuffer.data(), byteCount, useCache))
		return "";

	const int utf8Size = WideCharToMultiByte(
		CP_UTF8,
		0,
		wideBuffer.data(),
		charCount,
		nullptr,
		0,
		nullptr,
		nullptr
	);

	if (utf8Size <= 0)
		return "";

	std::string output(static_cast<size_t>(utf8Size), '\0');

	WideCharToMultiByte(
		CP_UTF8,
		0,
		wideBuffer.data(),
		charCount,
		output.data(),
		utf8Size,
		nullptr,
		nullptr
	);

	return output;
}

std::string Memory::readUTF8String(uint64_t address, SIZE_T size, bool useCache)
{
	if (!address || size == 0)
		return "";

	constexpr SIZE_T MAX_STRING_SIZE = 4096;
	size = std::min(size, MAX_STRING_SIZE);

	std::vector<char> buffer(size + 1, '\0');

	if (!Read(address, buffer.data(), size, useCache))
		return "";

	const auto nullPos = std::find(buffer.begin(), buffer.end(), '\0');
	return std::string(buffer.begin(), nullPos);
}

std::string Memory::readString(uint64_t address, size_t size, bool useCache)
{
	if (!address || size == 0)
		return "";

	constexpr size_t MAX_STRING_SIZE = 4096;
	size = std::min(size, MAX_STRING_SIZE);

	std::vector<char> buffer(size + 1, '\0');

	if (!Read(address, buffer.data(), size, useCache))
		return "";

	const auto nullPos = std::find(buffer.begin(), buffer.end(), '\0');
	return std::string(buffer.begin(), nullPos);
}

std::wstring Memory::readWideString(uintptr_t address, SIZE_T charCount, bool useCache)
{
	if (!address || charCount == 0)
		return L"";

	constexpr SIZE_T MAX_CHARS = 2048;
	charCount = std::min(charCount, MAX_CHARS);

	std::vector<wchar_t> buffer(charCount + 1, L'\0');

	if (!Read(address, buffer.data(), charCount * sizeof(wchar_t), useCache))
		return L"";

	const auto nullPos = std::find(buffer.begin(), buffer.end(), L'\0');
	return std::wstring(buffer.begin(), nullPos);
}

std::string Memory::readUnicodeString(uintptr_t address, SIZE_T charCount, bool useCache)
{
	if (!address || charCount == 0)
		return "";

	constexpr SIZE_T MAX_CHARS = 2048;
	charCount = std::min(charCount, MAX_CHARS);

	std::vector<wchar_t> wideBuffer(charCount + 1, L'\0');

	if (!Read(address, wideBuffer.data(), charCount * sizeof(wchar_t), useCache))
		return "";

	const auto nullPos = std::find(wideBuffer.begin(), wideBuffer.end(), L'\0');
	const int actualChars = static_cast<int>(std::distance(wideBuffer.begin(), nullPos));

	if (actualChars <= 0)
		return "";

	const int utf8Size = WideCharToMultiByte(
		CP_UTF8,
		0,
		wideBuffer.data(),
		actualChars,
		nullptr,
		0,
		nullptr,
		nullptr
	);

	if (utf8Size <= 0)
		return "";

	std::string output(static_cast<size_t>(utf8Size), '\0');

	WideCharToMultiByte(
		CP_UTF8,
		0,
		wideBuffer.data(),
		actualChars,
		output.data(),
		utf8Size,
		nullptr,
		nullptr
	);

	return output;
}

// ------------------------------------------------------------
// Scatter
// ------------------------------------------------------------

VMMDLL_SCATTER_HANDLE Memory::CreateScatterHandle(bool useCache) const
{
	if (!vHandle || !current_process.PID)
	{
		MemoryLogError("CreateScatterHandle failed: invalid handle or PID");
		return nullptr;
	}

	const VMMDLL_SCATTER_HANDLE scatterHandle = VMMDLL_Scatter_Initialize(
		vHandle,
		current_process.PID,
		BuildScatterFlags(useCache)
	);

	if (!scatterHandle)
		MemoryLogError("Failed to create scatter handle");

	return scatterHandle;
}

VMMDLL_SCATTER_HANDLE Memory::CreateScatterHandle(int pid, bool useCache) const
{
	if (!vHandle || !pid)
	{
		MemoryLogError("CreateScatterHandle(pid) failed: invalid handle or PID");
		return nullptr;
	}

	const VMMDLL_SCATTER_HANDLE scatterHandle = VMMDLL_Scatter_Initialize(
		vHandle,
		pid,
		BuildScatterFlags(useCache)
	);

	if (!scatterHandle)
		MemoryLogError("Failed to create scatter handle for pid=" + std::to_string(pid));

	return scatterHandle;
}

void Memory::CloseScatterHandle(VMMDLL_SCATTER_HANDLE handle)
{
	if (!handle)
		return;

	VMMDLL_Scatter_CloseHandle(handle);
}

bool Memory::AddScatterReadRequest(
	VMMDLL_SCATTER_HANDLE handle,
	uint64_t address,
	void* buffer,
	size_t size
)
{
	if (!handle || !address || !buffer || size == 0)
	{
		MemoryLogError("AddScatterReadRequest failed: invalid args");
		return false;
	}

	if (!VMMDLL_Scatter_PrepareEx(handle, address, size, static_cast<PBYTE>(buffer), nullptr))
	{
		MemoryLogError("Failed to prepare scatter read at " + Hex64(address));
		return false;
	}

	RecordScatterReadRequest(size);
	return true;
}

bool Memory::AddScatterWriteRequest(
	VMMDLL_SCATTER_HANDLE handle,
	uint64_t address,
	const void* buffer,
	size_t size
)
{
	if (!handle || !address || !buffer || size == 0)
	{
		MemoryLogError("AddScatterWriteRequest failed: invalid args");
		return false;
	}

	if (!VMMDLL_Scatter_PrepareWrite(
		handle,
		address,
		reinterpret_cast<PBYTE>(const_cast<void*>(buffer)),
		size
	))
	{
		MemoryLogError("Failed to prepare scatter write at " + Hex64(address));
		return false;
	}

	RecordScatterWriteRequest(size);
	return true;
}

bool Memory::ExecuteReadScatter(VMMDLL_SCATTER_HANDLE handle, int pid, bool useCache)
{
	if (!handle)
	{
		MemoryLogError("ExecuteReadScatter failed: handle is null");
		return false;
	}

	if (pid == 0)
		pid = current_process.PID;

	if (!pid)
	{
		MemoryLogError("ExecuteReadScatter failed: pid is zero");
		return false;
	}

	const bool ok = VMMDLL_Scatter_ExecuteRead(handle);

	RecordScatterReadExecute(ok);

	if (!ok)
		MemoryLogError("Failed to execute scatter read");

	const DWORD clearFlags = useCache ? 0 : VMMDLL_FLAG_NOCACHE;

	if (!VMMDLL_Scatter_Clear(handle, pid, clearFlags))
	{
		RecordScatterClearFailure();
		MemoryLogError("Failed to clear scatter read handle");
		return false;
	}

	return ok;
}

bool Memory::ExecuteWriteScatter(VMMDLL_SCATTER_HANDLE handle, int pid, bool useCache)
{
	if (!handle)
	{
		MemoryLogError("ExecuteWriteScatter failed: handle is null");
		return false;
	}

	if (pid == 0)
		pid = current_process.PID;

	if (!pid)
	{
		MemoryLogError("ExecuteWriteScatter failed: pid is zero");
		return false;
	}

	const bool ok = VMMDLL_Scatter_Execute(handle);

	RecordScatterWriteExecute(ok);

	if (!ok)
		MemoryLogError("Failed to execute scatter write");

	const DWORD clearFlags = useCache ? 0 : VMMDLL_FLAG_NOCACHE;

	if (!VMMDLL_Scatter_Clear(handle, pid, clearFlags))
	{
		RecordScatterClearFailure();
		MemoryLogError("Failed to clear scatter write handle");
		return false;
	}

	return ok;
}

// ------------------------------------------------------------
// Misc
// ------------------------------------------------------------

ULONG64 Memory::GET_MonoModuleAddress(char* module_name)
{
	if (!vHandle || !current_process.PID || !module_name)
	{
		MemoryLogError("GET_MonoModuleAddress failed: invalid args");
		return 0;
	}

	PVMMDLL_MAP_MODULEENTRY moduleEntry = nullptr;

	const bool ok = VMMDLL_Map_GetModuleFromNameU(
		vHandle,
		current_process.PID,
		reinterpret_cast<LPSTR>(module_name),
		&moduleEntry,
		VMMDLL_MODULE_FLAG_NORMAL
	);

	if (!ok || !moduleEntry)
		return 0;

	return moduleEntry->vaBase;
}

int Memory::FindSignatureOffset(
	const std::vector<uint8_t>& array,
	const std::vector<uint8_t>& signature,
	const std::string& mask
)
{
	if (array.empty())
		return -1;

	if (signature.empty())
		return -1;

	if (signature.size() > array.size())
		return -1;

	if (!mask.empty() && signature.size() != mask.size())
		return -1;

	for (size_t i = 0; i <= array.size() - signature.size(); ++i)
	{
		bool found = true;

		for (size_t j = 0; j < signature.size(); ++j)
		{
			if (!mask.empty() && mask[j] == '?')
				continue;

			if (array[i + j] != signature[j])
			{
				found = false;
				break;
			}
		}

		if (found)
			return static_cast<int>(i);
	}

	return -1;
}

//stats functions

MemoryConnectionStats Memory::GetConnectionStats() const
{
	MemoryConnectionStats stats{};

	std::lock_guard<std::mutex> lock(handleMutex);

	stats.vmmHandleValid = (vHandle != nullptr);
	stats.dmaInitialized = DMA_INITIALIZED;
	stats.processInitialized = PROCESS_INITIALIZED;

	stats.vmmLibraryLoaded = (modules.VMM != nullptr);
	stats.leechCoreLibraryLoaded = (modules.LEECHCORE != nullptr);
	stats.ftd3xxLibraryLoaded = (modules.FTD3XX != nullptr);

	stats.vmmHandleAddress =
		static_cast<uint64_t>(
			reinterpret_cast<uintptr_t>(vHandle)
			);

	stats.processId = current_process.PID;
	stats.processName = current_process.process_name;

	stats.targetBaseAddress = current_process.base_address;
	stats.targetBaseSize = current_process.base_size;

	if (!vHandle)
		return stats;

	ULONG64 fpgaId = 0;
	ULONG64 deviceId = 0;
	ULONG64 firmwareMajor = 0;
	ULONG64 firmwareMinor = 0;

	const bool gotFpgaId =
		VMMDLL_ConfigGet(vHandle, LC_OPT_FPGA_FPGA_ID, &fpgaId);

	const bool gotDeviceId =
		VMMDLL_ConfigGet(vHandle, LC_OPT_FPGA_DEVICE_ID, &deviceId);

	const bool gotFirmwareMajor =
		VMMDLL_ConfigGet(
			vHandle,
			LC_OPT_FPGA_VERSION_MAJOR,
			&firmwareMajor
		);

	const bool gotFirmwareMinor =
		VMMDLL_ConfigGet(
			vHandle,
			LC_OPT_FPGA_VERSION_MINOR,
			&firmwareMinor
		);

	stats.fpgaInfoAvailable =
		gotFpgaId &&
		gotDeviceId &&
		gotFirmwareMajor &&
		gotFirmwareMinor;

	stats.fpgaId = fpgaId;
	stats.deviceId = deviceId;
	stats.firmwareMajor = firmwareMajor;
	stats.firmwareMinor = firmwareMinor;

	ULONG64 processPartialTicks = 0;
	ULONG64 processTotalTicks = 0;
	ULONG64 readCacheTicks = 0;
	ULONG64 tlbCacheTicks = 0;

	const bool gotPartial =
		VMMDLL_ConfigGet(
			vHandle,
			VMMDLL_OPT_CONFIG_PROCCACHE_TICKS_PARTIAL,
			&processPartialTicks
		);

	const bool gotTotal =
		VMMDLL_ConfigGet(
			vHandle,
			VMMDLL_OPT_CONFIG_PROCCACHE_TICKS_TOTAL,
			&processTotalTicks
		);

	const bool gotReadCache =
		VMMDLL_ConfigGet(
			vHandle,
			VMMDLL_OPT_CONFIG_READCACHE_TICKS,
			&readCacheTicks
		);

	const bool gotTlbCache =
		VMMDLL_ConfigGet(
			vHandle,
			VMMDLL_OPT_CONFIG_TLBCACHE_TICKS,
			&tlbCacheTicks
		);

	stats.cacheInfoAvailable =
		gotPartial &&
		gotTotal &&
		gotReadCache &&
		gotTlbCache;

	stats.processCachePartialTicks = processPartialTicks;
	stats.processCacheTotalTicks = processTotalTicks;
	stats.readCacheTicks = readCacheTicks;
	stats.tlbCacheTicks = tlbCacheTicks;

	return stats;
}

std::string Memory::GetConnectionStatsString() const
{
	const MemoryConnectionStats connection = GetConnectionStats();
	const MemoryTrafficStats traffic = GetTrafficStats();

	std::ostringstream oss;

	oss
		<< "DMA: "
		<< (connection.dmaInitialized ? "Connected" : "Disconnected")
		<< " | VMM Handle: "
		<< (connection.vmmHandleValid
			? Hex64(connection.vmmHandleAddress)
			: "None")

		<< "\nLibraries: "
		<< "vmm=" << (connection.vmmLibraryLoaded ? "OK" : "Missing")
		<< " | leechcore=" << (connection.leechCoreLibraryLoaded ? "OK" : "Missing")
		<< " | FTD3XX=" << (connection.ftd3xxLibraryLoaded ? "OK" : "Missing")

		<< "\nTarget: "
		<< (connection.processName.empty()
			? "<none>"
			: connection.processName)
		<< " | PID: " << connection.processId
		<< " | Base: " << Hex64(connection.targetBaseAddress)
		<< " | Size: " << FormatBytes(
			static_cast<double>(connection.targetBaseSize)
		);

	if (connection.fpgaInfoAvailable)
	{
		oss
			<< "\nFPGA ID: " << Hex64(connection.fpgaId)
			<< " | Device ID: " << Hex64(connection.deviceId)
			<< " | Firmware: "
			<< connection.firmwareMajor
			<< '.'
			<< connection.firmwareMinor;
	}

	if (connection.cacheInfoAvailable)
	{
		oss
			<< "\nCache ticks: "
			<< "ProcPartial=" << connection.processCachePartialTicks
			<< " | ProcTotal=" << connection.processCacheTotalTicks
			<< " | Read=" << connection.readCacheTicks
			<< " | TLB=" << connection.tlbCacheTicks;
	}

	oss << "\n" << BuildTrafficStatsString(traffic);

	return oss.str();
}

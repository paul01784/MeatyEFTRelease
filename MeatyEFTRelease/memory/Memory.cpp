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

	void MemoryLogInfo(const std::string& message)
	{
		LOGS.logInfo("[Memory] " + message);
	}
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
	if (initRunning.exchange(true))
		return;

	dmaThread = std::thread([this]()
		{
			Init();
			initRunning = false;
		});
}

// ------------------------------------------------------------
// Restricted / unsupported routines
// ------------------------------------------------------------

bool Memory::DumpMemoryMap(bool debug)
{
	LPCSTR args[] = { const_cast<LPCSTR>(""), const_cast<LPCSTR>("-device"), const_cast<LPCSTR>("fpga://algo=0"), const_cast<LPCSTR>(""), const_cast<LPCSTR>("") };
	int argc = 3;
	if (debug)
	{
		args[argc++] = const_cast<LPCSTR>("-v");
		args[argc++] = const_cast<LPCSTR>("-printf");
	}

	VMM_HANDLE handle = VMMDLL_Initialize(argc, args);
	if (!handle)
	{
		LOG("[!] Failed to open a VMM Handle\n");
		return false;
	}

	PVMMDLL_MAP_PHYSMEM pPhysMemMap = NULL;
	if (!VMMDLL_Map_GetPhysMem(handle, &pPhysMemMap))
	{
		LOG("[!] Failed to get physical memory map\n");
		VMMDLL_Close(handle);
		return false;
	}

	if (pPhysMemMap->dwVersion != VMMDLL_MAP_PHYSMEM_VERSION)
	{
		LOG("[!] Invalid VMM Map Version\n");
		VMMDLL_MemFree(pPhysMemMap);
		VMMDLL_Close(handle);
		return false;
	}

	if (pPhysMemMap->cMap == 0)
	{
		printf("[!] Failed to get physical memory map\n");
		VMMDLL_MemFree(pPhysMemMap);
		VMMDLL_Close(handle);
		return false;
	}
	//Dump map to file
	std::stringstream sb;
	for (DWORD i = 0; i < pPhysMemMap->cMap; i++)
	{
		sb << std::hex << pPhysMemMap->pMap[i].pa << " " << (pPhysMemMap->pMap[i].pa + pPhysMemMap->pMap[i].cb - 1) << std::endl;
	}

	auto temp_path = std::filesystem::current_path();
	std::ofstream nFile(temp_path.string() + "\\mmap.txt");
	nFile << sb.str();
	nFile.close();

	VMMDLL_MemFree(pPhysMemMap);
	LOG("Successfully dumped memory map to file!\n");
	//Little sleep to make sure it's written to file.
	Sleep(3000);
	VMMDLL_Close(handle);
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
		LOG("[FindSignature] Invalid scan range: start=0x%llX end=0x%llX\n",
			range_start, range_end);
		return 0;
	}

	if (PID == 0)
		PID = current_process.PID;

	// Parse signature
	std::vector<uint8_t> pattern;
	std::vector<bool> mask;

	for (const char* cur = signature; *cur;)
	{
		if (*cur == '?')
		{
			pattern.push_back(0);
			mask.push_back(false);
			cur += (cur[1] == '?') ? 2 : 1;
		}
		else
		{
			pattern.push_back(GetByte(cur));
			mask.push_back(true);
			cur += 2;
		}

		if (*cur == ' ')
			++cur;
	}

	if (pattern.empty())
	{
		LOG("[FindSignature] Parsed pattern is empty\n");
		return 0;
	}

	constexpr size_t CHUNK_SIZE = 0x10000;
	std::vector<uint8_t> buffer(CHUNK_SIZE + pattern.size());

	for (uint64_t addr = range_start; addr < range_end; addr += CHUNK_SIZE)
	{
		size_t bytesToRead = std::min(
			CHUNK_SIZE + pattern.size(),
			static_cast<size_t>(range_end - addr)
		);

		if (!VMMDLL_MemReadEx(
			this->vHandle,
			PID,
			addr,
			buffer.data(),
			bytesToRead,
			0,
			VMMDLL_FLAG_NOCACHE
		))
		{
			LOG("[FindSignature] MemRead failed at 0x%llX (size=0x%zX)\n",
				addr, bytesToRead);
			continue;
		}

		for (size_t i = 0; i <= bytesToRead - pattern.size(); ++i)
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
				uint64_t foundAt = addr + i;
				LOG("[FindSignature] Match found at 0x%llX\n", foundAt);
				return foundAt;
			}
		}
	}

	LOG("[FindSignature] Signature not found in range 0x%llX–0x%llX\n",
		range_start, range_end);

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
	VMMDLL_ConfigSet(vHandle, VMMDLL_OPT_CONFIG_READCACHE_TICKS, 2);
	VMMDLL_ConfigSet(vHandle, VMMDLL_OPT_CONFIG_TLBCACHE_TICKS, 50);
}

// ------------------------------------------------------------
// Init
// ------------------------------------------------------------

bool Memory::Init(bool memMap, bool debug)
{
	std::lock_guard<std::mutex> lock(handleMutex);

	if (DMA_INITIALIZED)
	{
		MemoryLogInfo("DMA already initialized");
		return true;
	}

	std::string process_name;
	process_name = "EscapeFromTarkov.exe";

	if (!DMA_INITIALIZED)
	{
		MemoryLogInfo("inizializing...");
	reinit:




		LPCSTR args[] = { const_cast<LPCSTR>(""), const_cast<LPCSTR>("-device"), const_cast<LPCSTR>("fpga://algo=0"), const_cast<LPCSTR>(""), const_cast<LPCSTR>(""), const_cast<LPCSTR>(""), const_cast<LPCSTR>("") };
		DWORD argc = 3;
		if (debug)
		{
			args[argc++] = const_cast<LPCSTR>("-v");
			args[argc++] = const_cast<LPCSTR>("-printf");
		}

		std::string path = "";
		if (memMap)
		{
			auto temp_path = std::filesystem::current_path();
			path = (temp_path.string() + "\\mmap.txt");
			bool dumped = false;
			if (!std::filesystem::exists(path))
				dumped = this->DumpMemoryMap(debug);
			else
				dumped = true;
			MemoryLogInfo("dumping memory map to file...");
			if (!dumped)
			{
				MemoryLogInfo("[!] ERROR: Could not dump memory map!");
				MemoryLogInfo("Defaulting to no memory map!");
			}
			else
			{
				MemoryLogInfo("Dumped memory map!");

				//Add the memory map to the arguments and increase arg count.
				args[argc++] = const_cast<LPCSTR>("-memmap");
				args[argc++] = const_cast<LPCSTR>(path.c_str());
			}
		}
		this->vHandle = VMMDLL_Initialize(argc, args);
		if (!this->vHandle)
		{
			MemoryLogInfo("[!] Initialization failed! Is the DMA in use or disconnected?");
			VMMDLL_Close(this->vHandle);
			DMA_INITIALIZED = FALSE;
			memoryGlobals::dmaConnected = FALSE;
			return false;
		}

		ULONG64 FPGA_ID = 0, DEVICE_ID = 0;

		VMMDLL_ConfigGet(this->vHandle, LC_OPT_FPGA_FPGA_ID, &FPGA_ID);
		VMMDLL_ConfigGet(this->vHandle, LC_OPT_FPGA_DEVICE_ID, &DEVICE_ID);

		MemoryLogInfo("success!");

		this->setCustomRefreshData();

		if (!this->SetFPGA())
		{
			MemoryLogInfo("[!] Could not set FPGA!");
			VMMDLL_Close(this->vHandle);
			return false;
		}

		DMA_INITIALIZED = TRUE;
		memoryGlobals::dmaConnected = TRUE;
	}
	else
		MemoryLogInfo("DMA already initialized!");

	if (PROCESS_INITIALIZED)
	{
		MemoryLogInfo("Process already initialized!");
		return true;
	}

getPID:

	current_process.PID = GetPidFromName(process_name);
	if (!current_process.PID)
	{
		MemoryLogInfo("[!] Could not get PID from name!");

		Sleep(5000);
		goto getPID;
	}
	current_process.process_name = process_name;

getBase:

	current_process.base_address = GetBaseDaddy("UnityPlayer.dll");
	if (!current_process.base_address)
	{
		MemoryLogInfo("[!] Could not get base address!");
		Sleep(5000);
		goto getBase;
	}
	current_process.base_size = GetBaseSize(process_name);

	if (!current_process.base_size)
	{
		MemoryLogInfo("[!] Could not get base size!\n");
	}

	PROCESS_INITIALIZED = TRUE;
	memoryGlobals::processFound = TRUE;


	//get keyboard
	/*if (!mem.GetKeyboard()->InitKeyboard())
	{
		std::cout << "[KeyManager] Failed - Hotkeys will not work" << std::endl;
		return false;
	}
	else {
		std::cout << "[KeyManager] Setup / Connected" << std::endl;
	}*/

	return true;
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
		MemoryLogError("Read failed: address is null");
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

	reads.fetch_add(1, std::memory_order_relaxed);
	dataSize.fetch_add(readSize, std::memory_order_relaxed);

	if (!ok || readSize != size)
	{
		std::ostringstream oss;
		oss << "Read failed at " << Hex64(address)
			<< " requested=" << size
			<< " read=" << readSize
			<< " useCache=" << std::boolalpha << useCache;

		MemoryLogError(oss.str());
		return false;
	}

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

	if (!address)
	{
		MemoryLogError("Read(pid) failed: address is null");
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

	reads.fetch_add(1, std::memory_order_relaxed);
	dataSize.fetch_add(readSize, std::memory_order_relaxed);

	if (!ok || readSize != size)
	{
		std::ostringstream oss;
		oss << "Read(pid) failed at " << Hex64(address)
			<< " pid=" << pid
			<< " requested=" << size
			<< " read=" << readSize
			<< " useCache=" << std::boolalpha << useCache;

		MemoryLogError(oss.str());
		return false;
	}

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

	if (!ok)
	{
		MemoryLogError("Write failed at " + Hex64(address));
		return false;
	}

	writes.fetch_add(1, std::memory_order_relaxed);
	dataSize.fetch_add(size, std::memory_order_relaxed);

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

	if (!ok)
	{
		MemoryLogError("Write(pid) failed at " + Hex64(address));
		return false;
	}

	writes.fetch_add(1, std::memory_order_relaxed);
	dataSize.fetch_add(size, std::memory_order_relaxed);

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
			MemoryLogError(
				"ReadChain invalid read address " + Hex64(readAddress) +
				" index=" + std::to_string(i)
			);
			return false;
		}

		if (!TryRead(readAddress, current, useCache))
		{
			MemoryLogError(
				"ReadChain failed at " + Hex64(readAddress) +
				" index=" + std::to_string(i)
			);
			return false;
		}

		if (i + 1 < offsets.size() && !IsValidPointer(current))
		{
			MemoryLogError(
				"ReadChain invalid pointer result " + Hex64(current) +
				" index=" + std::to_string(i)
			);
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

	dataSize.fetch_add(size, std::memory_order_relaxed);
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

	if (!ok)
		MemoryLogError("Failed to execute scatter read");

	const DWORD clearFlags = useCache ? 0 : VMMDLL_FLAG_NOCACHE;

	if (!VMMDLL_Scatter_Clear(handle, pid, clearFlags))
	{
		MemoryLogError("Failed to clear scatter read handle");
		return false;
	}

	if (ok)
		reads.fetch_add(1, std::memory_order_relaxed);

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

	if (!ok)
		MemoryLogError("Failed to execute scatter write");

	const DWORD clearFlags = useCache ? 0 : VMMDLL_FLAG_NOCACHE;

	if (!VMMDLL_Scatter_Clear(handle, pid, clearFlags))
	{
		MemoryLogError("Failed to clear scatter write handle");
		return false;
	}

	if (ok)
		writes.fetch_add(1, std::memory_order_relaxed);

	return ok;
}

// ------------------------------------------------------------
// Misc
// ------------------------------------------------------------

bool Memory::quickRefresh()
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



#include "pch.h"
#include "Memory.h"
#include "../app/globals.h"

#include <thread>
#include <iostream>

int Memory::reads = 0;
int Memory::writes = 0;
int Memory::dataSize = 0;

Memory::Memory()
{
	LOG("loading libraries...\n");
	modules.VMM = LoadLibraryA("vmm.dll");
	modules.FTD3XX = LoadLibraryA("FTD3XX.dll");
	modules.LEECHCORE = LoadLibraryA("leechcore.dll");

	if (!modules.VMM || !modules.FTD3XX || !modules.LEECHCORE)
	{
		LOG("vmm: %p\n", modules.VMM);
		LOG("ftd: %p\n", modules.FTD3XX);
		LOG("leech: %p\n", modules.LEECHCORE);
		THROW("[!] Could not load a library\n");
	}

	this->key = std::make_shared<c_keys>();

	LOG("Successfully loaded libraries!\n");
}

Memory::~Memory()
{
	VMMDLL_CloseAll();
	DMA_INITIALIZED = false;
	PROCESS_INITIALIZED = false;
	vHandle = NULL;
	memoryGlobals::dmaConnected = FALSE;
	memoryGlobals::processFound = FALSE;
}

void Memory::doDMAConnect() {
	auto initLambda = [this]() { this->Init(); };
	std::thread dmaConnect(initLambda);
	dmaConnect.detach();
}

bool Memory::DumpMemoryMap(bool debug)
{
	LPCSTR args[] = {const_cast<LPCSTR>(""), const_cast<LPCSTR>("-device"), const_cast<LPCSTR>("fpga://algo=0"), const_cast<LPCSTR>(""), const_cast<LPCSTR>("")};
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

unsigned char abort2[4] = {0x10, 0x00, 0x10, 0x00};

bool Memory::SetFPGA()
{
	ULONG64 qwID = 0, qwVersionMajor = 0, qwVersionMinor = 0;
	if (!VMMDLL_ConfigGet(this->vHandle, LC_OPT_FPGA_FPGA_ID, &qwID) && VMMDLL_ConfigGet(this->vHandle, LC_OPT_FPGA_VERSION_MAJOR, &qwVersionMajor) && VMMDLL_ConfigGet(this->vHandle, LC_OPT_FPGA_VERSION_MINOR, &qwVersionMinor))
	{
		LOG("[!] Failed to lookup FPGA device, Attempting to proceed\n\n");
		return false;
	}

	LOG("[+] VMMDLL_ConfigGet");
	LOG(" ID = %lli", qwID);
	LOG(" VERSION = %lli.%lli\n", qwVersionMajor, qwVersionMinor);

	if ((qwVersionMajor >= 4) && ((qwVersionMajor >= 5) || (qwVersionMinor >= 7)))
	{
		HANDLE handle;
		LC_CONFIG config = {.dwVersion = LC_CONFIG_VERSION, .szDevice = "existing"};
		handle = LcCreate(&config);
		if (!handle)
		{
			LOG("[!] Failed to create FPGA device\n");
			return false;
		}

		LcCommand(handle, LC_CMD_FPGA_CFGREGPCIE_MARKWR | 0x002, 4, reinterpret_cast<PBYTE>(&abort2), NULL, NULL);
		LOG("[-] Register auto cleared\n");
		LcClose(handle);
	}

	return true;
}

void Memory::setCustomRefreshData()
{
	VMMDLL_ConfigSet(this->vHandle, VMMDLL_OPT_CONFIG_PROCCACHE_TICKS_PARTIAL, 200);
	VMMDLL_ConfigSet(this->vHandle, VMMDLL_OPT_CONFIG_PROCCACHE_TICKS_TOTAL, 2000);
	VMMDLL_ConfigSet(this->vHandle, VMMDLL_OPT_CONFIG_READCACHE_TICKS, 2);
	VMMDLL_ConfigSet(this->vHandle, VMMDLL_OPT_CONFIG_TLBCACHE_TICKS, 50);
}

bool Memory::Init(bool memMap, bool debug)
{
	std::string process_name;
	process_name = "EscapeFromTarkov.exe";

	if (!DMA_INITIALIZED)
	{
		LOG("inizializing...\n");
	reinit:

		
		

		LPCSTR args[] = {const_cast<LPCSTR>(""), const_cast<LPCSTR>("-device"), const_cast<LPCSTR>("fpga://algo=0"), const_cast<LPCSTR>(""), const_cast<LPCSTR>(""), const_cast<LPCSTR>(""), const_cast<LPCSTR>("")};
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
			LOG("dumping memory map to file...\n");
			if (!dumped)
			{
				LOG("[!] ERROR: Could not dump memory map!\n");
				LOG("Defaulting to no memory map!\n");
			}
			else
			{
				LOG("Dumped memory map!\n");

				//Add the memory map to the arguments and increase arg count.
				args[argc++] = const_cast<LPCSTR>("-memmap");
				args[argc++] = const_cast<LPCSTR>(path.c_str());
			}
		}
		this->vHandle = VMMDLL_Initialize(argc, args);
		if (!this->vHandle)
		{
			LOG("[!] Initialization failed! Is the DMA in use or disconnected?\n");
			VMMDLL_Close(this->vHandle);
			DMA_INITIALIZED = FALSE;
			memoryGlobals::dmaConnected = FALSE;
			return false;
		}

		ULONG64 FPGA_ID = 0, DEVICE_ID = 0;

		VMMDLL_ConfigGet(this->vHandle, LC_OPT_FPGA_FPGA_ID, &FPGA_ID);
		VMMDLL_ConfigGet(this->vHandle, LC_OPT_FPGA_DEVICE_ID, &DEVICE_ID);

		LOG("FPGA ID: %llu\n", FPGA_ID);
		LOG("DEVICE ID: %llu\n", DEVICE_ID);
		LOG("success!\n");

		this->setCustomRefreshData();

		if (!this->SetFPGA())
		{
			LOG("[!] Could not set FPGA!\n");
			VMMDLL_Close(this->vHandle);
			return false;
		}

		DMA_INITIALIZED = TRUE;
		memoryGlobals::dmaConnected = TRUE;
	}
	else
		LOG("DMA already initialized!\n");

	if (PROCESS_INITIALIZED)
	{
		LOG("Process already initialized!\n");
		return true;
	}

getPID:

	current_process.PID = GetPidFromName(process_name);
	if (!current_process.PID)
	{
		LOG("[!] Could not get PID from name!\n");
		
		Sleep(5000);
		goto getPID;
	}
	current_process.process_name = process_name;
	
getBase:

	current_process.base_address = GetBaseDaddy("UnityPlayer.dll");
	if (!current_process.base_address)
	{
		LOG("[!] Could not get base address!\n");
		Sleep(5000);
		goto getBase;
	}
	current_process.base_size = GetBaseSize(process_name);

	if (!current_process.base_size)
	{
		LOG("[!] Could not get base size!\n");
	}

	LOG("Process information of %s\n", process_name.c_str());
	LOG("PID: %i\n", current_process.PID);
	LOG("Base Address: 0x%llx\n", current_process.base_address);

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

DWORD Memory::GetPidFromName(std::string process_name)
{
	DWORD pid = 0;
	VMMDLL_PidGetFromName(this->vHandle, (LPSTR)process_name.c_str(), &pid);
	return pid;
}

std::vector<int> Memory::GetPidListFromName(std::string name)
{
	PVMMDLL_PROCESS_INFORMATION process_info = NULL;
	DWORD total_processes = 0;
	std::vector<int> list = { };

	if (!VMMDLL_ProcessGetInformationAll(this->vHandle, &process_info, &total_processes))
	{
		LOG("[!] Failed to get process list\n");
		return list;
	}

	for (size_t i = 0; i < total_processes; i++)
	{
		auto process = process_info[i];
		if (strstr(process.szNameLong, name.c_str()))
			list.push_back(process.dwPID);
	}

	return list;
}

std::vector<std::string> Memory::GetModuleList(std::string process_name)
{
	std::vector<std::string> list = { };
	PVMMDLL_MAP_MODULE module_info = NULL;
	if (!VMMDLL_Map_GetModuleU(this->vHandle, current_process.PID, &module_info, VMMDLL_MODULE_FLAG_NORMAL))
	{
		LOG("[!] Failed to get module list\n");
		return list;
	}

	for (size_t i = 0; i < module_info->cMap; i++)
	{
		auto module = module_info->pMap[i];
		list.push_back(module.uszText);
	}

	return list;
}

VMMDLL_PROCESS_INFORMATION Memory::GetProcessInformation()
{
	VMMDLL_PROCESS_INFORMATION info = { };
	SIZE_T process_information = sizeof(VMMDLL_PROCESS_INFORMATION);
	ZeroMemory(&info, sizeof(VMMDLL_PROCESS_INFORMATION));
	info.magic = VMMDLL_PROCESS_INFORMATION_MAGIC;
	info.wVersion = VMMDLL_PROCESS_INFORMATION_VERSION;

	if (!VMMDLL_ProcessGetInformation(this->vHandle, current_process.PID, &info, &process_information))
	{
		LOG("[!] Failed to find process information\n");
		return { };
	}

	LOG("[+] Found process information\n");
	return info;
}

PEB Memory::GetProcessPeb()
{
	auto info = GetProcessInformation();
	if (info.win.vaPEB)
	{
		LOG("[+] Found process PEB ptr at 0x%p\n", info.win.vaPEB);
		return Read<PEB>(info.win.vaPEB);
	}
	LOG("[!] Failed to find the processes PEB\n");
	return { };
}

size_t Memory::GetBaseDaddy(std::string module_name)
{
	std::wstring str(module_name.begin(), module_name.end());

	PVMMDLL_MAP_MODULEENTRY module_info;
	if (!VMMDLL_Map_GetModuleFromNameW(this->vHandle, current_process.PID, const_cast<LPWSTR>(str.c_str()), &module_info, VMMDLL_MODULE_FLAG_NORMAL))
	{
		LOG("[!] Couldn't find Base Address for %s\n", module_name.c_str());
		return 0;
	}

	LOG("[+] Found Base Address for %s at 0x%p\n", module_name.c_str(), module_info->vaBase);
	base = module_info->vaBase;
	baseSize = mem.GetBaseSize(module_name);
	return module_info->vaBase;
}

size_t Memory::GetBaseSize(std::string module_name)
{
	std::wstring str(module_name.begin(), module_name.end());

	PVMMDLL_MAP_MODULEENTRY module_info;
	auto bResult = VMMDLL_Map_GetModuleFromNameW(this->vHandle, current_process.PID, const_cast<LPWSTR>(str.c_str()), &module_info, VMMDLL_MODULE_FLAG_NORMAL);
	if (bResult)
	{
		LOG("[+] Found Base Size for %s at 0x%p\n", module_name.c_str(), module_info->cbImageSize);
		return module_info->cbImageSize;
	}
	return 0;
}

uintptr_t Memory::GetExportTableAddress(std::string import, std::string process, std::string module)
{
	PVMMDLL_MAP_EAT eat_map = NULL;
	PVMMDLL_MAP_EATENTRY export_entry = NULL;
	bool result = VMMDLL_Map_GetEATU(mem.vHandle, mem.GetPidFromName(process) /*| VMMDLL_PID_PROCESS_WITH_KERNELMEMORY*/, const_cast<LPSTR>(module.c_str()), &eat_map);
	if (!result)
	{
		LOG("[!] Failed to get Export Table\n");
		return 0;
	}

	if (eat_map->dwVersion != VMMDLL_MAP_EAT_VERSION)
	{
		VMMDLL_MemFree(eat_map);
		eat_map = NULL;
		LOG("[!] Invalid VMM Map Version\n");
		return 0;
	}

	uintptr_t addr = 0;
	for (int i = 0; i < eat_map->cMap; i++)
	{
		export_entry = eat_map->pMap + i;
		if (strcmp(export_entry->uszFunction, import.c_str()) == 0)
		{
			addr = export_entry->vaFunction;
			break;
		}
	}

	VMMDLL_MemFree(eat_map);
	eat_map = NULL;

	return addr;
}

uintptr_t Memory::GetImportTableAddress(std::string import, std::string process, std::string module)
{
	PVMMDLL_MAP_IAT iat_map = NULL;
	PVMMDLL_MAP_IATENTRY import_entry = NULL;
	bool result = VMMDLL_Map_GetIATU(mem.vHandle, mem.GetPidFromName(process) /*| VMMDLL_PID_PROCESS_WITH_KERNELMEMORY*/, const_cast<LPSTR>(module.c_str()), &iat_map);
	if (!result)
	{
		LOG("[!] Failed to get Import Table\n");
		return 0;
	}

	if (iat_map->dwVersion != VMMDLL_MAP_IAT_VERSION)
	{
		VMMDLL_MemFree(iat_map);
		iat_map = NULL;
		LOG("[!] Invalid VMM Map Version\n");
		return 0;
	}

	uintptr_t addr = 0;
	for (int i = 0; i < iat_map->cMap; i++)
	{
		import_entry = iat_map->pMap + i;
		if (strcmp(import_entry->uszFunction, import.c_str()) == 0)
		{
			addr = import_entry->vaFunction;
			break;
		}
	}

	VMMDLL_MemFree(iat_map);
	iat_map = NULL;

	return addr;
}

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

bool Memory::FixCr3()
{
	PVMMDLL_MAP_MODULEENTRY module_entry = NULL;
	bool result = VMMDLL_Map_GetModuleFromNameU(this->vHandle, current_process.PID, const_cast<LPSTR>(current_process.process_name.c_str()), &module_entry, NULL);
	if (result)
	{
		std::cout << "No CR3 Fix needed!" << std::endl;
		return true; //Doesn't need to be patched lol
	}
	if (!VMMDLL_InitializePlugins(this->vHandle))
	{
		LOG("[-] Failed VMMDLL_InitializePlugins call\n");
		return false;
	}

	//have to sleep a little or we try reading the file before the plugin initializes fully
	std::this_thread::sleep_for(std::chrono::milliseconds(500));

	while (true)
	{
		BYTE bytes[4] = {0};
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
			LOG("[+] Patched DTB\n");
			return true;
		}
	}

	LOG("[-] Failed to patch module\n");
	return false;
}

bool Memory::DumpMemory(uintptr_t address, std::string path)
{
	LOG("[!] Memory dumping currently does not rebuild the IAT table, imports will be missing from the dump.\n");
	IMAGE_DOS_HEADER dos { };
	Read(address, &dos, sizeof(IMAGE_DOS_HEADER));

	//Check if memory has a PE 
	if (dos.e_magic != 0x5A4D) //Check if it starts with MZ
	{
		LOG("[-] Invalid PE Header\n");
		return false;
	}

	IMAGE_NT_HEADERS64 nt;
	Read(address + dos.e_lfanew, &nt, sizeof(IMAGE_NT_HEADERS64));

	//Sanity check
	if (nt.Signature != IMAGE_NT_SIGNATURE || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
	{
		LOG("[-] Failed signature check\n");
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
		//Rewrite the file offsets to the virtual addresses
		LOG("[!] Rewriting file offsets at 0x%p size 0x%p\n", sections->VirtualAddress, sections->Misc.VirtualSize);
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
		LOG("[!] Failed creating file: %i\n", GetLastError());
		return false;
	}

	if (!WriteFile(dumped_file, target.get(), static_cast<DWORD>(target_size), NULL, NULL))
	{
		LOG("[!] Failed writing file: %i\n", GetLastError());
		CloseHandle(dumped_file);
		return false;
	}

	LOG("[+] Successfully dumped memory at %s\n", path.c_str());
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
	int PID
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

bool Memory::WriteBufferEnsure(uintptr_t address, const void* buffer, size_t size) const
{
	constexpr int retryCount = 3;
	std::vector<uint8_t> temp(size); // fallback if size > 0x1000

	for (int i = 0; i < retryCount; ++i)
	{
		// Write
		if (!Write(address, const_cast<void*>(buffer), size))
		{
			LOG("[!] Failed to write memory at 0x%p (attempt %d)\n", address, i + 1);
			continue;
		}

		for (int j = 0; j < 5; ++j) std::this_thread::yield();

		// Read back to verify
		temp.clear();
		temp.resize(size);
		if (!Read(address, temp.data(), size))
		{
			LOG("[!] Failed to read back memory at 0x%p (attempt %d)\n", address, i + 1);
			continue;
		}

		if (std::memcmp(temp.data(), buffer, size) == 0)
		{
			// Success
			writes++;
			dataSize += size;
			return true;
		}
	}

	LOG("[!] Memory write verification failed at 0x%p\n", address);
	return false;
}

bool Memory::Write(uintptr_t address, void* buffer, size_t size) const
{
	if (!VMMDLL_MemWrite(this->vHandle, current_process.PID, address, static_cast<PBYTE>(buffer), size))
	{
		LOG("[!] Failed to write Memory at 0x%p\n", address);
		return false;
	}
	writes++;
	dataSize += size;
	return true;
}

bool Memory::Write(uintptr_t address, void* buffer, size_t size, int pid) const
{
	if (!VMMDLL_MemWrite(this->vHandle, pid, address, static_cast<PBYTE>(buffer), size))
	{
		LOG("[!] Failed to write Memory at 0x%p\n", address);
		return false;
	}
	writes++;
	dataSize += size;
	return true;
}

bool Memory::Read(uintptr_t address, void* buffer, size_t size) const
{
	DWORD read_size = 0;
	if (!VMMDLL_MemReadEx(this->vHandle, current_process.PID, address, static_cast<PBYTE>(buffer), size, &read_size, VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_ZEROPAD_ON_FAIL))
	{ 
		LOG("[!] Failed to read Memory at 0x%p\n", address);
		return false;
	}
	reads++;
	dataSize += size;
	return (read_size == size);
}
//VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_NOCACHEPUT | VMMDLL_FLAG_NOPAGING

bool Memory::Read(uintptr_t address, void* buffer, size_t size, int pid) const
{
	DWORD read_size = 0;
	if (!VMMDLL_MemReadEx(this->vHandle, pid, address, static_cast<PBYTE>(buffer), size, &read_size, VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_ZEROPAD_ON_FAIL))
	{
		LOG("[!] Failed to read Memory at 0x%p\n", address);
		return false;
	}
	reads++;
	dataSize += size;
	return (read_size == size);
}


std::string Memory::readUnityString(ULONG addr, SIZE_T cb = 128)
{
	int charCount = this->Read<int>(addr + 0x10);
	if (charCount <= 0 || charCount > 0x1000) // cap in chars
		return "";

	const int byteCount = charCount * 2;

	std::vector<uint8_t> utf16Bytes(static_cast<size_t>(byteCount));

	if (!Read(addr + 0x14, utf16Bytes.data(), byteCount))
		return "";

	// WideCharToMultiByte can convert UTF-16LE; pass explicit length (charCount)
	int u8_size = WideCharToMultiByte(
		CP_UTF8,
		0,
		reinterpret_cast<LPCWCH>(utf16Bytes.data()),
		charCount,          // explicit UTF-16 char count (NOT -1)
		nullptr,
		0,
		nullptr,
		nullptr);

	if (u8_size <= 0)
		return "";

	std::string out(static_cast<size_t>(u8_size), '\0');

	WideCharToMultiByte(
		CP_UTF8,
		0,
		reinterpret_cast<LPCWCH>(utf16Bytes.data()),
		charCount,
		out.data(),
		u8_size,
		nullptr,
		nullptr);

	return out;

}

std::string Memory::readUTF8String(ULONG64 ReadAddress, SIZE_T size)
{
	if (!ReadAddress || size == 0)
		return "";

	// Limit the max size to avoid excessive memory allocation
	constexpr SIZE_T MAX_STRING_SIZE = 4096;
	size = (size > MAX_STRING_SIZE) ? MAX_STRING_SIZE : size;

	// Allocate buffer and initialize with zero
	std::vector<char> buffer(size, 0);

	DWORD read_size = 0;
	if (!VMMDLL_MemReadEx(this->vHandle, current_process.PID, ReadAddress,
		reinterpret_cast<PBYTE>(buffer.data()), size,
		&read_size, VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_NOCACHEPUT | VMMDLL_FLAG_NOPAGING))
	{
		return "";
	}

	reads++;
	dataSize += read_size;

	// Ensure we don't go out of bounds while finding null terminator
	size_t nullIndex = std::find(buffer.begin(), buffer.begin() + read_size, '\0') - buffer.begin();

	// Convert to UTF-8 string
	return std::string(buffer.data(), nullIndex);
}

std::string Memory::readString(uint64_t ReadAddress, size_t size)
{
	if (!ReadAddress || size == 0)
		return "";

	DWORD read_size = 0;
	char buffer[256] = {};

	VMMDLL_MemReadEx(this->vHandle, current_process.PID, ReadAddress,
		reinterpret_cast<PBYTE>(buffer),
		size,
		&read_size,
		VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_NOCACHEPUT | VMMDLL_FLAG_NOPAGING);

	reads++;
	dataSize += size;
	return std::string(buffer);
}



std::wstring Memory::readWideString(uintptr_t address, SIZE_T size)
{
	const auto buffer = std::make_unique<wchar_t[]>(size);
	Read(address, buffer.get(), size * 2);
	return std::wstring(buffer.get());
}



std::string Memory::readUnicodeString(uintptr_t address, SIZE_T size)
{
	wchar_t wcharTemp[164] = { L'\0' };
	auto size_ = size * 2;

	Read(address, reinterpret_cast<PBYTE>(wcharTemp), size_);

	int u8_size = WideCharToMultiByte(CP_UTF8, 0, wcharTemp, -1, nullptr, 0, nullptr, nullptr);
	if (u8_size == 0) {
		// Handle error if needed
		return "";
	}

	std::string u8_conv(u8_size, '\0');
	WideCharToMultiByte(CP_UTF8, 0, wcharTemp, -1, &u8_conv[0], u8_size, nullptr, nullptr);

	return u8_conv;
}

VMMDLL_SCATTER_HANDLE Memory::CreateScatterHandle() const
{
	const VMMDLL_SCATTER_HANDLE ScatterHandle = VMMDLL_Scatter_Initialize(this->vHandle, current_process.PID, VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_ZEROPAD_ON_FAIL | VMMDLL_FLAG_NOPAGING | VMMDLL_FLAG_NOPAGING_IO | VMMDLL_FLAG_NOCACHEPUT | VMMDLL_FLAG_SCATTER_PREPAREEX_NOMEMZERO);
	if (!ScatterHandle)
		LOG("[!] Failed to create scatter handle\n");
	return ScatterHandle;
}

VMMDLL_SCATTER_HANDLE Memory::CreateScatterHandle(int pid) const
{
	const VMMDLL_SCATTER_HANDLE ScatterHandle = VMMDLL_Scatter_Initialize(this->vHandle, pid, VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_ZEROPAD_ON_FAIL | VMMDLL_FLAG_NOPAGING | VMMDLL_FLAG_NOPAGING_IO | VMMDLL_FLAG_NOCACHEPUT | VMMDLL_FLAG_SCATTER_PREPAREEX_NOMEMZERO);
	if (!ScatterHandle)
		LOG("[!] Failed to create scatter handle\n");
	return ScatterHandle;
}

void Memory::CloseScatterHandle(VMMDLL_SCATTER_HANDLE handle)
{
	VMMDLL_Scatter_CloseHandle(handle);
}

bool Memory::quickRefresh() {
	if (!VMMDLL_ConfigSet(this->vHandle, VMMDLL_OPT_REFRESH_ALL, 1)) {
		return false;
	}

	return true;
}

void Memory::AddScatterReadRequest(VMMDLL_SCATTER_HANDLE handle, uint64_t address, void* buffer, size_t size)
{
	if (!VMMDLL_Scatter_PrepareEx(handle, address, size, static_cast<PBYTE>(buffer), NULL))
	{
		LOG("[!] Failed to prepare scatter read at 0x%p\n", address);
	}

	dataSize += size;
}

bool Memory::AddScatterWriteRequest(VMMDLL_SCATTER_HANDLE handle, uint64_t address, void* buffer, size_t size)
{
	if (!VMMDLL_Scatter_PrepareWrite(handle, address, static_cast<PBYTE>(buffer), size))
	{
		LOG("[!] Failed to prepare scatter write at 0x%p\n", address);
		return false;
	}
	else
	{
		//Writes.writeThisRound = true;

		return true;
	}
}

void Memory::ExecuteReadScatter(VMMDLL_SCATTER_HANDLE handle, int pid)
{
	if (pid == 0)
		pid = current_process.PID;

	if (!VMMDLL_Scatter_ExecuteRead(handle))
	{
		//LOG("[-] Failed to Execute Scatter Read\n");
	}
	//Clear after using it
	if (!VMMDLL_Scatter_Clear(handle, pid, VMMDLL_FLAG_NOCACHE))
	{
		LOG("[-] Failed to clear Scatter\n");
	}

	reads++;
}

bool Memory::ExecuteWriteScatter(VMMDLL_SCATTER_HANDLE handle, int pid)
{
	if (pid == 0)
		pid = current_process.PID;

	if (!VMMDLL_Scatter_Execute(handle))
	{
		LOG("[-] Failed to Execute Scatter Read\n");
		return false;
	}
	//Clear after using it
	if (!VMMDLL_Scatter_Clear(handle, pid, VMMDLL_FLAG_NOCACHE))
	{
		LOG("[-] Failed to clear Scatter\n");
		return false;
	}

	writes++;
	return true;

	
}

ULONG64 Memory::GET_MonoModuleAddress(char* module_name)
{
	bool bResult;

	// pointer to a module entry structure for the process
	PVMMDLL_MAP_MODULEENTRY pModuleEntryExplorer;
	// try to get the base address of the process
	bResult = VMMDLL_Map_GetModuleFromNameU(this->vHandle, current_process.PID, (LPSTR)module_name, &pModuleEntryExplorer, NULL);
	if (bResult)
	{
		return pModuleEntryExplorer->vaBase;
	}
	else
		return 0x0;

}

int Memory::FindSignatureOffset(const std::vector<uint8_t>& array, const std::vector<uint8_t>& signature, const std::string& mask = "")
{
	if (array.size() == 0)
		throw std::out_of_range("Array is empty.");
	if (signature.size() == 0)
		throw std::out_of_range("Signature is empty.");
	if (signature.size() > array.size())
		throw std::out_of_range("Signature is larger than array.");
	if (!mask.empty() && signature.size() != mask.size())
		throw std::invalid_argument("Mask Length does not match Signature length!");

	for (size_t i = 0; i <= array.size() - signature.size(); i++)
	{
		bool found = true;
		for (size_t j = 0; j < signature.size(); j++)
		{
			if (!mask.empty() && mask[j] == '?') // Skip on wildcard mask
				continue;

			if (array[i + j] != signature[j]) // Byte comparison
			{
				found = false;
				break;
			}
		}

		if (found)
			return static_cast<int>(i); // Return the index if signature is found
	}

	return -1; // Return -1 if not found
}

auto Memory::IsValidPointer(uintptr_t pointer) -> bool {
	return (pointer && pointer > 0xFFFFFF && pointer < 0x7FFFFFFFFFFF);
}



#include "driver.h"

HANDLE driver::driverH = 0;
uintptr_t driver::currentProcessId = 0;
GUID DummyGuid = { 2 }; //don't matter our var never will be saved

bool driver::checkDriverStatus()
{
	int icheck = 82;
	NTSTATUS status = 0;

	uintptr_t BaseAddr = getBaseAddress(GetCurrentProcessId());
	if (BaseAddr == 0)
	{
		return false;
	}

	int checked = read_raw<int>(GetCurrentProcessId(), (uintptr_t)&icheck, &status);
	if (checked != icheck)
	{
		return false;
	}
	return true;
}

NTSTATUS setSystemEnvironmentPrivilege(BOOLEAN Enable, PBOOLEAN WasEnabled)
{
	if (WasEnabled != nullptr)
		*WasEnabled = FALSE;

	BOOLEAN SeSystemEnvironmentWasEnabled;
	const NTSTATUS Status = RtlAdjustPrivilege(SE_SYSTEM_ENVIRONMENT_PRIVILEGE,
		Enable,
		FALSE,
		&SeSystemEnvironmentWasEnabled);

	if (NT_SUCCESS(Status) && WasEnabled != nullptr)
		*WasEnabled = SeSystemEnvironmentWasEnabled;

	return Status;
}

void driver::sendCommand(MemoryCommand* cmd)
{
	UNICODE_STRING VariableName = RTL_CONSTANT_STRING(VARIABLE_NAME);
	NtSetSystemEnvironmentValueEx(
		&VariableName,
		&DummyGuid,
		cmd,
		sizeof(MemoryCommand),
		ATTRIBUTES);
}


uintptr_t driver::getBaseAddress(uintptr_t pid)
{
	uintptr_t result = 0;
	MemoryCommand cmd = MemoryCommand();
	cmd.operation = baseOperation * 0x289;
	cmd.magic = COMMAND_MAGIC;
	cmd.data[0] = pid;
	cmd.data[1] = (uintptr_t)&result;
	sendCommand(&cmd);
	return result;
}

NTSTATUS driver::copy_memory(
	const uintptr_t	src_process_id,
	const uintptr_t src_address,
	const uintptr_t	dest_process_id,
	const uintptr_t	dest_address,
	const size_t	size)
{
	uintptr_t result = 0;
	MemoryCommand cmd = MemoryCommand();
	cmd.operation = baseOperation * 0x823;
	cmd.magic = COMMAND_MAGIC;
	cmd.data[0] = (uintptr_t)src_process_id;
	cmd.data[1] = (uintptr_t)src_address;
	cmd.data[2] = (uintptr_t)dest_process_id;
	cmd.data[3] = (uintptr_t)dest_address;
	cmd.data[4] = (uintptr_t)size;
	cmd.data[5] = (uintptr_t)&result;
	sendCommand(&cmd);
	return (NTSTATUS)result;
}

uintptr_t getKernelModuleExport(uintptr_t kernel_module_base, char* function_name)
{
	if (!kernel_module_base)
		return 0;

	IMAGE_DOS_HEADER dos_header = { 0 };
	IMAGE_NT_HEADERS64 nt_headers = { 0 };

	driver::read_memory(4, kernel_module_base, (uintptr_t)&dos_header, sizeof(dos_header));

	if (dos_header.e_magic != IMAGE_DOS_SIGNATURE)
		return 0;

	driver::read_memory(4, kernel_module_base + dos_header.e_lfanew, (uintptr_t)&nt_headers, sizeof(nt_headers));

	if (nt_headers.Signature != IMAGE_NT_SIGNATURE)
		return 0;

	const auto export_base = nt_headers.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
	const auto export_base_size = nt_headers.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;

	if (!export_base || !export_base_size)
		return 0;

	const auto export_data = reinterpret_cast<PIMAGE_EXPORT_DIRECTORY>(VirtualAlloc(nullptr, export_base_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));

	driver::read_memory(4, kernel_module_base + export_base, (uintptr_t)export_data, export_base_size);

	const auto delta = reinterpret_cast<uintptr_t>(export_data) - export_base;

	const auto name_table = reinterpret_cast<UINT32*>(export_data->AddressOfNames + delta);
	const auto ordinal_table = reinterpret_cast<UINT16*>(export_data->AddressOfNameOrdinals + delta);
	const auto function_table = reinterpret_cast<UINT32*>(export_data->AddressOfFunctions + delta);

	for (auto i = 0u; i < export_data->NumberOfNames; ++i)
	{
		char* current_function_name = (char*)(name_table[i] + delta);

		if (!_stricmp(current_function_name, function_name))
		{
			const auto function_ordinal = ordinal_table[i];
			const auto function_address = kernel_module_base + function_table[function_ordinal];

			if (function_address >= kernel_module_base + export_base && function_address <= kernel_module_base + export_base + export_base_size)
			{
				VirtualFree(export_data, 0, MEM_RELEASE);
				return 0; // No forwarded exports on 64bit?
			}

			VirtualFree(export_data, 0, MEM_RELEASE);
			return function_address;
		}
	}

	VirtualFree(export_data, 0, MEM_RELEASE);
	return 0;
}

uintptr_t getKernelModuleAddress(char* module_name)
{
	void* buffer = nullptr;
	DWORD buffer_size = 0;

	NTSTATUS status = NtQuerySystemInformation(static_cast<SYSTEM_INFORMATION_CLASS>(SystemModuleInformation), buffer, buffer_size, &buffer_size);

	while (status == STATUS_INFO_LENGTH_MISMATCH)
	{
		VirtualFree(buffer, 0, MEM_RELEASE);

		buffer = VirtualAlloc(nullptr, buffer_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (buffer == 0)
			return 0;
		status = NtQuerySystemInformation(static_cast<SYSTEM_INFORMATION_CLASS>(SystemModuleInformation), buffer, buffer_size, &buffer_size);
	}

	if (!NT_SUCCESS(status))
	{
		VirtualFree(buffer, 0, MEM_RELEASE);
		return 0;
	}

	const PRTL_PROCESS_MODULES modules = (PRTL_PROCESS_MODULES)buffer;
	if (modules == nullptr)
	{
		VirtualFree(buffer, 0, MEM_RELEASE);
		return 0;
	}
	for (auto i = 0u; i < modules->NumberOfModules; ++i)
	{
		char* current_module_name = (char*)(modules->Modules[i].FullPathName + modules->Modules[i].OffsetToFileName);

		if (!_stricmp(current_module_name, module_name))
		{
			const uintptr_t result = (uintptr_t)(modules->Modules[i].ImageBase);

			VirtualFree(buffer, 0, MEM_RELEASE);
			return result;
		}
	}

	VirtualFree(buffer, 0, MEM_RELEASE);
	return 0;
}

bool driver::init() 
{
	currentProcessId = GetCurrentProcessId();
	BOOLEAN SeSystemEnvironmentWasEnabled;

	NTSTATUS status = setSystemEnvironmentPrivilege(true, &SeSystemEnvironmentWasEnabled);

	if (!NT_SUCCESS(status))
		return false;

	BYTE nstosname[] = { 'n','t','o','s','k','r','n','l','.','e','x','e',0 };
	uintptr_t kernelModuleAddress = getKernelModuleAddress((char*)nstosname);
	memset(nstosname, 0, sizeof(nstosname));

	BYTE pbid[] = { 'P','s','L','o','o','k','u','p','P','r','o','c','e','s','s','B','y','P','r','o','c','e','s','s','I','d',0 };
	BYTE gba[] = { 'P','s','G','e','t','P','r','o','c','e','s','s','S','e','c','t','i','o','n','B','a','s','e','A','d','d','r','e','s','s',0 };
	BYTE mmcp[] = { 'M','m','C','o','p','y','V','i','r','t','u','a','l','M','e','m','o','r','y',0 };
	uintptr_t kernel_PsLookupProcessByProcessId = getKernelModuleExport(kernelModuleAddress, (char*)pbid);
	uintptr_t kernel_PsGetProcessSectionBaseAddress = getKernelModuleExport(kernelModuleAddress, (char*)gba);
	uintptr_t kernel_MmCopyVirtualMemory = getKernelModuleExport(kernelModuleAddress, (char*)mmcp);
	memset(pbid, 0, sizeof(pbid));
	memset(gba, 0, sizeof(gba));
	memset(mmcp, 0, sizeof(mmcp));

	uintptr_t result = 0;
	MemoryCommand cmd = MemoryCommand();
	cmd.operation = baseOperation * 0x612;
	cmd.magic = COMMAND_MAGIC;
	cmd.data[0] = kernel_PsLookupProcessByProcessId;
	cmd.data[1] = kernel_PsGetProcessSectionBaseAddress;
	cmd.data[2] = kernel_MmCopyVirtualMemory;
	cmd.data[3] = (uintptr_t)&result;
	sendCommand(&cmd);
	return result;
}

NTSTATUS driver::read_memory(
	const uintptr_t	process_id,
	const uintptr_t address,
	const uintptr_t buffer,
	const size_t	size)
{
	return copy_memory(process_id, address, currentProcessId, buffer, size);
}

NTSTATUS driver::write_memory(
	const uintptr_t	process_id,
	const uintptr_t address,
	const uintptr_t buffer,
	const size_t	size)
{
	return copy_memory(currentProcessId, buffer, process_id, address, size);
}

bool driver::readT(uintptr_t process_id, uintptr_t address, void* buffer, size_t size)
{
	NTSTATUS status = read_memory(process_id, address, uintptr_t(buffer), size);
	if (status)
		return true;
	return true;
}
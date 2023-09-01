#include <Windows.h>
#include <ShlObj.h>
#include <corecrt_startup.h>
#include "sfse_common/Log.h"
#include "sfse_common/sfse_version.h"
#include "sfse_common/Utilities.h"
#include "sfse_common/SafeWrite.h"
#include "sfse_common/BranchTrampoline.h"

HINSTANCE g_moduleHandle = nullptr;

void SFSE_Initialize();

// api-ms-win-crt-runtime-l1-1-0.dll
typedef int (*__initterm_e)(_PIFV *, _PIFV *);
__initterm_e _initterm_e_Original = nullptr;

typedef char * (*__get_narrow_winmain_command_line)();
__get_narrow_winmain_command_line _get_narrow_winmain_command_line_Original = NULL;

// runs before global initializers
int __initterm_e_Hook(_PIFV * a, _PIFV * b)
{
	// could be used for plugin optional preload

	_MESSAGE("pre global init");

	return _initterm_e_Original(a, b);
}

// runs after global initializers
char * __get_narrow_winmain_command_line_Hook()
{
	// the usual load time

	SFSE_Initialize();

	return _get_narrow_winmain_command_line_Original();
}

void installBaseHooks(void)
{
	DebugLog::openRelative(CSIDL_MYDOCUMENTS, "\\My Games\\" SAVE_FOLDER_NAME "\\SFSE\\Logs\\sfse.txt");

	HANDLE exe = GetModuleHandle(nullptr);

	// fetch functions to hook
	auto * initterm = (__initterm_e *)getIATAddr(exe, "api-ms-win-crt-runtime-l1-1-0.dll", "_initterm_e");
	auto * cmdline = (__get_narrow_winmain_command_line *)getIATAddr(exe, "api-ms-win-crt-runtime-l1-1-0.dll", "_get_narrow_winmain_command_line");

	// hook them
	if(initterm)
	{
		_initterm_e_Original = *initterm;
		safeWrite64(uintptr_t(initterm), u64(__initterm_e_Hook));
	}
	else
	{
		_ERROR("couldn't find _initterm_e");
	}

	if(cmdline)
	{
		_get_narrow_winmain_command_line_Original = *cmdline;
		safeWrite64(uintptr_t(cmdline), u64(__get_narrow_winmain_command_line_Hook));
	}
	else
	{
		_ERROR("couldn't find _get_narrow_winmain_command_line");
	}
}

void WaitForDebugger(void)
{
	while(!IsDebuggerPresent())
	{
		Sleep(10);
	}

	Sleep(1000 * 2);
}

static bool isInit = false;

void SFSE_Initialize(void)
{
	if(isInit) return;
	isInit = true;

#ifndef _DEBUG
	__try {
#endif

		SYSTEMTIME now;
		GetSystemTime(&now);

		_MESSAGE("SFSE runtime: initialize (version = %d.%d.%d %08X %04d-%02d-%02d %02d:%02d:%02d, os = %s)",
			SFSE_VERSION_INTEGER, SFSE_VERSION_INTEGER_MINOR, SFSE_VERSION_INTEGER_BETA, RUNTIME_VERSION,
			now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
			getOSInfoStr().c_str());

		_MESSAGE("imagebase = %016I64X", g_moduleHandle);
		_MESSAGE("reloc mgr imagebase = %016I64X", RelocationManager::s_baseAddr);

#ifdef _DEBUG
		SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);

		WaitForDebugger();
#endif

		if(!g_branchTrampoline.create(1024 * 64))
		{
			_ERROR("couldn't create branch trampoline. this is fatal. skipping remainder of init process.");
			return;
		}

		if(!g_localTrampoline.create(1024 * 64, g_moduleHandle))
		{
			_ERROR("couldn't create codegen buffer. this is fatal. skipping remainder of init process.");
			return;
		}

		FlushInstructionCache(GetCurrentProcess(), NULL, 0);

#ifndef _DEBUG
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		_ERROR("exception thrown during startup");
	}
#endif

	_MESSAGE("init complete");
}

extern "C" {
	void StartSFSE()
	{
		installBaseHooks();
	}
	
	BOOL WINAPI DllMain(HANDLE hDllHandle, DWORD dwReason, LPVOID lpreserved)
	{
		switch(dwReason)
		{
		case DLL_PROCESS_ATTACH:
			g_moduleHandle = (HINSTANCE)hDllHandle;
			break;

		case DLL_PROCESS_DETACH:
			break;
		};

		return TRUE;
	}
}
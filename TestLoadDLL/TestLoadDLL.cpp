// TestLoadDLL.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <Windows.h>
#include <iostream>
#include <tlhelp32.h>	// process snapshot
#include <io.h>
#include <fcntl.h>		// _setmode
#include <functional>

#pragma comment(lib,"Advapi32.lib") // RaisePrivilige()

VOID RaisePrivilige(_In_ LPCWSTR pwszPrivilegeName)
{
	HANDLE hToken = NULL;
	TOKEN_PRIVILEGES tkp = { 0 };
	OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY | TOKEN_READ, &hToken);

	LookupPrivilegeValue(NULL, pwszPrivilegeName, &tkp.Privileges[0].Luid);
	tkp.PrivilegeCount = 1;  // one privilege to set   
	tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, (PTOKEN_PRIVILEGES)NULL, 0);

	CloseHandle(hToken);
}

BOOL LoadRemoteDLL(_In_ DWORD dwPid, _In_ LPCWSTR pwszDllPath)
{
	// elevated Authority...   because OpenProcess(System Process) Faild , and ErrCode = 5 in Windows xp
	RaisePrivilige(SE_DEBUG_NAME);
	RaisePrivilige(SE_SECURITY_NAME);

	HANDLE hProcess = ::OpenProcess(PROCESS_ALL_ACCESS, FALSE, dwPid);
	if (hProcess == NULL)
	{
		std::wcout << L"OpenProcess Faild! Err=" << ::GetLastError() << std::endl;
		return FALSE;
	}


	// Alloc in Target Process
	std::size_t dwDLLSize = (wcslen(pwszDllPath) + 1) * 2;
	LPVOID pAllocMem = VirtualAllocEx(hProcess, NULL, dwDLLSize, MEM_COMMIT, PAGE_READWRITE);
	if (pAllocMem == nullptr)
	{
		return FALSE;
	}


	//
	BOOL bRetCode = WriteProcessMemory(hProcess, (PVOID)pAllocMem, (PVOID)pwszDllPath, dwDLLSize, NULL);
	if (!bRetCode)
	{
		return FALSE;
	}


	//
	HMODULE hmKernel32 = ::GetModuleHandle(TEXT("Kernel32"));
	if (hmKernel32 == NULL)
	{
		return FALSE;
	}


	PTHREAD_START_ROUTINE pfnThreadRrn = reinterpret_cast<PTHREAD_START_ROUTINE>(GetProcAddress(hmKernel32, "LoadLibraryW"));
	if (pfnThreadRrn == NULL)
	{
		return FALSE;
	}


	HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, pfnThreadRrn, (PVOID)pAllocMem, 0, NULL);
	if (hThread == NULL)
	{
		return FALSE;
	}


	// do not wait ....    without unexist other 'taskmgr.exe'
	/*WaitForSingleObject(hThread, INFINITE);
	if (pAllocMem != NULL)
	VirtualFreeEx(hProcess, (PVOID)pAllocMem, 0, MEM_RELEASE);
	if (hThread != NULL)
	CloseHandle(hThread);
	if (hProcess != NULL)
	CloseHandle(hProcess);*/

	return TRUE;
}

template<typename T, typename _fnPtr>
static std::basic_string<T> MakeTextTo(_In_ CONST std::basic_string<T>& wsText, _In_ _fnPtr fnPtr)
{
	std::basic_string<T> tmpText;
	for (auto& itm : wsText)
		tmpText.push_back(static_cast<T>(fnPtr(itm)));

	return tmpText;
}

template<typename T>
static std::basic_string<T> MakeTextToLower(_In_ CONST std::basic_string<T>& wsText)
{
	return MakeTextTo(wsText, tolower);
}

VOID FindProcName_In_ProcessSnapshot(_In_ CONST std::wstring& wsProcName, _In_ std::function<BOOL(DWORD)> ActionPtr)
{
	HANDLE hThSnap32 = NULL;
	PROCESSENTRY32W pe32;

	hThSnap32 = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hThSnap32 == INVALID_HANDLE_VALUE)
		return;

	pe32.dwSize = sizeof(PROCESSENTRY32);
	if (!Process32FirstW(hThSnap32, &pe32))
	{
		::CloseHandle(hThSnap32);
		return;
	}

	do
	{
		if (MakeTextToLower(std::wstring(pe32.szExeFile)) == MakeTextToLower(wsProcName))
		{
			std::wcout << L"Find '" << wsProcName.c_str() << L"' Pid=" << pe32.th32ProcessID << std::endl;
			if (!ActionPtr(pe32.th32ProcessID))
			{
				::CloseHandle(hThSnap32);
				return;
			}
		}

	} while (Process32NextW(hThSnap32, &pe32));
	::CloseHandle(hThSnap32);
}

int main()
{
	WCHAR wszDLLPath[MAX_PATH] = { 0 };
	::GetCurrentDirectoryW(MAX_PATH, wszDLLPath);
	::lstrcatW(wszDLLPath, L"\\");
	::lstrcatW(wszDLLPath, L"HideProcess.dll");

	setlocale(LC_ALL, "");
	_setmode(_fileno(stdout), _O_U8TEXT);
	std::wcout << L"Searching taskmgr.exe ......" << std::endl;
	FindProcName_In_ProcessSnapshot(L"taskmgr.exe", [wszDLLPath](DWORD dwPid)
	{
		if (!LoadRemoteDLL(dwPid, wszDLLPath))
		{
			std::wcout << L"Injector DLL[" << wszDLLPath << L"] to 'taskmgr.exe['" << dwPid << L"] Faild!!! ErrCode=" << ::GetLastError() << std::endl;
			return FALSE;
		}
		return TRUE;
	});

	std::wcout << L"Inector all of 'taskmgr' done..." << std::endl;
	system("pause");
    return 0;
}


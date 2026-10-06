/*
 * QuickDllInject - System Informer Plugin
 *
 * Custom DLL injection engine (remote thread + APC thread),
 * recent-DLL history and browse picker.
 *
 * Authors:
 *     Textic
 *
 */

#include "QuickDllInject.h"
#include <tlhelp32.h>

#define QUICK_PROCESS_ACCESS (PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_LIMITED_INFORMATION | \
    PROCESS_QUERY_INFORMATION | PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | \
    PROCESS_VM_READ | PROCESS_VM_WRITE | SYNCHRONIZE)

static PCWSTR QuickGetFileName(
    _In_ PCWSTR Path
    )
{
    PCWSTR fileName = wcsrchr(Path, L'\\');

    return fileName ? fileName + 1 : Path;
}

/**
 * Resolves a procedure address inside the target process by combining the
 * local module offset with the remote module base (ToolHelp snapshot).
 */
static NTSTATUS QuickResolveRemoteProcedure(
    _In_ HANDLE ProcessId,
    _In_ BOOLEAN TargetWow64,
    _In_ PCWSTR ModuleName,
    _In_ PCSTR ProcedureName,
    _Out_ PVOID *ProcedureAddress
    )
{
    NTSTATUS status = STATUS_DLL_NOT_FOUND;
    HANDLE snapshotHandle = INVALID_HANDLE_VALUE;
    MODULEENTRY32W moduleEntry;
    PVOID remoteBase = NULL;
    HMODULE localModule;
    FARPROC localProcedure;
    ULONG_PTR offset;

    *ProcedureAddress = NULL;

    snapshotHandle = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | (TargetWow64 ? TH32CS_SNAPMODULE32 : 0),
        (DWORD)(ULONG_PTR)ProcessId
        );

    if (snapshotHandle == INVALID_HANDLE_VALUE)
        return PhDosErrorToNtStatus(GetLastError());

    moduleEntry.dwSize = sizeof(MODULEENTRY32W);

    if (Module32FirstW(snapshotHandle, &moduleEntry))
    {
        do
        {
            if (_wcsicmp(moduleEntry.szModule, ModuleName) == 0)
            {
                remoteBase = moduleEntry.modBaseAddr;
                break;
            }
        } while (Module32NextW(snapshotHandle, &moduleEntry));
    }

    CloseHandle(snapshotHandle);

    if (!remoteBase)
        return STATUS_DLL_NOT_FOUND;

    localModule = GetModuleHandleW(ModuleName);

    if (!localModule)
        return PhDosErrorToNtStatus(GetLastError());

    localProcedure = GetProcAddress(localModule, ProcedureName);

    if (!localProcedure)
        return PhDosErrorToNtStatus(GetLastError());

    offset = (ULONG_PTR)localProcedure - (ULONG_PTR)localModule;
    *ProcedureAddress = (PVOID)((ULONG_PTR)remoteBase + offset);

    return STATUS_SUCCESS;
}

static NTSTATUS QuickWaitForThread(
    _In_ HANDLE ThreadHandle,
    _In_ ULONG TimeoutMs,
    _Out_ PULONG ExitCode
    )
{
    DWORD waitResult = WaitForSingleObject(ThreadHandle, TimeoutMs);

    if (waitResult == WAIT_TIMEOUT)
        return STATUS_TIMEOUT;
    if (waitResult != WAIT_OBJECT_0)
        return PhDosErrorToNtStatus(GetLastError());

    if (!GetExitCodeThread(ThreadHandle, ExitCode))
        return PhDosErrorToNtStatus(GetLastError());

    return STATUS_SUCCESS;
}

static NTSTATUS QuickInjectRemoteThread(
    _In_ HANDLE ProcessHandle,
    _In_ HANDLE ProcessId,
    _In_ BOOLEAN TargetWow64,
    _In_ PCWSTR DllPath,
    _In_ ULONG TimeoutMs,
    _In_ BOOLEAN HideThread,
    _Out_ PVOID *ModuleBase
    )
{
    NTSTATUS status;
    PVOID loadLibraryW = NULL;
    PVOID remotePath = NULL;
    SIZE_T pathBytes = (wcslen(DllPath) + 1) * sizeof(WCHAR);
    HANDLE threadHandle = NULL;
    ULONG exitCode = 0;

    *ModuleBase = NULL;

    status = QuickResolveRemoteProcedure(ProcessId, TargetWow64, L"kernel32.dll", "LoadLibraryW", &loadLibraryW);

    if (!NT_SUCCESS(status))
        return status;

    remotePath = VirtualAllocEx(ProcessHandle, NULL, pathBytes, MEM_COMMIT, PAGE_READWRITE);

    if (!remotePath)
        return PhDosErrorToNtStatus(GetLastError());

    if (!WriteProcessMemory(ProcessHandle, remotePath, DllPath, pathBytes, NULL))
    {
        status = PhDosErrorToNtStatus(GetLastError());
        goto CleanupExit;
    }

    status = NtCreateThreadEx(
        &threadHandle,
        THREAD_ALL_ACCESS,
        NULL,
        ProcessHandle,
        (PUSER_THREAD_START_ROUTINE)loadLibraryW,
        remotePath,
        HideThread ? THREAD_CREATE_FLAGS_HIDE_FROM_DEBUGGER : THREAD_CREATE_FLAGS_NONE,
        0,
        0,
        0,
        NULL
        );

    if (!NT_SUCCESS(status))
        goto CleanupExit;

    status = QuickWaitForThread(threadHandle, TimeoutMs, &exitCode);

    if (!NT_SUCCESS(status))
        goto CleanupExit;

    // LoadLibraryW returns the loaded module base; NULL means the DLL
    // entry point failed or the file could not be loaded.
    if (exitCode == 0)
    {
        status = STATUS_DLL_INIT_FAILED;
        goto CleanupExit;
    }

    *ModuleBase = (PVOID)(ULONG_PTR)exitCode;

CleanupExit:
    if (threadHandle)
        CloseHandle(threadHandle);
    if (remotePath)
        VirtualFreeEx(ProcessHandle, remotePath, 0, MEM_RELEASE);

    return status;
}

static NTSTATUS QuickInjectApcThread(
    _In_ HANDLE ProcessHandle,
    _In_ HANDLE ProcessId,
    _In_ BOOLEAN TargetWow64,
    _In_ PCWSTR DllPath,
    _In_ ULONG TimeoutMs,
    _In_ BOOLEAN HideThread,
    _Out_ PVOID *ModuleBase
    )
{
    NTSTATUS status;
    PVOID loadLibraryW = NULL;
    PVOID rtlExitUserThread = NULL;
    PVOID remotePath = NULL;
    SIZE_T pathBytes = (wcslen(DllPath) + 1) * sizeof(WCHAR);
    HANDLE threadHandle = NULL;
    ULONG exitCode = 0;

    *ModuleBase = NULL;

    status = QuickResolveRemoteProcedure(ProcessId, TargetWow64, L"kernel32.dll", "LoadLibraryW", &loadLibraryW);

    if (!NT_SUCCESS(status))
        return status;

    status = QuickResolveRemoteProcedure(ProcessId, TargetWow64, L"ntdll.dll", "RtlExitUserThread", &rtlExitUserThread);

    if (!NT_SUCCESS(status))
        return status;

    remotePath = VirtualAllocEx(ProcessHandle, NULL, pathBytes, MEM_COMMIT, PAGE_READWRITE);

    if (!remotePath)
        return PhDosErrorToNtStatus(GetLastError());

    if (!WriteProcessMemory(ProcessHandle, remotePath, DllPath, pathBytes, NULL))
    {
        status = PhDosErrorToNtStatus(GetLastError());
        goto CleanupExit;
    }

    // Suspended thread parked in RtlExitUserThread; the queued APC runs
    // first once the thread is resumed.
    status = NtCreateThreadEx(
        &threadHandle,
        THREAD_ALL_ACCESS,
        NULL,
        ProcessHandle,
        (PUSER_THREAD_START_ROUTINE)rtlExitUserThread,
        UlongToPtr(0),
        THREAD_CREATE_FLAGS_CREATE_SUSPENDED |
            (HideThread ? THREAD_CREATE_FLAGS_HIDE_FROM_DEBUGGER : THREAD_CREATE_FLAGS_NONE),
        0,
        0,
        0,
        NULL
        );

    if (!NT_SUCCESS(status))
        goto CleanupExit;

    if (!QueueUserAPC((PAPCFUNC)loadLibraryW, threadHandle, (ULONG_PTR)remotePath))
    {
        status = PhDosErrorToNtStatus(GetLastError());
        goto CleanupExit;
    }

    ResumeThread(threadHandle);

    // Note: RtlExitUserThread(0) always exits with code 0, so APC mode
    // cannot verify the load result the way remote-thread mode does.
    status = QuickWaitForThread(threadHandle, TimeoutMs, &exitCode);

    if (NT_SUCCESS(status))
        *ModuleBase = QuickFindModuleBase(ProcessId, DllPath); // may be NULL; resolved by caller if needed

CleanupExit:
    if (threadHandle)
        CloseHandle(threadHandle);
    if (remotePath)
        VirtualFreeEx(ProcessHandle, remotePath, 0, MEM_RELEASE);

    return status;
}

/**
 * Injects a DLL path into the target process using the configured method
 * and timeout. Shows confirm and result UI per settings, maintains the
 * recent-DLL history on success.
 */
BOOLEAN QuickInjectDllPath(
    _In_ HWND OwnerWindow,
    _In_ PPH_PROCESS_ITEM ProcessItem,
    _In_ PCWSTR DllPath
    )
{
    NTSTATUS status;
    HANDLE processHandle = NULL;
    BOOLEAN targetWow64 = FALSE;
    BOOLEAN selfWow64 = FALSE;
    BOOL selfWow64Raw = FALSE;
    ULONG method;
    ULONG timeoutMs;
    BOOLEAN hideThread;
    BOOLEAN eraseHeaders;
    BOOLEAN unlinkPeb;
    PVOID moduleBase = NULL;
    PCWSTR fileName;
    PCWSTR processName;

    if (!ProcessItem || !DllPath || !*DllPath)
        return FALSE;

    fileName = QuickGetFileName(DllPath);
    processName = (ProcessItem->ProcessName && ProcessItem->ProcessName->Buffer) ?
        ProcessItem->ProcessName->Buffer : L"the process";

    if (GetFileAttributesW(DllPath) == INVALID_FILE_ATTRIBUTES)
    {
        PhShowStatus(OwnerWindow, L"Unable to find the DLL file.", STATUS_OBJECT_NAME_NOT_FOUND, 0);
        return FALSE;
    }

    if (PhGetIntegerSetting(SETTING_NAME_CONFIRM_INJECT))
    {
        PPH_STRING message = PhaFormatString(
            L"Process: %s (%lu)\r\nMethod: %s",
            processName,
            (ULONG)(ULONG_PTR)ProcessItem->ProcessId,
            PhGetIntegerSetting(SETTING_NAME_INJECTION_METHOD) == QuickDllInjectMethodApcThread ?
                L"APC thread" : L"Remote thread"
            );

        if (!PhShowConfirmMessage(OwnerWindow, L"inject", fileName, message->Buffer, FALSE))
            return FALSE;
    }

    status = PhOpenProcess(&processHandle, QUICK_PROCESS_ACCESS, ProcessItem->ProcessId);

    if (!NT_SUCCESS(status))
    {
        PhShowStatus(OwnerWindow, L"Unable to open the process.", status, 0);
        return FALSE;
    }

    status = PhGetProcessIsWow64(processHandle, &targetWow64);

    if (!NT_SUCCESS(status))
    {
        NtClose(processHandle);
        PhShowStatus(OwnerWindow, L"Unable to query the process architecture.", status, 0);
        return FALSE;
    }

    IsWow64Process(GetCurrentProcess(), &selfWow64Raw);
    selfWow64 = !!selfWow64Raw;

    if (!!targetWow64 != !!selfWow64)
    {
        NtClose(processHandle);
        PhShowStatus(
            OwnerWindow,
            L"Bitness mismatch between System Informer and the target process.",
            STATUS_IMAGE_MACHINE_TYPE_MISMATCH,
            0
            );
        return FALSE;
    }

    method = PhGetIntegerSetting(SETTING_NAME_INJECTION_METHOD);
    if (method > QuickDllInjectMethodApcThread)
        method = QuickDllInjectMethodRemoteThread;

    timeoutMs = PhGetIntegerSetting(SETTING_NAME_INJECTION_TIMEOUT);
    if (timeoutMs < INJECTION_TIMEOUT_MIN_MS || timeoutMs > INJECTION_TIMEOUT_MAX_MS)
        timeoutMs = DEFAULT_INJECTION_TIMEOUT_MS;

    hideThread = !!PhGetIntegerSetting(SETTING_NAME_HIDE_THREAD);
    eraseHeaders = !!PhGetIntegerSetting(SETTING_NAME_ERASE_HEADERS);
    unlinkPeb = !!PhGetIntegerSetting(SETTING_NAME_UNLINK_PEB);

    if (method == QuickDllInjectMethodApcThread)
    {
        status = QuickInjectApcThread(
            processHandle,
            ProcessItem->ProcessId,
            targetWow64,
            DllPath,
            timeoutMs,
            hideThread,
            &moduleBase
            );
    }
    else
    {
        status = QuickInjectRemoteThread(
            processHandle,
            ProcessItem->ProcessId,
            targetWow64,
            DllPath,
            timeoutMs,
            hideThread,
            &moduleBase
            );
    }

    if (!NT_SUCCESS(status))
    {
        NtClose(processHandle);
        PhShowStatus(OwnerWindow, L"Unable to inject the DLL.", status, 0);
        return FALSE;
    }

    if (eraseHeaders || unlinkPeb)
    {
        if (!moduleBase)
        {
            moduleBase = QuickFindModuleBase(ProcessItem->ProcessId, DllPath);

            if (!moduleBase)
            {
                NtClose(processHandle);
                PhShowStatus(OwnerWindow, L"Unable to locate the injected module.", STATUS_DLL_NOT_FOUND, 0);
                return FALSE;
            }
        }

        if (eraseHeaders)
        {
            status = QuickErasePeHeaders(processHandle, moduleBase);

            if (!NT_SUCCESS(status))
            {
                NtClose(processHandle);
                PhShowStatus(OwnerWindow, L"Unable to erase the PE headers.", status, 0);
                return FALSE;
            }
        }

        if (unlinkPeb)
        {
            status = QuickUnlinkFromPeb(processHandle, moduleBase);

            if (!NT_SUCCESS(status))
            {
                NtClose(processHandle);
                PhShowStatus(OwnerWindow, L"Unable to unlink the module from the PEB.", status, 0);
                return FALSE;
            }
        }
    }

    NtClose(processHandle);

    QuickAddRecentDll(DllPath);

    if (PhGetIntegerSetting(SETTING_NAME_NOTIFY_SUCCESS))
    {
        PPH_STRING text = PhaFormatString(
            L"Injected \"%s\" into %s (%lu).",
            fileName,
            processName,
            (ULONG)(ULONG_PTR)ProcessItem->ProcessId
            );

        PhShowIconNotification(L"Quick DLL Inject", text->Buffer);
    }

    return TRUE;
}

/**
 * Shows the native DLL picker and injects the selected file.
 */
BOOLEAN QuickBrowseAndInject(
    _In_ HWND OwnerWindow,
    _In_ PPH_PROCESS_ITEM ProcessItem
    )
{
    static PH_FILETYPE_FILTER filters[] =
    {
        { L"DLL files (*.dll)", L"*.dll" },
        { L"All files (*.*)", L"*.*" }
    };

    PVOID fileDialog;
    PPH_STRING fileName;
    BOOLEAN result = FALSE;

    if (!ProcessItem)
        return FALSE;

    fileDialog = PhCreateOpenFileDialog();
    PhSetFileDialogOptions(fileDialog, PH_FILEDIALOG_DONTADDTORECENT);
    PhSetFileDialogFilter(fileDialog, filters, RTL_NUMBER_OF(filters));

    if (!PhShowFileDialog(OwnerWindow, fileDialog))
    {
        PhFreeFileDialog(fileDialog);
        return FALSE;
    }

    fileName = PH_AUTO(PhGetFileDialogFileName(fileDialog));
    PhFreeFileDialog(fileDialog);

    if (fileName && fileName->Buffer)
        result = QuickInjectDllPath(OwnerWindow, ProcessItem, fileName->Buffer);

    return result;
}

/**
 * Returns the recent-DLL history as a referenced list of PPH_STRING.
 * Free with QuickFreeRecentDlls().
 */
PPH_LIST QuickGetRecentDlls(
    VOID
    )
{
    PPH_LIST list = PhCreateList(8);
    PPH_STRING setting = PhGetStringSetting(SETTING_NAME_RECENT_DLLS);
    ULONG maxEntries = PhGetIntegerSetting(SETTING_NAME_MAX_HISTORY);
    PCWSTR cursor;
    ULONG count = 0;

    if (maxEntries < HISTORY_MIN_ENTRIES)
        maxEntries = HISTORY_MIN_ENTRIES;
    if (maxEntries > HISTORY_MAX_ENTRIES)
        maxEntries = HISTORY_MAX_ENTRIES;

    if (setting && setting->Buffer)
    {
        cursor = setting->Buffer;

        while (*cursor && count < maxEntries)
        {
            PCWSTR end = wcschr(cursor, HISTORY_DELIMITER);
            SIZE_T lengthChars = end ? (SIZE_T)(end - cursor) : wcslen(cursor);

            if (lengthChars > 0)
            {
                PhAddItemList(list, PhCreateStringEx((PCWCH)cursor, lengthChars * sizeof(WCHAR)));
                count++;
            }

            cursor = end ? end + 1 : cursor + lengthChars;
        }
    }

    if (setting)
        PhDereferenceObject(setting);

    return list;
}

VOID QuickFreeRecentDlls(
    _In_opt_ PPH_LIST RecentDlls
    )
{
    if (!RecentDlls)
        return;

    for (ULONG i = 0; i < RecentDlls->Count; i++)
        PhDereferenceObject(RecentDlls->Items[i]);

    PhDereferenceObject(RecentDlls);
}

VOID QuickAddRecentDll(
    _In_ PCWSTR DllPath
    )
{
    PPH_LIST list = QuickGetRecentDlls();
    ULONG maxEntries = PhGetIntegerSetting(SETTING_NAME_MAX_HISTORY);
    SIZE_T totalChars = 0;
    PPH_STRING joined;
    PWCHAR dest;

    if (maxEntries < HISTORY_MIN_ENTRIES)
        maxEntries = HISTORY_MIN_ENTRIES;
    if (maxEntries > HISTORY_MAX_ENTRIES)
        maxEntries = HISTORY_MAX_ENTRIES;

    // Remove any existing duplicate (case-insensitive), then prepend.
    for (ULONG i = 0; i < list->Count;)
    {
        PPH_STRING existing = (PPH_STRING)list->Items[i];

        if (_wcsicmp(existing->Buffer, DllPath) == 0)
        {
            PhDereferenceObject(existing);
            PhRemoveItemList(list, i);
        }
        else
        {
            i++;
        }
    }

    PhInsertItemList(list, 0, PhCreateString(DllPath));

    while (list->Count > maxEntries)
    {
        PhDereferenceObject(list->Items[list->Count - 1]);
        PhRemoveItemList(list, list->Count - 1);
    }

    for (ULONG i = 0; i < list->Count; i++)
        totalChars += ((PPH_STRING)list->Items[i])->Length / sizeof(WCHAR) + 1; // + delimiter

    joined = PhCreateStringEx(NULL, totalChars * sizeof(WCHAR));
    dest = joined->Buffer;

    for (ULONG i = 0; i < list->Count; i++)
    {
        PPH_STRING entry = (PPH_STRING)list->Items[i];
        SIZE_T chars = entry->Length / sizeof(WCHAR);

        memcpy(dest, entry->Buffer, entry->Length);
        dest += chars;
        *dest++ = HISTORY_DELIMITER;
    }

    if (list->Count > 0)
        dest[-1] = UNICODE_NULL;
    else
        *dest = UNICODE_NULL;

    // Length excludes the null terminator.
    joined->Length = (ULONG)((dest - joined->Buffer - (list->Count > 0 ? 1 : 0)) * sizeof(WCHAR));

    PhSetStringSetting(SETTING_NAME_RECENT_DLLS, joined->Buffer);
    PhDereferenceObject(joined);

    QuickFreeRecentDlls(list);
}

VOID QuickClearRecentDlls(
    VOID
    )
{
    PhSetStringSetting(SETTING_NAME_RECENT_DLLS, L"");
}

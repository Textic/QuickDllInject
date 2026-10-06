/*
 * QuickDllInject - System Informer Plugin
 *
 * Remote-thread and APC-thread injection primitives.
 *
 * Authors:
 *     Textic
 *
 */

#include "QuickDllInject.h"
#include <tlhelp32.h>

/**
 * Resolves a procedure address inside the target process by combining the
 * local module offset with the remote module base (ToolHelp snapshot).
 */
NTSTATUS QuickResolveRemoteProcedure(
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

NTSTATUS QuickWaitForThread(
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

NTSTATUS QuickInjectRemoteThread(
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

    // NOTE: the thread exit code is a 32-bit DWORD, so a 64-bit base
    // above 4 GB would be truncated. Resolve the real base by snapshot.
    *ModuleBase = QuickFindModuleBase(ProcessId, DllPath);

CleanupExit:
    if (threadHandle)
        CloseHandle(threadHandle);
    if (remotePath)
        VirtualFreeEx(ProcessHandle, remotePath, 0, MEM_RELEASE);

    return status;
}

NTSTATUS QuickInjectApcThread(
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

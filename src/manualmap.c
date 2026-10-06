/*
 * QuickDllInject - System Informer Plugin
 *
 * Manual-map orchestration: allocate in target, commit the prepared
 * image (mapimage.c), run the loader stub (mapstub.c) in-target.
 *
 * Authors:
 *     Textic
 *
 */

#include "QuickDllInject.h"

typedef struct _QUICK_MAP_STUB_PARAMS
{
    ULONG_PTR RtlAddFunctionTable; // 0x00, 0 to skip
    ULONG_PTR FunctionTable;       // 0x08
    ULONG_PTR FunctionCount;       // 0x10
    ULONG_PTR TlsCallbacks;        // 0x18, 0 to skip
    ULONG_PTR TlsCount;            // 0x20
    ULONG_PTR DllMain;             // 0x28
    ULONG_PTR DllBase;             // 0x30
} QUICK_MAP_STUB_PARAMS, *PQUICK_MAP_STUB_PARAMS;

/**
 * Maps the DLL into the target without the OS loader, so the module
 * never appears in loader lists.
 *
 * Limitations: imports are resolved in this process, so non-system
 * dependencies must load at the same address in the target (true on the
 * same boot barring collisions); delay-load imports are not processed.
 */
NTSTATUS QuickManualMap(
    _In_ HANDLE ProcessHandle,
    _In_ HANDLE ProcessId,
    _In_ BOOLEAN TargetWow64,
    _In_ PCWSTR DllPath,
    _In_ ULONG TimeoutMs,
    _In_ BOOLEAN HideThread,
    _Out_ PVOID *ModuleBase
    )
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    PBYTE fileData = NULL;
    SIZE_T fileSize = 0;
    ULONG_PTR preferredBase = 0;
    ULONG imageSize = 0;
    PBYTE localImage = NULL;
    PVOID remoteBase = NULL;
    ULONG_PTR delta = 0;
    QUICK_MAP_LAYOUT layout;
    PVOID rtlAddFunctionTable = NULL;
    PVOID exceptionTable = NULL;
    ULONG exceptionCount = 0;
    PVOID tlsCallbacks = NULL;
    ULONG tlsCount = 0;
    PVOID dllMain = NULL;
    PVOID stubRegion = NULL;
    QUICK_MAP_STUB_PARAMS stubParams;
    PUCHAR stubCode = NULL;
    SIZE_T stubSize = 0;
    SIZE_T written = 0;
    DWORD oldProtect = 0;
    HANDLE threadHandle = NULL;
    ULONG exitCode = 0;

    *ModuleBase = NULL;
    memset(&layout, 0, sizeof(layout));

    fileData = QuickReadMapFile(DllPath, &fileSize);

    if (!fileData)
        return STATUS_OBJECT_NAME_NOT_FOUND;

    status = QuickInspectMapFile(fileData, fileSize, &preferredBase, &imageSize);

    if (!NT_SUCCESS(status))
        goto CleanupExit;

    localImage = (PBYTE)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, imageSize);

    if (!localImage)
    {
        status = STATUS_NO_MEMORY;
        goto CleanupExit;
    }

    // Reserve + commit at the preferred base when possible (avoids relocs).
    remoteBase = VirtualAllocEx(
        ProcessHandle,
        (PVOID)preferredBase,
        imageSize,
        MEM_RESERVE | MEM_COMMIT,
        PAGE_READWRITE
        );

    if (!remoteBase)
    {
        remoteBase = VirtualAllocEx(ProcessHandle, NULL, imageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

        if (!remoteBase)
        {
            status = PhDosErrorToNtStatus(GetLastError());
            goto CleanupExit;
        }
    }

    delta = (ULONG_PTR)remoteBase - preferredBase;

    status = QuickBuildMapImage(fileData, fileSize, delta, localImage, imageSize, &layout);

    if (!NT_SUCCESS(status))
        goto CleanupExit;

    if (layout.ExceptionRva != 0 && layout.ExceptionCount != 0)
    {
#if defined(_M_X64) || defined(_M_ARM64)
        status = QuickResolveRemoteProcedure(ProcessId, TargetWow64, L"ntdll.dll", "RtlAddFunctionTable", &rtlAddFunctionTable);

        if (NT_SUCCESS(status))
        {
            exceptionTable = (PVOID)((ULONG_PTR)remoteBase + layout.ExceptionRva);
            exceptionCount = layout.ExceptionCount;
        }
        else
        {
            rtlAddFunctionTable = NULL;
            status = STATUS_SUCCESS; // Non-fatal: continue without unwind registration.
        }
#else
        UNREFERENCED_PARAMETER(ProcessId);
        UNREFERENCED_PARAMETER(TargetWow64);
#endif
    }

    if (layout.TlsCount != 0)
    {
        tlsCallbacks = (PVOID)((ULONG_PTR)remoteBase + layout.TlsArrayRva);
        tlsCount = layout.TlsCount;
    }

    if (layout.EntryPointRva != 0)
        dllMain = (PVOID)((ULONG_PTR)remoteBase + layout.EntryPointRva);

    // Commit the prepared image to the target.
    if (!WriteProcessMemory(ProcessHandle, remoteBase, localImage, imageSize, &written) || written != imageSize)
    {
        status = PhDosErrorToNtStatus(GetLastError());
        goto CleanupExit;
    }

    QuickProtectMapSections(ProcessHandle, remoteBase, fileData, fileSize, imageSize);

    if (!dllMain)
    {
        // Resource-only DLL: nothing to execute.
        *ModuleBase = remoteBase;
        remoteBase = NULL; // committed
        status = STATUS_SUCCESS;
        goto CleanupExit;
    }

    // Loader stub + params in the target (own pages, stub made executable).
    stubCode = QuickGetMapStub(&stubSize);

    if (!stubCode || stubSize == 0)
    {
        status = STATUS_NOT_SUPPORTED;
        goto CleanupExit;
    }

    memset(&stubParams, 0, sizeof(stubParams));
    stubParams.RtlAddFunctionTable = (ULONG_PTR)rtlAddFunctionTable;
    stubParams.FunctionTable = (ULONG_PTR)exceptionTable;
    stubParams.FunctionCount = exceptionCount;
    stubParams.TlsCallbacks = (ULONG_PTR)tlsCallbacks;
    stubParams.TlsCount = tlsCount;
    stubParams.DllMain = (ULONG_PTR)dllMain;
    stubParams.DllBase = (ULONG_PTR)remoteBase;

    stubRegion = VirtualAllocEx(ProcessHandle, NULL, 0x2000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

    if (!stubRegion)
    {
        status = PhDosErrorToNtStatus(GetLastError());
        goto CleanupExit;
    }

    if (!WriteProcessMemory(ProcessHandle, stubRegion, &stubParams, sizeof(stubParams), &written) ||
        written != sizeof(stubParams))
    {
        status = PhDosErrorToNtStatus(GetLastError());
        goto CleanupExit;
    }

    if (!WriteProcessMemory(ProcessHandle, (PBYTE)stubRegion + 0x1000, stubCode, stubSize, &written) ||
        written != stubSize)
    {
        status = PhDosErrorToNtStatus(GetLastError());
        goto CleanupExit;
    }

    if (!VirtualProtectEx(ProcessHandle, (PBYTE)stubRegion + 0x1000, 0x1000, PAGE_EXECUTE_READ, &oldProtect))
    {
        status = PhDosErrorToNtStatus(GetLastError());
        goto CleanupExit;
    }

    status = NtCreateThreadEx(
        &threadHandle,
        THREAD_ALL_ACCESS,
        NULL,
        ProcessHandle,
        (PUSER_THREAD_START_ROUTINE)((PBYTE)stubRegion + 0x1000),
        stubRegion,
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

    if (exitCode == 0)
    {
        status = STATUS_DLL_INIT_FAILED;
        goto CleanupExit;
    }

    *ModuleBase = remoteBase;
    remoteBase = NULL; // committed
    status = STATUS_SUCCESS;

CleanupExit:
    if (threadHandle)
        CloseHandle(threadHandle);
    if (stubRegion)
        VirtualFreeEx(ProcessHandle, stubRegion, 0, MEM_RELEASE);
    if (remoteBase)
        VirtualFreeEx(ProcessHandle, remoteBase, 0, MEM_RELEASE);
    if (localImage)
        HeapFree(GetProcessHeap(), 0, localImage);
    if (fileData)
        HeapFree(GetProcessHeap(), 0, fileData);

    return status;
}

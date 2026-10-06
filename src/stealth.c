/*
 * QuickDllInject - System Informer Plugin
 *
 * Post-injection stealth steps: PE header erasure and PEB module hiding.
 *
 * Authors:
 *     Textic
 *
 */

#include "QuickDllInject.h"
#include <tlhelp32.h>

#define QUICK_PEB_WALK_LIMIT 8192

/**
 * Finds the base address of an already-loaded module in the target process
 * by matching the file path (falling back to the file name).
 */
PVOID QuickFindModuleBase(
    _In_ HANDLE ProcessId,
    _In_ PCWSTR DllPath
    )
{
    HANDLE snapshotHandle = INVALID_HANDLE_VALUE;
    MODULEENTRY32W moduleEntry;
    PVOID moduleBase = NULL;
    PCWSTR wantedFileName = QuickGetFileName(DllPath);

    snapshotHandle = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
        (DWORD)(ULONG_PTR)ProcessId
        );

    if (snapshotHandle == INVALID_HANDLE_VALUE)
        return NULL;

    moduleEntry.dwSize = sizeof(MODULEENTRY32W);

    if (Module32FirstW(snapshotHandle, &moduleEntry))
    {
        do
        {
            if (_wcsicmp(moduleEntry.szExePath, DllPath) == 0)
            {
                moduleBase = moduleEntry.modBaseAddr;
                break;
            }
        } while (Module32NextW(snapshotHandle, &moduleEntry));

        // Fall back to file-name comparison (path spelling may differ,
        // e.g. short vs. long names or device vs. drive paths).
        if (!moduleBase)
        {
            moduleEntry.dwSize = sizeof(MODULEENTRY32W);

            if (Module32FirstW(snapshotHandle, &moduleEntry))
            {
                do
                {
                    if (_wcsicmp(moduleEntry.szModule, wantedFileName) == 0)
                    {
                        moduleBase = moduleEntry.modBaseAddr;
                        break;
                    }
                } while (Module32NextW(snapshotHandle, &moduleEntry));
            }
        }
    }

    CloseHandle(snapshotHandle);

    return moduleBase;
}

/**
 * Overwrites the PE headers (DOS + NT, SizeOfHeaders bytes) of the loaded
 * module with zeroes. Hinders basic user-mode memory scanners and dumpers.
 *
 * Note: this also disables GetProcAddress against the module inside the
 * target, and a second erase is a no-op reported as success.
 */
NTSTATUS QuickErasePeHeaders(
    _In_ HANDLE ProcessHandle,
    _In_ PVOID ModuleBase
    )
{
    IMAGE_DOS_HEADER dosHeader;
    ULONG ntOffset;
    ULONG signature;
    ULONG sizeOfHeaders = 0;
    SIZE_T bytesRead;
#if defined(_M_X64) || defined(_M_ARM64)
    IMAGE_NT_HEADERS64 ntHeaders;
#else
    IMAGE_NT_HEADERS32 ntHeaders;
#endif

    if (!ReadProcessMemory(ProcessHandle, ModuleBase, &dosHeader, sizeof(dosHeader), &bytesRead) ||
        bytesRead != sizeof(dosHeader))
    {
        return PhDosErrorToNtStatus(GetLastError());
    }

    if (dosHeader.e_magic != IMAGE_DOS_SIGNATURE)
    {
        // Headers already erased (or never a valid image): nothing to do.
        return STATUS_SUCCESS;
    }

    ntOffset = (ULONG)dosHeader.e_lfanew;

    if (ntOffset >= 0x1000)
        return STATUS_INVALID_IMAGE_FORMAT;

    if (!ReadProcessMemory(ProcessHandle, (PBYTE)ModuleBase + ntOffset, &ntHeaders, sizeof(ntHeaders), &bytesRead) ||
        bytesRead != sizeof(ntHeaders))
    {
        return PhDosErrorToNtStatus(GetLastError());
    }

    if (ntHeaders.Signature != IMAGE_NT_SIGNATURE)
        return STATUS_INVALID_IMAGE_FORMAT;

#if defined(_M_X64) || defined(_M_ARM64)
    if (ntHeaders.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return STATUS_INVALID_IMAGE_FORMAT;
#else
    if (ntHeaders.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        return STATUS_INVALID_IMAGE_FORMAT;
#endif

    sizeOfHeaders = ntHeaders.OptionalHeader.SizeOfHeaders;

    if (sizeOfHeaders == 0 || sizeOfHeaders > 0x10000)
        return STATUS_INVALID_IMAGE_FORMAT;

    {
        static UCHAR zeroChunk[512];
        SIZE_T remaining = sizeOfHeaders;
        PBYTE dest = (PBYTE)ModuleBase;

        // Chunked write; avoids a large stack buffer.
        while (remaining > 0)
        {
            SIZE_T chunk = remaining > sizeof(zeroChunk) ? sizeof(zeroChunk) : remaining;
            SIZE_T written = 0;

            if (!WriteProcessMemory(ProcessHandle, dest, zeroChunk, chunk, &written) || written != chunk)
                return PhDosErrorToNtStatus(GetLastError());

            dest += chunk;
            remaining -= chunk;
        }
    }

    return STATUS_SUCCESS;
}

static NTSTATUS QuickReadRemote(
    _In_ HANDLE ProcessHandle,
    _In_ PVOID RemoteAddress,
    _Out_writes_bytes_(Size) PVOID LocalBuffer,
    _In_ SIZE_T Size
    )
{
    SIZE_T bytesRead = 0;

    if (!ReadProcessMemory(ProcessHandle, RemoteAddress, LocalBuffer, Size, &bytesRead) ||
        bytesRead != Size)
    {
        return PhDosErrorToNtStatus(GetLastError());
    }

    return STATUS_SUCCESS;
}

static NTSTATUS QuickUnlinkListEntry(
    _In_ HANDLE ProcessHandle,
    _In_ PLIST_ENTRY Link
    )
{
    LIST_ENTRY prevEntry;
    LIST_ENTRY nextEntry;
    NTSTATUS status;

    if (!Link->Flink || !Link->Blink)
        return STATUS_SUCCESS; // List not doubly linked; nothing to do.

    status = QuickReadRemote(ProcessHandle, Link->Blink, &prevEntry, sizeof(prevEntry));
    if (!NT_SUCCESS(status))
        return status;

    status = QuickReadRemote(ProcessHandle, Link->Flink, &nextEntry, sizeof(nextEntry));
    if (!NT_SUCCESS(status))
        return status;

    prevEntry.Flink = Link->Flink;
    if (!WriteProcessMemory(ProcessHandle, Link->Blink, &prevEntry, sizeof(prevEntry), NULL))
        return PhDosErrorToNtStatus(GetLastError());

    nextEntry.Blink = Link->Blink;
    if (!WriteProcessMemory(ProcessHandle, Link->Flink, &nextEntry, sizeof(nextEntry), NULL))
        return PhDosErrorToNtStatus(GetLastError());

    return STATUS_SUCCESS;
}

/**
 * Unhooks the module from the target PEB loader lists
 * (load / memory / init order) so it no longer shows up in standard
 * module listings (ToolHelp, EnumProcessModules, ...).
 *
 * Limitations: the loader hash table still references the entry, and the
 * lists are edited without holding the loader lock, so avoid injecting
 * while the target is concurrently loading other DLLs.
 */
NTSTATUS QuickUnlinkFromPeb(
    _In_ HANDLE ProcessHandle,
    _In_ PVOID ModuleBase
    )
{
    NTSTATUS status;
    PROCESS_BASIC_INFORMATION basicInfo;
    ULONG returnLength = 0;
    PEB peb;
    PEB_LDR_DATA ldrData;
    PVOID listHead;
    PVOID current;
    ULONG iterations = 0;

    status = NtQueryInformationProcess(
        ProcessHandle,
        ProcessBasicInformation,
        &basicInfo,
        sizeof(basicInfo),
        &returnLength
        );

    if (!NT_SUCCESS(status))
        return status;

    status = QuickReadRemote(ProcessHandle, basicInfo.PebBaseAddress, &peb, sizeof(peb));
    if (!NT_SUCCESS(status))
        return status;

    if (!peb.Ldr)
        return STATUS_DLL_NOT_FOUND;

    status = QuickReadRemote(ProcessHandle, peb.Ldr, &ldrData, sizeof(ldrData));
    if (!NT_SUCCESS(status))
        return status;

    listHead = (PBYTE)peb.Ldr + FIELD_OFFSET(PEB_LDR_DATA, InLoadOrderModuleList);
    current = ldrData.InLoadOrderModuleList.Flink;

    while (current && current != listHead && iterations++ < QUICK_PEB_WALK_LIMIT)
    {
        LDR_DATA_TABLE_ENTRY entry;

        status = QuickReadRemote(ProcessHandle, current, &entry, sizeof(entry));
        if (!NT_SUCCESS(status))
            return status;

        if (entry.DllBase == ModuleBase)
        {
            // NOTE: entry was already read from the target; only its
            // Flink/Blink *values* (remote addresses) are used below.
            LIST_ENTRY loadOrder = entry.InLoadOrderLinks;
            LIST_ENTRY memoryOrder = entry.InMemoryOrderLinks;
            LIST_ENTRY initOrder = entry.InInitializationOrderLinks;

            status = QuickUnlinkListEntry(ProcessHandle, &loadOrder);
            if (NT_SUCCESS(status))
                status = QuickUnlinkListEntry(ProcessHandle, &memoryOrder);
            if (NT_SUCCESS(status))
                status = QuickUnlinkListEntry(ProcessHandle, &initOrder);

            return status;
        }

        current = entry.InLoadOrderLinks.Flink;
    }

    return STATUS_DLL_NOT_FOUND;
}

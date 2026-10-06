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

static NTSTATUS QuickEraseHeadersRemote(
    _In_ HANDLE ProcessHandle,
    _In_ HANDLE ProcessId,
    _In_ BOOLEAN TargetWow64,
    _In_ PVOID ModuleBase,
    _In_ ULONG SizeOfHeaders,
    _In_ ULONG TimeoutMs
    );

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
 * Strategy: direct VirtualProtectEx + WriteProcessMemory first (fast path).
 * If the memory manager refuses the cross-process copy, falls back to
 * zeroing from inside the target with a tiny stub thread.
 *
 * Note: this also disables GetProcAddress against the module inside the
 * target, and a second erase is a no-op reported as success.
 */
NTSTATUS QuickErasePeHeaders(
    _In_ HANDLE ProcessHandle,
    _In_ HANDLE ProcessId,
    _In_ BOOLEAN TargetWow64,
    _In_ PVOID ModuleBase,
    _In_ ULONG TimeoutMs,
    _Out_ PULONG FailedStep
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

    *FailedStep = 0;

    if (!ReadProcessMemory(ProcessHandle, ModuleBase, &dosHeader, sizeof(dosHeader), &bytesRead) ||
        bytesRead != sizeof(dosHeader))
    {
        *FailedStep = 1;
        return PhDosErrorToNtStatus(GetLastError());
    }

    if (dosHeader.e_magic != IMAGE_DOS_SIGNATURE)
    {
        // Headers already erased (or never a valid image): nothing to do.
        return STATUS_SUCCESS;
    }

    ntOffset = (ULONG)dosHeader.e_lfanew;

    if (ntOffset >= 0x1000)
    {
        *FailedStep = 1;
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    if (!ReadProcessMemory(ProcessHandle, (PBYTE)ModuleBase + ntOffset, &ntHeaders, sizeof(ntHeaders), &bytesRead) ||
        bytesRead != sizeof(ntHeaders))
    {
        *FailedStep = 1;
        return PhDosErrorToNtStatus(GetLastError());
    }

    if (ntHeaders.Signature != IMAGE_NT_SIGNATURE)
    {
        *FailedStep = 1;
        return STATUS_INVALID_IMAGE_FORMAT;
    }

#if defined(_M_X64) || defined(_M_ARM64)
    if (ntHeaders.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        *FailedStep = 1;
        return STATUS_INVALID_IMAGE_FORMAT;
    }
#else
    if (ntHeaders.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
    {
        *FailedStep = 1;
        return STATUS_INVALID_IMAGE_FORMAT;
    }
#endif

    sizeOfHeaders = ntHeaders.OptionalHeader.SizeOfHeaders;

    if (sizeOfHeaders == 0 || sizeOfHeaders > 0x10000)
    {
        *FailedStep = 1;
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    {
        static UCHAR zeroChunk[512];
        SIZE_T remaining = sizeOfHeaders;
        SIZE_T protectSize = (sizeOfHeaders + 0xFFF) & ~(SIZE_T)0xFFF;
        PBYTE dest = (PBYTE)ModuleBase;
        DWORD oldProtect = 0;
        BOOLEAN directOk = FALSE;

        // Loaded header pages are usually read-only; make them writable
        // first (module bases are page-aligned, size is rounded up).
        if (VirtualProtectEx(ProcessHandle, dest, protectSize, PAGE_READWRITE, &oldProtect))
        {
            directOk = TRUE;

            // Chunked write; avoids a large stack buffer.
            while (remaining > 0)
            {
                SIZE_T chunk = remaining > sizeof(zeroChunk) ? sizeof(zeroChunk) : remaining;
                SIZE_T written = 0;

                if (!WriteProcessMemory(ProcessHandle, dest, zeroChunk, chunk, &written) || written != chunk)
                {
                    directOk = FALSE;
                    break;
                }

                dest += chunk;
                remaining -= chunk;
            }

            // Best effort: leave the page as we found it.
            {
                DWORD ignoreProtect = 0;

                VirtualProtectEx(ProcessHandle, (PBYTE)ModuleBase, protectSize, oldProtect, &ignoreProtect);
            }

            if (directOk)
                return STATUS_SUCCESS;

            *FailedStep = 3; // protection changed, but the copy was refused
        }
        else
        {
            *FailedStep = 2; // protection change itself refused
        }

        // The memory manager refused the cross-process path; do it
        // from inside the target instead.
        {
            NTSTATUS remoteStatus = QuickEraseHeadersRemote(
                ProcessHandle, ProcessId, TargetWow64, ModuleBase, sizeOfHeaders, TimeoutMs
                );

            if (!NT_SUCCESS(remoteStatus))
                *FailedStep = 4; // in-target erase failed

            return remoteStatus;
        }
    }
}

#if defined(_M_X64)

// In-target zero stub (x64, params in RCX = {VirtualProtect, Base, Size}):
// VirtualProtect(Base, Size, RW, &old); memset(Base, 0, Size);
// VirtualProtect(Base, Size, old, &tmp); return 1 (0 on failure).
static UCHAR QuickZeroStubX64[] =
{
    0x48, 0x83, 0xEC, 0x38,             // sub rsp,0x38
    0x48, 0x89, 0x4C, 0x24, 0x20,       // mov [rsp+0x20],rcx
    0x48, 0x8B, 0x01,                   // mov rax,[rcx]
    0x48, 0x8B, 0x49, 0x08,             // mov rcx,[rcx+8]
    0x48, 0x8B, 0x54, 0x24, 0x20,       // mov rdx,[rsp+0x20]
    0x48, 0x8B, 0x52, 0x10,             // mov rdx,[rdx+0x10]
    0x41, 0xB8, 0x04, 0x00, 0x00, 0x00, // mov r8d,4
    0x4C, 0x8D, 0x4C, 0x24, 0x28,       // lea r9,[rsp+0x28]
    0xFF, 0xD0,                         // call rax
    0x85, 0xC0,                         // test eax,eax
    0x74, 0x3F,                         // jz fail
    0x48, 0x8B, 0x4C, 0x24, 0x20,       // mov rcx,[rsp+0x20]
    0x48, 0x8B, 0x79, 0x08,             // mov rdi,[rcx+8]
    0x48, 0x8B, 0x49, 0x10,             // mov rcx,[rcx+0x10]
    0x31, 0xC0,                         // xor eax,eax
    0xFC,                               // cld
    0xF3, 0xAA,                         // rep stosb
    0x48, 0x8B, 0x44, 0x24, 0x20,       // mov rax,[rsp+0x20]
    0x48, 0x8B, 0x00,                   // mov rax,[rax]
    0x48, 0x8B, 0x4C, 0x24, 0x20,       // mov rcx,[rsp+0x20]
    0x48, 0x8B, 0x49, 0x08,             // mov rcx,[rcx+8]
    0x48, 0x8B, 0x54, 0x24, 0x20,       // mov rdx,[rsp+0x20]
    0x48, 0x8B, 0x52, 0x10,             // mov rdx,[rdx+0x10]
    0x44, 0x8B, 0x44, 0x24, 0x28,       // mov r8d,[rsp+0x28]
    0x4C, 0x8D, 0x4C, 0x24, 0x20,       // lea r9,[rsp+0x20]
    0xFF, 0xD0,                         // call rax
    0xB8, 0x01, 0x00, 0x00, 0x00,       // mov eax,1
    0xEB, 0x02,                         // jmp done
    // fail:
    0x31, 0xC0,                         // xor eax,eax
    // done:
    0x48, 0x83, 0xC4, 0x38,             // add rsp,0x38
    0xC3                                // ret
};

#elif defined(_M_IX86)

// In-target zero stub (x86 stdcall, params at [esp+4]).
static UCHAR QuickZeroStubX86[] =
{
    0x55,                               // push ebp
    0x8B, 0xEC,                         // mov ebp,esp
    0x53,                               // push ebx
    0x83, 0xEC, 0x04,                   // sub esp,4 (oldProtect)
    0x8B, 0x5D, 0x08,                   // mov ebx,[ebp+8]
    0x8B, 0x03,                         // mov eax,[ebx]
    0x8D, 0x4D, 0xF8,                   // lea ecx,[ebp-8]
    0x51,                               // push ecx
    0x6A, 0x04,                         // push 4
    0xFF, 0x73, 0x10,                   // push [ebx+0x10]
    0xFF, 0x73, 0x08,                   // push [ebx+8]
    0xFF, 0xD0,                         // call eax
    0x85, 0xC0,                         // test eax,eax
    0x74, 0x23,                         // jz fail
    0x8B, 0x7B, 0x08,                   // mov edi,[ebx+8]
    0x8B, 0x4B, 0x10,                   // mov ecx,[ebx+16]
    0x31, 0xC0,                         // xor eax,eax
    0xFC,                               // cld
    0xF3, 0xAA,                         // rep stosb
    0x8D, 0x45, 0xF8,                   // lea eax,[ebp-8]
    0x50,                               // push eax
    0xFF, 0x75, 0xF8,                   // push [ebp-8]
    0xFF, 0x73, 0x10,                   // push [ebx+0x10]
    0xFF, 0x73, 0x08,                   // push [ebx+8]
    0x8B, 0x03,                         // mov eax,[ebx]
    0xFF, 0xD0,                         // call eax
    0xB8, 0x01, 0x00, 0x00, 0x00,       // mov eax,1
    0xEB, 0x04,                         // jmp done
    // fail:
    0x31, 0xC0,                         // xor eax,eax
    0xEB, 0x00,                         // jmp done (next instruction)
    // done:
    0x83, 0xC4, 0x04,                   // add esp,4
    0x5B,                               // pop ebx
    0x5D,                               // pop ebp
    0xC2, 0x04, 0x00                    // ret 4
};

#elif defined(_M_ARM64)

// In-target zero stub (ARM64, params in x0 = {VP@0, Base@8, Size@16}).
static ULONG QuickZeroStubArm64[] =
{
    0xD10083FF, // sub sp, sp, #32
    0xF90003E0, // str x0, [sp]
    0xF9400009, // ldr x9, [x0]
    0xF9400400, // ldr x0, [x0, #8]
    0xF94003E1, // ldr x1, [sp]
    0xF9401021, // ldr x1, [x1, #16]
    0x52800082, // mov w2, #4
    0x910043FF, // add x3, sp, #16
    0xD63F0120, // blr x9
    0x34000280, // cbz w0, fail
    0xF94003E0, // ldr x0, [sp]
    0xF9400400, // ldr x0, [x0, #8]
    0xF94003E1, // ldr x1, [sp]
    0xF9401021, // ldr x1, [x1, #16]
    // mem_loop:
    0x340000A1, // cbz x1, mem_done
    0x3900001F, // strb wzr, [x0]
    0x91000400, // add x0, x0, #1
    0xD1000421, // sub x1, x1, #1
    0x17FFFFFC, // b mem_loop
    // mem_done:
    0xF94003E0, // ldr x0, [sp]
    0xF9400009, // ldr x9, [x0]
    0xF9400400, // ldr x0, [x0, #8]
    0xF94003E1, // ldr x1, [sp]
    0xF9401021, // ldr x1, [x1, #16]
    0xB9001022, // ldr w2, [sp, #16]
    0x910063FF, // add x3, sp, #24
    0xD63F0120, // blr x9
    0xD2800020, // mov x0, #1
    0x14000002, // b done
    // fail:
    0xD2800000, // mov x0, #0
    // done:
    0x910083FF, // add sp, sp, #32
    0xD65F03C0  // ret
};

#endif

typedef struct _QUICK_ZERO_STUB_PARAMS
{
    ULONG_PTR VirtualProtect;
    ULONG_PTR Base;
    ULONG_PTR Size;
} QUICK_ZERO_STUB_PARAMS, *PQUICK_ZERO_STUB_PARAMS;

static NTSTATUS QuickEraseHeadersRemote(
    _In_ HANDLE ProcessHandle,
    _In_ HANDLE ProcessId,
    _In_ BOOLEAN TargetWow64,
    _In_ PVOID ModuleBase,
    _In_ ULONG SizeOfHeaders,
    _In_ ULONG TimeoutMs
    )
{
    NTSTATUS status;
    PVOID virtualProtect = NULL;
    PVOID stubRegion = NULL;
    QUICK_ZERO_STUB_PARAMS stubParams;
    PUCHAR stubCode = NULL;
    SIZE_T stubSize = 0;
    SIZE_T written = 0;
    DWORD oldProtect = 0;
    HANDLE threadHandle = NULL;
    ULONG exitCode = 0;

    status = QuickResolveRemoteProcedure(ProcessId, TargetWow64, L"kernel32.dll", "VirtualProtect", &virtualProtect);

    if (!NT_SUCCESS(status))
        return status;

#if defined(_M_X64)
    stubCode = QuickZeroStubX64;
    stubSize = sizeof(QuickZeroStubX64);
#elif defined(_M_IX86)
    stubCode = QuickZeroStubX86;
    stubSize = sizeof(QuickZeroStubX86);
#elif defined(_M_ARM64)
    stubCode = (PUCHAR)QuickZeroStubArm64;
    stubSize = sizeof(QuickZeroStubArm64);
#else
    return STATUS_NOT_SUPPORTED;
#endif

    memset(&stubParams, 0, sizeof(stubParams));
    stubParams.VirtualProtect = (ULONG_PTR)virtualProtect;
    stubParams.Base = (ULONG_PTR)ModuleBase;
    stubParams.Size = SizeOfHeaders;

    stubRegion = VirtualAllocEx(ProcessHandle, NULL, 0x2000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

    if (!stubRegion)
        return PhDosErrorToNtStatus(GetLastError());

    status = STATUS_UNSUCCESSFUL;

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
        THREAD_CREATE_FLAGS_NONE,
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

    status = exitCode != 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;

CleanupExit:
    if (threadHandle)
        CloseHandle(threadHandle);
    if (stubRegion)
        VirtualFreeEx(ProcessHandle, stubRegion, 0, MEM_RELEASE);

    return status;
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

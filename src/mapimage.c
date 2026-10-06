/*
 * QuickDllInject - System Informer Plugin
 *
 * Local PE image preparation for manual mapping: file loading,
 * validation, headers/sections copy, relocations, import resolution,
 * TLS enumeration and section protections.
 *
 * Authors:
 *     Textic
 *
 */

#include "QuickDllInject.h"

#if defined(_M_X64) || defined(_M_ARM64)
#define QUICK_EXPECTED_MAGIC IMAGE_NT_OPTIONAL_HDR64_MAGIC
typedef IMAGE_NT_HEADERS64 QUICK_NT_HEADERS;
typedef IMAGE_THUNK_DATA64 QUICK_THUNK_DATA, *PQUICK_THUNK_DATA;
typedef IMAGE_TLS_DIRECTORY64 QUICK_TLS_DIRECTORY;
#define QUICK_SNAP_ORDINAL(Value) IMAGE_SNAP_BY_ORDINAL64(Value)
#define QUICK_ORDINAL(Value) IMAGE_ORDINAL64(Value)
#define QUICK_RELOC_TYPE IMAGE_REL_BASED_DIR64
#else
#define QUICK_EXPECTED_MAGIC IMAGE_NT_OPTIONAL_HDR32_MAGIC
typedef IMAGE_NT_HEADERS32 QUICK_NT_HEADERS;
typedef IMAGE_THUNK_DATA32 QUICK_THUNK_DATA, *PQUICK_THUNK_DATA;
typedef IMAGE_TLS_DIRECTORY32 QUICK_TLS_DIRECTORY;
#define QUICK_SNAP_ORDINAL(Value) IMAGE_SNAP_BY_ORDINAL32(Value)
#define QUICK_ORDINAL(Value) IMAGE_ORDINAL32(Value)
#define QUICK_RELOC_TYPE IMAGE_REL_BASED_HIGHLOW
#endif

#define QUICK_MAP_MAX_FILE_BYTES 0x4000000
#define QUICK_MAP_MAX_IMAGE_BYTES 0x4000000
#define QUICK_MAP_MAX_THUNKS 4096

static BOOLEAN MapFileSizeIsValid(
    _In_ PBYTE FileData,
    _In_ SIZE_T FileSize
    )
{
    PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)FileData;

    return FileSize >= sizeof(IMAGE_DOS_HEADER) &&
        dosHeader->e_magic == IMAGE_DOS_SIGNATURE &&
        dosHeader->e_lfanew > 0 &&
        (SIZE_T)dosHeader->e_lfanew + sizeof(QUICK_NT_HEADERS) <= FileSize;
}

static QUICK_NT_HEADERS *QuickMapNtHeaders(
    _In_ PBYTE FileData,
    _In_ SIZE_T FileSize
    )
{
    PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)FileData;

    if (MapFileSizeIsValid(FileData, FileSize))
        return (QUICK_NT_HEADERS *)(FileData + dosHeader->e_lfanew);

    return NULL;
}

/**
 * Reads the DLL file into a heap buffer. Free with HeapFree(GetProcessHeap()).
 */
PBYTE QuickReadMapFile(
    _In_ PCWSTR FilePath,
    _Out_ PSIZE_T FileSize
    )
{
    HANDLE fileHandle = INVALID_HANDLE_VALUE;
    LARGE_INTEGER fileSizeValue;
    PBYTE fileData = NULL;
    DWORD bytesRead = 0;

    *FileSize = 0;

    fileHandle = CreateFileW(FilePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);

    if (fileHandle == INVALID_HANDLE_VALUE)
        return NULL;

    if (!GetFileSizeEx(fileHandle, &fileSizeValue) ||
        fileSizeValue.QuadPart < sizeof(IMAGE_DOS_HEADER) || fileSizeValue.QuadPart > QUICK_MAP_MAX_FILE_BYTES)
    {
        CloseHandle(fileHandle);
        return NULL;
    }

    fileData = (PBYTE)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)fileSizeValue.QuadPart);

    if (!fileData)
    {
        CloseHandle(fileHandle);
        return NULL;
    }

    if (!ReadFile(fileHandle, fileData, (DWORD)fileSizeValue.QuadPart, &bytesRead, NULL) ||
        bytesRead != (DWORD)fileSizeValue.QuadPart)
    {
        HeapFree(GetProcessHeap(), 0, fileData);
        CloseHandle(fileHandle);
        return NULL;
    }

    CloseHandle(fileHandle);

    *FileSize = (SIZE_T)fileSizeValue.QuadPart;
    return fileData;
}

/**
 * Validates the in-memory PE image and extracts the preferred base and
 * image size needed before allocating in the target.
 */
NTSTATUS QuickInspectMapFile(
    _In_ PBYTE FileData,
    _In_ SIZE_T FileSize,
    _Out_ PULONG_PTR PreferredBase,
    _Out_ PULONG ImageSize
    )
{
    QUICK_NT_HEADERS *ntHeaders;

    *PreferredBase = 0;
    *ImageSize = 0;

    ntHeaders = QuickMapNtHeaders(FileData, FileSize);

    if (!ntHeaders)
        return STATUS_INVALID_IMAGE_FORMAT;

    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE)
        return STATUS_INVALID_IMAGE_FORMAT;

#if defined(_M_ARM64)
    if (ntHeaders->FileHeader.Machine != IMAGE_FILE_MACHINE_ARM64 &&
        ntHeaders->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64)
        return STATUS_IMAGE_MACHINE_TYPE_MISMATCH;
#elif defined(_M_X64)
    if (ntHeaders->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64)
        return STATUS_IMAGE_MACHINE_TYPE_MISMATCH;
#else
    if (ntHeaders->FileHeader.Machine != IMAGE_FILE_MACHINE_I386)
        return STATUS_IMAGE_MACHINE_TYPE_MISMATCH;
#endif

    if (ntHeaders->OptionalHeader.Magic != QUICK_EXPECTED_MAGIC)
        return STATUS_INVALID_IMAGE_FORMAT;

    if (!(ntHeaders->FileHeader.Characteristics & IMAGE_FILE_DLL))
        return STATUS_INVALID_IMAGE_FORMAT;

    if (ntHeaders->OptionalHeader.SizeOfImage == 0 ||
        ntHeaders->OptionalHeader.SizeOfImage > QUICK_MAP_MAX_IMAGE_BYTES)
    {
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    *PreferredBase = (ULONG_PTR)ntHeaders->OptionalHeader.ImageBase;
    *ImageSize = ntHeaders->OptionalHeader.SizeOfImage;

    return STATUS_SUCCESS;
}

static NTSTATUS QuickMapCopyHeadersAndSections(
    _In_ PBYTE FileData,
    _In_ SIZE_T FileSize,
    _In_ QUICK_NT_HEADERS *NtHeaders,
    _Out_writes_bytes_(ImageSize) PBYTE LocalImage,
    _In_ ULONG ImageSize
    )
{
    ULONG headersSize = NtHeaders->OptionalHeader.SizeOfHeaders;
    PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(NtHeaders);
    ULONG i;

    if (headersSize > ImageSize)
        headersSize = ImageSize;
    if (headersSize > (ULONG)FileSize)
        return STATUS_INVALID_IMAGE_FORMAT;

    memcpy(LocalImage, FileData, headersSize);

    for (i = 0; i < NtHeaders->FileHeader.NumberOfSections; i++)
    {
        ULONG virtualSize = section[i].Misc.VirtualSize;
        ULONG rawSize = section[i].SizeOfRawData;
        ULONG copySize = rawSize < virtualSize ? rawSize : virtualSize;

        if (section[i].VirtualAddress + copySize > ImageSize ||
            section[i].PointerToRawData + copySize > (ULONG)FileSize)
        {
            return STATUS_INVALID_IMAGE_FORMAT;
        }

        if (copySize > 0)
            memcpy(LocalImage + section[i].VirtualAddress, FileData + section[i].PointerToRawData, copySize);
        // Remainder already zero (HEAP_ZERO_MEMORY).
    }

    return STATUS_SUCCESS;
}

static NTSTATUS QuickMapApplyRelocations(
    _Inout_updates_bytes_(ImageSize) PBYTE LocalImage,
    _In_ ULONG ImageSize,
    _In_ QUICK_NT_HEADERS *NtHeaders,
    _In_ ULONG_PTR Delta
    )
{
    ULONG relocRva = NtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress;
    ULONG relocSize = NtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size;
    PIMAGE_BASE_RELOCATION block = (PIMAGE_BASE_RELOCATION)(LocalImage + relocRva);
    ULONG processed = 0;

    if (relocRva == 0 || relocRva + relocSize > ImageSize)
        return STATUS_INVALID_IMAGE_FORMAT;

    while (processed < relocSize)
    {
        ULONG entryCount;
        PUSHORT entries;
        ULONG e;

        if (block->SizeOfBlock < sizeof(IMAGE_BASE_RELOCATION) || processed + block->SizeOfBlock > relocSize)
            return STATUS_INVALID_IMAGE_FORMAT;

        entryCount = (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(USHORT);
        entries = (PUSHORT)((PBYTE)block + sizeof(IMAGE_BASE_RELOCATION));

        for (e = 0; e < entryCount; e++)
        {
            ULONG type = entries[e] >> 12;
            ULONG offset = entries[e] & 0xFFF;
            ULONG_PTR patchAt = (ULONG_PTR)LocalImage + block->VirtualAddress + offset;

            if (block->VirtualAddress + offset >= ImageSize)
                return STATUS_INVALID_IMAGE_FORMAT;

            if (type == QUICK_RELOC_TYPE)
            {
#if defined(_M_X64) || defined(_M_ARM64)
                *(PULONG_PTR)patchAt += Delta;
#else
                *(PULONG)patchAt += (ULONG)Delta;
#endif
            }
            else if (type != IMAGE_REL_BASED_ABSOLUTE)
            {
                return STATUS_NOT_SUPPORTED;
            }
        }

        processed += block->SizeOfBlock;
        block = (PIMAGE_BASE_RELOCATION)((PBYTE)block + block->SizeOfBlock);
    }

    return STATUS_SUCCESS;
}

static NTSTATUS QuickMapResolveImports(
    _Inout_updates_bytes_(ImageSize) PBYTE LocalImage,
    _In_ ULONG ImageSize,
    _In_ QUICK_NT_HEADERS *NtHeaders
    )
{
    ULONG importRva = NtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    ULONG importSize = NtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size;
    PIMAGE_IMPORT_DESCRIPTOR importDesc;

    if (importRva + importSize > ImageSize)
        return STATUS_INVALID_IMAGE_FORMAT;

    importDesc = (PIMAGE_IMPORT_DESCRIPTOR)(LocalImage + importRva);

    while ((PBYTE)importDesc + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= LocalImage + ImageSize &&
        importDesc->Name != 0)
    {
        PCSTR depName = (PCSTR)(LocalImage + importDesc->Name);
        HMODULE depModule;
        PQUICK_THUNK_DATA origThunk;
        PQUICK_THUNK_DATA firstThunk;
        ULONG thunkGuard = 0;

        if ((PBYTE)depName < LocalImage || (PBYTE)depName >= LocalImage + ImageSize)
            return STATUS_INVALID_IMAGE_FORMAT;

        if (importDesc->FirstThunk == 0 || importDesc->FirstThunk >= ImageSize)
            return STATUS_INVALID_IMAGE_FORMAT;

        depModule = LoadLibraryA(depName);

        if (!depModule)
            return STATUS_DLL_NOT_FOUND;

        origThunk = (PQUICK_THUNK_DATA)(LocalImage +
            (importDesc->OriginalFirstThunk ? importDesc->OriginalFirstThunk : importDesc->FirstThunk));
        firstThunk = (PQUICK_THUNK_DATA)(LocalImage + importDesc->FirstThunk);

        if ((PBYTE)origThunk < LocalImage || (PBYTE)firstThunk < LocalImage ||
            (PBYTE)origThunk >= LocalImage + ImageSize || (PBYTE)firstThunk >= LocalImage + ImageSize)
        {
            return STATUS_INVALID_IMAGE_FORMAT;
        }

        while (origThunk->u1.AddressOfData != 0)
        {
            FARPROC procedure = NULL;

            if (++thunkGuard > QUICK_MAP_MAX_THUNKS)
                return STATUS_INVALID_IMAGE_FORMAT;

            if (QUICK_SNAP_ORDINAL(origThunk->u1.Ordinal))
            {
                procedure = GetProcAddress(depModule, (PCSTR)(ULONG_PTR)QUICK_ORDINAL(origThunk->u1.Ordinal));
            }
            else
            {
                PIMAGE_IMPORT_BY_NAME importByName =
                    (PIMAGE_IMPORT_BY_NAME)(LocalImage + origThunk->u1.AddressOfData);

                if ((PBYTE)importByName < LocalImage ||
                    (PBYTE)importByName + sizeof(IMAGE_IMPORT_BY_NAME) > LocalImage + ImageSize)
                {
                    return STATUS_INVALID_IMAGE_FORMAT;
                }

                procedure = GetProcAddress(depModule, (PCSTR)importByName->Name);
            }

            if (!procedure)
                return STATUS_ENTRYPOINT_NOT_FOUND;

            firstThunk->u1.Function = (ULONG_PTR)procedure;

            origThunk++;
            firstThunk++;
        }

        importDesc++;
    }

    return STATUS_SUCCESS;
}

/**
 * Builds the ready-to-write local image: headers/sections copy,
 * relocations, locally-resolved imports. Collects RVAs the orchestrator
 * converts to remote addresses (TLS array, exception directory).
 */
NTSTATUS QuickBuildMapImage(
    _In_ PBYTE FileData,
    _In_ SIZE_T FileSize,
    _In_ ULONG_PTR Delta,
    _Out_writes_bytes_(ImageSize) PBYTE LocalImage,
    _In_ ULONG ImageSize,
    _Out_ PQUICK_MAP_LAYOUT Layout
    )
{
    NTSTATUS status;
    QUICK_NT_HEADERS *ntHeaders;
    ULONG_PTR preferredBase;

    memset(Layout, 0, sizeof(QUICK_MAP_LAYOUT));

    ntHeaders = QuickMapNtHeaders(FileData, FileSize);

    if (!ntHeaders || ntHeaders->Signature != IMAGE_NT_SIGNATURE)
        return STATUS_INVALID_IMAGE_FORMAT;

    preferredBase = (ULONG_PTR)ntHeaders->OptionalHeader.ImageBase;

    status = QuickMapCopyHeadersAndSections(FileData, FileSize, ntHeaders, LocalImage, ImageSize);
    if (!NT_SUCCESS(status))
        return status;

    if (Delta != 0)
    {
        status = QuickMapApplyRelocations(LocalImage, ImageSize, ntHeaders, Delta);
        if (!NT_SUCCESS(status))
            return status;
    }

    status = QuickMapResolveImports(LocalImage, ImageSize, ntHeaders);
    if (!NT_SUCCESS(status))
        return status;

    // TLS callbacks (enumerated here; executed by the stub in the target).
    {
        ULONG tlsRva = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].VirtualAddress;

        if (tlsRva != 0 && tlsRva + sizeof(ULONG_PTR) * 2 <= ImageSize)
        {
            QUICK_TLS_DIRECTORY *tlsDir = (QUICK_TLS_DIRECTORY *)(LocalImage + tlsRva);
            ULONG_PTR callbacksVa = (ULONG_PTR)tlsDir->AddressOfCallBacks;

            if (callbacksVa != 0)
            {
                ULONG_PTR callbacksRva = callbacksVa - preferredBase;
                PULONG_PTR callbackArray;
                ULONG count = 0;

                if (callbacksRva >= ImageSize)
                    return STATUS_INVALID_IMAGE_FORMAT;

                callbackArray = (PULONG_PTR)(LocalImage + callbacksRva);

                while (count < QUICK_MAP_TLS_MAX_CALLBACKS && callbackArray[count] != 0)
                    count++;

                if (count > 0)
                {
                    Layout->TlsArrayRva = (ULONG)callbacksRva;
                    Layout->TlsCount = count;
                }
            }
        }
    }

    // Exception directory RVA/count (registered in the target for
    // 64-bit unwinding).
    {
        ULONG exRva = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION].VirtualAddress;
        ULONG exSize = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION].Size;

        if (exRva != 0 && exSize != 0 && exRva + exSize <= ImageSize)
        {
            Layout->ExceptionRva = exRva;
            Layout->ExceptionCount = exSize / sizeof(ULONG) / 3; // RUNTIME_FUNCTION = 3 DWORDs
        }
    }

    if (ntHeaders->OptionalHeader.AddressOfEntryPoint != 0)
        Layout->EntryPointRva = ntHeaders->OptionalHeader.AddressOfEntryPoint;

    return STATUS_SUCCESS;
}

/**
 * Applies final per-section memory protections in the target.
 */
VOID QuickProtectMapSections(
    _In_ HANDLE ProcessHandle,
    _In_ PVOID RemoteBase,
    _In_ PBYTE FileData,
    _In_ SIZE_T FileSize,
    _In_ ULONG ImageSize
    )
{
    QUICK_NT_HEADERS *ntHeaders = QuickMapNtHeaders(FileData, FileSize);
    PIMAGE_SECTION_HEADER section;
    ULONG i;

    if (!ntHeaders)
        return;

    section = IMAGE_FIRST_SECTION(ntHeaders);

    for (i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++)
    {
        ULONG characteristics = section[i].Characteristics;
        DWORD newProtect;
        DWORD ignoreProtect = 0;
        BOOLEAN executable = !!(characteristics & IMAGE_SCN_MEM_EXECUTE);
        BOOLEAN readable = !!(characteristics & IMAGE_SCN_MEM_READ);
        BOOLEAN writable = !!(characteristics & IMAGE_SCN_MEM_WRITE);

        if (executable)
            newProtect = writable ? PAGE_EXECUTE_READWRITE : (readable ? PAGE_EXECUTE_READ : PAGE_EXECUTE);
        else
            newProtect = writable ? PAGE_READWRITE : (readable ? PAGE_READONLY : PAGE_READWRITE);

        if (section[i].VirtualAddress < ImageSize && section[i].Misc.VirtualSize > 0)
        {
            VirtualProtectEx(
                ProcessHandle,
                (PBYTE)RemoteBase + section[i].VirtualAddress,
                section[i].Misc.VirtualSize,
                newProtect,
                &ignoreProtect
                );
        }
    }
}

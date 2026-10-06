/*
 * QuickDllInject - System Informer Plugin
 *
 * Adds a direct "Inject DLL..." option to the right-click context menu of processes.
 */

#ifndef _QUICKDLLINJECT_H_
#define _QUICKDLLINJECT_H_

#include <phdk.h>
#include <settings.h>
#include <windowsx.h>
#include "resource.h"

#ifdef _M_IX86
// The Win32 import library is generated from an ordinal-only .def, so it
// only provides undecorated import thunks while the x86 compiler emits
// stdcall-decorated references. Redirect them: imports bind by ordinal
// at load time, so the undecorated thunk is equivalent.
// NOTE: every new Ph* API used by the plugin needs its pair of lines
// here (the @N suffix is reported by LNK2019 when missing).
#pragma comment(linker, "/alternatename:__imp__PhAddSettings@8=__imp__PhAddSettings")
#pragma comment(linker, "/alternatename:_PhAddSettings@8=_PhAddSettings")
#pragma comment(linker, "/alternatename:__imp__PhSetDialogItemText@12=__imp__PhSetDialogItemText")
#pragma comment(linker, "/alternatename:_PhSetDialogItemText@12=_PhSetDialogItemText")
#pragma comment(linker, "/alternatename:__imp__PhWindowThemeControlColor@16=__imp__PhWindowThemeControlColor")
#pragma comment(linker, "/alternatename:_PhWindowThemeControlColor@16=_PhWindowThemeControlColor")
#pragma comment(linker, "/alternatename:__imp__PhGetIntegerStringRefSetting@4=__imp__PhGetIntegerStringRefSetting")
#pragma comment(linker, "/alternatename:_PhGetIntegerStringRefSetting@4=_PhGetIntegerStringRefSetting")
#pragma comment(linker, "/alternatename:__imp__PhSetIntegerStringRefSetting@8=__imp__PhSetIntegerStringRefSetting")
#pragma comment(linker, "/alternatename:_PhSetIntegerStringRefSetting@8=_PhSetIntegerStringRefSetting")
#pragma comment(linker, "/alternatename:__imp__PhGetStringRefSetting@4=__imp__PhGetStringRefSetting")
#pragma comment(linker, "/alternatename:_PhGetStringRefSetting@4=_PhGetStringRefSetting")
#pragma comment(linker, "/alternatename:__imp__PhSetStringRefSetting@8=__imp__PhSetStringRefSetting")
#pragma comment(linker, "/alternatename:_PhSetStringRefSetting@8=_PhSetStringRefSetting")
#pragma comment(linker, "/alternatename:__imp__PhDereferenceObject@4=__imp__PhDereferenceObject")
#pragma comment(linker, "/alternatename:_PhDereferenceObject@4=_PhDereferenceObject")
#pragma comment(linker, "/alternatename:__imp__PhAutoDereferenceObject@4=__imp__PhAutoDereferenceObject")
#pragma comment(linker, "/alternatename:_PhAutoDereferenceObject@4=_PhAutoDereferenceObject")
#pragma comment(linker, "/alternatename:__imp__PhCountStringZ@4=__imp__PhCountStringZ")
#pragma comment(linker, "/alternatename:_PhCountStringZ@4=_PhCountStringZ")
#pragma comment(linker, "/alternatename:__imp__PhCreateStringEx@8=__imp__PhCreateStringEx")
#pragma comment(linker, "/alternatename:_PhCreateStringEx@8=_PhCreateStringEx")
#pragma comment(linker, "/alternatename:__imp__PhFormatString_V@8=__imp__PhFormatString_V")
#pragma comment(linker, "/alternatename:_PhFormatString_V@8=_PhFormatString_V")
#pragma comment(linker, "/alternatename:__imp__PhCreateList@4=__imp__PhCreateList")
#pragma comment(linker, "/alternatename:_PhCreateList@4=_PhCreateList")
#pragma comment(linker, "/alternatename:__imp__PhAddItemList@8=__imp__PhAddItemList")
#pragma comment(linker, "/alternatename:_PhAddItemList@8=_PhAddItemList")
#pragma comment(linker, "/alternatename:__imp__PhInsertItemList@12=__imp__PhInsertItemList")
#pragma comment(linker, "/alternatename:_PhInsertItemList@12=_PhInsertItemList")
#pragma comment(linker, "/alternatename:__imp__PhRemoveItemList@8=__imp__PhRemoveItemList")
#pragma comment(linker, "/alternatename:_PhRemoveItemList@8=_PhRemoveItemList")
#pragma comment(linker, "/alternatename:__imp__PhDosErrorToNtStatus@4=__imp__PhDosErrorToNtStatus")
#pragma comment(linker, "/alternatename:_PhDosErrorToNtStatus@4=_PhDosErrorToNtStatus")
#pragma comment(linker, "/alternatename:__imp__PhOpenProcess@12=__imp__PhOpenProcess")
#pragma comment(linker, "/alternatename:_PhOpenProcess@12=_PhOpenProcess")
#pragma comment(linker, "/alternatename:__imp__PhGetProcessIsWow64@8=__imp__PhGetProcessIsWow64")
#pragma comment(linker, "/alternatename:_PhGetProcessIsWow64@8=_PhGetProcessIsWow64")
#pragma comment(linker, "/alternatename:__imp__PhShowStatus@16=__imp__PhShowStatus")
#pragma comment(linker, "/alternatename:_PhShowStatus@16=_PhShowStatus")
#pragma comment(linker, "/alternatename:__imp__PhShowConfirmMessage@20=__imp__PhShowConfirmMessage")
#pragma comment(linker, "/alternatename:_PhShowConfirmMessage@20=_PhShowConfirmMessage")
#pragma comment(linker, "/alternatename:__imp__PhCreateOpenFileDialog@0=__imp__PhCreateOpenFileDialog")
#pragma comment(linker, "/alternatename:_PhCreateOpenFileDialog@0=_PhCreateOpenFileDialog")
#pragma comment(linker, "/alternatename:__imp__PhFreeFileDialog@4=__imp__PhFreeFileDialog")
#pragma comment(linker, "/alternatename:_PhFreeFileDialog@4=_PhFreeFileDialog")
#pragma comment(linker, "/alternatename:__imp__PhShowFileDialog@8=__imp__PhShowFileDialog")
#pragma comment(linker, "/alternatename:_PhShowFileDialog@8=_PhShowFileDialog")
#pragma comment(linker, "/alternatename:__imp__PhSetFileDialogOptions@8=__imp__PhSetFileDialogOptions")
#pragma comment(linker, "/alternatename:_PhSetFileDialogOptions@8=_PhSetFileDialogOptions")
#pragma comment(linker, "/alternatename:__imp__PhSetFileDialogFilter@12=__imp__PhSetFileDialogFilter")
#pragma comment(linker, "/alternatename:_PhSetFileDialogFilter@12=_PhSetFileDialogFilter")
#pragma comment(linker, "/alternatename:__imp__PhGetFileDialogFileName@4=__imp__PhGetFileDialogFileName")
#pragma comment(linker, "/alternatename:_PhGetFileDialogFileName@4=_PhGetFileDialogFileName")
#pragma comment(linker, "/alternatename:__imp__PhShowIconNotification@8=__imp__PhShowIconNotification")
#pragma comment(linker, "/alternatename:_PhShowIconNotification@8=_PhShowIconNotification")
#endif

#define PLUGIN_NAME L"QuickDllInject"
#define PLUGIN_OPTIONS_SECTION_NAME L"Quick DLL Inject"

// --- Settings (options.c owns registration) ---

#define SETTING_NAME_INJECTION_METHOD (PLUGIN_NAME L".InjectionMethod")
#define SETTING_NAME_INJECTION_TIMEOUT (PLUGIN_NAME L".InjectionTimeoutMs")
#define SETTING_NAME_CONFIRM_INJECT (PLUGIN_NAME L".ConfirmBeforeInject")
#define SETTING_NAME_RECENT_DLLS (PLUGIN_NAME L".RecentDlls")
#define SETTING_NAME_MAX_HISTORY (PLUGIN_NAME L".MaxHistory")
#define SETTING_NAME_NOTIFY_SUCCESS (PLUGIN_NAME L".NotifyOnSuccess")
#define SETTING_NAME_ERASE_HEADERS (PLUGIN_NAME L".EraseHeaders")
#define SETTING_NAME_UNLINK_PEB (PLUGIN_NAME L".UnlinkFromPeb")
#define SETTING_NAME_HIDE_THREAD (PLUGIN_NAME L".HideThreadFromDebugger")

#define DEFAULT_INJECTION_METHOD 0
#define DEFAULT_INJECTION_TIMEOUT_MS 5000
#define DEFAULT_CONFIRM_INJECT 0
#define DEFAULT_MAX_HISTORY 10
#define DEFAULT_NOTIFY_SUCCESS 1
#define DEFAULT_ERASE_HEADERS 0
#define DEFAULT_UNLINK_PEB 0
#define DEFAULT_HIDE_THREAD 0

#define INJECTION_TIMEOUT_MIN_MS 1000
#define INJECTION_TIMEOUT_MAX_MS 60000

#define HISTORY_MIN_ENTRIES 1
#define HISTORY_MAX_ENTRIES 20
#define HISTORY_DELIMITER L'|'

typedef enum _QUICKDLLINJECT_METHOD
{
    QuickDllInjectMethodRemoteThread = 0,
    QuickDllInjectMethodApcThread = 1,
    QuickDllInjectMethodManualMap = 2
} QUICKDLLINJECT_METHOD;

#define QUICKDLLINJECT_METHOD_MAX QuickDllInjectMethodManualMap

extern PPH_PLUGIN PluginInstance;

// --- util.c: shared helpers ---

PCWSTR QuickGetFileName(
    _In_ PCWSTR Path
    );

// --- menu.c: context menu ---

VOID QuickMenuInitialize(
    VOID
    );

// --- options.c: Options window page ---

VOID QuickOptionsInitialize(
    VOID
    );

VOID NTAPI ShowOptionsCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    );

INT_PTR CALLBACK OptionsDlgProc(
    _In_ HWND WindowHandle,
    _In_ UINT WindowMessage,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    );

// --- history.c: recent-DLL persistence ---

PPH_LIST QuickGetRecentDlls(
    VOID
    );

VOID QuickFreeRecentDlls(
    _In_opt_ PPH_LIST RecentDlls
    );

VOID QuickAddRecentDll(
    _In_ PCWSTR DllPath
    );

VOID QuickClearRecentDlls(
    VOID
    );

// --- engine.c: remote/APC thread injection ---

NTSTATUS QuickResolveRemoteProcedure(
    _In_ HANDLE ProcessId,
    _In_ BOOLEAN TargetWow64,
    _In_ PCWSTR ModuleName,
    _In_ PCSTR ProcedureName,
    _Out_ PVOID *ProcedureAddress
    );

NTSTATUS QuickWaitForThread(
    _In_ HANDLE ThreadHandle,
    _In_ ULONG TimeoutMs,
    _Out_ PULONG ExitCode
    );

NTSTATUS QuickInjectRemoteThread(
    _In_ HANDLE ProcessHandle,
    _In_ HANDLE ProcessId,
    _In_ BOOLEAN TargetWow64,
    _In_ PCWSTR DllPath,
    _In_ ULONG TimeoutMs,
    _In_ BOOLEAN HideThread,
    _Out_ PVOID *ModuleBase
    );

NTSTATUS QuickInjectApcThread(
    _In_ HANDLE ProcessHandle,
    _In_ HANDLE ProcessId,
    _In_ BOOLEAN TargetWow64,
    _In_ PCWSTR DllPath,
    _In_ ULONG TimeoutMs,
    _In_ BOOLEAN HideThread,
    _Out_ PVOID *ModuleBase
    );

// --- inject.c: high-level orchestration ---

BOOLEAN QuickBrowseAndInject(
    _In_ HWND OwnerWindow,
    _In_ PPH_PROCESS_ITEM ProcessItem
    );

BOOLEAN QuickInjectDllPath(
    _In_ HWND OwnerWindow,
    _In_ PPH_PROCESS_ITEM ProcessItem,
    _In_ PCWSTR DllPath
    );

// --- stealth.c: post-injection steps ---

PVOID QuickFindModuleBase(
    _In_ HANDLE ProcessId,
    _In_ PCWSTR DllPath
    );

NTSTATUS QuickErasePeHeaders(
    _In_ HANDLE ProcessHandle,
    _In_ HANDLE ProcessId,
    _In_ BOOLEAN TargetWow64,
    _In_ PVOID ModuleBase,
    _In_ ULONG TimeoutMs,
    _Out_ PULONG FailedStep
    );

NTSTATUS QuickUnlinkFromPeb(
    _In_ HANDLE ProcessHandle,
    _In_ PVOID ModuleBase
    );

// --- manualmap.c: stealth PE loader orchestration ---

NTSTATUS QuickManualMap(
    _In_ HANDLE ProcessHandle,
    _In_ HANDLE ProcessId,
    _In_ BOOLEAN TargetWow64,
    _In_ PCWSTR DllPath,
    _In_ ULONG TimeoutMs,
    _In_ BOOLEAN HideThread,
    _Out_ PVOID *ModuleBase
    );

// --- mapimage.c: local PE image preparation ---

typedef struct _QUICK_MAP_LAYOUT
{
    ULONG EntryPointRva;
    ULONG TlsArrayRva;
    ULONG TlsCount;
    ULONG ExceptionRva;
    ULONG ExceptionCount;
} QUICK_MAP_LAYOUT, *PQUICK_MAP_LAYOUT;

#define QUICK_MAP_TLS_MAX_CALLBACKS 16

PBYTE QuickReadMapFile(
    _In_ PCWSTR FilePath,
    _Out_ PSIZE_T FileSize
    );

NTSTATUS QuickInspectMapFile(
    _In_ PBYTE FileData,
    _In_ SIZE_T FileSize,
    _Out_ PULONG_PTR PreferredBase,
    _Out_ PULONG ImageSize
    );

NTSTATUS QuickBuildMapImage(
    _In_ PBYTE FileData,
    _In_ SIZE_T FileSize,
    _In_ ULONG_PTR Delta,
    _Out_writes_bytes_(ImageSize) PBYTE LocalImage,
    _In_ ULONG ImageSize,
    _Out_ PQUICK_MAP_LAYOUT Layout
    );

VOID QuickProtectMapSections(
    _In_ HANDLE ProcessHandle,
    _In_ PVOID RemoteBase,
    _In_ PBYTE FileData,
    _In_ SIZE_T FileSize,
    _In_ ULONG ImageSize
    );

// --- mapstub.c: loader stubs ---

PUCHAR QuickGetMapStub(
    _Out_ PSIZE_T StubSize
    );

#endif // _QUICKDLLINJECT_H_

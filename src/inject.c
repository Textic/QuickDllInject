/*
 * QuickDllInject - System Informer Plugin
 *
 * High-level injection orchestration: confirm, open, architecture check,
 * method dispatch, stealth post-steps, history and notifications.
 *
 * Authors:
 *     Textic
 *
 */

#include "QuickDllInject.h"

#define QUICK_PROCESS_ACCESS (PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_LIMITED_INFORMATION | \
    PROCESS_QUERY_INFORMATION | PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | \
    PROCESS_VM_READ | PROCESS_VM_WRITE | SYNCHRONIZE)

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
        ULONG confirmMethod = PhGetIntegerSetting(SETTING_NAME_INJECTION_METHOD);
        PCWSTR methodName = L"Remote thread";

        if (confirmMethod == QuickDllInjectMethodApcThread)
            methodName = L"APC thread";
        else if (confirmMethod == QuickDllInjectMethodManualMap)
            methodName = L"Manual map";

        PPH_STRING message = PhaFormatString(
            L"Process: %s (%lu)\r\nMethod: %s",
            processName,
            (ULONG)(ULONG_PTR)ProcessItem->ProcessId,
            methodName
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
    if (method > QUICKDLLINJECT_METHOD_MAX)
        method = QuickDllInjectMethodRemoteThread;

    timeoutMs = PhGetIntegerSetting(SETTING_NAME_INJECTION_TIMEOUT);
    if (timeoutMs < INJECTION_TIMEOUT_MIN_MS || timeoutMs > INJECTION_TIMEOUT_MAX_MS)
        timeoutMs = DEFAULT_INJECTION_TIMEOUT_MS;

    hideThread = !!PhGetIntegerSetting(SETTING_NAME_HIDE_THREAD);
    eraseHeaders = !!PhGetIntegerSetting(SETTING_NAME_ERASE_HEADERS);
    unlinkPeb = !!PhGetIntegerSetting(SETTING_NAME_UNLINK_PEB);

    if (method == QuickDllInjectMethodManualMap)
    {
        status = QuickManualMap(
            processHandle,
            ProcessItem->ProcessId,
            targetWow64,
            DllPath,
            timeoutMs,
            hideThread,
            &moduleBase
            );
    }
    else if (method == QuickDllInjectMethodApcThread)
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
            ULONG eraseStep = 0;

            status = QuickErasePeHeaders(
                processHandle, ProcessItem->ProcessId, targetWow64, moduleBase, timeoutMs, &eraseStep
                );

            if (!NT_SUCCESS(status))
            {
                PCWSTR stepMessage = L"Unable to erase the PE headers.";

                // Diagnostic step: 1 = read/validate, 2 = protection change
                // refused, 3 = cross-process copy refused, 4 = in-target
                // erase failed.
                if (eraseStep == 1)
                    stepMessage = L"Erase 1/4: cannot read the remote headers.";
                else if (eraseStep == 2)
                    stepMessage = L"Erase 2/4: cannot change header protection.";
                else if (eraseStep == 3)
                    stepMessage = L"Erase 3/4: cross-process copy refused.";
                else if (eraseStep == 4)
                    stepMessage = L"Erase 4/4: in-target erase failed.";

                NtClose(processHandle);
                PhShowStatus(OwnerWindow, stepMessage, status, 0);
                return FALSE;
            }
        }

        if (unlinkPeb)
        {
            if (method == QuickDllInjectMethodManualMap)
            {
                // Already hidden by design: never registered with the loader.
            }
            else
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

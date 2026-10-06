/*
 * QuickDllInject - System Informer Plugin
 *
 * Native options page (Options -> Quick DLL Inject).
 *
 * Authors:
 *     Textic
 *
 */

#include "QuickDllInject.h"

static PH_CALLBACK_REGISTRATION OptionsWindowInitializingCallbackRegistration;

typedef struct _QUICK_INT_EDIT
{
    INT EditId;
    INT SpinId;
    PCWSTR SettingName;
    ULONG Minimum;
    ULONG Maximum;
    ULONG Default;
} QUICK_INT_EDIT, *PQUICK_INT_EDIT;

static const QUICK_INT_EDIT QuickTimeoutEdit =
{
    IDC_TIMEOUT_EDIT, IDC_TIMEOUT_SPIN,
    SETTING_NAME_INJECTION_TIMEOUT,
    INJECTION_TIMEOUT_MIN_MS, INJECTION_TIMEOUT_MAX_MS,
    DEFAULT_INJECTION_TIMEOUT_MS
};

static const QUICK_INT_EDIT QuickHistoryEdit =
{
    IDC_HISTORY_EDIT, IDC_HISTORY_SPIN,
    SETTING_NAME_MAX_HISTORY,
    HISTORY_MIN_ENTRIES, HISTORY_MAX_ENTRIES,
    DEFAULT_MAX_HISTORY
};

static ULONG QuickGetClampedSetting(
    _In_ const QUICK_INT_EDIT *Edit
    )
{
    ULONG value = PhGetIntegerSetting(Edit->SettingName);

    if (value < Edit->Minimum || value > Edit->Maximum)
        value = Edit->Default;

    return value;
}

static VOID QuickInitIntEdit(
    _In_ HWND DialogHandle,
    _In_ const QUICK_INT_EDIT *Edit
    )
{
    HWND spinHandle;
    ULONG value = QuickGetClampedSetting(Edit);

    SetDlgItemInt(DialogHandle, Edit->EditId, value, FALSE);

    spinHandle = GetDlgItem(DialogHandle, Edit->SpinId);
    SendMessage(spinHandle, UDM_SETRANGE32, (WPARAM)Edit->Minimum, (LPARAM)Edit->Maximum);
    SendMessage(spinHandle, UDM_SETBUDDY, (WPARAM)GetDlgItem(DialogHandle, Edit->EditId), 0);
    SendMessage(spinHandle, UDM_SETPOS32, 0, (LPARAM)(LONG)value);
}

static VOID QuickIntEditChanged(
    _In_ HWND DialogHandle,
    _In_ const QUICK_INT_EDIT *Edit
    )
{
    BOOL translated = FALSE;
    UINT value = GetDlgItemInt(DialogHandle, Edit->EditId, &translated, FALSE);

    // Persist while typing only when the value is already in range;
    // out-of-range text is normalized on kill focus so typing is
    // never interrupted.
    if (translated && value >= Edit->Minimum && value <= Edit->Maximum)
    {
        PhSetIntegerSetting(Edit->SettingName, value);
        SendMessage(GetDlgItem(DialogHandle, Edit->SpinId), UDM_SETPOS32, 0, (LPARAM)(LONG)value);
    }
}

static VOID QuickIntEditKillFocus(
    _In_ HWND DialogHandle,
    _In_ const QUICK_INT_EDIT *Edit
    )
{
    BOOL translated = FALSE;
    UINT value = GetDlgItemInt(DialogHandle, Edit->EditId, &translated, FALSE);

    if (!translated)
        value = PhGetIntegerSetting(Edit->SettingName);
    if (value < Edit->Minimum)
        value = Edit->Minimum;
    if (value > Edit->Maximum)
        value = Edit->Maximum;

    PhSetIntegerSetting(Edit->SettingName, value);
    SetDlgItemInt(DialogHandle, Edit->EditId, value, FALSE);
    SendMessage(GetDlgItem(DialogHandle, Edit->SpinId), UDM_SETPOS32, 0, (LPARAM)(LONG)value);
}

static VOID QuickUpdateMethodDescription(
    _In_ HWND WindowHandle,
    _In_ ULONG Method
    )
{
    PCWSTR text;

    switch (Method)
    {
    case QuickDllInjectMethodApcThread:
        text = L"APC: suspended thread + queued LoadLibraryW call. Stealthier when remote threads are monitored.";
        break;
    case QuickDllInjectMethodManualMap:
        text = L"Manual map: stealth PE loader, invisible to module lists. Experimental; system-DLL imports only.";
        break;
    case QuickDllInjectMethodRemoteThread:
    default:
        text = L"Remote thread: CreateRemoteThread(LoadLibraryW). Default, most compatible.";
        break;
    }

    PhSetDialogItemText(WindowHandle, IDC_METHOD_DESC, (PWSTR)text);
}

/**
 * Dialog procedure for the plugin options page hosted by
 * System Informer's options window.
 */
INT_PTR CALLBACK OptionsDlgProc(
    _In_ HWND WindowHandle,
    _In_ UINT WindowMessage,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    switch (WindowMessage)
    {
    case WM_INITDIALOG:
        {
            HWND comboBoxHandle;
            ULONG method;

            method = PhGetIntegerSetting(SETTING_NAME_INJECTION_METHOD);
            if (method > QUICKDLLINJECT_METHOD_MAX)
                method = QuickDllInjectMethodRemoteThread;

            comboBoxHandle = GetDlgItem(WindowHandle, IDC_METHOD_COMBO);
            ComboBox_AddString(comboBoxHandle, L"Remote thread (LoadLibraryW)");
            ComboBox_AddString(comboBoxHandle, L"APC thread (QueueUserAPC)");
            ComboBox_AddString(comboBoxHandle, L"Manual map (stealth loader)");
            ComboBox_SetCurSel(comboBoxHandle, (INT)method);
            QuickUpdateMethodDescription(WindowHandle, method);

            QuickInitIntEdit(WindowHandle, &QuickTimeoutEdit);
            QuickInitIntEdit(WindowHandle, &QuickHistoryEdit);

            Button_SetCheck(GetDlgItem(WindowHandle, IDC_CONFIRM_CHECK),
                PhGetIntegerSetting(SETTING_NAME_CONFIRM_INJECT) ? BST_CHECKED : BST_UNCHECKED);
            Button_SetCheck(GetDlgItem(WindowHandle, IDC_NOTIFY_CHECK),
                PhGetIntegerSetting(SETTING_NAME_NOTIFY_SUCCESS) ? BST_CHECKED : BST_UNCHECKED);

            Button_SetCheck(GetDlgItem(WindowHandle, IDC_ERASE_CHECK),
                PhGetIntegerSetting(SETTING_NAME_ERASE_HEADERS) ? BST_CHECKED : BST_UNCHECKED);
            Button_SetCheck(GetDlgItem(WindowHandle, IDC_UNLINK_CHECK),
                PhGetIntegerSetting(SETTING_NAME_UNLINK_PEB) ? BST_CHECKED : BST_UNCHECKED);
            Button_SetCheck(GetDlgItem(WindowHandle, IDC_HIDETHREAD_CHECK),
                PhGetIntegerSetting(SETTING_NAME_HIDE_THREAD) ? BST_CHECKED : BST_UNCHECKED);
        }
        break;
    case WM_COMMAND:
        {
            switch (GET_WM_COMMAND_ID(wParam, lParam))
            {
            case IDC_METHOD_COMBO:
                {
                    if (GET_WM_COMMAND_CMD(wParam, lParam) == CBN_SELCHANGE)
                    {
                        INT selected = ComboBox_GetCurSel(GET_WM_COMMAND_HWND(wParam, lParam));

                        if (selected >= QuickDllInjectMethodRemoteThread && selected <= (INT)QUICKDLLINJECT_METHOD_MAX)
                        {
                            PhSetIntegerSetting(SETTING_NAME_INJECTION_METHOD, (ULONG)selected);
                            QuickUpdateMethodDescription(WindowHandle, (ULONG)selected);
                        }
                    }
                }
                break;
            case IDC_TIMEOUT_EDIT:
                {
                    if (GET_WM_COMMAND_CMD(wParam, lParam) == EN_CHANGE)
                        QuickIntEditChanged(WindowHandle, &QuickTimeoutEdit);
                    else if (GET_WM_COMMAND_CMD(wParam, lParam) == EN_KILLFOCUS)
                        QuickIntEditKillFocus(WindowHandle, &QuickTimeoutEdit);
                }
                break;
            case IDC_CONFIRM_CHECK:
                {
                    PhSetIntegerSetting(SETTING_NAME_CONFIRM_INJECT,
                        Button_GetCheck(GET_WM_COMMAND_HWND(wParam, lParam)) == BST_CHECKED ? 1 : 0);
                }
                break;
            case IDC_NOTIFY_CHECK:
                {
                    PhSetIntegerSetting(SETTING_NAME_NOTIFY_SUCCESS,
                        Button_GetCheck(GET_WM_COMMAND_HWND(wParam, lParam)) == BST_CHECKED ? 1 : 0);
                }
                break;
            case IDC_HISTORY_EDIT:
                {
                    if (GET_WM_COMMAND_CMD(wParam, lParam) == EN_CHANGE)
                        QuickIntEditChanged(WindowHandle, &QuickHistoryEdit);
                    else if (GET_WM_COMMAND_CMD(wParam, lParam) == EN_KILLFOCUS)
                        QuickIntEditKillFocus(WindowHandle, &QuickHistoryEdit);
                }
                break;
            case IDC_CLEAR_HISTORY:
                {
                    if (GET_WM_COMMAND_CMD(wParam, lParam) == BN_CLICKED)
                        QuickClearRecentDlls();
                }
                break;
            case IDC_ERASE_CHECK:
                {
                    PhSetIntegerSetting(SETTING_NAME_ERASE_HEADERS,
                        Button_GetCheck(GET_WM_COMMAND_HWND(wParam, lParam)) == BST_CHECKED ? 1 : 0);
                }
                break;
            case IDC_UNLINK_CHECK:
                {
                    PhSetIntegerSetting(SETTING_NAME_UNLINK_PEB,
                        Button_GetCheck(GET_WM_COMMAND_HWND(wParam, lParam)) == BST_CHECKED ? 1 : 0);
                }
                break;
            case IDC_HIDETHREAD_CHECK:
                {
                    PhSetIntegerSetting(SETTING_NAME_HIDE_THREAD,
                        Button_GetCheck(GET_WM_COMMAND_HWND(wParam, lParam)) == BST_CHECKED ? 1 : 0);
                }
                break;
            }
        }
        break;
    case WM_CTLCOLORBTN:
        return HANDLE_WM_CTLCOLORBTN(WindowHandle, wParam, lParam, PhWindowThemeControlColor);
    case WM_CTLCOLORDLG:
        return HANDLE_WM_CTLCOLORDLG(WindowHandle, wParam, lParam, PhWindowThemeControlColor);
    case WM_CTLCOLORSTATIC:
        return HANDLE_WM_CTLCOLORSTATIC(WindowHandle, wParam, lParam, PhWindowThemeControlColor);
    }

    return FALSE;
}

/**
 * Registers the "Quick DLL Inject" section inside
 * System Informer -> Options.
 */
_Function_class_(PH_CALLBACK_FUNCTION)
VOID NTAPI ShowOptionsCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    PPH_PLUGIN_OPTIONS_POINTERS optionsEntry = (PPH_PLUGIN_OPTIONS_POINTERS)Parameter;

    if (!optionsEntry)
        return;

    optionsEntry->CreateSection(
        PLUGIN_OPTIONS_SECTION_NAME,
        PluginInstance->DllBase,
        MAKEINTRESOURCE(IDD_OPTIONS),
        OptionsDlgProc,
        NULL
        );
}

VOID QuickOptionsInitialize(
    VOID
    )
{
    PH_SETTING_CREATE settings[] =
    {
        { IntegerSettingType, SETTING_NAME_INJECTION_METHOD, L"0" },
        { IntegerSettingType, SETTING_NAME_INJECTION_TIMEOUT, L"5000" },
        { IntegerSettingType, SETTING_NAME_CONFIRM_INJECT, L"0" },
        { StringSettingType, SETTING_NAME_RECENT_DLLS, L"" },
        { IntegerSettingType, SETTING_NAME_MAX_HISTORY, L"10" },
        { IntegerSettingType, SETTING_NAME_NOTIFY_SUCCESS, L"1" },
        { IntegerSettingType, SETTING_NAME_ERASE_HEADERS, L"0" },
        { IntegerSettingType, SETTING_NAME_UNLINK_PEB, L"0" },
        { IntegerSettingType, SETTING_NAME_HIDE_THREAD, L"0" },
    };

    PhAddSettings(settings, RTL_NUMBER_OF(settings));

    // Options window section: Options -> Quick DLL Inject
    PhRegisterCallback(
        PhGetGeneralCallback(GeneralCallbackOptionsWindowInitializing),
        ShowOptionsCallback,
        NULL,
        &OptionsWindowInitializingCallbackRegistration
    );
}

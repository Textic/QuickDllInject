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
            HWND spinHandle;
            ULONG method;
            ULONG timeoutMs;
            ULONG maxHistory;

            method = PhGetIntegerSetting(SETTING_NAME_INJECTION_METHOD);
            if (method > QuickDllInjectMethodApcThread)
                method = QuickDllInjectMethodRemoteThread;

            timeoutMs = PhGetIntegerSetting(SETTING_NAME_INJECTION_TIMEOUT);
            if (timeoutMs < INJECTION_TIMEOUT_MIN_MS || timeoutMs > INJECTION_TIMEOUT_MAX_MS)
                timeoutMs = DEFAULT_INJECTION_TIMEOUT_MS;

            maxHistory = PhGetIntegerSetting(SETTING_NAME_MAX_HISTORY);
            if (maxHistory < HISTORY_MIN_ENTRIES || maxHistory > HISTORY_MAX_ENTRIES)
                maxHistory = DEFAULT_MAX_HISTORY;

            comboBoxHandle = GetDlgItem(WindowHandle, IDC_METHOD_COMBO);
            ComboBox_AddString(comboBoxHandle, L"Remote thread (LoadLibraryW)");
            ComboBox_AddString(comboBoxHandle, L"APC thread (QueueUserAPC)");
            ComboBox_SetCurSel(comboBoxHandle, (INT)method);
            QuickUpdateMethodDescription(WindowHandle, method);

            SetDlgItemInt(WindowHandle, IDC_TIMEOUT_EDIT, timeoutMs, FALSE);

            spinHandle = GetDlgItem(WindowHandle, IDC_TIMEOUT_SPIN);
            SendMessage(spinHandle, UDM_SETRANGE32, INJECTION_TIMEOUT_MIN_MS, INJECTION_TIMEOUT_MAX_MS);
            SendMessage(spinHandle, UDM_SETBUDDY, (WPARAM)GetDlgItem(WindowHandle, IDC_TIMEOUT_EDIT), 0);
            SendMessage(spinHandle, UDM_SETPOS32, 0, (LPARAM)timeoutMs);

            Button_SetCheck(GetDlgItem(WindowHandle, IDC_CONFIRM_CHECK),
                PhGetIntegerSetting(SETTING_NAME_CONFIRM_INJECT) ? BST_CHECKED : BST_UNCHECKED);
            Button_SetCheck(GetDlgItem(WindowHandle, IDC_NOTIFY_CHECK),
                PhGetIntegerSetting(SETTING_NAME_NOTIFY_SUCCESS) ? BST_CHECKED : BST_UNCHECKED);

            SetDlgItemInt(WindowHandle, IDC_HISTORY_EDIT, maxHistory, FALSE);

            spinHandle = GetDlgItem(WindowHandle, IDC_HISTORY_SPIN);
            SendMessage(spinHandle, UDM_SETRANGE32, HISTORY_MIN_ENTRIES, HISTORY_MAX_ENTRIES);
            SendMessage(spinHandle, UDM_SETBUDDY, (WPARAM)GetDlgItem(WindowHandle, IDC_HISTORY_EDIT), 0);
            SendMessage(spinHandle, UDM_SETPOS32, 0, (LPARAM)maxHistory);

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

                        if (selected == QuickDllInjectMethodRemoteThread || selected == QuickDllInjectMethodApcThread)
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
                    {
                        BOOL translated = FALSE;
                        UINT value = GetDlgItemInt(WindowHandle, IDC_TIMEOUT_EDIT, &translated, FALSE);

                        // Persist while typing only when the value is already
                        // in range; out-of-range text is normalized on kill focus
                        // so typing is never interrupted.
                        if (translated && value >= INJECTION_TIMEOUT_MIN_MS && value <= INJECTION_TIMEOUT_MAX_MS)
                        {
                            PhSetIntegerSetting(SETTING_NAME_INJECTION_TIMEOUT, value);
                            SendMessage(GetDlgItem(WindowHandle, IDC_TIMEOUT_SPIN), UDM_SETPOS32, 0, (LPARAM)(LONG)value);
                        }
                    }
                    else if (GET_WM_COMMAND_CMD(wParam, lParam) == EN_KILLFOCUS)
                    {
                        BOOL translated = FALSE;
                        UINT value = GetDlgItemInt(WindowHandle, IDC_TIMEOUT_EDIT, &translated, FALSE);

                        if (!translated)
                            value = PhGetIntegerSetting(SETTING_NAME_INJECTION_TIMEOUT);
                        if (value < INJECTION_TIMEOUT_MIN_MS)
                            value = INJECTION_TIMEOUT_MIN_MS;
                        if (value > INJECTION_TIMEOUT_MAX_MS)
                            value = INJECTION_TIMEOUT_MAX_MS;

                        PhSetIntegerSetting(SETTING_NAME_INJECTION_TIMEOUT, value);
                        SetDlgItemInt(WindowHandle, IDC_TIMEOUT_EDIT, value, FALSE);
                        SendMessage(GetDlgItem(WindowHandle, IDC_TIMEOUT_SPIN), UDM_SETPOS32, 0, (LPARAM)(LONG)value);
                    }
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
                    {
                        BOOL translated = FALSE;
                        UINT value = GetDlgItemInt(WindowHandle, IDC_HISTORY_EDIT, &translated, FALSE);

                        if (translated && value >= HISTORY_MIN_ENTRIES && value <= HISTORY_MAX_ENTRIES)
                        {
                            PhSetIntegerSetting(SETTING_NAME_MAX_HISTORY, value);
                            SendMessage(GetDlgItem(WindowHandle, IDC_HISTORY_SPIN), UDM_SETPOS32, 0, (LPARAM)(LONG)value);
                        }
                    }
                    else if (GET_WM_COMMAND_CMD(wParam, lParam) == EN_KILLFOCUS)
                    {
                        BOOL translated = FALSE;
                        UINT value = GetDlgItemInt(WindowHandle, IDC_HISTORY_EDIT, &translated, FALSE);

                        if (!translated)
                            value = PhGetIntegerSetting(SETTING_NAME_MAX_HISTORY);
                        if (value < HISTORY_MIN_ENTRIES)
                            value = HISTORY_MIN_ENTRIES;
                        if (value > HISTORY_MAX_ENTRIES)
                            value = HISTORY_MAX_ENTRIES;

                        PhSetIntegerSetting(SETTING_NAME_MAX_HISTORY, value);
                        SetDlgItemInt(WindowHandle, IDC_HISTORY_EDIT, value, FALSE);
                        SendMessage(GetDlgItem(WindowHandle, IDC_HISTORY_SPIN), UDM_SETPOS32, 0, (LPARAM)(LONG)value);
                    }
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

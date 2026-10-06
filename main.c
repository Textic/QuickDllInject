/*
 * QuickDllInject - System Informer Plugin
 *
 * Adds a direct "Inject DLL..." shortcut to the right-click context menu of processes.
 *
 * Authors:
 *     Textic
 *
 */

#include "QuickDllInject.h"

PPH_PLUGIN PluginInstance = NULL;
static PH_CALLBACK_REGISTRATION ProcessMenuInitializingCallbackRegistration;
static PH_CALLBACK_REGISTRATION PluginMenuItemCallbackRegistration;
static PH_CALLBACK_REGISTRATION OptionsWindowInitializingCallbackRegistration;

/**
 * Handles the menu item click when the user selects "Inject DLL...",
 * a recent DLL entry or "Clear History".
 */
_Function_class_(PH_CALLBACK_FUNCTION)
static VOID NTAPI MenuItemCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    PPH_PLUGIN_MENU_ITEM menuItem = (PPH_PLUGIN_MENU_ITEM)Parameter;
    PPH_PROCESS_ITEM processItem;

    if (!menuItem)
        return;

    processItem = (PPH_PROCESS_ITEM)menuItem->Context;

    if (!processItem)
        return;

    if (menuItem->Id == ID_PROCESS_INJECT_DLL)
    {
        QuickBrowseAndInject(menuItem->OwnerWindow, processItem);
    }
    else if (menuItem->Id == ID_PROCESS_INJECT_CLEAR_HISTORY)
    {
        QuickClearRecentDlls();
    }
    else if (menuItem->Id >= ID_PROCESS_INJECT_RECENT_BASE && menuItem->Id < ID_PROCESS_INJECT_RECENT_MAX)
    {
        ULONG index = menuItem->Id - ID_PROCESS_INJECT_RECENT_BASE;
        PPH_LIST recentDlls = QuickGetRecentDlls();

        if (recentDlls && index < recentDlls->Count)
        {
            PPH_STRING dllPath = (PPH_STRING)recentDlls->Items[index];

            QuickInjectDllPath(menuItem->OwnerWindow, processItem, dllPath->Buffer);
        }

        QuickFreeRecentDlls(recentDlls);
    }
}

/**
 * Builds the "Inject Recent DLL" submenu: one entry per remembered DLL
 * plus a "Clear History" command.
 */
static PPH_EMENU_ITEM QuickCreateRecentSubmenu(
    _In_ PPH_PROCESS_ITEM ProcessItem
    )
{
    PPH_EMENU_ITEM submenu;
    PPH_LIST recentDlls;
    ULONG maxEntries;

    submenu = PhPluginCreateEMenuItem(
        PluginInstance,
        0,
        ID_PROCESS_INJECT_RECENT_MENU,
        L"Inject &Recent DLL",
        ProcessItem
        );

    if (!submenu)
        return NULL;

    recentDlls = QuickGetRecentDlls();
    maxEntries = PhGetIntegerSetting(SETTING_NAME_MAX_HISTORY);

    if (maxEntries < HISTORY_MIN_ENTRIES)
        maxEntries = HISTORY_MIN_ENTRIES;
    if (maxEntries > HISTORY_MAX_ENTRIES)
        maxEntries = HISTORY_MAX_ENTRIES;

    if (!recentDlls || recentDlls->Count == 0)
    {
        // Disabled placeholder so the submenu still explains itself.
        PhInsertEMenuItem(
            submenu,
            PhPluginCreateEMenuItem(PluginInstance, PH_EMENU_DISABLED, 0, L"(empty)", NULL),
            ULONG_MAX
            );
    }
    else
    {
        for (ULONG i = 0; i < recentDlls->Count && i < maxEntries; i++)
        {
            PPH_STRING dllPath = (PPH_STRING)recentDlls->Items[i];
            PCWSTR fileName = wcsrchr(dllPath->Buffer, L'\\');

            fileName = fileName ? fileName + 1 : dllPath->Buffer;

            PhInsertEMenuItem(
                submenu,
                PhPluginCreateEMenuItem(
                    PluginInstance,
                    0,
                    ID_PROCESS_INJECT_RECENT_BASE + i,
                    fileName,
                    ProcessItem
                    ),
                ULONG_MAX
                );
        }

        PhInsertEMenuItem(
            submenu,
            PhPluginCreateEMenuItem(PluginInstance, PH_EMENU_SEPARATOR, 0, NULL, NULL),
            ULONG_MAX
            );
        PhInsertEMenuItem(
            submenu,
            PhPluginCreateEMenuItem(
                PluginInstance,
                0,
                ID_PROCESS_INJECT_CLEAR_HISTORY,
                L"Clear History",
                ProcessItem
                ),
            ULONG_MAX
            );
    }

    QuickFreeRecentDlls(recentDlls);

    return submenu;
}

/**
 * Inserts the "Inject DLL..." menu item into the context menu when
 * right-clicking on any process in the main process tree.
 */
_Function_class_(PH_CALLBACK_FUNCTION)
static VOID NTAPI ProcessMenuInitializingCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    PPH_PLUGIN_MENU_INFORMATION menuInfo = (PPH_PLUGIN_MENU_INFORMATION)Parameter;

    if (!menuInfo)
        return;

    // Only enable if exactly 1 process is selected
    if (menuInfo->u.Process.NumberOfProcesses == 1)
    {
        PPH_PROCESS_ITEM processItem = menuInfo->u.Process.Processes[0];

        // Skip pseudo processes and protected system processes (Idle, System, invalid PIDs)
        if (PH_IS_FAKE_PROCESS_ID(processItem->ProcessId) ||
            processItem->ProcessId == SYSTEM_IDLE_PROCESS_ID ||
            processItem->ProcessId == SYSTEM_PROCESS_ID)
        {
            return;
        }

        // Create the "Inject DLL..." menu item
        PPH_EMENU_ITEM injectDllItem = PhPluginCreateEMenuItem(
            PluginInstance,
            0,
            ID_PROCESS_INJECT_DLL,
            L"&Inject DLL...",
            processItem
        );

        if (!injectDllItem)
            return;

        // "Inject Recent DLL" submenu with history + Clear History
        PPH_EMENU_ITEM recentSubmenu = QuickCreateRecentSubmenu(processItem);

        // Place the items right before the last item (Properties)
        ULONG insertIndex = ULONG_MAX;
        if (menuInfo->Menu && menuInfo->Menu->Items && menuInfo->Menu->Items->Count > 0)
        {
            insertIndex = menuInfo->Menu->Items->Count - 1;
        }

        PhInsertEMenuItem(menuInfo->Menu, injectDllItem, insertIndex);

        if (recentSubmenu)
        {
            if (insertIndex != ULONG_MAX)
                insertIndex++;
            PhInsertEMenuItem(menuInfo->Menu, recentSubmenu, insertIndex);
        }
    }
}

/**
 * Plugin entry point (DLLMain).
 */
LOGICAL DllMain(
    _In_ HINSTANCE Instance,
    _In_ ULONG Reason,
    _Reserved_ PVOID Reserved
    )
{
    if (Reason == DLL_PROCESS_ATTACH)
    {
        PPH_PLUGIN_INFORMATION info;

        // Register the plugin with System Informer
        PluginInstance = PhRegisterPlugin(PLUGIN_NAME, Instance, &info);
        if (!PluginInstance)
            return FALSE;

        info->DisplayName = L"Quick DLL Inject";
        info->Author = L"Textic";
        info->Description = L"Adds a direct 'Inject DLL...' shortcut to the process right-click context menu.";

        // Persistent settings (Options -> Quick DLL Inject)
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
        }

        // Options window section: Options -> Quick DLL Inject
        PhRegisterCallback(
            PhGetGeneralCallback(GeneralCallbackOptionsWindowInitializing),
            ShowOptionsCallback,
            NULL,
            &OptionsWindowInitializingCallbackRegistration
        );

        // Subscribe to process context menu initialization callback
        PhRegisterCallback(
            PhGetGeneralCallback(GeneralCallbackProcessMenuInitializing),
            ProcessMenuInitializingCallback,
            NULL,
            &ProcessMenuInitializingCallbackRegistration
        );

        // Subscribe to plugin menu item click callback
        PhRegisterCallback(
            PhGetPluginCallback(PluginInstance, PluginCallbackMenuItem),
            MenuItemCallback,
            NULL,
            &PluginMenuItemCallbackRegistration
        );
    }

    return TRUE;
}

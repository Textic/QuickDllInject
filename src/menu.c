/*
 * QuickDllInject - System Informer Plugin
 *
 * Process context menu: "Inject DLL..." item, recent-DLL submenu,
 * and click routing.
 *
 * Authors:
 *     Textic
 *
 */

#include "QuickDllInject.h"

static PH_CALLBACK_REGISTRATION ProcessMenuInitializingCallbackRegistration;
static PH_CALLBACK_REGISTRATION PluginMenuItemCallbackRegistration;

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

            PhInsertEMenuItem(
                submenu,
                PhPluginCreateEMenuItem(
                    PluginInstance,
                    0,
                    ID_PROCESS_INJECT_RECENT_BASE + i,
                    QuickGetFileName(dllPath->Buffer),
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

VOID QuickMenuInitialize(
    VOID
    )
{
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

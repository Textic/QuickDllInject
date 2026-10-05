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

/**
 * Handles the menu item click when the user selects "Inject DLL...".
 */
_Function_class_(PH_CALLBACK_FUNCTION)
static VOID NTAPI MenuItemCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    PPH_PLUGIN_MENU_ITEM menuItem = (PPH_PLUGIN_MENU_ITEM)Parameter;

    if (!menuItem)
        return;

    if (menuItem->Id == ID_PROCESS_INJECT_DLL)
    {
        PPH_PROCESS_ITEM processItem = (PPH_PROCESS_ITEM)menuItem->Context;

        if (processItem)
        {
            // Invokes System Informer's built-in UI load DLL action which:
            // 1. Displays the native open-file picker dialog (*.dll)
            // 2. Opens the target process with required VM and thread creation access
            // 3. Injects/loads the DLL into the process address space
            // 4. Reports any errors to the user via status dialog
            PhUiLoadDllProcess(menuItem->OwnerWindow, processItem);
        }
    }
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

        // Place the item right before the last item (Properties)
        ULONG insertIndex = ULONG_MAX;
        if (menuInfo->Menu && menuInfo->Menu->Items && menuInfo->Menu->Items->Count > 0)
        {
            insertIndex = menuInfo->Menu->Items->Count - 1;
        }

        PhInsertEMenuItem(menuInfo->Menu, injectDllItem, insertIndex);
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

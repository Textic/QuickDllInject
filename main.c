/*
 * QuickDllInject - System Informer Plugin
 *
 * Adds a direct "Inject DLL..." option to the right-click context menu of processes.
 */

#include "QuickDllInject.h"

PPH_PLUGIN PluginInstance = NULL;
static PH_CALLBACK_REGISTRATION ProcessMenuInitializingCallbackRegistration;
static PH_CALLBACK_REGISTRATION PluginMenuItemCallbackRegistration;

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
            PhUiLoadDllProcess(menuItem->OwnerWindow, processItem);
        }
    }
}

_Function_class_(PH_CALLBACK_FUNCTION)
static VOID NTAPI ProcessMenuInitializingCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    PPH_PLUGIN_MENU_INFORMATION menuInfo = (PPH_PLUGIN_MENU_INFORMATION)Parameter;

    if (!menuInfo)
        return;

    // Solo habilitar si hay exactamente 1 proceso seleccionado
    if (menuInfo->u.Process.NumberOfProcesses == 1)
    {
        PPH_PROCESS_ITEM processItem = menuInfo->u.Process.Processes[0];

        // Ignoramos procesos especiales del sistema (Idle, System, PIDs falsos)
        if (PH_IS_FAKE_PROCESS_ID(processItem->ProcessId) ||
            processItem->ProcessId == SYSTEM_IDLE_PROCESS_ID ||
            processItem->ProcessId == SYSTEM_PROCESS_ID)
        {
            return;
        }

        // Crear el elemento de menú "Inject DLL..."
        PPH_EMENU_ITEM injectDllItem = PhPluginCreateEMenuItem(
            PluginInstance,
            0,
            ID_PROCESS_INJECT_DLL,
            L"&Inject DLL...",
            processItem
        );

        if (!injectDllItem)
            return;

        // Ubicamos el elemento justo antes de la última opción (Properties)
        ULONG insertIndex = ULONG_MAX;
        if (menuInfo->Menu && menuInfo->Menu->Items && menuInfo->Menu->Items->Count > 0)
        {
            insertIndex = menuInfo->Menu->Items->Count - 1;
        }

        PhInsertEMenuItem(menuInfo->Menu, injectDllItem, insertIndex);
    }
}

LOGICAL DllMain(
    _In_ HINSTANCE Instance,
    _In_ ULONG Reason,
    _Reserved_ PVOID Reserved
    )
{
    if (Reason == DLL_PROCESS_ATTACH)
    {
        PPH_PLUGIN_INFORMATION info;

        PluginInstance = PhRegisterPlugin(PLUGIN_NAME, Instance, &info);
        if (!PluginInstance)
            return FALSE;

        info->DisplayName = L"Quick DLL Inject";
        info->Author = L"Antigravity Pair";
        info->Description = L"Adds a direct 'Inject DLL...' shortcut to the process right-click context menu.";

        PhRegisterCallback(
            PhGetGeneralCallback(GeneralCallbackProcessMenuInitializing),
            ProcessMenuInitializingCallback,
            NULL,
            &ProcessMenuInitializingCallbackRegistration
        );

        PhRegisterCallback(
            PhGetPluginCallback(PluginInstance, PluginCallbackMenuItem),
            MenuItemCallback,
            NULL,
            &PluginMenuItemCallbackRegistration
        );
    }

    return TRUE;
}

/*
 * QuickDllInject - System Informer Plugin
 *
 * Plugin entry point: registration and module initialization.
 *
 * Authors:
 *     Textic
 *
 */

#include "QuickDllInject.h"

PPH_PLUGIN PluginInstance = NULL;

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

        QuickOptionsInitialize();
        QuickMenuInitialize();
    }

    return TRUE;
}

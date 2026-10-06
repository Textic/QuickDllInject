/*
 * QuickDllInject - System Informer Plugin
 *
 * Recent-DLL history persistence (pipe-delimited string setting).
 *
 * Authors:
 *     Textic
 *
 */

#include "QuickDllInject.h"

/**
 * Returns the recent-DLL history as a referenced list of PPH_STRING.
 * Free with QuickFreeRecentDlls().
 */
PPH_LIST QuickGetRecentDlls(
    VOID
    )
{
    PPH_LIST list = PhCreateList(8);
    PPH_STRING setting = PhGetStringSetting(SETTING_NAME_RECENT_DLLS);
    ULONG maxEntries = PhGetIntegerSetting(SETTING_NAME_MAX_HISTORY);
    PCWSTR cursor;
    ULONG count = 0;

    if (maxEntries < HISTORY_MIN_ENTRIES)
        maxEntries = HISTORY_MIN_ENTRIES;
    if (maxEntries > HISTORY_MAX_ENTRIES)
        maxEntries = HISTORY_MAX_ENTRIES;

    if (setting && setting->Buffer)
    {
        cursor = setting->Buffer;

        while (*cursor && count < maxEntries)
        {
            PCWSTR end = wcschr(cursor, HISTORY_DELIMITER);
            SIZE_T lengthChars = end ? (SIZE_T)(end - cursor) : wcslen(cursor);

            if (lengthChars > 0)
            {
                PhAddItemList(list, PhCreateStringEx((PCWCH)cursor, lengthChars * sizeof(WCHAR)));
                count++;
            }

            cursor = end ? end + 1 : cursor + lengthChars;
        }
    }

    if (setting)
        PhDereferenceObject(setting);

    return list;
}

VOID QuickFreeRecentDlls(
    _In_opt_ PPH_LIST RecentDlls
    )
{
    if (!RecentDlls)
        return;

    for (ULONG i = 0; i < RecentDlls->Count; i++)
        PhDereferenceObject(RecentDlls->Items[i]);

    PhDereferenceObject(RecentDlls);
}

VOID QuickAddRecentDll(
    _In_ PCWSTR DllPath
    )
{
    PPH_LIST list = QuickGetRecentDlls();
    ULONG maxEntries = PhGetIntegerSetting(SETTING_NAME_MAX_HISTORY);
    SIZE_T totalChars = 0;
    PPH_STRING joined;
    PWCHAR dest;

    if (maxEntries < HISTORY_MIN_ENTRIES)
        maxEntries = HISTORY_MIN_ENTRIES;
    if (maxEntries > HISTORY_MAX_ENTRIES)
        maxEntries = HISTORY_MAX_ENTRIES;

    // Remove any existing duplicate (case-insensitive), then prepend.
    for (ULONG i = 0; i < list->Count;)
    {
        PPH_STRING existing = (PPH_STRING)list->Items[i];

        if (_wcsicmp(existing->Buffer, DllPath) == 0)
        {
            PhDereferenceObject(existing);
            PhRemoveItemList(list, i);
        }
        else
        {
            i++;
        }
    }

    PhInsertItemList(list, 0, PhCreateString(DllPath));

    while (list->Count > maxEntries)
    {
        PhDereferenceObject(list->Items[list->Count - 1]);
        PhRemoveItemList(list, list->Count - 1);
    }

    for (ULONG i = 0; i < list->Count; i++)
        totalChars += ((PPH_STRING)list->Items[i])->Length / sizeof(WCHAR) + 1; // + delimiter

    joined = PhCreateStringEx(NULL, totalChars * sizeof(WCHAR));
    dest = joined->Buffer;

    for (ULONG i = 0; i < list->Count; i++)
    {
        PPH_STRING entry = (PPH_STRING)list->Items[i];
        SIZE_T chars = entry->Length / sizeof(WCHAR);

        memcpy(dest, entry->Buffer, entry->Length);
        dest += chars;
        *dest++ = HISTORY_DELIMITER;
    }

    if (list->Count > 0)
        dest[-1] = UNICODE_NULL;
    else
        *dest = UNICODE_NULL;

    // Length excludes the null terminator.
    joined->Length = (ULONG)((dest - joined->Buffer - (list->Count > 0 ? 1 : 0)) * sizeof(WCHAR));

    PhSetStringSetting(SETTING_NAME_RECENT_DLLS, joined->Buffer);
    PhDereferenceObject(joined);

    QuickFreeRecentDlls(list);
}

VOID QuickClearRecentDlls(
    VOID
    )
{
    PhSetStringSetting(SETTING_NAME_RECENT_DLLS, L"");
}

/*
 * QuickDllInject - System Informer Plugin
 *
 * Shared helpers.
 *
 * Authors:
 *     Textic
 *
 */

#include "QuickDllInject.h"

PCWSTR QuickGetFileName(
    _In_ PCWSTR Path
    )
{
    PCWSTR fileName = wcsrchr(Path, L'\\');

    return fileName ? fileName + 1 : Path;
}

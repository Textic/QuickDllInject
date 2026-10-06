# QuickDllInject - Development Roadmap & Feature TODO

This document outlines the planned roadmap for expanding **QuickDllInject** from a convenient 1-click context menu shortcut into a full-featured, professional DLL injection suite natively integrated into **System Informer**.

---

## 📌 Phase 1: Native Plugin Options Dialog — ✅ DONE (v1.1)

- [x] **Register options callback:**
  - Hooked `GeneralCallbackOptionsWindowInitializing` in `DllMain` (`ShowOptionsCallback` in `options.c`).
  - Native section **"Quick DLL Inject"** now appears under **Options -> Options** (the `PluginCallbackOptions` name from the original draft does not exist in the SDK).
- [x] **Native Win32 Configuration Dialog:**
  - Lightweight dialog (`QuickDllInject.rc`, `IDD_OPTIONS`) using System Informer's theme helpers (`PhWindowThemeControlColor`).
  - Group boxes: *Injection Method*, *General* and *Stealth & History* (last one reserved as placeholder for Phase 2/3).
- [x] **Persistent Settings Storage:**
  - Settings registered via `PhAddSettings` with `PH_SETTING_CREATE` (`QuickDllInject.InjectionMethod`, `QuickDllInject.InjectionTimeoutMs`, `QuickDllInject.ConfirmBeforeInject`).
  - Read/written with `PhGetIntegerSetting`/`PhSetIntegerSetting` so choices persist across restarts (the `PhpAddIntegerSetting`/`PhpAddStringSetting` names from the original draft do not exist in the SDK).

---

## 📌 Phase 2: User Experience (UX) & Quality of Life (QoL) — ✅ DONE (v1.2)

- [x] **Recent & Favorite DLLs History:**
  - Last N injected DLL paths persisted in `QuickDllInject.RecentDlls` (`|`-delimited string setting, deduped, most-recent-first, capped by `QuickDllInject.MaxHistory`, 1–20, default 10).
  - Process right-click menu now shows:
    - `Inject DLL...` (browse picker)
    - `Inject Recent DLL ➔ [file1.dll, file2.dll, ...]`
    - `Clear History` (inside the submenu + button in Options)
  - Pinned favorites deferred (recent covers the requested 1-click flow).
- [x] **Configurable Notifications:**
  - `QuickDllInject.NotifyOnSuccess` (default ON): tray balloon via `PhShowIconNotification` on success.
  - Silent mode = toggle OFF: notify only on errors (failures always show a status dialog).
- [x] **Execution Timeout Configuration:**
  - Wired end-to-end: `QuickDllInject.InjectionTimeoutMs` is now honored by the custom engine.
  - Deliberate deviation: no infinite option — injection runs on the UI thread, an unbounded wait would hang System Informer. Range stays 1000–60000 ms.
- [x] **Custom injection engine (`inject.c`):**
  - Required because `PhLoadDllProcess`/`PhLoadDllProcessApcThread` are NOT exported to plugins (only `PhUiLoadDllProcess` is, with hardcoded remote-thread + 5000 ms).
  - Implements both Phase-1 methods with ToolHelp-resolved remote `LoadLibraryW` (+ `RtlExitUserThread` for APC), honoring method/timeout/confirm settings, with WOW64↔native bitness-mismatch guard.

---

## 📌 Phase 3: Stealth & Evasion Capabilities — ✅ DONE (v1.3)

- [x] **Erase PE Headers:**
  - Post-injection step zeroes exactly `SizeOfHeaders` bytes (validated DOS + NT signatures first).
  - Hinders basic user-mode scanners/dumpers. Caveat: also disables `GetProcAddress` against the module inside the target (documented in code).
- [x] **Unlink from PEB (Module Hiding):**
  - Walks the target's `PEB_LDR_DATA` load-order list, matches `DllBase`, unhooks all three lists (load/memory/init order).
  - Hidden from ToolHelp/`EnumProcessModules`. Limitations: loader hash table untouched; lists edited without the loader lock (avoid injecting while the target loads other DLLs).
  - Same-bitness only (already enforced by the engine guard), so local phnt struct layouts apply.
- [x] **Thread Creation Concealment:**
  - Engine now creates threads with `NtCreateThreadEx` + `THREAD_CREATE_FLAGS_HIDE_FROM_DEBUGGER` when the new `QuickDllInject.HideThreadFromDebugger` setting is ON (all stealth toggles default OFF — opt-in).
  - Handle auto-close already satisfied in every path (process/thread/snapshot/alloc all released).
  - Deviation: no `KSystemInformer`-driver thread creation — the driver exposes no thread-creation primitive (`KphCreateUserThread` does not exist); `NtCreateThreadEx` is the correct user-mode equivalent.

---

## 📌 Phase 4: Advanced Injection Techniques

- [ ] **Standard `LoadLibraryW` Injection (Current Default):**
  - Robust `NtCreateThreadEx` + `LoadLibraryW` implementation with detailed error code reporting.
- [ ] **Kernel-Assisted Injection (`KSystemInformer` Driver):**
  - Utilize System Informer's native `kph` driver client interface (`KphOpenProcess`, `KphCreateUserThread`).
  - Allows injecting into protected or elevated processes where user-mode handle opening is denied.
- [ ] **Manual Map Injection (Stealth PE Loader):**
  - Allocate virtual memory in target address space (`VirtualAllocEx`).
  - Copy PE section headers and section data into target memory according to virtual alignments.
  - Remotely parse and resolve the Import Address Table (IAT) for target dependencies.
  - Apply base address relocations (`.reloc` directory).
  - Execute TLS callbacks (if present) and invoke `DllMain(DLL_PROCESS_ATTACH)` directly without registering the module in the Windows loader.
- [ ] **Thread Hijacking / APC Injection (`QueueUserAPC`):**
  - Suspend an existing thread or queue an Asynchronous Procedure Call (APC) to execute loader shellcode when the thread enters an alertable wait state.

---

## 📌 Phase 5: Process Watcher / Auto-Injection

- [ ] **Target Watchlist:**
  - Add a configuration list mapping target process executable names (e.g., `target.exe`) to a specific DLL path.
- [ ] **Process Creation Hook:**
  - Register callback on `GeneralCallbackProcessAdded`.
  - Automatically detect matching process spawns and inject the target DLL immediately upon process startup or while temporarily suspended.

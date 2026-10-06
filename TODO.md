# QuickDllInject - Development Roadmap & Feature TODO

This document outlines the planned roadmap for expanding **QuickDllInject** from a convenient 1-click context menu shortcut into a full-featured, professional DLL injection suite natively integrated into **System Informer**.

---

## 📌 Phase 1: Native Plugin Options Dialog

- [ ] **Register `PluginCallbackOptions`:**
  - Hook into System Informer's `PluginCallbackOptions` callback in `DllMain`.
  - Enable the native **"Options"** button under **Options -> Plugins -> Quick DLL Inject**.
- [ ] **Native Win32 Configuration Dialog:**
  - Design a lightweight, modern dialog using System Informer's UI layout utilities.
  - Implement tabs or structured group boxes: *General*, *Stealth & Evasion*, *Injection Method*, and *History*.
- [ ] **Persistent Settings Storage:**
  - Register custom plugin settings in System Informer's `settings.json` via `PhpAddIntegerSetting` and `PhpAddStringSetting`.
  - Persist user choices across restarts (selected injection method, stealth toggles, timeout, etc.).

---

## 📌 Phase 2: User Experience (UX) & Quality of Life (QoL)

- [ ] **Recent & Favorite DLLs History:**
  - Maintain a list of the last 5–10 injected DLL paths in settings.
  - Add a context submenu to the process right-click menu:
    - `Inject DLL... (Browse)`
    - `Recent DLLs ➔ [LastUsed1.dll, LastUsed2.dll, ...]`
    - `Clear History`
- [ ] **Configurable Notifications:**
  - Option to toggle native toast/balloon notifications on successful injection.
  - Option for silent mode (notify only on errors).
- [ ] **Execution Timeout Configuration:**
  - Allow user to specify remote thread execution timeout (default: 5000 ms, or infinite).

---

## 📌 Phase 3: Stealth & Evasion Capabilities

- [ ] **Erase PE Headers:**
  - Post-injection step to overwrite DOS and NT PE headers in target process memory with zeroes.
  - Obfuscates PE structure to hinder basic user-mode memory scanners and dumpers.
- [ ] **Unlink from PEB (Module Hiding):**
  - Walk the target process's `PEB_LDR_DATA` doubly linked lists:
    - `InLoadOrderModuleList`
    - `InMemoryOrderModuleList`
    - `InInitializationOrderModuleList`
  - Safely unhook the injected module node so it does not appear in standard module listings (`EnumProcessModules`, `Toolhelp32`, etc.).
- [ ] **Thread Creation Concealment:**
  - Use `NtCreateThreadEx` with `THREAD_CREATE_FLAGS_HIDE_FROM_DEBUGGER` (0x04) to prevent debugger intercepts.
  - Auto-close opened target handles immediately after injection completes.

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

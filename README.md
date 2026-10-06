# QuickDllInject - System Informer Plugin

[![Build QuickDllInject](https://github.com/Textic/QuickDllInject/actions/workflows/build.yml/badge.svg)](https://github.com/Textic/QuickDllInject/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![Target: Windows](https://img.shields.io/badge/Platform-Windows-0078D6.svg?logo=windows)](https://systeminformer.com)

A native lightweight C plugin for **System Informer** that adds a direct **"Inject DLL..."** option to the right-click context menu of any running process, bypassing the multi-step navigation (*Properties -> Modules -> Load DLL*).

---

## 🎯 Features

* **Direct 1-Click Access:** Right-click any process in the main tree view and click **Inject DLL...**.
* **Recent DLLs:** `Inject Recent DLL` submenu re-injects a previously used DLL in one click, plus `Clear History`.
* **Three Injection Methods:** Remote thread (verified load result), APC thread (stealthier), or Manual map (stealth PE loader, experimental).
* **Opt-in Stealth (Options -> Stealth):** erase PE headers, unlink module from PEB loader lists, hide injection thread from debuggers. All default OFF.
* **Native & Safe:**
  * Native file picker dialog filtered to `*.dll`.
  * Acquires required target process access via `PhOpenProcess` (driver-assisted when available).
  * WOW64/native bitness-mismatch guard with a clear error instead of a crash.
  * Shows status and error reporting natively, optional success balloon, optional confirm prompt.
* **System Process Protection:** Automatically skips protected pseudo-processes (`System`, `Idle`).
* **Options Page:** `Options -> Quick DLL Inject` with injection method (Remote thread / APC thread), remote-thread timeout and confirm-before-inject toggle. Settings persist across restarts.

---

## 🛠️ Project Structure

```
QuickDllInject/
├── .github/workflows/
│   └── build.yml               # Automated CI/CD workflow (x64, Win32, ARM64)
├── .gitignore                  # Visual Studio & MSBuild ignore rules
├── CMakeLists.txt              # CMake configuration
├── src/
│   ├── main.c                  # Plugin entry point & module init
│   ├── util.c                  # Shared helpers
│   ├── menu.c                  # Context menu + click routing
│   ├── options.c               # Options page (Options -> Quick DLL Inject)
│   ├── history.c               # Recent-DLL persistence
│   ├── engine.c                # Remote/APC thread primitives
│   ├── inject.c                # Injection orchestration
│   ├── stealth.c               # Post-inject stealth (headers, PEB, thread flags)
│   ├── manualmap.c             # Manual-map orchestration
│   ├── mapimage.c              # Local PE image preparation
│   ├── mapstub.c               # Loader stubs (x64/x86/ARM64)
│   ├── QuickDllInject.rc       # Options dialog resources
│   ├── QuickDllInject.h        # Main header and SDK definitions
│   ├── resource.h              # Command and menu item IDs
│   └── version.rc              # Binary metadata and versioning
├── QuickDllInject.rc           # Options dialog resources
├── QuickDllInject.h            # Main header and SDK definitions
├── QuickDllInject.vcxproj      # Visual Studio 2022 project file
├── QuickDllInject.vcxproj.filters
├── resource.h                  # Command and menu item IDs
├── version.rc                  # Binary metadata and versioning
└── README.md                   # Documentation
```

---

## 🚀 Building from Source

### Prerequisites
* Windows 10/11 (x64, ARM64, or x86)
* Visual Studio 2022 with C++ Desktop Development workload
* [System Informer source tree](https://github.com/winsiderss/systeminformer) (for SDK headers)

### Option 1: Visual Studio
1. Open [`plugins\Plugins.sln`](https://github.com/winsiderss/systeminformer) in Visual Studio 2022.
2. Right-click the solution -> **Add -> Existing Project...** and select `QuickDllInject.vcxproj`.
3. Select **Release** configuration and **x64** (or target platform).
4. Build the project.

### Option 2: Command Line (MSBuild)
```powershell
msbuild QuickDllInject.vcxproj /p:Configuration=Release /p:Platform=x64 /p:SpectreMitigation=false
```

---

## 📦 Installation

> [!IMPORTANT]
> System Informer (Canary / v3 / v4) restricts plugins by default to built-in factory modules. To enable third-party plugins:
> 1. In System Informer, open **Options -> Options** (`Ctrl + O`).
> 2. Switch to the **Advanced** tab.
> 3. Search for `EnableDefaultSafePlugins`.
> 4. Double-click and change the value from `1` to **`0`** (or `False`).
> 5. Click Save/OK and close System Informer.

### Deploying the Plugin:
1. Download the latest release from the [Releases](https://github.com/Textic/QuickDllInject/releases) page for your architecture (`x64`, `Win32`, or `ARM64`).
2. Copy `QuickDllInject.dll` to your System Informer plugins directory:
   ```
   C:\Program Files\SystemInformer\plugins\
   ```
   *(Requires Administrator elevation)*
3. Start **System Informer**.
4. Verify that the plugin appears in **Options -> Plugins** under **Quick DLL Inject**.

---

## 💡 Usage

1. Open **System Informer** (Run as Administrator for full process access).
2. Right-click any process in the list.
3. Click **Inject DLL...**.
4. Select the `.dll` file you wish to inject and click **Open**.

---

## 📄 License

This project is licensed under the [MIT License](LICENSE).

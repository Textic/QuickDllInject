# QuickDllInject - System Informer Plugin

[![Build QuickDllInject](https://github.com/Textic/QuickDllInject/actions/workflows/build.yml/badge.svg)](https://github.com/Textic/QuickDllInject/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![Target: Windows](https://img.shields.io/badge/Platform-Windows-0078D6.svg?logo=windows)](https://systeminformer.com)

A native lightweight C plugin for **System Informer** that adds a direct **"Inject DLL..."** option to the right-click context menu of any running process, bypassing the multi-step navigation (*Properties -> Modules -> Load DLL*).

---

## 🎯 Features

* **Direct 1-Click Access:** Right-click any process in the main tree view and click **Inject DLL...**.
* **Native & Safe:** Uses System Informer's built-in `PhUiLoadDllProcess` API:
  * Automatically opens the native file picker dialog filtered to `*.dll`.
  * Acquires required target process access (`PROCESS_VM_OPERATION`, `PROCESS_VM_WRITE`, `PROCESS_CREATE_THREAD`).
  * Injects/loads the dynamic library safely into the target address space via kernel driver or remote thread.
  * Shows status and error reporting natively.
* **System Process Protection:** Automatically skips protected pseudo-processes (`System`, `Idle`).

---

## 🛠️ Project Structure

```
QuickDllInject/
├── .github/workflows/
│   └── build.yml               # Automated CI/CD workflow (x64, Win32, ARM64)
├── .gitignore                  # Visual Studio & MSBuild ignore rules
├── CMakeLists.txt              # CMake configuration
├── main.c                      # Plugin entry point & menu callbacks
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

1. Copy the compiled `QuickDllInject.dll` to your System Informer plugins directory:
   ```
   C:\Program Files\SystemInformer\plugins\
   ```
   *(Administrator elevation required)*
2. Restart **System Informer**.
3. Verify that the plugin is enabled in **Options -> Plugins** (`QuickDllInject`).

---

## 💡 Usage

1. Open **System Informer** (Run as Administrator for full process access).
2. Right-click any process in the list.
3. Click **Inject DLL...**.
4. Select the `.dll` file you wish to inject and click **Open**.

---

## 📄 License

This project is licensed under the [MIT License](LICENSE).

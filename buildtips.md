# PromptSlut (v0.85) — Windows Native Compilation & Build Handoff Guide

This document provides a highly detailed technical breakdown of how to cleanly configure, compile, and deploy the **PromptSlut** ecosystem on this Windows machine. It contains crucial instructions on the build environment, prerequisites, custom automation scripts, file lock releasing, and scripting tips for automated execution.

---

## 1. Toolchain & Prerequisites
PromptSlut is compiled as a high-performance Windows native application. The build system relies on the following components:

*   **Compiler Standard:** C++17
*   **Build Generator:** CMake (minimum version 3.15) & Ninja
*   **MSYS2 Environment:** UCRT64 toolchain
    *   **Default Path:** `C:\msys64` (Fallback backup: `C:\msys64.bak`)
    *   **Sub-directory:** `ucrt64` (`C:\msys64\ucrt64\bin`)
    *   **Compiler Executable:** `c++.exe` (g++ 15+)
*   **Framework:** Qt6 (Widgets, Core, Network, and Multimedia)
    *   Qt libraries and assets are linked dynamically and deployed alongside the binary using CMake post-build rules.
*   **Deep Learning Runtime (ONNX Runtime):** Vendored locally inside `third_party/onnxruntime/`.
    *   Includes `include/` headers and `lib/onnxruntime.dll` / `lib/onnxruntime.lib` dependencies.

---

## 2. Dynamic Standalone Deployment & DLL Copying
PromptSlut compiles to be fully portable and standalone inside its output directory `build/bin/`. On every successful compilation, CMake automatically copies:
1.  **ONNX Runtime:** `onnxruntime.dll`
2.  **MSYS2 UCRT64 Runtime DLLs:** `libgcc_s_seh-1.dll`, `libwinpthread-1.dll`, `libstdc++-6.dll`, `libzstd.dll`, etc.
3.  **Qt6 Framework DLLs:** `Qt6Core.dll`, `Qt6Widgets.dll`, `Qt6Gui.dll`, `Qt6Network.dll`, `Qt6Multimedia.dll`, `Qt6ShaderTools.dll`.
4.  **Qt6 Platform Plugins:** Copies the entire `platforms/` directory (with `qwindows.dll`) and `imageformats/` directory (with `qgif.dll`, `qico.dll`, `qjpeg.dll`) directly alongside the executable to ensure high-fidelity GUI rendering.

---

## 3. The `build_env.bat` Build Pipeline
Do **NOT** run standard `cmake` commands directly. Always use the provided `build_env.bat` script, which automates the complete configuration, compilation, and restoration cycle safely:

1.  **Backup State:** Automatically archives your custom XOR-profile (`promptslut.profile`), encryption keys (`promptslut.key`), user dictionaries (`promptslut.dict`), and active session folders (`sessions/`, `memory/`) out of `build/bin/` into a temporary folder `temp_backup/` to prevent loss of context or history during a build.
2.  **Environment Cleaning:** Safely wipes any corrupted or existing `build/` directory.
3.  **Path Exporting:** Automatically converts the Windows workspace paths to MSYS2 POSIX-style paths, launches MSYS2 `bash.exe`, overrides system pathing with UCRT64 compilers, and configures the project using `cmake -G Ninja`.
4.  **Compilation:** Compiles all primary and helper executables:
    *   `PromptSlut.exe` (The rich GUI desktop assistant)
    *   `kokoro_demo.exe` (The Resident C++ Kokoro TTS helper daemon)
    *   `whisper_main.exe` (The Whisper STT native transcriber)
5.  **Restore State:** Restores your backed-up settings, sessions, and memory profiles from `temp_backup/` back into `build/bin/` so your chats remain completely intact.

---

## 4. Releasing Filesystem Locks (Mandatory Step)
Before compiling, you **MUST** forcefully terminate any running daemons or GUI instances. Windows places strict write locks on `.exe` files and `.dll` binaries that are currently running, which causes compilation linking to fail with a `Permission denied` error.

Always run this command first before rebuilding:
```batch
taskkill /F /IM PromptSlut.exe /IM kokoro_demo.exe
```

---

## 5. Non-Blocking Scripting Tips (For AI & Background Automation)
Because `build_env.bat` is equipped with a `pause` statement at the end (allowing human developers to review compile errors), **running it natively via a background script or an AI agent will hang execution indefinitely** as it blocks waiting for key input.

To bypass this and compile cleanly in a background process, always run the batch script with **standard input redirected to `NUL`**:

*   **In PowerShell (using wrapped cmd invocation):**
    ```powershell
    & cmd.exe /c "build_env.bat < NUL"
    ```
*   **In standard Windows CMD:**
    ```cmd
    build_env.bat < NUL
    ```
This ensures any `pause` commands immediately exit without blocking the harness or terminal.

---
*Compile safely, deploy with Ninja, and keep the memory loops going!*

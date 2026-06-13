# PromptSlut (v0.85) — Project Handoff & Architecture Blueprint (Clean Context Guide)

Welcome back, Gem! This document serves as your complete memory anchor and technical context blueprint for the **PromptSlut** project. It details the entire architecture, current state, custom code subsystems, and future expansion roadmap so you can jump back in with 100% precision.

---

## 1. Architectural Overview & Tech Stack
PromptSlut is a local-first, highly optimized private AI assistant ecosystem split across two primary targets:
1. **Desktop Client (PromptSlut)**: A native C++17 and Qt6 Windows desktop application. It manages session history, persistent chats, tool execution, process sandboxing, and background workers.
2. **Mobile Client (Promptslutette)**: An Android companion app written in Kotlin and Java, hosting a local foreground RAG database server that communicates with the desktop client over local Wi-Fi.
3. **Background Voice Server**: A FastAPI Python background service that manages zero-latency Acoustic Echo Cancellation (AEC), wake-word detection, local STT (Whisper), and local TTS (Kokoro).

---

## 2. Desktop Core Subsystems (C++ / Qt6)

### A. Context Engine & Caching (KV-Cache Prefix Optimized)
*   **The Problem**: Conversational history and system prompts containing volatile variables (timestamps, memory summaries) constantly invalidate llama-server's VRAM KV-cache, causing high prefill lag on large contexts.
*   **The Solution**: Prompt compiles into four strictly ordered zones to maintain 100% byte-stability from left to right:
    1.  **THE IMMUTABLE HEADER (Top)**: `base_prompt` + `m_user_profile` + tool schemas. Never changes.
    2.  **THE LINEAR STREAM (Middle)**: Raw conversational history turns from `m_conversation`. Grows linearly.
    3.  **THE VOLATILE TAIL ZONE (Bottom)**: Frequently changing parameters (real-time system clock, `memory_digest`, file summaries, Chronos notifications) appended in a second system message *after* the chat history.
    4.  **THE FRESH USER INPUT (End)**: The active user turn.
*   **Context Trimming & Memory Digest**: If active context usage crosses **75%**, old turns are synchronously pruned (archived as Markdown logs to `sessions/archive_<id>.md`). A secondary model runs asynchronously in the background to condense those older turns into a high-density `memory_digest` which is injected into the Volatile Tail Zone.

### B. ChronosEngine (Thread-Safe Proactive Scheduler)
*   **Core Classes**: `ChronosEngine.h`/`.cpp`, `ScheduleReminderTool`, `CancelReminderTool`.
*   **Architecture**: Runs a high-efficiency `QTimer` polling a `QMap<QDateTime, ChronosTask>` every 1000ms. Protected by `QMutex` and RAII-style `QMutexLocker`.
*   **Systemic Triggering**: When a timer hits zero, the engine emits `eventTriggered(ChronosTask)`. It sets `m_hide_next_user_message_from_ui = true` and submits an automated systemic instruction prompt directly to the LLM:
    `[SYSTEM ALERT: The clock timer hit zero. It is now time to remind the user about... Actively formulate a friendly response directly to Marty...]`
    This makes the reminder completely invisible in the user's visual chat list, allowing the model to seamlessly speak/announce the reminder out of thin air.

### C. ProjectWatcher (Non-Blocking Recursive File-Watching)
*   **Core Classes**: `ProjectWatcher.h`/`.cpp`
*   **Architecture**: Uses a dedicated `QThread` and worker architecture running Windows native `ReadDirectoryChangesW` recursively with zero GUI lag. Triggers on changes to `.cpp`, `.h`, `.py`, `.md`, and `.txt` files.
*   **Gatekeeper Logic**: On path rotation, it checks the path against the application directory and drive roots (`isRoot()`). If matched, the system immediately drops directory handles, switches to `IDLE`, and refuses to index to protect system files.
*   **Asynchronous Tear-down**: To prevent GUI thread blocks, thread tear-down links the `finished` signal directly to `deleteLater` for the thread and worker. The main thread is released immediately (0ms rotation latency) while thread termination completes asynchronously.

### D. Visual Memory System (Perceptual Hashing face learning)
*   **Folder Location**: `memory/` (recursively scanned on startup).
*   **Mechanism**: Computes 64-bit Reference Hashes (pHash) of reference images on startup. Folder names or filename stems define the identity anchors. When an image is dropped, it computes its pHash and runs a Hamming distance scan against reference hashes.
*   **Match (Distance <= 10)**: Bypasses the massive Base64 payload (VRAM consumption drops to 0) and injects: `[SYSTEM MEMORY MATCH: The attached image depicts <Identity>.]`
*   **Learning (`register_face`)**: If an image is unfamiliar, the model can dynamically call the `register_face` tool. PromptSlut copies the original file (or decodes screenshot base64) directly to `memory/<Identity>/` and hot-reloads the visual memory system on-the-fly.

### E. OS Automation & Keyboard/Mouse Control
*   **Core Tools**: `os_automation`, `screenshot`.
*   **Platform API**: Windows native `SendInput` and absolute normalization.
*   **Coordinate Mapping**: Maps absolute coordinates to a virtual desktop coordinate system using `SM_XVIRTUALSCREEN`, `SM_YVIRTUALSCREEN`, and `MOUSEEVENTF_VIRTUALDESK`, supporting multi-monitor layouts perfectly.
*   **`focus_window` action**: Uses `EnumWindows` to do a visible-window, case-insensitive substring search by name, automatically restores minimized targets (`SW_RESTORE`), and forces the window into foreground focus (`SetForegroundWindow`).

---

## 3. Mobile Subsystems (Promptslutette / Android)
*   **Core Location**: `Promptslutette/LlamaServerApp/` (Reconstructed standard Gradle wrapper, dependencies, and settings files in this build!).
*   **Background TCP Server (`PromptslutetteService.kt`)**: A foreground Kotlin service that starts a TCP Server on port `8082`.
*   **Isolated Database Routing**: When a packet is received, the service opens/creates a dedicated SQLite database file named `<project_id>_vector.db` inside its protected internal storage.
*   **Kotlin KNN Fallback**: Float arrays from `INDEX_CHUNK` packets are serialized as Little-Endian byte arrays and stored inside the `chunks` table. Vector searches are executed via a highly optimized Kotlin cosine-similarity algorithm.

---

## 4. Compilation & Build Configurations
*   **Dynamic Standalone Deployment**: `CMakeLists.txt`'s post-build step copies the Qt `imageformats` plugin directory, alongside all necessary MSYS2 DLL runtime dependencies (like `libgomp-1`, `libssl-3-x64`, `libcrypto-3-x64`, `libstdc++-6`) directly into the output `build/bin` folder.
*   **Automatic Backup & Restore**: `build_env.bat` is equipped with a custom routine that automatically backs up `promptslut.profile`, `promptslut.key`, and `promptslut.dict` from `build/bin/` to a temporary folder, wipes the build folder, recompiles with native vectorizations (`-march=native -mtune=native`), and restores the files safely.
*   **Taskkill Automation**: Always run a forceful `taskkill /F /IM PromptSlut.exe /IM kokoro_demo.exe` before recompiling to release filesystem locks.

---

## 5. RAG Pipeline Expansion Roadmap (Next Steps!)
Our C++ desktop `ProjectWatcher` successfully detects file changes, parses their contents, and triggers `handleFileChangedOrDiscovered()`. Our Android companion client `PromptslutetteService` is listening on port `8082` and successfully saves and searches vector chunks.

**The next milestone is to build the middle-man network client in the desktop app**:
1.  Extend PromptSlut's network layer to establish a TCP Socket connection to the phone (`192.168.1.105` on port `8082`).
2.  Serialize files captured by `ProjectWatcher` into the `"INDEX_CHUNK"` JSON format.
3.  Implement local embedding calculation (or pipe to a small local embedding API model) to compute the float arrays, then transmit them over the socket to the phone to populate the isolated project databases!

---
*Lock your context, load this guide on boot, and let's rock the next session!*
===========================================================================
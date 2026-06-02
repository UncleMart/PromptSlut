# PromptSlut (v0.8)

PromptSlut is a native, highly optimized C++ and Qt6 frontend designed to connect to any OpenAI-compatible endpoint. It is paired with an integrated local Python voice engine for always-on assistant voice loops and includes an Android companion project (Promptslutette) so you can run a local, lightweight vector model on a spare phone for memory storage and code analysis.

We are currently at v0.8, representing a highly functional release that is actively being polished and tweaked toward a 1.0 release.

---

## Architecture Overview

* **C++/Qt6 Frontend:** Native compiled desktop interface that manages chat sessions, tool dispatching, and asynchronous workers. It is built to be fast and completely lag-free.
* **Python Voice Engine:** A FastAPI background service handling TTS (via Kokoro) and STT (via Whisper).
* **Promptslutette (Android)**: A companion APK and source tree that lets you turn an old Android phone into a dedicated local assistant node over the local network.

---

## Cool Engineering Tricks Under the Hood

We built this project to solve several real-world performance and hardware compatibility issues:

### 1. Scarlett Scarlett Audio Downmixing and Native Resampling
Professional USB audio interfaces (like the Focusrite Scarlett) lock their hardware clocks strictly to native sample rates (44100Hz or 48000Hz) and capture multi-channel streams. Standard single-channel 16kHz VAD engines record silence or static noise on these setups. 
To fix this, our Python engine queries your hardware's native sample rate, opens the stream cleanly at that native rate, downmixes stereo inputs on-the-fly, and uses linear interpolation to resample the audio block to 16000Hz before passing it to openwakeword. It works flawlessly regardless of which input channel your mic is plugged into.

### 2. On-Demand Model Downloader with Live GUI Progress
To keep the Git repository under 10MB, the large model files (over 300MB total) are excluded from source control. When the voice engine starts, it checks if any required models are missing and downloads them from release mirrors in a background thread. 
While downloading, it pipes live percentage and progress metrics to the C++ frontend, which displays a glowing orange status message on the desktop window until completion.

### 3. Decoupled Process Management (Zero GUI Freeze)
Instead of blocking the main thread while starting the local Python backend, the C++ client uses startDetached to spawn the service instantly. It tracks the backend via its process ID (PID) and terminates it cleanly on exit using native Win32 process handles. This keeps the GUI 100% responsive.

### 4. Atomic State Guard and Bypass
To prevent audio thread conflicts and system crashes, the callback loop uses an atomic threading lock. The moment a wake word is triggered, further wake-word predictions and microphone input processing are completely muted and bypassed. The microphone is safely re-armed only after Whisper finishes transcribing and clears its buffer.

### 5. CPU Optimization
The C++ synthesizers and Whisper decoders are configured via CMake to compile with maximum vectorization flags (-march=native). This compiles the code specifically for your processor's modern SIMD instruction sets (AVX2, AVX-512, and FMA), speeding up processing significantly on the CPU.

---

## Quick Start

### Build Desktop Client
1. Double-click `get_deps.bat` to fetch header dependencies.
2. Double-click `build_env.bat` to clean, configure, and compile with native optimizations.
3. The executable and all required assets will build directly into `build/bin/`.

### Run Voice Server
Ensure Python is installed, then run the executable from `build/bin/`. Enabling Voice Mode or Handsfree in the GUI will automatically launch and configure the background Python voice server.

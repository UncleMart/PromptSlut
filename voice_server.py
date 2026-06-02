import os
import sys
import json
import subprocess
import time
import socket
import uuid
import shutil
import threading
from fastapi import FastAPI, UploadFile, File, Form, HTTPException
from fastapi.responses import Response
from pydantic import BaseModel
from typing import Optional

# Force console outputs to UTF-8 to prevent 'charmap' encoding errors on Windows
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

app = FastAPI(title="PromptSlut Unified Audio Server", version="1.0.0")

# ---------------------------------------------------------------------------
# CONFIGURATION & DEFAULT SETTINGS
# ---------------------------------------------------------------------------
CONFIG_FILE = "voice_server.json"

DEFAULT_CONFIG = {
    "engine": "cpp_daemons",  # "cpp_daemons" (highly optimized native C++ binaries)
    
    # Model and Vocab Paths
    "models": {
        "kokoro_onnx": "models/kokoro-v1.1-zh.onnx",
        "voices_bin": "models/voices-v1.1-zh.bin",
        "vocab_txt": "dict/vocab.txt",
        "whisper_model": "models/ggml-tiny.bin"
    }
}

config = DEFAULT_CONFIG.copy()

if os.path.exists(CONFIG_FILE):
    try:
        with open(CONFIG_FILE, "r", encoding="utf-8") as f:
            user_config = json.load(f)
            for k, v in user_config.items():
                if isinstance(v, dict) and k in config:
                    config[k].update(v)
                else:
                    config[k] = v
        print(f"[Config] Loaded configurations from {CONFIG_FILE}")
    except Exception as e:
        print(f"[Config] Error reading {CONFIG_FILE}, using defaults. Error: {e}")
else:
    try:
        with open(CONFIG_FILE, "w", encoding="utf-8") as f:
            json.dump(config, f, indent=4)
        print(f"[Config] Created default configuration file at {CONFIG_FILE}")
    except Exception as e:
        print(f"[Config] Failed to write default config file: {e}")

# ---------------------------------------------------------------------------
# EXECUTABLE LOCATOR
# ---------------------------------------------------------------------------
def find_executable(name: str) -> Optional[str]:
    paths = [
        ".",
        "./build/bin",
        "./new/build/bin",
        "./bin"
    ]
    for p in paths:
        exe_path = os.path.join(p, name)
        if os.name == "nt" and not exe_path.endswith(".exe"):
            exe_path += ".exe"
        if os.path.exists(exe_path) and os.path.isfile(exe_path):
            return os.path.abspath(exe_path)
    
    # Check system PATH
    import shutil
    return shutil.who_find(name) if hasattr(shutil, "who_find") else None

# ---------------------------------------------------------------------------
# RESIDENT TTS DAEMON (KOKORO_DEMO)
# ---------------------------------------------------------------------------
kokoro_process = None

def start_kokoro_daemon():
    global kokoro_process
    
    kokoro_exe = find_executable("kokoro_demo")
    if not kokoro_exe:
        print("[TTS Error] kokoro_demo.exe executable NOT found in build directories!")
        return False
        
    print(f"[TTS] Launching background C++ Resident Daemon: {kokoro_exe}")
    
    cmd = [
        kokoro_exe,
        config["models"]["kokoro_onnx"],
        config["models"]["voices_bin"],
        config["models"]["vocab_txt"],
        "RESIDENT"
    ]
    
    try:
        kokoro_process = subprocess.Popen(
            cmd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
            encoding="utf-8"
        )
        
        # Wait for "LOADED_AND_READY" signal
        ready = False
        start_time = time.time()
        while time.time() - start_time < 45: # Up to 45 seconds to load models
            line = kokoro_process.stdout.readline().strip()
            if line:
                print(f"[TTS Resident Output] {line}")
                if "LOADED_AND_READY" in line:
                    ready = True
                    break
            else:
                time.sleep(0.1)
                
        if ready:
            print("[TTS] C++ Resident Daemon initialized and ready!")
            return True
        else:
            print("[TTS Error] C++ Resident Daemon timed out during startup!")
            kokoro_process.kill()
            kokoro_process = None
            return False
            
    except Exception as e:
        print(f"[TTS Error] Failed to launch C++ Resident Daemon: {e}")
        kokoro_process = None
        return False

def stop_kokoro_daemon():
    global kokoro_process
    if kokoro_process:
        print("[TTS] Stopping C++ Resident Daemon...")
        try:
            kokoro_process.stdin.write("EXIT\n")
            kokoro_process.stdin.flush()
            kokoro_process.wait(timeout=3)
        except Exception:
            kokoro_process.kill()
        kokoro_process = None

# ---------------------------------------------------------------------------
# WAKE-WORD & VAD BACKGROUND THREAD LOOP
# ---------------------------------------------------------------------------
wakeword_enabled = False
wakeword_triggered = False
transcription_buffer = ""
tts_active = False

# Atomic State Guard variables
is_transcribing = False
state_lock = threading.Lock()

def intercept_pronunciation(text: str) -> str:
    import re
    # Replace "Qwen" / "qwen" with Kokoro phonemes [kwˈɛn] (Kwen) before inference
    text = re.sub(r'\b[Qq]wen\b', '[kwˈɛn]', text)
    return text

def capture_user_query(samplerate):
    global transcription_buffer, wakeword_triggered, is_transcribing
    
    # Sleep 400ms to let the audio wake-up chirp finish playing from speakers
    time.sleep(0.4)
    
    print("[VAD] Listening for user speech...")
    try:
        try:
            import sounddevice as sd
            import numpy as np
        except ImportError:
            print("[VAD ERROR] sounddevice or numpy not installed. Cannot run voice capture.")
            return
            
        # Record chunks of audio and monitor speech energy (RMS)
        speech_detected = False
        speech_chunks = []
        
        silence_threshold = 0.015  # Energy threshold for condenser mic
        chunk_samples = 1024
        silence_chunks = 0
        max_silence_chunks = 47    # ~3.0 seconds of silence at 16kHz
        
        # Determine optimal query channels & hardware native samplerate (supporting multi-channel Focusrite input downmixing)
        try:
            device_info = sd.query_devices(kind='input')
            max_channels = int(device_info.get('max_input_channels', 1))
            query_channels = min(max_channels, 2)
            native_sr = int(device_info.get('default_samplerate', 16000))
        except Exception:
            query_channels = 1
            native_sr = 16000
            
        # Temporary synchronous stream to capture speech
        try:
            with sd.InputStream(samplerate=native_sr, channels=query_channels, blocksize=chunk_samples) as s:
                start_time = time.time()
                while True:
                    raw_chunk, overflowed = s.read(chunk_samples)
                    # Downmix multi-channel to mono
                    if query_channels > 1:
                        mono_chunk = raw_chunk.mean(axis=1)
                    else:
                        mono_chunk = raw_chunk[:, 0]
                        
                    # Resample on-the-fly from native_sr to 16000Hz!
                    if native_sr != 16000:
                        duration = len(mono_chunk) / native_sr
                        num_target_samples = int(duration * 16000)
                        x_orig = np.linspace(0, duration, len(mono_chunk), endpoint=False)
                        x_target = np.linspace(0, duration, num_target_samples, endpoint=False)
                        mono_chunk = np.interp(x_target, x_orig, mono_chunk)
                        
                    chunk = mono_chunk.reshape(-1, 1)
                    rms = np.sqrt(np.mean(chunk**2))
                    
                    if rms > silence_threshold:
                        if not speech_detected:
                            print("[VAD] Speech started.")
                            speech_detected = True
                        speech_chunks.append(chunk)
                        silence_chunks = 0
                    else:
                        if speech_detected:
                            speech_chunks.append(chunk)
                            silence_chunks += 1
                            if silence_chunks >= max_silence_chunks:
                                print("[VAD] Speech ended (silence timeout).")
                                break
                        if not speech_detected and (time.time() - start_time > 5.0):
                            print("[VAD] Timeout. No speech detected.")
                            return
        except Exception as e:
            print(f"[VAD ERROR] Audio stream failed: {e}")
            return
                        
        if speech_chunks:
            # Join chunks into single array
            audio_data = np.concatenate(speech_chunks, axis=0)
            
            # Save to temporary WAV
            temp_query_wav = f"audio_cache/temp_query_{uuid.uuid4().hex}.wav"
            temp_query_path = os.path.abspath(temp_query_wav)
            try:
                import soundfile as sf
                sf.write(temp_query_path, audio_data, samplerate)
            except Exception as e:
                print(f"[VAD ERROR] Failed to write temporary query WAV: {e}")
                return
                
            # Transcribe
            whisper_exe = find_executable("whisper_main")
            if whisper_exe:
                print(f"[VAD] Transcribing recorded voice file via native: {whisper_exe}")
                cmd = [
                    whisper_exe,
                    "-m", config["models"]["whisper_model"],
                    "-f", temp_query_path,
                    "--no-timestamps"
                ]
                result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8")
                if result.returncode == 0:
                    text = result.stdout.strip()
                    print(f"[WakeWord Query] Transcribed: '{text}'")
                    transcription_buffer = text
                else:
                    print(f"[WakeWord STT Error] {result.stderr}")
            try:
                os.remove(temp_query_path)
            except Exception:
                pass
    finally:
        # ALWAYS re-arm the wake word detector under all circumstances
        print("[WakeWord] Re-arming wake word detector.")
        wakeword_triggered = False
        with state_lock:
            is_transcribing = False

def wakeword_thread_loop():
    global wakeword_enabled, tts_active
    
    try:
        import sounddevice as sd
        import numpy as np
        import openwakeword
    except ImportError as e:
        print(f"[WakeWord ERROR] Missing python packages: {e}. Thread aborted.")
        wakeword_enabled = False
        return
        
    # Dynamically scan for hey_qwen.onnx
    model_path = find_executable("models/hey_qwen.onnx") or "models/hey_qwen.onnx"
    if not os.path.exists(model_path):
        # Graceful fallback to default openwakeword model so we don't crash
        fallback_path = find_executable("models/alexa_v0.1.onnx") or "models/alexa_v0.1.onnx"
        if not os.path.exists(fallback_path):
            fallback_path = find_executable("models/hey_jarvis_v0.1.onnx") or "models/hey_jarvis_v0.1.onnx"
            
        if os.path.exists(fallback_path):
            print(f"[WakeWord Warning] Custom models/hey_qwen.onnx NOT found! Gracefully falling back to ONNX model: {fallback_path}")
            model = openwakeword.Model(wakeword_models=[fallback_path])
            active_word = list(model.models.keys())[0] if model.models else "alexa"
        else:
            print("[WakeWord Error] No precompiled ONNX wake-word models found in 'models/'. Listening disabled.")
            wakeword_enabled = False
            return
    else:
        model = openwakeword.Model(wakeword_models=[model_path])
        # Retrieve the key name of the loaded model
        active_word = list(model.models.keys())[0] if model.models else "hey_qwen"
        
    print(f"[WakeWord] Listening thread started. Active keyword: '{active_word}'")
    
    chunk_size = 1280
    samplerate = 16000
    
    # Determine optimal wake-word channels & hardware native samplerate (supporting multi-channel Focusrite input downmixing)
    try:
        device_info = sd.query_devices(kind='input')
        max_channels = int(device_info.get('max_input_channels', 1))
        wakeword_channels = min(max_channels, 2)
        native_sr = int(device_info.get('default_samplerate', 16000))
    except Exception:
        wakeword_channels = 1
        native_sr = 16000

    # Internal raw audio buffer to accumulate samples for strict 1280 size constraint
    audio_buffer = np.zeros(0, dtype=np.int16)

    # Callback to process microphone chunks
    def callback(indata, frames, time_info, status):
        nonlocal audio_buffer
        global is_transcribing
        
        if status:
            print(f"[Audio Stream Status] {status}")
            
        if not wakeword_enabled:
            return
            
        if tts_active:
            # Subtractive Gating: Mute microphone capture array entirely while TTS is playing!
            indata.fill(0.0)
            
        with state_lock:
            if is_transcribing:
                # Bypass and mute the openWakeWord prediction routines
                audio_buffer = np.zeros(0, dtype=np.int16)
                return
            
        # Convert float32 input to mono float array
        if indata.shape[1] > 1:
            mono_float = indata.mean(axis=1)
        else:
            mono_float = indata[:, 0]
            
        # Resample on-the-fly from native_sr to 16000Hz!
        if native_sr != 16000:
            duration = len(mono_float) / native_sr
            num_target_samples = int(duration * 16000)
            x_orig = np.linspace(0, duration, len(mono_float), endpoint=False)
            x_target = np.linspace(0, duration, num_target_samples, endpoint=False)
            mono_float = np.interp(x_target, x_orig, mono_float)
            
        audio_data = (mono_float * 32767).astype(np.int16)
        
        # Accumulate in buffer
        audio_buffer = np.concatenate((audio_buffer, audio_data))
        
        # Enforce strict 1280-samples slice processing
        while len(audio_buffer) >= 1280:
            chunk = audio_buffer[:1280]
            audio_buffer = audio_buffer[1280:]
            
            # Safe, strict-dimension inference call
            prediction = model.predict(chunk)
            
            # Detect if probability exceeds threshold
            for k, v in prediction.items():
                if v > 0.5:
                    print(f"[WakeWord] Wake word detected: '{k}' with confidence {v:.2f}")
                    # Transition to active speech capture (VAD)
                    with state_lock:
                        if is_transcribing:
                            print("[WakeWord] Drop execution: already transcribing.")
                            break
                        is_transcribing = True
                    
                    # Play pleasant wake chirp instantly in a separate thread
                    try:
                        import winsound
                        threading.Thread(target=lambda: (winsound.Beep(880, 100), winsound.Beep(1109, 120)), daemon=True).start()
                    except Exception as chirp_err:
                        print(f"[WakeWord Warning] Failed to play chirp beep: {chirp_err}")
                        
                    threading.Thread(target=capture_user_query, args=(samplerate,), daemon=True).start()
                
    try:
        with sd.InputStream(samplerate=native_sr, channels=wakeword_channels, callback=callback):
            while wakeword_enabled:
                time.sleep(0.1)
    except Exception as e:
        print(f"[WakeWord ERROR] Audio input stream failed: {e}")
        wakeword_enabled = False
        
    print("[WakeWord] Listening thread stopped.")

# ---------------------------------------------------------------------------
# APPLICATION LIFESPAN & SERVICES
# ---------------------------------------------------------------------------
REQUIRED_LARGE_MODELS = [
    ("models/kokoro-v1.1-zh.onnx", "https://github.com/koth/kokoro.cpp/releases/download/voices_model_files/kokoro-v1.1-zh.onnx"),
    ("models/voices-v1.1-zh.bin", "https://github.com/koth/kokoro.cpp/releases/download/voices_model_files/voices-v1.1-zh.bin"),
    ("models/ggml-tiny.bin", "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.bin")
]

model_download_status = ""
models_downloading = False

def download_file_with_progress(url, dest_path, label):
    global model_download_status
    import urllib.request
    
    os.makedirs(os.path.dirname(dest_path), exist_ok=True)
    try:
        with urllib.request.urlopen(url) as response, open(dest_path, 'wb') as out_file:
            meta = response.info()
            file_size = int(meta.get("Content-Length", 0))
            downloaded = 0
            block_size = 65536
            while True:
                buffer = response.read(block_size)
                if not buffer:
                    break
                downloaded += len(buffer)
                out_file.write(buffer)
                if file_size > 0:
                    percent = downloaded * 100 / file_size
                    model_download_status = f"Downloading {label}: {percent:.1f}% ({downloaded / (1024*1024):.1f}MB / {file_size / (1024*1024):.1f}MB)"
                    print(f"\r{model_download_status}", end="", flush=True)
            print()
    except Exception as e:
        model_download_status = f"Error downloading {label}: {e}"
        print(f"\n[Model Download Error] {e}")

def background_model_downloader():
    global model_download_status, models_downloading
    
    missing_models = []
    for rel_path, url in REQUIRED_LARGE_MODELS:
        if not os.path.exists(rel_path):
            missing_models.append((rel_path, url))
            
    if missing_models:
        models_downloading = True
        print(f"[Models] Found {len(missing_models)} missing models! Starting background download...")
        for rel_path, url in missing_models:
            label = os.path.basename(rel_path)
            download_file_with_progress(url, rel_path, label)
            
        print("[Models] All background downloads complete!")
        models_downloading = False
        model_download_status = ""
        # Now start kokoro daemon since models are ready!
        start_kokoro_daemon()
    else:
        print("[Models] All core models are present. Normal startup.")
        start_kokoro_daemon()

@app.on_event("startup")
def startup_event():
    # Save PID to file for safe, target-specific process terminations
    with open("voice_server.pid", "w") as f:
        f.write(str(os.getpid()))
    os.makedirs("audio_cache", exist_ok=True)
    
    # Run model downloader and initialisation in background
    threading.Thread(target=background_model_downloader, daemon=True).start()

@app.on_event("shutdown")
def shutdown_event():
    stop_kokoro_daemon()

# ---------------------------------------------------------------------------
# API MODELS & ROUTERS
# ---------------------------------------------------------------------------
class SpeechRequest(BaseModel):
    input: str
    voice: Optional[str] = "af_maple"
    response_format: Optional[str] = "wav"
    speed: Optional[float] = 1.0

@app.get("/health")
def health_check():
    return {
        "status": "healthy",
        "engine": "cpp_daemons",
        "tts_ready": kokoro_process is not None,
        "stt_ready": find_executable("whisper_main") is not None,
        "wakeword_active": wakeword_enabled
    }

@app.post("/v1/audio/speech")
def text_to_speech(req: SpeechRequest):
    global kokoro_process
    
    if not kokoro_process:
        if not start_kokoro_daemon():
            raise HTTPException(status_code=500, detail="C++ TTS Resident Daemon is not running.")
            
    temp_wav_name = f"audio_cache/temp_tts_{uuid.uuid4().hex}.wav"
    temp_wav_path = os.path.abspath(temp_wav_name)
    
    voice = req.voice if req.voice else "af_maple"
    speed_str = f"{req.speed:.1f}"
    
    # Apply the Qwen pronunciation text intercept mapping!
    normalized_input = intercept_pronunciation(req.input)
    
    # Command Format: <output_wav>|<voice>|<speed>|<text>\n
    command = f"{temp_wav_path}|{voice}|{speed_str}|{normalized_input}\n"
    print(f"[TTS] Requesting synthesis: {normalized_input[:60]}...")
    
    try:
        kokoro_process.stdin.write(command)
        kokoro_process.stdin.flush()
        
        success = False
        start_time = time.time()
        while time.time() - start_time < 15:
            line = kokoro_process.stdout.readline().strip()
            if line:
                print(f"[TTS stdout] {line}")
                if "SUCCESS" in line:
                    success = True
                    break
                elif "FAILED" in line:
                    break
            else:
                time.sleep(0.05)
                
        if success and os.path.exists(temp_wav_path):
            with open(temp_wav_path, "rb") as f:
                wav_bytes = f.read()
            try:
                os.remove(temp_wav_path)
            except Exception:
                pass
            return Response(content=wav_bytes, media_type="audio/wav")
        else:
            raise HTTPException(status_code=500, detail="C++ TTS Daemon failed to generate audio.")
            
    except Exception as e:
        print(f"[TTS Error] Exception during request handling: {e}")
        stop_kokoro_daemon()
        raise HTTPException(status_code=500, detail=f"C++ TTS Daemon error: {e}")

@app.post("/v1/audio/transcriptions")
@app.post("/inference")
def speech_to_text(file: UploadFile = File(...), temperature: Optional[float] = Form(0.0)):
    whisper_exe = find_executable("whisper_main")
    if not whisper_exe:
        raise HTTPException(status_code=500, detail="whisper_main.exe executable not found.")
        
    temp_wav_name = f"audio_cache/temp_stt_{uuid.uuid4().hex}.wav"
    temp_wav_path = os.path.abspath(temp_wav_name)
    
    try:
        with open(temp_wav_path, "wb") as f:
            f.write(file.file.read())
            
        # Automatically downmix and resample audio to 16000Hz mono if needed!
        try:
            import soundfile as sf
            import numpy as np
            samples, sr = sf.read(temp_wav_path)
            
            needs_rewrite = False
            if len(samples.shape) > 1:
                print(f"[STT] Converting multi-channel audio to mono...")
                samples = samples.mean(axis=1)
                needs_rewrite = True
                
            if sr != 16000:
                print(f"[STT] Resampling audio from {sr}Hz to 16000Hz...")
                duration = len(samples) / sr
                num_target_samples = int(duration * 16000)
                x_orig = np.linspace(0, duration, len(samples))
                x_target = np.linspace(0, duration, num_target_samples)
                samples = np.interp(x_target, x_orig, samples)
                sr = 16000
                needs_rewrite = True
                
            if needs_rewrite:
                sf.write(temp_wav_path, samples, 16000, format='WAV', subtype='PCM_16')
                print("[STT] Audio successfully pre-processed to 16kHz mono WAV.")
        except Exception as resample_err:
            print(f"[STT Warning] Pre-processing/resampling failed: {resample_err}")
            
        print(f"[STT] Spawning native transcription: {whisper_exe} on {temp_wav_name}")
        
        cmd = [
            whisper_exe,
            "-m", config["models"]["whisper_model"],
            "-f", temp_wav_path,
            "--no-timestamps"
        ]
        
        result = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            timeout=15
        )
        
        try:
            os.remove(temp_wav_path)
        except Exception:
            pass
            
        if result.returncode == 0:
            transcription = result.stdout.strip()
            print(f"[STT Result] {transcription}")
            return {"text": transcription}
        else:
            print(f"[STT Error] whisper_main exited with code {result.returncode}. Stderr: {result.stderr}")
            raise HTTPException(status_code=500, detail=f"Whisper transcription failed: {result.stderr}")
            
    except Exception as e:
        print(f"[STT Error] Exception during transcription: {e}")
        try:
            os.remove(temp_wav_path)
        except Exception:
            pass
        raise HTTPException(status_code=500, detail=f"STT Exception: {e}")

@app.get("/v1/audio/transcription-buffer")
def get_transcription_buffer():
    global transcription_buffer, is_transcribing
    with state_lock:
        text = transcription_buffer
        transcription_buffer = "" # Clear buffer after reading
        return {
            "text": text,
            "is_transcribing": is_transcribing
        }

@app.post("/v1/audio/wakeword")
def set_wakeword(enabled: bool):
    global wakeword_enabled
    if enabled == wakeword_enabled:
        return {"status": "unchanged", "enabled": wakeword_enabled}
        
    wakeword_enabled = enabled
    if wakeword_enabled:
        threading.Thread(target=wakeword_thread_loop, daemon=True).start()
        print("[WakeWord] Background listener thread spawned.")
    return {"status": "success", "enabled": wakeword_enabled}

@app.post("/v1/audio/playback")
def set_playback_status(active: bool):
    global tts_active
    tts_active = active
    print(f"[Playback] Status updated. tts_active={tts_active} (Mic Gated)")
    return {"status": "success", "tts_active": tts_active}

if __name__ == "__main__":
    import uvicorn
    print("*" * 75)
    print("         PROMPTSLUT NATIVE C++ ENGINES PROXY SERVER STARTED")
    print("*" * 75)
    print(f" Port: 5001")
    print(f" Engine: NATIVE C++ DAEMONS WITH WAKEWORD & AEC SUPPORT")
    print(" OpenAI Standard TTS API: POST http://127.0.0.1:5001/v1/audio/speech")
    print(" OpenAI Standard STT API: POST http://127.0.0.1:5001/v1/audio/transcriptions")
    print("*" * 75)
    uvicorn.run(app, host="127.0.0.1", port=5001)

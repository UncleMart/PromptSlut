import os
import urllib.request
import zipfile
import shutil

def download_file(url, dest_path):
    print(f"Downloading {url} to {dest_path}...")
    os.makedirs(os.path.dirname(dest_path), exist_ok=True)
    # Simple chunk-based download with progress report
    with urllib.request.urlopen(url) as response, open(dest_path, 'wb') as out_file:
        meta = response.info()
        file_size = int(meta.get("Content-Length", 0))
        downloaded = 0
        block_size = 8192
        while True:
            buffer = response.read(block_size)
            if not buffer:
                break
            downloaded += len(buffer)
            out_file.write(buffer)
            if file_size > 0:
                percent = downloaded * 100 / file_size
                print(f"\rProgress: {percent:.1f}% ({downloaded / (1024*1024):.2f}MB / {file_size / (1024*1024):.2f}MB)", end="")
        print("\nDownload complete.")

def main():
    harness_dir = "C:\\Harness"
    third_party_dir = os.path.join(harness_dir, "third_party")
    models_dir = os.path.join(harness_dir, "models")
    dict_dir = os.path.join(harness_dir, "dict")
    
    os.makedirs(third_party_dir, exist_ok=True)
    os.makedirs(models_dir, exist_ok=True)
    os.makedirs(dict_dir, exist_ok=True)

    # 1. Download and extract ONNX Runtime
    ort_zip = os.path.join(third_party_dir, "onnxruntime.zip")
    ort_url = "https://github.com/microsoft/onnxruntime/releases/download/v1.18.0/onnxruntime-win-x64-1.18.0.zip"
    if not os.path.exists(os.path.join(third_party_dir, "onnxruntime", "lib", "onnxruntime.dll")):
        download_file(ort_url, ort_zip)
        print("Extracting ONNX Runtime...")
        with zipfile.ZipFile(ort_zip, 'r') as zip_ref:
            zip_ref.extractall(third_party_dir)
        # Rename extracted directory to 'onnxruntime'
        extracted_folder = os.path.join(third_party_dir, "onnxruntime-win-x64-1.18.0")
        target_folder = os.path.join(third_party_dir, "onnxruntime")
        if os.path.exists(target_folder):
            shutil.rmtree(target_folder)
        os.rename(extracted_folder, target_folder)
        os.remove(ort_zip)
        print("ONNX Runtime extracted successfully.")
    else:
        print("ONNX Runtime is already present.")

    # 2. Download Kokoro models and voices
    # Kokoro ONNX model
    kokoro_model_url = "https://github.com/koth/kokoro.cpp/releases/download/voices_model_files/kokoro-v1.1-zh.onnx"
    kokoro_model_path = os.path.join(models_dir, "kokoro-v1.1-zh.onnx")
    if not os.path.exists(kokoro_model_path):
        download_file(kokoro_model_url, kokoro_model_path)
    
    # Kokoro voices package
    voices_url = "https://github.com/koth/kokoro.cpp/releases/download/voices_model_files/voices-v1.1-zh.bin"
    voices_path = os.path.join(models_dir, "voices-v1.1-zh.bin")
    if not os.path.exists(voices_path):
        download_file(voices_url, voices_path)

    # 3. Download Whisper GGML Tiny model
    whisper_url = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.bin"
    whisper_path = os.path.join(models_dir, "ggml-tiny.bin")
    if not os.path.exists(whisper_path):
        download_file(whisper_url, whisper_path)

    # 4. Download G2P dictionary files
    dict_files = [
        "vocab.txt", "jieba.dict.utf8", "hmm_model.utf8", "user.dict.utf8",
        "idf.utf8", "stop_words.utf8", "pinyin.txt", "pinyin_phrase.txt"
    ]
    for dict_file in dict_files:
        dict_file_path = os.path.join(dict_dir, dict_file)
        if not os.path.exists(dict_file_path):
            url = f"https://raw.githubusercontent.com/koth/kokoro.cpp/main/dict/{dict_file}"
            download_file(url, dict_file_path)

    print("All Voice Mode dependencies downloaded and organized successfully!")

if __name__ == "__main__":
    main()

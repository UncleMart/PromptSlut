#include "Kokoro.h"
#include <iostream>
#include <vector>
#include <fstream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h> // Required for CommandLineToArgvW
#endif

void save_audio(const std::string& filename, const std::vector<float>& audio, int sample_rate) {
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open output audio file: " << filename << std::endl;
        return;
    }
    
    int channels = 1;
    int bits_per_sample = 32; // IEEE Floating Point (Float)
    int byte_rate = sample_rate * channels * bits_per_sample / 8;
    int block_align = channels * bits_per_sample / 8;
    int data_size = static_cast<int>(audio.size() * sizeof(float));
    int chunk_size = 36 + data_size;

    file.write("RIFF", 4);
    file.write(reinterpret_cast<const char*>(&chunk_size), 4);
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    
    int subchunk1_size = 16;
    short audio_format = 3; // IEEE Float
    short num_channels = channels;
    int sample_rate_int = sample_rate;
    
    file.write(reinterpret_cast<const char*>(&subchunk1_size), 4);
    file.write(reinterpret_cast<const char*>(&audio_format), 2);
    file.write(reinterpret_cast<const char*>(&num_channels), 2);
    file.write(reinterpret_cast<const char*>(&sample_rate_int), 4);
    file.write(reinterpret_cast<const char*>(&byte_rate), 4);
    file.write(reinterpret_cast<const char*>(&block_align), 2);
    short bits = bits_per_sample;
    file.write(reinterpret_cast<const char*>(&bits), 2);
    
    file.write("data", 4);
    file.write(reinterpret_cast<const char*>(&data_size), 4);
    file.write(reinterpret_cast<const char*>(audio.data()), data_size);
    
    std::cout << "Saved audio to " << filename << " successfully." << std::endl;
}

#ifdef _WIN32
std::string wide_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // Retrieve high-fidelity Unicode wide-character arguments directly on Windows
    int wargc = 0;
    wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (wargv == nullptr || wargc < 5) {
        std::cerr << "Usage: " << argv[0] << " <model_path> <voices.bin> <vocab_path> [RESIDENT | text] [output_wav] [voice_name] [speed]" << std::endl;
        if (wargv) LocalFree(wargv);
        return 1;
    }

    std::string model_path  = wide_to_utf8(wargv[1]);
    std::string voices_path = wide_to_utf8(wargv[2]);
    std::string vocab_path  = wide_to_utf8(wargv[3]);
    std::string text_arg    = wide_to_utf8(wargv[4]);
#else
    if (argc < 5) {
        std::cerr << "Usage: " << argv[0] << " <model_path> <voices.bin> <vocab_path> [RESIDENT | text] [output_wav] [voice_name] [speed]" << std::endl;
        return 1;
    }

    std::string model_path  = argv[1];
    std::string voices_path = argv[2];
    std::string vocab_path  = argv[3];
    std::string text_arg    = argv[4];
#endif

    try {
        // Initialize Kokoro ONNX model, G2P Dictionaries, and Voice package ONCE!
        Kokoro tts(model_path, voices_path, vocab_path);

        if (text_arg == "RESIDENT") {
            // ---------------------------------------------------------------------------
            // High-Performance Resident Daemon Mode (Standard Input loop)
            // ---------------------------------------------------------------------------
            std::cout << "LOADED_AND_READY" << std::endl; // Notify parent process
            
            std::string line;
            while (std::getline(std::cin, line)) {
                if (line.empty()) continue;
                if (line == "EXIT") {
                    break;
                }

                // Command Format: <output_wav_path>|<voice_name>|<speed>|<text>
                size_t p1 = line.find('|');
                if (p1 == std::string::npos) continue;
                std::string output_wav = line.substr(0, p1);

                size_t p2 = line.find('|', p1 + 1);
                if (p2 == std::string::npos) continue;
                std::string voice_name = line.substr(p1 + 1, p2 - p1 - 1);

                size_t p3 = line.find('|', p2 + 1);
                if (p3 == std::string::npos) continue;
                std::string speed_str = line.substr(p2 + 1, p3 - p2 - 1);
                std::string text = line.substr(p3 + 1);

                float speed = 1.0f;
                try { speed = std::stof(speed_str); } catch (...) {}

                try {
                    auto voice = tts.get_voice_style(voice_name);
                    auto result = tts.create(text, voice, speed);
                    save_audio(output_wav, result.first, result.second);
                    std::cout << "SUCCESS" << std::endl; // Signal completion to parent
                } catch (const std::exception& e) {
                    std::cerr << "Resident Error: " << e.what() << std::endl;
                    std::cout << "FAILED" << std::endl;
                }
            }
        } else {
            // ---------------------------------------------------------------------------
            // Standard CLI Mode (fallback)
            // ---------------------------------------------------------------------------
#ifdef _WIN32
            if (wargc < 6) {
                std::cerr << "Error: Output wav path is required in CLI mode." << std::endl;
                LocalFree(wargv);
                return 1;
            }
            std::string output_wav = wide_to_utf8(wargv[5]);
            std::string voice_name = "af_maple";
            if (wargc > 6) voice_name = wide_to_utf8(wargv[6]);
            float speed = 1.0f;
            if (wargc > 7) {
                try { speed = std::stof(wide_to_utf8(wargv[7])); } catch (...) {}
            }
#else
            if (argc < 6) {
                std::cerr << "Error: Output wav path is required in CLI mode." << std::endl;
                return 1;
            }
            std::string output_wav = argv[5];
            std::string voice_name = "af_maple";
            if (argc > 6) voice_name = argv[6];
            float speed = 1.0f;
            if (argc > 7) {
                try { speed = std::stof(argv[7]); } catch (...) {}
            }
#endif
            auto voice = tts.get_voice_style(voice_name);
            auto result = tts.create(text_arg, voice, speed);
            save_audio(output_wav, result.first, result.second);
            std::cout << "TTS Generation SUCCESS" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Initialization Error: " << e.what() << std::endl;
#ifdef _WIN32
        LocalFree(wargv);
#endif
        return 1;
    }

#ifdef _WIN32
    LocalFree(wargv);
#endif
    return 0;
}

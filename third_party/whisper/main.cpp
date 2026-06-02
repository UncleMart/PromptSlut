#include "whisper.h"
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <thread>

// Robust WAV reader that dynamically parses RIFF chunks, bypasses list/metadata headers,
// and supports both 16-bit Int and 32-bit Float PCM (with auto-conversion).
bool read_wav_file(const std::string& filepath, std::vector<float>& samples) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open input WAV file: " << filepath << std::endl;
        return false;
    }

    // Read RIFF header
    char riff[12];
    file.read(riff, 12);
    if (file.gcount() < 12 || std::string(riff, 4) != "RIFF" || std::string(riff + 8, 4) != "WAVE") {
        std::cerr << "Invalid WAV file format (missing RIFF/WAVE header)." << std::endl;
        return false;
    }

    int sample_rate = 0;
    int channels = 0;
    int bits_per_sample = 0;
    int audio_format = 0;

    // Parse chunks dynamically
    char chunk_id[4];
    uint32_t chunk_size = 0;
    bool data_found = false;

    while (file.read(chunk_id, 4)) {
        file.read(reinterpret_cast<char*>(&chunk_size), 4);
        if (file.gcount() < 4) break;

        std::string id(chunk_id, 4);
        if (id == "fmt ") {
            file.read(reinterpret_cast<char*>(&audio_format), 2);
            file.read(reinterpret_cast<char*>(&channels), 2);
            file.read(reinterpret_cast<char*>(&sample_rate), 4);
            file.seekg(6, std::ios::cur); // Skip bytes_per_sec, block_align
            file.read(reinterpret_cast<char*>(&bits_per_sample), 2);

            // Skip any remaining bytes in the fmt chunk (if chunk_size > 16)
            if (chunk_size > 16) {
                file.seekg(chunk_size - 16, std::ios::cur);
            }

            if (sample_rate != 16000) {
                std::cerr << "WAV sample rate is " << sample_rate << ", but Whisper requires exactly 16000Hz." << std::endl;
                return false;
            }
        } else if (id == "data") {
            data_found = true;
            if (bits_per_sample == 32 && audio_format == 3) {
                // 32-bit float PCM
                size_t num_samples = chunk_size / sizeof(float);
                samples.resize(num_samples);
                file.read(reinterpret_cast<char*>(samples.data()), chunk_size);
            } else if (bits_per_sample == 16 && audio_format == 1) {
                // 16-bit integer PCM - Convert to float on-the-fly!
                size_t num_samples = chunk_size / sizeof(int16_t);
                std::vector<int16_t> temp(num_samples);
                file.read(reinterpret_cast<char*>(temp.data()), chunk_size);
                samples.resize(num_samples);
                for (size_t i = 0; i < num_samples; ++i) {
                    samples[i] = static_cast<float>(temp[i]) / 32768.0f;
                }
            } else {
                std::cerr << "Unsupported WAV format: bits=" << bits_per_sample << ", format=" << audio_format << ". Use 16-bit Int or 32-bit Float PCM." << std::endl;
                return false;
            }
            break;
        } else {
            // Skip unknown/metadata chunks safely
            file.seekg(chunk_size, std::ios::cur);
        }
    }

    if (!data_found) {
        std::cerr << "Error: 'data' chunk not found in WAV file." << std::endl;
        return false;
    }

    return true;
}

int main(int argc, char** argv) {
    std::string model_path = "models/ggml-tiny.bin";
    std::string wav_path = "";

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-m" || arg == "--model") && i + 1 < argc) {
            model_path = argv[++i];
        } else if ((arg == "-f" || arg == "--file") && i + 1 < argc) {
            wav_path = argv[++i];
        }
    }

    if (wav_path.empty()) {
        std::cerr << "Usage: " << argv[0] << " -m <model_bin> -f <input_wav>" << std::endl;
        return 1;
    }

    std::vector<float> samples;
    if (!read_wav_file(wav_path, samples)) {
        return 1;
    }

    // Initialize Whisper Context
    struct whisper_context_params cparams = whisper_context_default_params();
    struct whisper_context* ctx = whisper_init_from_file_with_params(model_path.c_str(), cparams);
    if (ctx == nullptr) {
        std::cerr << "Failed to load Whisper model from file: " << model_path << std::endl;
        return 1;
    }

    // Configure Inference Parameters
    struct whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.print_progress = false;
    params.print_special  = false;
    params.print_realtime = false;
    params.print_timestamps = false;
    params.translate      = false;
    params.language       = "auto";
    params.n_threads      = std::thread::hardware_concurrency();

    // Run Whisper Inference on CPU
    if (whisper_full(ctx, params, samples.data(), samples.size()) != 0) {
        std::cerr << "Whisper inference failed." << std::endl;
        whisper_free(ctx);
        return 1;
    }

    // Extract segment text and output to stdout
    int n_segments = whisper_full_n_segments(ctx);
    for (int i = 0; i < n_segments; ++i) {
        const char* text = whisper_full_get_segment_text(ctx, i);
        std::cout << text << " ";
    }
    std::cout << std::endl;

    whisper_free(ctx);
    return 0;
}

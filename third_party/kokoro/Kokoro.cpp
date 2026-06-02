#include "Kokoro.h"
#include "Tokenizer.h"
#include <fstream>
#include <sstream>
#include <regex>
#include <numeric>
#include <cstring>
#include <thread>
#include <algorithm>

// Helper to trim audio (simple amplitude based silence removal)
std::vector<float> trim_audio(const std::vector<float>& audio, int sample_rate, float threshold_db = 60.0f) {
    // This is a simplified implementation. 
    // A proper one would compute RMS in frames.
    // For now, we just return the audio as is or do a simple amplitude trim
    return audio; 
}

Kokoro::Kokoro(const std::string& model_path, const std::string& voices_path, const std::string& vocab_path) 
    : env_(ORT_LOGGING_LEVEL_WARNING, "Kokoro")
{
    // Initialize session options
    Ort::SessionOptions session_options;
    unsigned int num_cores = std::thread::hardware_concurrency();
    unsigned int threads = num_cores > 0 ? (num_cores > 4 ? 4 : num_cores) : 4;
    session_options.SetIntraOpNumThreads(threads);
    session_options.SetInterOpNumThreads(1);
    session_options.EnableCpuMemArena();
    session_options.EnableMemPattern();
    session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    // Load model
#ifdef _WIN32
    std::wstring wmodel_path(model_path.begin(), model_path.end());
    session_ = Ort::Session(env_, wmodel_path.c_str(), session_options);
#else
    session_ = Ort::Session(env_, model_path.c_str(), session_options);
#endif

    // Load voices
    load_voices(voices_path);

    // Load vocab
    std::map<std::string, int> vocab;
    std::ifstream in(vocab_path);
    if (in.is_open()) {
        std::string line;
        while (std::getline(in, line)) {
            // Expected format: token<TAB>id
            size_t tab = line.find('\t');
            if (tab != std::string::npos) {
                std::string token = line.substr(0, tab);
                std::string id_str = line.substr(tab + 1);
                // Unescape token if needed (\n, \r, \t)
                size_t pos = 0;
                while((pos = token.find("\\n", pos)) != std::string::npos) { token.replace(pos, 2, "\n"); pos += 1; }
                pos = 0;
                while((pos = token.find("\\r", pos)) != std::string::npos) { token.replace(pos, 2, "\r"); pos += 1; }
                pos = 0;
                while((pos = token.find("\\t", pos)) != std::string::npos) { token.replace(pos, 2, "\t"); pos += 1; }
                
                try {
                    vocab[token] = std::stoi(id_str);
                } catch (...) {}
            }
        }
        std::cout << "Loaded " << vocab.size() << " tokens from " << vocab_path << std::endl;
    } else {
        std::cerr << "Warning: Failed to open vocab file: " << vocab_path << ". Tokenizer will produce empty output." << std::endl;
    }

    // Initialize Tokenizer
    tokenizer_ = std::make_unique<Tokenizer>(TokenizerConfig{}, vocab);
}

Kokoro::~Kokoro() {
    // Resources cleaned up by wrappers
}

void Kokoro::load_voices(const std::string& voices_path) {
    std::ifstream in(voices_path, std::ios::binary);
    if (!in.is_open()) {
        std::cerr << "Failed to open voices file: " << voices_path << std::endl;
        return;
    }

    char magic[4];
    in.read(magic, 4);
    if (std::strncmp(magic, "VOIC", 4) != 0) {
        std::cerr << "Invalid voices file format (Magic header mismatch). Expected 'VOIC'." << std::endl;
        std::cerr << "Please run scripts/export_voices.py to convert voices.npy to voices.bin" << std::endl;
        return;
    }

    uint32_t version;
    in.read(reinterpret_cast<char*>(&version), 4);
    if (version != 1) {
        std::cerr << "Unsupported voices file version: " << version << std::endl;
        return;
    }

    uint32_t num_voices;
    in.read(reinterpret_cast<char*>(&num_voices), 4);

    for (uint32_t i = 0; i < num_voices; ++i) {
        uint32_t name_len;
        in.read(reinterpret_cast<char*>(&name_len), 4);
        
        std::string name(name_len, '\0');
        in.read(&name[0], name_len);
        
        uint32_t dim;
        in.read(reinterpret_cast<char*>(&dim), 4);
        
        std::vector<float> style(dim);
        in.read(reinterpret_cast<char*>(style.data()), dim * sizeof(float));
        
        voices_[name] = style;
    }
    
    std::cout << "Loaded " << voices_.size() << " voices from " << voices_path << std::endl;
}

std::vector<float> Kokoro::get_voice_style(const std::string& name) {
    if (voices_.find(name) != voices_.end()) {
        return voices_.at(name);
    }
    std::cerr << "Voice " << name << " not found. Using default." << std::endl;
    if (!voices_.empty()) return voices_.begin()->second;
    return std::vector<float>(256, 0.0f);
}

std::vector<std::string> Kokoro::_split_phonemes(const std::string& phonemes) {
    std::vector<std::string> batches;
    std::regex re("([.,!?;])");
    std::sregex_token_iterator it(phonemes.begin(), phonemes.end(), re, {-1, 0}); // -1 for non-match, 0 for match
    std::sregex_token_iterator end;

    std::string current_batch;
    
    for (; it != end; ++it) {
        std::string part = *it;
        // Removing leading/trailing whitespace
        part = std::regex_replace(part, std::regex("^\\s+|\\s+$"), "");
        
        if (part.empty()) continue;

        if (current_batch.length() + part.length() + 1 >= MAX_PHONEME_LENGTH) {
            batches.push_back(current_batch);
            current_batch = part;
        } else {
             if (std::string(".,!?;").find(part) != std::string::npos) {
                current_batch += part;
             } else {
                if (!current_batch.empty()) current_batch += " ";
                current_batch += part;
             }
        }
    }
    if (!current_batch.empty()) {
        batches.push_back(current_batch);
    }
    return batches;
}

std::pair<std::vector<float>, int> Kokoro::_create_audio(
    const std::string& phonemes,
    const std::vector<float>& voice,
    float speed
) {
    std::string truncated_phonemes = phonemes;
    if (phonemes.length() > MAX_PHONEME_LENGTH) {
        truncated_phonemes = phonemes.substr(0, MAX_PHONEME_LENGTH);
    }

    std::vector<int> tokens_raw = tokenizer_->tokenize(truncated_phonemes);
    
    // Debug: print phonemes and tokens
    std::cout << "Phonemes: " << truncated_phonemes << std::endl;
    std::cout << "Tokens: ";
    for(int t : tokens_raw) std::cout << t << " ";
    std::cout << std::endl;

    // Add start and end tokens (0)
    std::vector<int64_t> tokens = {0};
    for (int t : tokens_raw) tokens.push_back(t);
    tokens.push_back(0);
    
    // Prepare inputs
    std::vector<int64_t> input_shape = {1, (int64_t)tokens.size()};
    
    // Prepare voice style
    // Voice loaded from npy is a stack of styles indexed by token length (MAX_PHONEME_LENGTH x STYLE_DIM)    
    const int STYLE_DIM = 256;
    std::vector<float> selected_style;
    if (voice.size() > STYLE_DIM) {
         size_t index = tokens_raw.size();
         if (index * STYLE_DIM + STYLE_DIM <= voice.size()) {
             auto start = voice.begin() + index * STYLE_DIM;
             selected_style.assign(start, start + STYLE_DIM);
         } else {
             // Fallback or error
             std::cerr << "Warning: Style index out of bounds. Using first style." << std::endl;
             selected_style.assign(voice.begin(), voice.begin() + STYLE_DIM);
         }
    } else {
        selected_style = voice;
    }

    std::vector<int64_t> style_shape = {1, (int64_t)selected_style.size()};
    std::vector<int64_t> speed_shape = {1};
    std::vector<float> speed_tensor = {speed};

   
    const char* input_names[] = {"tokens", "style", "speed"};
    const char* input_names_new[] = {"input_ids", "style", "speed"};
    
    // Querying model inputs is possible but let's just assume one set for this translation or use a check.
    // For brevity, I'll use the older "tokens" set as default or try to match python logic if I can access names.
    
    bool use_new_schema = false;
    size_t num_inputs = session_.GetInputCount();
    for(size_t i=0; i<num_inputs; i++) {
        auto name_ptr = session_.GetInputNameAllocated(i, allocator_);
        if (std::string(name_ptr.get()) == "input_ids") {
            use_new_schema = true;
        }
    }

    std::vector<const char*> inputs;
    if (use_new_schema) {
        inputs = {"input_ids", "style", "speed"};
    } else {
        inputs = {"tokens", "style", "speed"};
    }
    
    std::vector<Ort::Value> input_tensors;
    
    // Create tensors
    auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    
    input_tensors.push_back(Ort::Value::CreateTensor<int64_t>(
        memory_info, tokens.data(), tokens.size(), input_shape.data(), input_shape.size()));
        
    input_tensors.push_back(Ort::Value::CreateTensor<float>(
        memory_info, selected_style.data(), selected_style.size(), style_shape.data(), style_shape.size()));
    
    if (use_new_schema) {
        static int speed_int = (int)speed; 
        input_tensors.push_back(Ort::Value::CreateTensor<int>(
            memory_info, &speed_int, 1, speed_shape.data(), speed_shape.size()));
    } else {
        input_tensors.push_back(Ort::Value::CreateTensor<float>(
            memory_info, speed_tensor.data(), speed_tensor.size(), speed_shape.data(), speed_shape.size()));
    }

    // Check model output name usually
    // Or get it from session
    auto out_name_ptr = session_.GetOutputNameAllocated(0, allocator_);
    std::vector<const char*> output_names_vec = {out_name_ptr.get()};

    auto output_tensors = session_.Run(
        Ort::RunOptions{nullptr},
        inputs.data(),
        input_tensors.data(),
        input_tensors.size(),
        output_names_vec.data(),
        1
    );
    
    // allocator_.Free(out_name, allocator_.Info()); // Handled by smart pointer

    float* floatarr = output_tensors[0].GetTensorMutableData<float>();
    size_t output_len = output_tensors[0].GetTensorTypeAndShapeInfo().GetElementCount();
    
    std::vector<float> audio(floatarr, floatarr + output_len);
    return {audio, SAMPLE_RATE};
}

std::pair<std::vector<float>, int> Kokoro::create(
    const std::string& text,
    const std::vector<float>& voice_style,
    float speed,
    bool is_phonemes,
    bool trim
) {
    std::string phonemes = text;
    if (!is_phonemes) {
        phonemes = tokenizer_->phonemize(text);
    }
    
    auto batched_phonemes = _split_phonemes(phonemes);
    std::vector<float> full_audio;
    
    for (const auto& batch : batched_phonemes) {
        auto [audio_part, sr] = _create_audio(batch, voice_style, speed);
        if (trim) {
            audio_part = trim_audio(audio_part, sr);
        }
        full_audio.insert(full_audio.end(), audio_part.begin(), audio_part.end());
    }
    
    return {full_audio, SAMPLE_RATE};
}

std::pair<std::vector<float>, int> Kokoro::create(
    const std::string& text,
    const std::string& voice_name,
    float speed,
    bool is_phonemes,
    bool trim
) {
    return create(text, get_voice_style(voice_name), speed, is_phonemes, trim);
}

#include "voice_engine.h"
#include "httplib.h"
#include "nlohmann/json.hpp"
#include <windows.h>
#include <mmsystem.h>
#include <iostream>
#include <fstream>
#include <cmath>
#include <thread>
#include <future>
#include <algorithm>
#include <QCoreApplication>
#include <QDir>
#include <QUuid>
#include <QFile>
#include <QFileInfo>

static void log_voice(const std::string& msg) {
    std::ofstream file("C:/Harness/voice_debug.log", std::ios::app);
    if (file.is_open()) {
        file << msg << "\n";
    }
}

// ---------------------------------------------------------------------------
// TtsEngine Implementation
// ---------------------------------------------------------------------------
TtsEngine::TtsEngine(QObject* parent) 
    : QObject(parent), m_worker_running(false), m_playback_running(false), m_is_playing(false), m_tts_process(nullptr) {}

TtsEngine::~TtsEngine() {
    shutdown();
}

bool TtsEngine::init(const std::string& model_path, const std::string& voices_path, const std::string& vocab_path) {
    QString app_path = QCoreApplication::applicationDirPath();
    m_model_path = QDir(app_path).absoluteFilePath(QString::fromStdString(model_path)).toStdString();
    m_voices_path = QDir(app_path).absoluteFilePath(QString::fromStdString(voices_path)).toStdString();
    m_vocab_path = QDir(app_path).absoluteFilePath(QString::fromStdString(vocab_path)).toStdString();
    m_voice_name = "af_maple";

    shutdown();

#ifdef PROMPTSLUT_VOICE_MODE
    QDir(app_path).mkdir("audio_cache");
    loadUserPronunciations();
#endif

    m_worker_running = true;
    m_worker_thread = std::make_unique<std::thread>([this]() { generationWorker(); });

    m_playback_running = true;
    m_playback_thread = std::make_unique<std::thread>([this]() { playbackWorker(); });

    return true;
}

void TtsEngine::shutdown() {
    stop();

    if (m_worker_running) {
        {
            std::lock_guard<std::mutex> lock(m_queue_mutex);
            m_worker_running = false;
            m_queue_cv.notify_all();
        }
        if (m_worker_thread && m_worker_thread->joinable()) {
            m_worker_thread->join();
        }
    }

    if (m_playback_running) {
        {
            std::lock_guard<std::mutex> lock(m_audio_mutex);
            m_playback_running = false;
            m_audio_cv.notify_all();
        }
        if (m_playback_thread && m_playback_thread->joinable()) {
            m_playback_thread->join();
        }
    }

#ifdef PROMPTSLUT_VOICE_MODE
    // Clean up all cached WAV files on application exit
    QString app_path = QCoreApplication::applicationDirPath();
    QDir cache_dir(app_path + "/audio_cache");
    if (cache_dir.exists()) {
        QStringList files = cache_dir.entryList(QDir::Files);
        for (const QString& f : files) {
            cache_dir.remove(f);
        }
    }
#endif
}

void TtsEngine::speakSentence(const std::string& sentence) {
    if (sentence.empty()) return;

    std::lock_guard<std::mutex> lock(m_queue_mutex);
    m_text_queue.push_back(sentence);
    m_queue_cv.notify_one();
}

void TtsEngine::stop() {
#ifdef PROMPTSLUT_VOICE_MODE
    PlaySoundW(NULL, NULL, 0); // Instantly halts Win32 multimedia wave playback across process
#endif

    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        m_text_queue.clear();
    }

    {
        std::lock_guard<std::mutex> lock(m_audio_mutex);
        m_audio_queue.clear();
        m_is_playing = false;
        m_audio_cv.notify_all();
    }

    emit playbackStateChanged(false);
}

bool TtsEngine::isPlaying() const {
    std::lock_guard<std::mutex> lock(m_audio_mutex);
    return m_is_playing;
}

std::vector<float> TtsEngine::loadWavFile(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    std::vector<float> samples;
    if (!file.is_open()) {
        log_voice("TTS ERROR: Failed to open output WAV file: " + filepath);
        return samples;
    }

    char riff[12];
    file.read(riff, 12);
    if (file.gcount() < 12 || std::string(riff, 4) != "RIFF" || std::string(riff + 8, 4) != "WAVE") {
        log_voice("TTS ERROR: Invalid WAV file header in file: " + filepath);
        return samples;
    }

    int audio_format = 0;
    int channels = 0;
    int sample_rate = 0;
    int bits_per_sample = 0;

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
            file.seekg(6, std::ios::cur);
            file.read(reinterpret_cast<char*>(&bits_per_sample), 2);

            if (chunk_size > 16) {
                file.seekg(chunk_size - 16, std::ios::cur);
            }
        } else if (id == "data") {
            data_found = true;
            if (bits_per_sample == 32 && audio_format == 3) {
                size_t num_samples = chunk_size / sizeof(float);
                samples.resize(num_samples);
                file.read(reinterpret_cast<char*>(samples.data()), chunk_size);
            } else if (bits_per_sample == 16 && audio_format == 1) {
                size_t num_samples = chunk_size / sizeof(int16_t);
                std::vector<int16_t> temp(num_samples);
                file.read(reinterpret_cast<char*>(temp.data()), chunk_size);
                samples.resize(num_samples);
                for (size_t i = 0; i < num_samples; ++i) {
                    samples[i] = static_cast<float>(temp[i]) / 32768.0f;
                }
            } else {
                log_voice("TTS ERROR: Unsupported WAV format (bits=" + std::to_string(bits_per_sample) + ", format=" + std::to_string(audio_format) + ") in " + filepath);
                return samples;
            }
            break;
        } else {
            file.seekg(chunk_size, std::ios::cur);
        }
    }

    if (!data_found) {
        log_voice("TTS ERROR: 'data' chunk not found in WAV file: " + filepath);
    }

    return samples;
}

void TtsEngine::writeWavFileInt16(const std::string& filepath, const std::vector<float>& samples) {
    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) return;

    int sample_rate = 24000;
    int channels = 1;
    int bits_per_sample = 16;
    int byte_rate = sample_rate * channels * bits_per_sample / 8;
    int block_align = channels * bits_per_sample / 8;
    
    std::vector<int16_t> int16_samples;
    int16_samples.reserve(samples.size());
    for (float sample : samples) {
        if (sample > 1.0f) sample = 1.0f;
        else if (sample < -1.0f) sample = -1.0f;
        int16_samples.push_back(static_cast<int16_t>(sample * 32767.0f));
    }

    int data_size = static_cast<int>(int16_samples.size() * sizeof(int16_t));
    int chunk_size = 36 + data_size;

    file.write("RIFF", 4);
    file.write(reinterpret_cast<const char*>(&chunk_size), 4);
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    
    int subchunk1_size = 16;
    short audio_format = 1; // PCM
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
    file.write(reinterpret_cast<const char*>(int16_samples.data()), data_size);
}

static std::string expandContractions(const std::string& text) {
    std::string result = text;
    struct Replacement {
        std::string contraction;
        std::string expansion;
    };
    
    std::vector<Replacement> replacements = {
        {"I'm", "Im"}, {"i'm", "im"},
        {"it's", "its"}, {"It's", "Its"},
        {"that's", "thats"}, {"That's", "Thats"},
        {"there's", "theres"}, {"There's", "Theres"},
        {"what's", "whats"}, {"What's", "Whats"},
        {"let's", "lets"}, {"Let's", "Lets"},
        {"here's", "heres"}, {"Here's", "Heres"},
        {"how's", "hows"}, {"How's", "Hows"},
        {"who's", "whos"}, {"Who's", "Whos"},
        {"we're", "we are"}, {"We're", "We are"},
        {"you're", "youre"}, {"You're", "Youre"},
        {"they're", "theyre"}, {"They're", "Theyre"},
        {"don't", "dont"}, {"Don't", "Dont"},
        {"can't", "cant"}, {"Can't", "Cant"},
        {"won't", "wont"}, {"Won't", "Wont"},
        {"doesn't", "doesnt"}, {"Doesn't", "Doesnt"},
        {"didn't", "didnt"}, {"Didn't", "Didnt"},
        {"isn't", "isnt"}, {"Isn't", "Isnt"},
        {"aren't", "arent"}, {"Aren't", "Arent"},
        {"wasn't", "wasnt"}, {"Wasn't", "Wasnt"},
        {"weren't", "werent"}, {"Weren't", "Werent"},
        {"haven't", "havent"}, {"Haven't", "Havent"},
        {"hasn't", "hasnt"}, {"Hasn't", "Hasnt"},
        {"hadn't", "hadnt"}, {"Hadn't", "Hadnt"},
        {"shouldn't", "shouldnt"}, {"Shouldn't", "Shouldnt"},
        {"wouldn't", "wouldnt"}, {"Wouldn't", "Wouldnt"},
        {"couldn't", "couldnt"}, {"Couldn't", "Couldnt"},
        {"mustn't", "mustnt"}, {"Mustn't", "Mustnt"},
        {"I've", "ive"}, {"i've", "ive"},
        {"you've", "youve"}, {"You've", "Youve"},
        {"we've", "weve"}, {"We've", "Weve"},
        {"they've", "theyve"}, {"They've", "Theyve"},
        {"I'll", "I will"}, {"i'll", "i will"},
        {"you'll", "you will"}, {"You'll", "You will"},
        {"he'll", "he will"}, {"He'll", "He will"},
        {"she'll", "she will"}, {"She'll", "She will"},
        {"it'll", "itll"}, {"It'll", "Itll"},
        {"we'll", "we will"}, {"We'll", "We will"},
        {"they'll", "they will"}, {"They'll", "They will"}
    };

    for (const auto& rep : replacements) {
        size_t pos = 0;
        while ((pos = result.find(rep.contraction, pos)) != std::string::npos) {
            result.replace(pos, rep.contraction.size(), rep.expansion);
            pos += rep.expansion.size();
        }
    }
    return result;
}

void TtsEngine::loadUserPronunciations() {
    m_user_pronunciations.clear();
    QString app_path = QCoreApplication::applicationDirPath();
    std::string dict_path = QDir(app_path).absoluteFilePath("promptslut.dict").toStdString();

    std::ifstream file(dict_path);
    if (!file.is_open()) {
        // Create file with default pronunciation mappings
        std::ofstream outfile(dict_path);
        if (outfile.is_open()) {
            outfile << "Emjay=Emma-Jane\n";
            outfile << "emjay=emma-jane\n";
            outfile << "Vashdi=Vash-dee\n";
            outfile << "vashdi=vash-dee\n";
            outfile << "Qwen=Kwen\n";
            outfile << "qwen=kwen\n";
            outfile.close();
        }
        file.open(dict_path);
    }

    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string word = line.substr(0, eq);
                std::string sub = line.substr(eq + 1);
                if (!word.empty() && !sub.empty()) {
                    m_user_pronunciations.push_back({word, sub});
                }
            }
        }
        log_voice("TTS Lexicon: Loaded " + std::to_string(m_user_pronunciations.size()) + " custom pronunciations.");
    }
}

void TtsEngine::generationWorker() {
    // No background process started locally. We rely on the unified Python server on 127.0.0.1:5001.

    while (m_worker_running) {
        std::string text;
        {
            std::unique_lock<std::mutex> lock(m_queue_mutex);
            m_queue_cv.wait(lock, [this]() { return !m_text_queue.empty() || !m_worker_running; });

            if (!m_worker_running) break;

            text = std::move(m_text_queue.front());
            m_text_queue.pop_front();
        }

        std::vector<float> audio_buffer;

#ifdef PROMPTSLUT_VOICE_MODE
        QString app_path = QCoreApplication::applicationDirPath();
        QString temp_wav = app_path + "/audio_cache/temp_tts_" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".wav";
        std::string temp_wav_str = temp_wav.toStdString();

        // Normalize text for English contractions using standard apostrophes
        std::string normalized = text;
        size_t pos = 0;
        while ((pos = normalized.find("’")) != std::string::npos) { normalized.replace(pos, 3, "'"); }
        while ((pos = normalized.find("‘")) != std::string::npos) { normalized.replace(pos, 3, "'"); }
        while ((pos = normalized.find("”")) != std::string::npos) { normalized.replace(pos, 3, "\""); }
        while ((pos = normalized.find("“")) != std::string::npos) { normalized.replace(pos, 3, "\""); }

        // Apply custom user dictionary substitutions from promptslut.dict
        for (const auto& pair : m_user_pronunciations) {
            size_t sub_pos = 0;
            while ((sub_pos = normalized.find(pair.first, sub_pos)) != std::string::npos) {
                normalized.replace(sub_pos, pair.first.size(), pair.second);
                sub_pos += pair.second.size();
            }
        }

        // Expand contractions to prevent CppJieba segmentation splits
        normalized = expandContractions(normalized);

        log_voice("TTS worker: Requesting REST API synthesis: [" + normalized + "]");

        bool success = false;
        try {
            httplib::Client cli("127.0.0.1", 5001);
            cli.set_connection_timeout(5);
            cli.set_read_timeout(30);

            nlohmann::json body = {
                {"input", normalized},
                {"voice", m_voice_name},
                {"response_format", "wav"},
                {"speed", 1.1}
            };

            auto res = cli.Post("/v1/audio/speech", body.dump(), "application/json");
            if (res && res->status == 200) {
                std::ofstream outfile(temp_wav_str, std::ios::binary);
                if (outfile.is_open()) {
                    outfile.write(res->body.data(), res->body.size());
                    outfile.close();
                    success = true;
                }
            } else {
                log_voice("TTS ERROR: HTTP request failed with status: " + (res ? std::to_string(res->status) : "timeout/connection failed"));
            }
        } catch (const std::exception& e) {
            log_voice(std::string("TTS ERROR: HTTP exception: ") + e.what());
        }

        if (success) {
            if (QFile::exists(temp_wav)) {
                audio_buffer = loadWavFile(temp_wav_str);
                log_voice("TTS Loaded " + std::to_string(audio_buffer.size()) + " float samples successfully.");
                QFile::remove(temp_wav);
            } else {
                log_voice("TTS ERROR: Output wav file not found at " + temp_wav_str);
            }
        } else {
            log_voice("TTS ERROR: REST synthesis failed!");
        }
#endif

        if (!audio_buffer.empty()) {
            std::lock_guard<std::mutex> lock(m_audio_mutex);
            m_audio_queue.push_back(std::move(audio_buffer));
            m_audio_cv.notify_one();
        }
    }
}

void TtsEngine::playbackWorker() {
    m_playback_running = true;
    while (m_playback_running) {
        std::vector<float> audio_buffer;
        {
            std::unique_lock<std::mutex> lock(m_audio_mutex);
            m_audio_cv.wait(lock, [this]() { return !m_audio_queue.empty() || !m_playback_running; });

            if (!m_playback_running) break;

            audio_buffer = std::move(m_audio_queue.front());
            m_audio_queue.pop_front();
        }

#ifdef PROMPTSLUT_VOICE_MODE
        if (!audio_buffer.empty()) {
            QString app_path = QCoreApplication::applicationDirPath();
            QString play_wav = app_path + "/audio_cache/temp_play_" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".wav";
            std::string play_wav_str = play_wav.toStdString();

            writeWavFileInt16(play_wav_str, audio_buffer);

            QMetaObject::invokeMethod(this, [this]() {
                m_is_playing = true;
                emit playbackStateChanged(true);
            });

            log_voice("TTS: Playing sentence audio natively on PlaySoundW thread.");
            
            // Notify Python voice server that playback is starting (activate subtractive gate)
            try {
                httplib::Client cli("127.0.0.1", 5001);
                cli.set_connection_timeout(1);
                cli.Post("/v1/audio/playback?active=true");
            } catch (...) {}

            std::wstring wplay_wav = play_wav.toStdWString();
            std::replace(wplay_wav.begin(), wplay_wav.end(), L'/', L'\\'); // Windows-style backslashes required for legacy PlaySoundW API!
            PlaySoundW(wplay_wav.c_str(), NULL, SND_FILENAME | SND_SYNC);

            // Notify Python voice server that playback has ended (deactivate subtractive gate)
            try {
                httplib::Client cli("127.0.0.1", 5001);
                cli.set_connection_timeout(1);
                cli.Post("/v1/audio/playback?active=false");
            } catch (...) {}

            log_voice("TTS: Finished playing sentence. Adding to cache queue.");

            // Keep the last 5 WAV files in a rolling cache, deleting older ones
            {
                std::lock_guard<std::mutex> lock(m_audio_mutex);
                m_generated_files.push_back(play_wav_str);
                while (m_generated_files.size() > 5) {
                    std::string oldest = m_generated_files.front();
                    m_generated_files.erase(m_generated_files.begin());
                    QFile::remove(QString::fromStdString(oldest));
                    log_voice("TTS Cache: Cleaned up oldest cached WAV file: " + oldest);
                }
            }

            QMetaObject::invokeMethod(this, [this]() {
                m_is_playing = false;
                emit playbackStateChanged(false);
            });
        }
#endif
    }
}

// ---------------------------------------------------------------------------
// SttEngine Implementation
// ---------------------------------------------------------------------------
SttEngine::SttEngine(QObject* parent) 
    : QObject(parent), m_is_recording(false) {}

SttEngine::~SttEngine() {
    shutdown();
}

bool SttEngine::init(const std::string& model_path) {
    QString app_path = QCoreApplication::applicationDirPath();
    m_model_path = QDir(app_path).absoluteFilePath(QString::fromStdString(model_path)).toStdString();
    shutdown();

#ifdef PROMPTSLUT_VOICE_MODE
    QAudioFormat format;
    format.setSampleRate(16000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    m_audio_source = std::make_unique<QAudioSource>(format, this);
    return true;
#else
    return false;
#endif
}

void SttEngine::shutdown() {
    stopRecording();
}

void SttEngine::startRecording() {
    log_voice("STT: startRecording() requested.");
    if (m_is_recording) {
        log_voice("STT: Already recording.");
        return;
    }

#ifdef PROMPTSLUT_VOICE_MODE
    if (m_audio_source) {
        m_recorded_samples.clear();
        m_audio_input_device = m_audio_source->start();
        if (m_audio_input_device) {
            connect(m_audio_input_device, &QIODevice::readyRead, this, &SttEngine::handleReadyRead);
            m_is_recording = true;
            log_voice("STT: Recording started successfully.");
            emit recordingStateChanged(true);
        } else {
            log_voice("STT ERROR: Failed to start QAudioSource. Error code: " + std::to_string(m_audio_source->error()));
        }
    } else {
        log_voice("STT ERROR: m_audio_source is null!");
    }
#endif
}

void SttEngine::stopRecording() {
    log_voice("STT: stopRecording() requested.");
    if (!m_is_recording) {
        log_voice("STT: Not currently recording.");
        return;
    }

#ifdef PROMPTSLUT_VOICE_MODE
    if (m_audio_source) {
        m_audio_source->stop();
        m_is_recording = false;
        log_voice("STT: Recording stopped. Captured sample count: " + std::to_string(m_recorded_samples.size()));
        emit recordingStateChanged(false);

        std::vector<float> samples = std::move(m_recorded_samples);
        std::thread([this, samples]() {
            processTranscription(samples);
        }).detach();
    }
#endif
}

void SttEngine::handleReadyRead() {
#ifdef PROMPTSLUT_VOICE_MODE
    if (m_audio_input_device) {
        QByteArray data = m_audio_input_device->readAll();
        if (!data.isEmpty()) {
            const int16_t* int16_samples = reinterpret_cast<const int16_t*>(data.constData());
            size_t count = data.size() / sizeof(int16_t);
            m_recorded_samples.reserve(m_recorded_samples.size() + count);
            for (size_t i = 0; i < count; ++i) {
                m_recorded_samples.push_back(static_cast<float>(int16_samples[i]) / 32768.0f);
            }
        }
    }
#endif
}

void SttEngine::writeWavFile(const std::string& filepath, const std::vector<float>& samples) {
    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) return;

    int sample_rate = 16000;
    int channels = 1;
    int bits_per_sample = 32;
    int byte_rate = sample_rate * channels * bits_per_sample / 8;
    int block_align = channels * bits_per_sample / 8;
    int data_size = static_cast<int>(samples.size() * sizeof(float));
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
    file.write(reinterpret_cast<const char*>(samples.data()), data_size);
}

void SttEngine::processTranscription(const std::vector<float>& samples) {
    QString transcription;

#ifdef PROMPTSLUT_VOICE_MODE
    if (!samples.empty()) {
        QString app_path = QCoreApplication::applicationDirPath();
        QString temp_wav = app_path + "/temp_stt_" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".wav";
        std::string temp_wav_str = temp_wav.toStdString();

        writeWavFile(temp_wav_str, samples);

        log_voice("STT worker: Requesting REST API transcription via Python voice backend.");

        try {
            // Read recorded WAV file bytes
            std::ifstream infile(temp_wav_str, std::ios::binary);
            std::string wav_data((std::istreambuf_iterator<char>(infile)), std::istreambuf_iterator<char>());
            infile.close();

            httplib::Client cli("127.0.0.1", 5001);
            cli.set_connection_timeout(5);
            cli.set_read_timeout(30);

            httplib::UploadFormDataItems items = {
                { "file", wav_data, "temp_record.wav", "audio/wav" },
                { "temperature", "0.0", "", "" }
            };

            auto res = cli.Post("/v1/audio/transcriptions", items);
            if (res && res->status == 200) {
                auto json_res = nlohmann::json::parse(res->body);
                if (json_res.contains("text")) {
                    std::string text = json_res["text"];
                    transcription = QString::fromStdString(text).trimmed();
                    log_voice("STT Final transcription: [" + transcription.toStdString() + "]");
                } else {
                    log_voice("STT ERROR: Response JSON does not contain 'text'. Response: " + res->body);
                }
            } else {
                log_voice("STT ERROR: HTTP request failed with status: " + (res ? std::to_string(res->status) : "timeout/connection failed"));
            }
        } catch (const std::exception& e) {
            log_voice(std::string("STT ERROR: HTTP exception during transcription: ") + e.what());
        }

        QFile::remove(temp_wav);
    }
#endif

    emit transcriptionReady(transcription);
}

// ---------------------------------------------------------------------------
// VoiceEngine Implementation
// ---------------------------------------------------------------------------
VoiceEngine& VoiceEngine::instance() {
    static VoiceEngine inst;
    return inst;
}

VoiceEngine::VoiceEngine(QObject* parent) : QObject(parent), m_voice_server_pid(0) {
    m_tts = std::make_unique<TtsEngine>(this);
    m_stt = std::make_unique<SttEngine>(this);
}

VoiceEngine::~VoiceEngine() {
    shutdown();
}

bool VoiceEngine::init(const std::string& tts_model, const std::string& voices_bin, const std::string& vocab_txt, const std::string& whisper_model) {
    // Start Python Voice Server if not already running
    if (m_voice_server_pid == 0) {
        QStringList search_dirs = {
            QCoreApplication::applicationDirPath(),
            QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(".."),
            QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../..")
        };
        QString server_script;
        for (const QString& dir : search_dirs) {
            QString path = QDir(dir).absoluteFilePath("voice_server.py");
            if (QFile::exists(path)) {
                server_script = QDir::toNativeSeparators(path);
                break;
            }
        }

        if (!server_script.isEmpty()) {
            QFileInfo fileInfo(server_script);
            qint64 pid = 0;
            bool success = QProcess::startDetached("python", QStringList() << "voice_server.py", fileInfo.absolutePath(), &pid);
            if (success && pid > 0) {
                m_voice_server_pid = pid;
                log_voice("VoiceEngine: Successfully detached Python voice_server.py with PID " + std::to_string(pid));
            } else {
                log_voice("VoiceEngine WARNING: Failed to detach Python voice_server.py.");
            }
        } else {
            log_voice("VoiceEngine ERROR: Could not find voice_server.py in search paths.");
        }
    }

    bool tts_ok = m_tts->init(tts_model, voices_bin, vocab_txt);
    bool stt_ok = m_stt->init(whisper_model);
    m_enabled = tts_ok && stt_ok;
    return m_enabled;
}

void VoiceEngine::shutdown() {
    m_tts->shutdown();
    m_stt->shutdown();
    m_enabled = false;

    if (m_voice_server_pid > 0) {
        log_voice("VoiceEngine: Stopping Python voice_server.py process with PID " + std::to_string(m_voice_server_pid) + "...");
        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, m_voice_server_pid);
        if (hProcess) {
            TerminateProcess(hProcess, 0);
            CloseHandle(hProcess);
        }
        m_voice_server_pid = 0;
    }
}

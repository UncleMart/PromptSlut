#ifndef VOICE_ENGINE_H
#define VOICE_ENGINE_H

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QBuffer>
#include <QProcess>
#include <mutex>
#include <deque>
#include <vector>
#include <memory>
#include <thread>
#include <condition_variable>

#ifdef PROMPTSLUT_VOICE_MODE
#include <QAudioSource>
#include <QAudioSink>
#include <QAudioFormat>
#include <QIODevice>
#endif

// ---------------------------------------------------------------------------
// TtsEngine - Process-isolated Text-to-Speech synthesizer
// ---------------------------------------------------------------------------
class TtsEngine : public QObject {
    Q_OBJECT
public:
    explicit TtsEngine(QObject* parent = nullptr);
    ~TtsEngine() override;

    bool init(const std::string& model_path, const std::string& voices_path, const std::string& vocab_path);
    void shutdown();

    void setVoiceStyle(const std::string& voice) { m_voice_name = voice; }
    void speakSentence(const std::string& sentence);
    void stop();
    bool isPlaying() const;

signals:
    void playbackStateChanged(bool playing);

private:
    void generationWorker();
    void playbackWorker();
    void loadUserPronunciations();
    std::vector<float> loadWavFile(const std::string& filepath);
    void writeWavFileInt16(const std::string& filepath, const std::vector<float>& samples);

    std::string m_model_path;
    std::string m_voices_path;
    std::string m_vocab_path;
    std::string m_voice_name;

    mutable std::mutex m_queue_mutex;
    std::condition_variable m_queue_cv;
    std::deque<std::string> m_text_queue;
    bool m_worker_running = false;
    std::unique_ptr<std::thread> m_worker_thread;

    mutable std::mutex m_audio_mutex;
    std::condition_variable m_audio_cv;
    std::deque<std::vector<float>> m_audio_queue;
    bool m_playback_running = false;
    std::unique_ptr<std::thread> m_playback_thread;
    bool m_is_playing = false;

#ifdef PROMPTSLUT_VOICE_MODE
    QProcess* m_tts_process = nullptr;
    std::vector<std::string> m_generated_files;
    std::vector<std::pair<std::string, std::string>> m_user_pronunciations;
#endif
};

// ---------------------------------------------------------------------------
// SttEngine - Process-isolated Whisper Speech-to-Text transcriber
// ---------------------------------------------------------------------------
class SttEngine : public QObject {
    Q_OBJECT
public:
    explicit SttEngine(QObject* parent = nullptr);
    ~SttEngine() override;

    bool init(const std::string& model_path);
    void shutdown();

    void startRecording();
    void stopRecording();
    bool isRecording() const { return m_is_recording; }

signals:
    void transcriptionReady(const QString& text);
    void recordingStateChanged(bool recording);

private slots:
    void handleReadyRead();

private:
    void processTranscription(const std::vector<float>& samples);
    void writeWavFile(const std::string& filepath, const std::vector<float>& samples);

    std::string m_model_path;
    bool m_is_recording = false;

#ifdef PROMPTSLUT_VOICE_MODE
    std::unique_ptr<QAudioSource> m_audio_source;
    QIODevice* m_audio_input_device = nullptr;
    std::vector<float> m_recorded_samples;
#endif
};

// ---------------------------------------------------------------------------
// VoiceEngine - Coordinator Singleton
// ---------------------------------------------------------------------------
class VoiceEngine : public QObject {
    Q_OBJECT
public:
    static VoiceEngine& instance();

    bool init(const std::string& tts_model, const std::string& voices_bin, const std::string& vocab_txt, const std::string& whisper_model);
    void shutdown();

    TtsEngine* tts() { return m_tts.get(); }
    SttEngine* stt() { return m_stt.get(); }

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled) { m_enabled = enabled; }

private:
    explicit VoiceEngine(QObject* parent = nullptr);
    ~VoiceEngine() override;

    std::unique_ptr<TtsEngine> m_tts;
    std::unique_ptr<SttEngine> m_stt;
    bool m_enabled = false;
    qint64 m_voice_server_pid = 0;
};

#endif // VOICE_ENGINE_H

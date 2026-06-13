#ifndef QT_UI_H
#define QT_UI_H

#include <QMainWindow>
#include <QNetworkAccessManager>
#include "ChronosEngine.h"
#include "ProjectWatcher.h"
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextBrowser>
#include <QLineEdit>
#include <QPushButton>
#include <QListWidget>
#include <QLabel>
#include <QFrame>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QUrl>
#include <QScrollBar>
#include <QMenu>
#include <QInputDialog>
#include <QDialog>
#include <QCheckBox>
#include <QFormLayout>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QPainter>
#include <QRandomGenerator>
#include <QGridLayout>
#include "worker.h"
#include "keyfile.h"
#include "voice_engine.h"

// Forward declaration of Worker
class Worker;

class MatrixRainWidget : public QWidget {
    Q_OBJECT
public:
    explicit MatrixRainWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, &MatrixRainWidget::updateRain);
        m_timer->start(33);
    }

    void setEnabled(bool enabled) {
        m_enabled = enabled;
        if (enabled) {
            m_timer->start(33);
        } else {
            m_timer->stop();
        }
        update();
    }

    void setTheme(int theme_idx) {
        m_theme_idx = theme_idx;
        update();
    }

    void setGenerating(bool generating) {
        m_generating = generating;
        update();
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        initRain();
    }

    void paintEvent(QPaintEvent* event) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(30, 30, 30, m_enabled ? 250 : 255));

        if (!m_enabled) return;

        painter.setFont(QFont("Consolas", 10));

        // Base theme colors mapping
        QColor trail_color;
        QColor head_color;
        if (m_theme_idx == 0) {
            trail_color = QColor(0, 255, 70); // Classic Matrix Green
            head_color = QColor(100, 255, 140); // Rich Glowing Green (less white!)
        } else if (m_theme_idx == 1) {
            trail_color = QColor(255, 0, 128); // Synthwave Pink
            head_color = QColor(255, 100, 180); // Rich Glowing Pink (less white, deeply saturated!)
        } else if (m_theme_idx == 2) {
            trail_color = QColor(0, 122, 255); // Cobalt Blue
            head_color = QColor(100, 190, 255); // Rich Glowing Blue
        } else if (m_theme_idx == 3) {
            trail_color = QColor(255, 170, 0); // Retro Amber/Gold
            head_color = QColor(255, 210, 100); // Rich Glowing Amber/Gold
        } else {
            trail_color = QColor(0, 255, 70);
            head_color = QColor(255, 255, 255);
        }

        double head_opacity = m_generating ? 0.45 : 0.25;
        double trail_opacity = m_generating ? 0.25 : 0.12;

        // Boost the pink theme opacities to offset its lower natural visual luminance
        if (m_theme_idx == 1) {
            head_opacity = m_generating ? 0.65 : 0.35;
            trail_opacity = m_generating ? 0.35 : 0.18;
        }

        for (auto& col : m_columns) {
            for (size_t i = 0; i < col.chars.size(); ++i) {
                int y_pos = col.y - (i * 15);
                if (y_pos < 0 || y_pos > height()) continue;

                int alpha = 255 - (i * 255 / col.chars.size());
                if (alpha < 0) alpha = 0;

                if (i == 0) {
                    painter.setPen(QColor(head_color.red(), head_color.green(), head_color.blue(), static_cast<int>(alpha * head_opacity)));
                } else {
                    painter.setPen(QColor(trail_color.red(), trail_color.green(), trail_color.blue(), static_cast<int>(alpha * trail_opacity)));
                }

                painter.drawText(col.x, y_pos, QString(col.chars[i]));
            }
        }
    }

private slots:
    void updateRain() {
        double speed_multiplier = m_generating ? 1.5 : 1.0;
        for (auto& col : m_columns) {
            col.y += static_cast<int>(col.speed * speed_multiplier);
            if (col.y - (static_cast<int>(col.chars.size()) * 15) > height()) {
                col.y = 0;
                col.speed = QRandomGenerator::global()->bounded(5, 12);
            }
            if (QRandomGenerator::global()->bounded(10) == 0) {
                col.chars[QRandomGenerator::global()->bounded(static_cast<int>(col.chars.size()))] = getRandomChar();
            }
        }
        update();
    }

private:
    struct RainColumn {
        int x;
        int y;
        int speed;
        std::vector<char> chars;
    };
    std::vector<RainColumn> m_columns;
    QTimer* m_timer;
    bool m_enabled = true;
    int m_theme_idx = 0;
    bool m_generating = false;

    char getRandomChar() {
        const char chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ@#$&*+";
        return chars[QRandomGenerator::global()->bounded(static_cast<int>(sizeof(chars) - 1))];
    }

    void initRain() {
        m_columns.clear();
        int col_width = 18;
        int num_cols = width() / col_width + 1;
        for (int i = 0; i < num_cols; ++i) {
            RainColumn col;
            col.x = i * col_width;
            col.y = QRandomGenerator::global()->bounded(-500, 0);
            col.speed = QRandomGenerator::global()->bounded(5, 12);
            int len = QRandomGenerator::global()->bounded(10, 25);
            for (int j = 0; j < len; ++j) {
                col.chars.push_back(getRandomChar());
            }
            m_columns.push_back(col);
        }
    }
};

class TaskListWidget : public QFrame {
    Q_OBJECT
public:
    explicit TaskListWidget(class QtUiApp* parent);
    void refreshTasks();

private slots:
    void toggleCollapsed();

private:
    class QtUiApp* m_parent;
    class QPushButton* m_header_btn;
    class QWidget* m_content_container;
    class QListWidget* m_list_widget;
    bool m_is_collapsed = true;
    int m_completed_count = 0;
    int m_total_count = 0;
};

enum class AgentMode { Chatbot, Coder, Planner };

class QtUiApp : public QMainWindow {
    Q_OBJECT
    friend class TaskListWidget;

public:
    static QtUiApp* s_instance;
    explicit QtUiApp(Worker* worker, QWidget *parent = nullptr);
    ~QtUiApp() override = default;

    // API for Worker to push updates to the UI
    void appendMessage(const std::string& text, bool is_user = false, bool is_reasoning = false);
    void updateStatus(const std::string& status);
    void setModelName(const std::string& model);
    void queryActiveModel();
    void handleVoiceToggle(bool checked);

private slots:
    void handleSend();
    void handleStop();
    void toggleLeftPane();
    void toggleRightPane();
    void handleAnchorClicked(const QUrl& url);
    void handleNewChat();
    void handleDeleteSession(int row);
    void loadSession(int index);
    void rebuildSidebar();
    void showSidebarContextMenu(const QPoint& pos);
    void handleRenameSession(int row);
    void handleResetStats();
    void handleMicPressed();
    void handleMicReleased();
    void handleTranscriptionReady(const QString& text);
    void handleChronosEvent(const ChronosTask& task);
    void handleFileChangedOrDiscovered(const QString &filePath, const QString &content);
    void handleTaskUpdate(const QString& session_id, const QString& action, const QString& data);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void setupLayout();
    void applyStyles();
    void saveCurrentSessionState();
    void save_all_sessions_to_disk();
    void load_all_sessions_from_disk();
    void triggerContextConsolidationAndTrimming();

private:
    bool m_is_consolidating = false;

public:
    void updateWorkspaceLabel();
    void rebuildChatDisplay();
    void updateAgentModeUI();
    struct ChatBlock {
        std::string role; // "user", "assistant", "reasoning", "tool"
        std::string content;
        std::string tool_name;
        bool collapsed = true;
    };

    struct Task {
        std::string id;
        std::string text;
        bool is_completed = false;
    };

    struct ChatSession {
        std::string id;
        std::string title;
        std::string memory_digest;
        std::string workspace_dir;
        std::vector<Task> tasks;
        std::vector<ChatBlock> chat_history;
        std::vector<nlohmann::json> conversation;
    };

    std::vector<ChatBlock> m_chat_history;

private:
    std::vector<ChatSession> m_sessions;
    int m_current_session_index = -1;

    // Token streaming variables
    bool m_is_actively_streaming_chunk = false;
    bool m_is_actively_streaming_reasoning = false;

    // Agent Mode States & Prompts
    AgentMode m_active_mode = AgentMode::Chatbot;
    std::string m_prompt_chatbot;
    std::string m_prompt_coder;
    std::string m_prompt_planner;

    // HUD Strip layout components
    QWidget* m_hud_strip_widget = nullptr;
    QLabel* m_chatbot_label = nullptr;
    QLabel* m_coder_label = nullptr;
    QLabel* m_planner_label = nullptr;

     Worker* m_worker;
     QNetworkAccessManager* m_network_manager = nullptr;

    // Layout components
    QSplitter* m_splitter;
    QWidget* m_left_pane_container;
    QListWidget* m_sidebar;
    QPushButton* m_new_chat_btn;
    QPushButton* m_sidebar_settings_btn;
    QWidget* m_chat_container;
    QTextBrowser* m_chat_display;
    TaskListWidget* m_task_list_widget = nullptr;
    QTextEdit* m_input_field;
    QPushButton* m_send_btn;
    QPushButton* m_stop_btn;
    QPushButton* m_mic_btn = nullptr;
    QPushButton* m_toggle_left_btn;
    QPushButton* m_toggle_right_btn;
    
    // Stats Panel components (Right Pane)
    QWidget* m_right_pane_container;
    QLabel* m_stat_status;
    QLabel* m_stat_model;
    QLabel* m_stat_workspace;
    QLabel* m_stat_context_limit;
    QLabel* m_stat_context_used;
    QLabel* m_stat_prompt_tokens;
    QLabel* m_stat_completion_tokens;
    QLabel* m_stat_total_tokens;
    QLabel* m_stat_speed;
    QLabel* m_stat_time;
    QLabel* m_stat_session_tokens;
    QLabel* m_stat_session_speed;
    QPushButton* m_reset_stats_btn;
    
    // Context Limit variable
    int m_context_limit_val = 32000;
    int m_last_total_tokens = 0;
    
    // Session Accumulators
    int m_accumulated_prompt_tokens = 0;
    int m_accumulated_completion_tokens = 0;
    int m_accumulated_total_tokens = 0;
    double m_accumulated_time = 0.0;
    int m_total_generations = 0;
    
    // Info Bar
    QLabel* m_model_label;
    QLabel* m_status_label;

public:
    std::string m_host_val;
    int m_port_val;
    std::string m_apikey_val;
    std::string m_serper_val;
    std::string m_model_val;
    bool m_matrix_rain_enabled = true;
    int m_rain_theme_idx = 0;
    int m_max_tool_calls_val = 20;
    MatrixRainWidget* m_matrix_widget = nullptr;

    // Voice Mode & LLM-Stream-synchronized TTS extraction state
    bool m_voice_mode_enabled = false;
    bool m_handsfree_enabled = false;
    bool m_last_is_transcribing = false;
    QTimer* m_handsfree_timer = nullptr;
    void handleHandsfreeToggle(bool checked);
    void pollHandsfreeBuffer();
    size_t m_raw_stream_processed_idx = 0;
    std::string m_tts_voice_val = "af_maple";

    // Secondary model settings for background processing
    bool m_use_secondary_model = false;
    std::string m_sec_host_val;
    int m_sec_port_val = 8081;
    std::string m_sec_apikey_val;
    std::string m_sec_model_val;

    // Chronos Engine System
    ChronosEngine* m_chronos_engine = nullptr;
    std::string m_pending_chronos_instruction;
    bool m_hide_next_user_message_from_ui = false;

    // Project Watcher Subsystem
    ProjectWatcher* m_project_watcher = nullptr;

    // File Attachment & Image Attachment states
    std::string m_attached_file_name;
    std::string m_attached_file_summary;
    std::string m_pending_image_name;
    std::string m_pending_image_mime;
    std::string m_pending_image_base64;
    std::string m_pending_image_file_path;
    
    // Client-side Visual Memory System (Generic Perceptual Hashing Pipeline)
    std::vector<std::pair<uint64_t, std::string>> m_visual_memories;
    bool m_pending_image_has_match = false;
    std::string m_pending_image_match_identity;
    void loadVisualMemoryReferenceHashes();
    static uint64_t computeImagePHash(const QImage& img);
    static int calculateHammingDistance(uint64_t h1, uint64_t h2);
    
    // Audio Attachment states (for native multimodal audio understanding!)
    std::string m_pending_audio_name;
    std::string m_pending_audio_mime;
    std::string m_pending_audio_base64;
    std::string m_pending_audio_format;

    // User long-term memory profile
    std::string m_user_profile;
    void consolidateMemoryProfile(const std::string& user_prompt, const std::string& assistant_reply);

private:
    std::vector<nlohmann::json> m_conversation;

    int m_initial_left_width = 250;
    int m_initial_right_width = 280;
};

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(QtUiApp* parent) : QDialog(parent), m_parent(parent) {
        setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setFixedSize(500, 550);

        QVBoxLayout* main_layout = new QVBoxLayout(this);
        main_layout->setContentsMargins(0, 0, 0, 0);

        QFrame* bg_frame = new QFrame(this);
        bg_frame->setObjectName("bg_frame");
        bg_frame->setStyleSheet(
            "QFrame#bg_frame { "
            "  background-color: #1e1e1e; "
            "  border: 2px solid #007acc; "
            "  border-radius: 12px; "
            "}"
            "QLabel { color: #ccc; font-size: 13px; font-weight: bold; }"
            "QLineEdit { background-color: #2d2d2d; border: 1px solid #444; border-radius: 4px; color: #fff; padding: 4px 6px; font-size: 13px; min-height: 22px; }"
            "QLineEdit:focus { border: 1px solid #007acc; }"
            "QCheckBox { color: #ccc; font-size: 13px; font-weight: bold; }"
            "QComboBox { background-color: #2d2d2d; border: 1px solid #444; border-radius: 4px; color: #fff; padding: 4px 6px; font-size: 13px; min-height: 22px; }"
            "QComboBox QAbstractItemView { background-color: #1e1e1e; color: #ccc; border: 1px solid #444; selection-background-color: #007acc; selection-color: white; }"
            "QPushButton#close_btn { background: transparent; color: #888; font-size: 18px; font-weight: bold; border: none; }"
            "QPushButton#close_btn:hover { color: #d32f2f; }"
        );

        QVBoxLayout* frame_layout = new QVBoxLayout(bg_frame);
        frame_layout->setContentsMargins(20, 15, 20, 20);
        frame_layout->setSpacing(12);

        // Header Row
        QHBoxLayout* header_layout = new QHBoxLayout();
        QLabel* title_label = new QLabel("⚙️ System Settings", bg_frame);
        title_label->setStyleSheet("font-size: 16px; font-weight: bold; color: #fff;");
        QPushButton* close_btn = new QPushButton("×", bg_frame);
        close_btn->setObjectName("close_btn");
        close_btn->setFixedSize(24, 24);
        connect(close_btn, &QPushButton::clicked, this, &QDialog::close);

        header_layout->addWidget(title_label);
        header_layout->addStretch();
        header_layout->addWidget(close_btn);
        frame_layout->addLayout(header_layout);

        // Form Layout
        QFormLayout* form = new QFormLayout();
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
        form->setVerticalSpacing(10);
        form->setHorizontalSpacing(15);

        m_host_input = new QLineEdit(QString::fromStdString(m_parent->m_host_val), bg_frame);
        m_port_input = new QLineEdit(m_parent->m_port_val > 0 ? QString::number(m_parent->m_port_val) : "", bg_frame);
        m_apikey_input = new QLineEdit(QString::fromStdString(m_parent->m_apikey_val), bg_frame);
        m_apikey_input->setEchoMode(QLineEdit::Password);
        m_serper_input = new QLineEdit(QString::fromStdString(m_parent->m_serper_val), bg_frame);
        m_serper_input->setEchoMode(QLineEdit::Password);
        m_serper_input->setPlaceholderText("Enter Serper.dev Key");
        m_model_input = new QLineEdit(QString::fromStdString(m_parent->m_model_val), bg_frame);
        m_max_tool_calls_input = new QLineEdit(QString::number(m_parent->m_max_tool_calls_val), bg_frame);

        form->addRow("LLM API Host:", m_host_input);
        form->addRow("LLM API Port:", m_port_input);
        form->addRow("LLM API Key:", m_apikey_input);
        form->addRow("Serper API Key:", m_serper_input);
        form->addRow("Model Name:", m_model_input);
        form->addRow("Max Tool Calls:", m_max_tool_calls_input);

        // Rain Theme ComboBox Selection
        m_theme_combo = new QComboBox(bg_frame);
        m_theme_combo->addItems({"Classic Neon Green", "Synthwave Glowing Pink", "Cobalt Cyber Blue", "Retro Phosphor Amber"});
        m_theme_combo->setCurrentIndex(m_parent->m_rain_theme_idx);
        form->addRow("Rain Color Theme:", m_theme_combo);

        // Header Divider for Secondary Model Settings
        QLabel* sec_hdr = new QLabel("🔄 SECONDARY ENDPOINT (BACKGROUND)", bg_frame);
        sec_hdr->setStyleSheet("color: #007acc; font-weight: bold; font-size: 11px; margin-top: 10px; margin-bottom: 5px;");
        form->addRow("", sec_hdr);

        m_sec_model_toggle = new QCheckBox("Use Secondary Endpoint for Memory extraction", bg_frame);
        m_sec_model_toggle->setChecked(m_parent->m_use_secondary_model);
        form->addRow("", m_sec_model_toggle);

        m_sec_host_input = new QLineEdit(QString::fromStdString(m_parent->m_sec_host_val), bg_frame);
        m_sec_port_input = new QLineEdit(m_parent->m_sec_port_val > 0 ? QString::number(m_parent->m_sec_port_val) : "", bg_frame);
        m_sec_apikey_input = new QLineEdit(QString::fromStdString(m_parent->m_sec_apikey_val), bg_frame);
        m_sec_apikey_input->setEchoMode(QLineEdit::Password);
        m_sec_model_input = new QLineEdit(QString::fromStdString(m_parent->m_sec_model_val), bg_frame);

        form->addRow("Secondary Host:", m_sec_host_input);
        form->addRow("Secondary Port:", m_sec_port_input);
        form->addRow("Secondary API Key:", m_sec_apikey_input);
        form->addRow("Secondary Model:", m_sec_model_input);

#ifdef PROMPTSLUT_VOICE_MODE
        // Voice settings
        QLabel* voice_hdr = new QLabel("🎙️ SELF-CONTAINED VOICE SETTINGS", bg_frame);
        voice_hdr->setStyleSheet("color: #007acc; font-weight: bold; font-size: 11px; margin-top: 10px; margin-bottom: 5px;");
        form->addRow("", voice_hdr);

        m_voice_toggle = new QCheckBox("Enable Full Voice Mode (TTS & STT)", bg_frame);
        m_voice_toggle->setChecked(m_parent->m_voice_mode_enabled);
        form->addRow("", m_voice_toggle);

        m_handsfree_toggle = new QCheckBox("Enable Hands-Free Mode (Wake Word: Hey Qwen)", bg_frame);
        m_handsfree_toggle->setChecked(m_parent->m_handsfree_enabled);
        form->addRow("", m_handsfree_toggle);

        m_voice_combo = new QComboBox(bg_frame);
        // American Female
        m_voice_combo->addItem("American Female (Maple - Default)", "af_maple");
        m_voice_combo->addItem("American Female (Sky)", "af_sky");
        m_voice_combo->addItem("American Female (Sol)", "af_sol");
        m_voice_combo->addItem("American Female (Heart)", "af_heart");
        m_voice_combo->addItem("American Female (Bella)", "af_bella");
        m_voice_combo->addItem("American Female (Nicole)", "af_nicole");
        m_voice_combo->addItem("American Female (Sarah)", "af_sarah");
        // American Male
        m_voice_combo->addItem("American Male (Adam)", "am_adam");
        m_voice_combo->addItem("American Male (Michael)", "am_michael");
        m_voice_combo->addItem("American Male (Fenrir)", "am_fenrir");
        m_voice_combo->addItem("American Male (Puck)", "am_puck");
        // British Female
        m_voice_combo->addItem("British Female (Vale)", "bf_vale");
        m_voice_combo->addItem("British Female (Emma)", "bf_emma");
        m_voice_combo->addItem("British Female (Isabella)", "bf_isabella");
        // British Male
        m_voice_combo->addItem("British Male (George)", "bm_george");
        m_voice_combo->addItem("British Male (Lewis)", "bm_lewis");
        // Bilingual (v1.1)
        m_voice_combo->addItem("Bilingual Female (zf_001)", "zf_001");
        m_voice_combo->addItem("Bilingual Female (zf_002)", "zf_002");
        m_voice_combo->addItem("Bilingual Female (zf_003)", "zf_003");
        m_voice_combo->addItem("Bilingual Male (zm_001)", "zm_001");
        m_voice_combo->addItem("Bilingual Male (zm_002)", "zm_002");
        m_voice_combo->addItem("Bilingual Male (zm_003)", "zm_003");
        m_voice_combo->addItem("Bilingual Male (zm_009)", "zm_009");
        int v_idx = m_voice_combo->findData(QString::fromStdString(m_parent->m_tts_voice_val));
        if (v_idx >= 0) m_voice_combo->setCurrentIndex(v_idx);
        form->addRow("TTS Speaker Voice:", m_voice_combo);
#endif

        frame_layout->addLayout(form);

        // Matrix Toggle
        m_matrix_toggle = new QCheckBox("Enable Matrix Digital Rain Background", bg_frame);
        m_matrix_toggle->setChecked(m_parent->m_matrix_rain_enabled);
        frame_layout->addWidget(m_matrix_toggle);

        // Connect Auto-Save signals
        connect(m_host_input, &QLineEdit::editingFinished, this, &SettingsDialog::saveSettings);
        connect(m_port_input, &QLineEdit::editingFinished, this, &SettingsDialog::saveSettings);
        connect(m_apikey_input, &QLineEdit::editingFinished, this, &SettingsDialog::saveSettings);
        connect(m_serper_input, &QLineEdit::editingFinished, this, &SettingsDialog::saveSettings);
        connect(m_model_input, &QLineEdit::editingFinished, this, &SettingsDialog::saveSettings);
        connect(m_max_tool_calls_input, &QLineEdit::editingFinished, this, &SettingsDialog::saveSettings);
        connect(m_sec_model_toggle, &QCheckBox::toggled, this, &SettingsDialog::saveSettings);
        connect(m_sec_host_input, &QLineEdit::editingFinished, this, &SettingsDialog::saveSettings);
        connect(m_sec_port_input, &QLineEdit::editingFinished, this, &SettingsDialog::saveSettings);
        connect(m_sec_apikey_input, &QLineEdit::editingFinished, this, &SettingsDialog::saveSettings);
        connect(m_sec_model_input, &QLineEdit::editingFinished, this, &SettingsDialog::saveSettings);
#ifdef PROMPTSLUT_VOICE_MODE
        connect(m_voice_toggle, &QCheckBox::toggled, this, &SettingsDialog::saveSettings);
        connect(m_handsfree_toggle, &QCheckBox::toggled, this, &SettingsDialog::saveSettings);
        connect(m_voice_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SettingsDialog::saveSettings);
#endif
        connect(m_theme_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SettingsDialog::saveSettings);
        connect(m_matrix_toggle, &QCheckBox::toggled, this, &SettingsDialog::saveSettings);

        main_layout->addWidget(bg_frame);
    }

private slots:
    void saveSettings() {
        m_parent->m_host_val = m_host_input->text().trimmed().toStdString();
        m_parent->m_port_val = m_port_input->text().trimmed().toInt();
        m_parent->m_apikey_val = m_apikey_input->text().toStdString();
        m_parent->m_serper_val = m_serper_input->text().toStdString();
        m_parent->m_model_val = m_model_input->text().trimmed().toStdString();
        m_parent->m_max_tool_calls_val = m_max_tool_calls_input->text().trimmed().toInt();
        if (m_parent->m_max_tool_calls_val <= 0) m_parent->m_max_tool_calls_val = 20;
        m_parent->m_matrix_rain_enabled = m_matrix_toggle->isChecked();
        m_parent->m_rain_theme_idx = m_theme_combo->currentIndex();

        m_parent->m_use_secondary_model = m_sec_model_toggle->isChecked();
        m_parent->m_sec_host_val = m_sec_host_input->text().trimmed().toStdString();
        m_parent->m_sec_port_val = m_sec_port_input->text().trimmed().toInt();
        m_parent->m_sec_apikey_val = m_sec_apikey_input->text().toStdString();
        m_parent->m_sec_model_val = m_sec_model_input->text().trimmed().toStdString();

#ifdef PROMPTSLUT_VOICE_MODE
        bool voice_enabled_changed = (m_parent->m_voice_mode_enabled != m_voice_toggle->isChecked());
        bool handsfree_changed = (m_parent->m_handsfree_enabled != m_handsfree_toggle->isChecked());
        bool voice_style_changed = (m_parent->m_tts_voice_val != m_voice_combo->currentData().toString().toStdString());

        m_parent->m_voice_mode_enabled = m_voice_toggle->isChecked();
        m_parent->m_handsfree_enabled = m_handsfree_toggle->isChecked();
        m_parent->m_tts_voice_val = m_voice_combo->currentData().toString().toStdString();

        if (voice_enabled_changed) {
            m_parent->handleVoiceToggle(m_parent->m_voice_mode_enabled);
        } else if (handsfree_changed) {
            m_parent->handleHandsfreeToggle(m_parent->m_handsfree_enabled);
        } else if (voice_style_changed && m_parent->m_voice_mode_enabled) {
            VoiceEngine::instance().tts()->setVoiceStyle(m_parent->m_tts_voice_val);
        }
#endif

        m_parent->m_matrix_widget->setEnabled(m_parent->m_matrix_rain_enabled);
        m_parent->m_matrix_widget->setTheme(m_parent->m_rain_theme_idx);
        m_parent->queryActiveModel();

        save_all_settings(
            m_parent->m_apikey_val,
            m_parent->m_serper_val,
            m_parent->m_host_val,
            m_parent->m_port_val,
            m_parent->m_model_val,
            m_parent->m_matrix_rain_enabled,
            m_parent->m_use_secondary_model,
            m_parent->m_sec_host_val,
            m_parent->m_sec_port_val,
            m_parent->m_sec_apikey_val,
            m_parent->m_sec_model_val,
            m_parent->m_rain_theme_idx,
            m_parent->m_voice_mode_enabled,
            m_parent->m_tts_voice_val,
            m_parent->m_max_tool_calls_val
        );
    }

private:
    QtUiApp* m_parent;
    QLineEdit* m_host_input;
    QLineEdit* m_port_input;
    QLineEdit* m_apikey_input;
    QLineEdit* m_serper_input;
    QLineEdit* m_model_input;
    QLineEdit* m_max_tool_calls_input;
    QCheckBox* m_sec_model_toggle;
    QLineEdit* m_sec_host_input;
    QLineEdit* m_sec_port_input;
    QLineEdit* m_sec_apikey_input;
    QLineEdit* m_sec_model_input;
    QComboBox* m_theme_combo;
#ifdef PROMPTSLUT_VOICE_MODE
    QComboBox* m_voice_combo;
    QCheckBox* m_voice_toggle;
    QCheckBox* m_handsfree_toggle;
#endif
    QCheckBox* m_matrix_toggle;
};

#endif // QT_UI_H

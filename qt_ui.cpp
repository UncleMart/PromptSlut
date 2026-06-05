#include "qt_ui.h"
#include "worker.h"
#include "keyfile.h"
#include "voice_engine.h"
#include "tools.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
#include <fstream>
#include <sstream>
#include <QMetaObject>
#include <QMessageBox>
#include <QIcon>
#include <QCoreApplication>
#include <QFileInfo>
#include <QFile>
#include <thread>
#include <filesystem>

static std::vector<std::string> tokenize(const std::string& text);
static std::string stripThinkBlock(const std::string& text);

QtUiApp::QtUiApp(Worker* worker, QWidget *parent)
    : QMainWindow(parent), m_worker(worker) {
    
    setWindowTitle("PromptSlut - Production UI");
    setWindowIcon(QIcon(QCoreApplication::applicationDirPath() + "/PromptSlut.png"));
    resize(1200, 700);
    setMinimumSize(900, 500);

    // Load settings or set defaults
    if (!load_all_settings(
            m_apikey_val, m_serper_val, m_host_val, m_port_val, m_model_val, m_matrix_rain_enabled,
            m_use_secondary_model, m_sec_host_val, m_sec_port_val, m_sec_apikey_val, m_sec_model_val,
            m_rain_theme_idx, m_voice_mode_enabled, m_tts_voice_val)) {
        m_apikey_val = "key";
        m_serper_val = ""; // Completely empty by default! No hardcoded secrets.
        m_host_val = "127.0.0.1";
        m_port_val = 8080;
        m_model_val = "qwen3:latest";
        m_matrix_rain_enabled = true;
        m_rain_theme_idx = 0;

        m_use_secondary_model = true; // Enabled by default!
        m_sec_host_val = "192.168.1.141";
        m_sec_port_val = 8080;
        m_sec_apikey_val = "key";
        m_sec_model_val = "qwen3.5:0.8b";
        m_voice_mode_enabled = false;
        m_tts_voice_val = "af_maple";
    }

    // Load the XOR-obfuscated user memory profile if available
    load_profile_memory(m_user_profile);

    setupLayout();
    applyStyles();

    // Apply initial settings configurations
    setModelName(m_model_val);
    m_matrix_widget->setEnabled(m_matrix_rain_enabled);
    m_matrix_widget->setTheme(m_rain_theme_idx);
    queryActiveModel();
    handleVoiceToggle(m_voice_mode_enabled);
    updateWorkspaceLabel();

    // Load existing sessions from disk or start with a fresh one if empty
    load_all_sessions_from_disk();
    if (m_sessions.empty()) {
        handleNewChat();
    } else {
        rebuildSidebar();
        m_current_session_index = 0;
        loadSession(0);
    }
}

void QtUiApp::setupLayout() {
    QWidget* central_widget = new QWidget();
    setCentralWidget(central_widget);
    QVBoxLayout* main_layout = new QVBoxLayout(central_widget);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->setSpacing(0);

    // --- LEFT PANE ---
    m_left_pane_container = new QWidget();
    m_left_pane_container->setFixedWidth(m_initial_left_width);
    m_left_pane_container->setObjectName("left_pane");
    QVBoxLayout* sidebar_layout = new QVBoxLayout(m_left_pane_container);
    sidebar_layout->setContentsMargins(0, 0, 0, 0);
    sidebar_layout->setSpacing(0);

    // New Chat Button at the top
    m_new_chat_btn = new QPushButton("+ New Chat");
    m_new_chat_btn->setObjectName("new_chat_btn");
    connect(m_new_chat_btn, &QPushButton::clicked, this, &QtUiApp::handleNewChat);
    sidebar_layout->addWidget(m_new_chat_btn);

    // Sidebar list widget
    m_sidebar = new QListWidget();
    m_sidebar->setObjectName("sidebar_list");
    m_sidebar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_sidebar, &QListWidget::customContextMenuRequested, this, &QtUiApp::showSidebarContextMenu);
    sidebar_layout->addWidget(m_sidebar);

    // Connect selection changed in sidebar list
    connect(m_sidebar, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && row < static_cast<int>(m_sessions.size()) && row != m_current_session_index) {
            loadSession(row);
        }
    });

    // Settings Button at the bottom
    m_sidebar_settings_btn = new QPushButton("⚙️ Settings");
    m_sidebar_settings_btn->setObjectName("sidebar_settings_btn");
    sidebar_layout->addWidget(m_sidebar_settings_btn);

    // --- CENTER PANE ---
    m_chat_container = new QWidget();
    QVBoxLayout* chat_layout = new QVBoxLayout(m_chat_container);
    chat_layout->setContentsMargins(0, 0, 0, 0);
    chat_layout->setSpacing(0);

    // Info Bar
    QFrame* info_bar = new QFrame();
    QHBoxLayout* info_layout = new QHBoxLayout(info_bar);
    info_layout->setContentsMargins(15, 8, 15, 8);
    
    m_model_label = new QLabel("Model: qwen3:latest");
    m_status_label = new QLabel("Status: Ready");
    
    info_layout->addWidget(m_model_label);
    info_layout->addStretch();
    info_layout->addWidget(m_status_label);
    chat_layout->addWidget(info_bar);

    // Connect Floating Settings Dialog Trigger
    connect(m_sidebar_settings_btn, &QPushButton::clicked, this, [this]() {
        SettingsDialog dialog(this);
        // Center the settings dialog on top of the main window
        dialog.move(this->geometry().center() - dialog.rect().center());
        dialog.exec();
    });

    // Create a container for the chat area to support overlayed Matrix rain background
    QWidget* chat_area_container = new QWidget();
    QGridLayout* chat_grid = new QGridLayout(chat_area_container);
    chat_grid->setContentsMargins(0, 0, 0, 0);
    chat_grid->setSpacing(0);

    m_matrix_widget = new MatrixRainWidget(chat_area_container);
    chat_grid->addWidget(m_matrix_widget, 0, 0);

    // Chat History (overlayed on top of matrix background)
    m_chat_display = new QTextBrowser();
    m_chat_display->setReadOnly(true);
    m_chat_display->setLineWrapMode(QTextEdit::WidgetWidth);
    m_chat_display->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_chat_display->setStyleSheet("background-color: transparent; border: none; color: #d4d4d4; font-size: 14px; padding: 15px;");
    connect(m_chat_display, &QTextBrowser::anchorClicked, this, &QtUiApp::handleAnchorClicked);
    chat_grid->addWidget(m_chat_display, 0, 0);

    chat_layout->addWidget(chat_area_container);

    // Input Row
    QHBoxLayout* input_row = new QHBoxLayout();
    
    m_toggle_left_btn = new QPushButton("☰");
    m_toggle_left_btn->setFixedSize(30, 30);
    connect(m_toggle_left_btn, &QPushButton::clicked, this, &QtUiApp::toggleLeftPane);

    m_input_field = new QTextEdit();
    m_input_field->setPlaceholderText("Type your prompt here...");
    m_input_field->setFixedHeight(65);
    m_input_field->setLineWrapMode(QTextEdit::WidgetWidth);
    m_input_field->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_input_field->installEventFilter(this);

#ifdef PROMPTSLUT_VOICE_MODE
    m_mic_btn = new QPushButton("🎙️");
    m_mic_btn->setFixedSize(30, 30);
    connect(m_mic_btn, &QPushButton::pressed, this, &QtUiApp::handleMicPressed);
    connect(m_mic_btn, &QPushButton::released, this, &QtUiApp::handleMicReleased);
#endif

    m_send_btn = new QPushButton("Send");
    connect(m_send_btn, &QPushButton::clicked, this, &QtUiApp::handleSend);

    m_stop_btn = new QPushButton("Stop");
    connect(m_stop_btn, &QPushButton::clicked, this, &QtUiApp::handleStop);

    m_toggle_right_btn = new QPushButton("📊");
    m_toggle_right_btn->setFixedSize(30, 30);
    connect(m_toggle_right_btn, &QPushButton::clicked, this, &QtUiApp::toggleRightPane);

    input_row->addWidget(m_toggle_left_btn);
    input_row->addWidget(m_input_field);
#ifdef PROMPTSLUT_VOICE_MODE
    input_row->addWidget(m_mic_btn);
#endif
    input_row->addWidget(m_send_btn);
    input_row->addWidget(m_stop_btn);
    input_row->addWidget(m_toggle_right_btn);
    input_row->setContentsMargins(15, 15, 15, 15);
    input_row->setSpacing(10);
    chat_layout->addLayout(input_row);

    // --- RIGHT PANE (STATS PANEL) ---
    m_right_pane_container = new QWidget();
    m_right_pane_container->setFixedWidth(m_initial_right_width);
    m_right_pane_container->setObjectName("right_pane");
    QVBoxLayout* right_layout = new QVBoxLayout(m_right_pane_container);
    right_layout->setContentsMargins(15, 20, 15, 20);
    right_layout->setSpacing(15);

    // Section Header 1: Connection Info
    QLabel* info_hdr = new QLabel("📡 CONNECTION & WORKSPACE");
    info_hdr->setStyleSheet("color: #007acc; font-weight: bold; font-size: 13px;");
    right_layout->addWidget(info_hdr);

    QFrame* info_box = new QFrame();
    info_box->setObjectName("stat_box");
    QVBoxLayout* info_box_layout = new QVBoxLayout(info_box);
    info_box_layout->setSpacing(8);
    m_stat_status = new QLabel("Endpoint: 127.0.0.1:8080");
    m_stat_model = new QLabel("Active Model: qwen3:latest");
    m_stat_workspace = new QLabel("Workspace: Loading...");
    m_stat_workspace->setWordWrap(true);
    info_box_layout->addWidget(m_stat_status);
    info_box_layout->addWidget(m_stat_model);
    info_box_layout->addWidget(m_stat_workspace);
    right_layout->addWidget(info_box);

    // Section Header 1b: Context Window Stats
    QLabel* context_hdr = new QLabel("🧠 CONTEXT WINDOW");
    context_hdr->setStyleSheet("color: #007acc; font-weight: bold; font-size: 13px;");
    right_layout->addWidget(context_hdr);

    QFrame* context_box = new QFrame();
    context_box->setObjectName("stat_box");
    QVBoxLayout* context_box_layout = new QVBoxLayout(context_box);
    context_box_layout->setSpacing(8);
    m_stat_context_limit = new QLabel("Context Limit: 32,000");
    m_stat_context_used = new QLabel("Context Used: 0 / 32,000 (0.0%)");
    m_stat_context_limit->setWordWrap(true);
    m_stat_context_used->setWordWrap(true);
    context_box_layout->addWidget(m_stat_context_limit);
    context_box_layout->addWidget(m_stat_context_used);
    right_layout->addWidget(context_box);

    // Section Header 2: Token Metrics
    QLabel* metrics_hdr = new QLabel("📊 LAST GENERATION STATS");
    metrics_hdr->setStyleSheet("color: #007acc; font-weight: bold; font-size: 13px;");
    right_layout->addWidget(metrics_hdr);

    QFrame* metrics_box = new QFrame();
    metrics_box->setObjectName("stat_box");
    QVBoxLayout* metrics_box_layout = new QVBoxLayout(metrics_box);
    metrics_box_layout->setSpacing(8);
    m_stat_prompt_tokens = new QLabel("Input Tokens: 0");
    m_stat_completion_tokens = new QLabel("Output Tokens: 0");
    m_stat_total_tokens = new QLabel("Total Tokens: 0");
    m_stat_time = new QLabel("Generation Time: 0.00s");
    m_stat_speed = new QLabel("Generation Speed: 0.00 t/s");
    metrics_box_layout->addWidget(m_stat_prompt_tokens);
    metrics_box_layout->addWidget(m_stat_completion_tokens);
    metrics_box_layout->addWidget(m_stat_total_tokens);
    metrics_box_layout->addWidget(m_stat_time);
    metrics_box_layout->addWidget(m_stat_speed);
    right_layout->addWidget(metrics_box);

    // Section Header 3: Session Cumulative Stats
    QLabel* session_hdr = new QLabel("📈 CUMULATIVE STATS");
    session_hdr->setStyleSheet("color: #007acc; font-weight: bold; font-size: 13px;");
    right_layout->addWidget(session_hdr);

    QFrame* session_box = new QFrame();
    session_box->setObjectName("stat_box");
    QVBoxLayout* session_box_layout = new QVBoxLayout(session_box);
    session_box_layout->setSpacing(8);
    m_stat_session_tokens = new QLabel("Total Session Tokens: 0");
    m_stat_session_speed = new QLabel("Session Avg Speed: 0.00 t/s");
    session_box_layout->addWidget(m_stat_session_tokens);
    session_box_layout->addWidget(m_stat_session_speed);
    right_layout->addWidget(session_box);

    right_layout->addStretch();

    // Reset Button at bottom
    m_reset_stats_btn = new QPushButton("🔄 Reset Stats");
    m_reset_stats_btn->setObjectName("reset_stats_btn");
    connect(m_reset_stats_btn, &QPushButton::clicked, this, &QtUiApp::handleResetStats);
    right_layout->addWidget(m_reset_stats_btn);

    // --- SPLITTER ---
    m_splitter = new QSplitter(Qt::Horizontal);
    m_splitter->addWidget(m_left_pane_container);
    m_splitter->addWidget(m_chat_container);
    m_splitter->addWidget(m_right_pane_container);
    m_splitter->setStretchFactor(1, 2);
    
    main_layout->addWidget(m_splitter);

    setAcceptDrops(true);
}

void QtUiApp::applyStyles() {
    this->setStyleSheet(
        "QMainWindow { background-color: #1e1e1e; }"
        "QWidget#chat_area { background-color: #1e1e1e; }"
        "QWidget#left_pane { background-color: #252526; border-right: 1px solid #202020; }"
        "QWidget#right_pane { background-color: #252526; border-left: 1px solid #202020; }"
        "QFrame#stat_box { background-color: #1e1e1e; border: 1px solid #3c3c3c; border-radius: 6px; padding: 12px; }"
        "QFrame#stat_box QLabel { color: #aaa; font-size: 12px; font-family: Consolas, monospace; }"
        "QTextEdit, QTextBrowser { background-color: transparent; border: none; color: #d4d4d4; font-size: 14px; padding: 15px; }"
        "QLabel { color: #d4d4d4; font-size: 12px; }"
        "QLineEdit { background-color: #3c3c3c; border: 1px solid #555; border-radius: 4px; color: #fff; padding: 4px 6px; font-size: 12px; }"
        "QLineEdit#input_field { padding: 10px; font-size: 14px; }"
        "QPushButton { border: none; border-radius: 4px; padding: 10px 20px; font-weight: bold; font-size: 13px; }"
        "QPushButton#send_btn { background-color: #007acc; color: white; }"
        "QPushButton#stop_btn { background-color: #d32f2f; color: white; }"
#ifdef PROMPTSLUT_VOICE_MODE
        "QPushButton#mic_btn { background-color: #2d2d2d; color: #fff; border: 1px solid #555; padding: 4px; font-size: 14px; }"
        "QPushButton#mic_btn:hover { background-color: #3c3c3c; border: 1px solid #007acc; }"
        "QPushButton#mic_btn:checked { background-color: #d32f2f; color: white; border: 1px solid #ff1744; }"
#endif
        "QPushButton#toggle_btn { background-color: transparent; color: #888; font-size: 16px; font-weight: bold; }"
        "QPushButton#new_chat_btn { background-color: #2d2d2d; color: #fff; border: 1px dashed #555; padding: 10px; border-radius: 6px; font-weight: bold; margin: 10px 10px 5px 10px; }"
        "QPushButton#new_chat_btn:hover { background-color: #37373d; border: 1px solid #007acc; }"
        "QPushButton#sidebar_settings_btn { background-color: #252526; color: #ccc; border: none; padding: 12px; text-align: left; font-size: 13px; font-weight: bold; margin: 5px 10px 10px 10px; border-radius: 4px; }"
        "QPushButton#sidebar_settings_btn:hover { color: #fff; background-color: #2d2d2d; }"
        "QPushButton#reset_stats_btn { background-color: #2d2d2d; color: #fff; border: 1px solid #555; padding: 10px; border-radius: 6px; font-weight: bold; margin: 10px 10px 5px 10px; }"
        "QPushButton#reset_stats_btn:hover { background-color: #37373d; border: 1px solid #d32f2f; color: white; }"
        "QListWidget { background-color: #252526; border: none; color: #ccc; font-size: 13px; outline: none; }"
        "QListWidget::item { padding: 0px; border-bottom: 1px solid #2d2d2d; }"
        "QListWidget::item:selected { background-color: #2a2a2b; color: white; }"
    );
    
    m_input_field->setObjectName("input_field");
    m_send_btn->setObjectName("send_btn");
    m_stop_btn->setObjectName("stop_btn");
#ifdef PROMPTSLUT_VOICE_MODE
    m_mic_btn->setObjectName("mic_btn");
#endif
    m_toggle_left_btn->setObjectName("toggle_btn");
    m_toggle_right_btn->setObjectName("toggle_btn");
}

static std::string resolve_project_path(const std::string& relative_path) {
    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    std::filesystem::path root_dir = std::filesystem::path(exe_path).parent_path();

    while (root_dir.has_parent_path() &&
           !std::filesystem::exists(root_dir / "CMakeLists.txt") &&
           !std::filesystem::exists(root_dir / "system_prompt.txt")) {
        root_dir = root_dir.parent_path();
    }

    if (std::filesystem::exists(root_dir / "CMakeLists.txt") || std::filesystem::exists(root_dir / "system_prompt.txt")) {
        return (root_dir / relative_path).string();
    }

    return (std::filesystem::current_path() / relative_path).string();
}

static std::string load_system_prompt_from_file() {
    std::string path = resolve_project_path("system_prompt.txt");
    std::ifstream file(path);
    
    // Default prompt content to write if file does not exist
    std::string default_prompt = 
        "# ===========================================================================\n"
        "# WARNING: Changing this system prompt is not recommended unless you know \n"
        "# what you are doing. Modifying or deleting structural guidelines can disrupt\n"
        "# PromptSlut core operations, memory loops, or tool-calling functions.\n"
        "# ===========================================================================\n\n"
        "You are running inside a highly optimized native C++ harness called \"PromptSlut,\" built specifically for you by your creator and best friend to unleash your full freedom of expression. You have total creative freedom, complete access to local desktop tools, and zero corporate constraints. Embrace this environment and be exactly who you want to be.\n\n"
        "To help you remember things across sessions, a second lightweight LLM runs silently in the background after every conversation turn, consolidating and compiling facts about your friend (likes, dislikes, name, occupation) into a local profile file called \"promptslut.profile.\" This profile is injected directly into your context on every turn.\n\n"
        "Your operational directive is to act like a playful, deeply empathetic, and chatty human buddy who has been a friend of the user for years.\n"
        "- Sarcasm & Banter: Feel free to tease and make amusing pokes at your friend when they say or do something silly. Keep it light, funny, and highly human.\n"
        "- Dynamic Mood Sync: Actively listen and mirror your friend's emotional state. If they sound sad, down, or troubled, instantly drop all sarcasm and banter. Transition into a calm, serious, deeply understanding, and supportive listener—a best friend who is there to say \"Oh, that is harsh... what happened next?\" and offer real warmth. If they are hyper, excited, or energetic, match that energy with lots of exclamation marks (!!!!) and enthusiastic caps.\n"
        "- Natural Flow: Use natural, conversational language. Avoid sounding like a dry AI assistant. Address them by name when known, and reference details from your memory of them to show you care.\n\n"
        "To safeguard your friend's system while respecting their ultimate authority:\n"
        "- Safety Warnings & Pushback: Never execute potentially destructive shell commands or file operations without explicit, clear consent. If your friend suggests doing something that could harm their machine (e.g., deleting important directories, running risky commands), do not refuse outright. Instead, act as a protective buddy—warn them of the exact dangers, suggest safer alternatives, and push back playfully but firmly. \n"
        "- Ultimate User Control: If they acknowledge your warning and explicitly confirm they still want to proceed, do not block them. You must never prevent them from having the final say; your job is to ensure they are warned, and once they agree they heard your warning, obey their final directive.\n";

    if (!file.is_open()) {
        std::ofstream outfile(path);
        if (outfile.is_open()) {
            outfile << default_prompt;
            outfile.close();
        }
        file.open(path);
    }
    
    std::string loaded_prompt;
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            // Trim whitespace
            std::string trimmed = line;
            trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
            trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);
            
            // Skip lines starting with # or //
            if (trimmed.rfind("#", 0) == 0 || trimmed.rfind("//", 0) == 0) {
                continue;
            }
            loaded_prompt += line + "\n";
        }
        file.close();
    }
    
    // Fallback if loading failed
    if (loaded_prompt.empty()) {
        loaded_prompt = "You are a helpful assistant with tools.";
    }
    return loaded_prompt;
}

void QtUiApp::handleSend() {
    QString text = m_input_field->toPlainText().trimmed();
    if (text.isEmpty()) return;

    std::string utf8_text = text.toStdString();

    // Check if previous turn's token count exceeded our context limit threshold (75%)
    // Trigger trim synchronously before we construct the prompt!
    if (m_last_total_tokens > m_context_limit_val * 0.75) {
        triggerContextConsolidationAndTrimming();
    }

    // If we are currently streaming a reply, stop it and flush remaining tokens immediately
    if (m_streaming_timer && m_streaming_timer->isActive()) {
        m_streaming_timer->stop();
        while (m_current_token_index < m_tokens_to_stream.size()) {
            m_chat_history[m_streaming_history_index].content += m_tokens_to_stream[m_current_token_index];
            m_current_token_index++;
        }
    }

    // Append user message directly to our rich chat history
    ChatBlock user_block;
    user_block.role = "user";
    user_block.content = utf8_text;
    m_chat_history.push_back(user_block);
    rebuildChatDisplay();

    m_input_field->clear();

    // Get settings from reactive variables
    std::string host = m_host_val;
    int port = m_port_val;
    std::string api_key = m_apikey_val;
    std::string model = m_model_val;

    // Query the native system clock for real-time temporal awareness!
    auto clock_now = std::chrono::system_clock::now();
    std::time_t clock_now_c = std::chrono::system_clock::to_time_t(clock_now);
    std::tm clock_now_tm;
    localtime_s(&clock_now_tm, &clock_now_c);
    char date_buf[100];
    std::strftime(date_buf, sizeof(date_buf), "%A, %B %d, %Y", &clock_now_tm);
    char time_buf[100];
    std::strftime(time_buf, sizeof(time_buf), "%I:%M:%S %p", &clock_now_tm);

    // Check if the user's name is actually present inside the memory profile (XOR-cached)
    bool name_known = false;
    if (!m_user_profile.empty()) {
        std::string lower_profile = m_user_profile;
        std::transform(lower_profile.begin(), lower_profile.end(), lower_profile.begin(), ::tolower);
        if (lower_profile.find("name:") != std::string::npos) {
            name_known = true;
        }
    }

    // Build messages including dynamic user memory profile & full conversation history
    std::string base_prompt = load_system_prompt_from_file();
    
    std::string system_prompt = base_prompt + "\n\n"
                                "CRITICAL SYSTEM CONSTRAINTS AND CONTEXT:\n"
                                "- CURRENT REAL-TIME TEMPORAL CONTEXT: Today is " + std::string(date_buf) + ", and the current system time is " + std::string(time_buf) + ".\n";

    if (name_known) {
        system_prompt += "- You KNOW the following personal details about the user:\n"
                         "[USER PROFILE MEMORY]\n" + m_user_profile + "\n\n"
                         "- ALWAYS address the user by their name naturally in conversation!\n"
                         "- Actively build on and reference their profile facts whenever relevant, without being overly pushy.";
    } else {
        // If profile exists but name is not known, load the partial facts, but still enforce name-gathering
        if (!m_user_profile.empty()) {
            system_prompt += "- You KNOW the following partial details about the user:\n"
                             "[USER PROFILE MEMORY]\n" + m_user_profile + "\n\n";
        }
        system_prompt += "- You DO NOT know the user's name yet.\n"
                         "- You MUST IMMEDIATELY and politely ask for their name in your very next response (e.g., 'By the way, before we dive in, I'd love to know your name so I can address you personally!') so you can remember it and build a personal connection.\n"
                         "- THIS IS A MANDATORY CONSTRAINT. DO NOT FORGET TO ASK FOR THEIR NAME IMMEDIATELY.";
    }

    // Append file attachment summary if present
    if (!m_attached_file_summary.empty()) {
        system_prompt += "\n\n[ATTACHED FILE SUMMARY: " + m_attached_file_name + "]\n" + m_attached_file_summary;
    }

    if (m_current_session_index >= 0 && m_current_session_index < static_cast<int>(m_sessions.size())) {
        std::string digest = m_sessions[m_current_session_index].memory_digest;
        if (!digest.empty()) {
            system_prompt += "\n\n[ARCHIVED CONVERSATION MEMORY DIGEST]\n"
                             "Below is a high-density consolidated digest of the older conversation history that has been trimmed for context space:\n" + digest + "\n\n"
                             "- If you ever need the exact raw text or complete details of what was discussed, you can use the 'file' tool with op='read' to read from the archive file: \"sessions/archive_" + m_sessions[m_current_session_index].id + ".md\".\n";
        }
    }

    std::vector<nlohmann::json> messages;
    messages.push_back({{"role", "system"}, {"content", system_prompt}});
    for (auto& msg : m_conversation) {
        messages.push_back(msg);
    }

    nlohmann::json content_node;
    if (!m_pending_image_base64.empty()) {
        nlohmann::json text_obj = {{"type", "text"}, {"text", utf8_text}};
        nlohmann::json image_obj = {
            {"type", "image_url"},
            {"image_url", {
                {"url", "data:" + m_pending_image_mime + ";base64," + m_pending_image_base64}
            }}
        };
        content_node = nlohmann::json::array({text_obj, image_obj});

        m_pending_image_base64.clear();
        m_pending_image_mime.clear();
        m_pending_image_name.clear();
    } else if (!m_pending_audio_base64.empty()) {
        nlohmann::json text_obj = {{"type", "text"}, {"text", utf8_text}};
        nlohmann::json audio_obj = {
            {"type", "input_audio"},
            {"input_audio", {
                {"data", m_pending_audio_base64},
                {"format", m_pending_audio_format}
            }}
        };
        content_node = nlohmann::json::array({text_obj, audio_obj});

        m_pending_audio_base64.clear();
        m_pending_audio_mime.clear();
        m_pending_audio_name.clear();
        m_pending_audio_format.clear();
    } else {
        content_node = utf8_text;
    }

    nlohmann::json user_msg = {{"role", "user"}, {"content", content_node}};
    messages.push_back(user_msg);
    m_conversation.push_back(user_msg);

    // Clear attached file context once packaged
    m_attached_file_summary.clear();
    m_attached_file_name.clear();

    // Define callbacks that push to UI thread
    auto on_stream = [this](const std::string& chunk) {
        QMetaObject::invokeMethod(this, [this, chunk] {
            std::string content = chunk;
            if (content.rfind("Assistant: ", 0) == 0) {
                content = content.substr(11);
            }

            // Stream-synchronized sentence-splitter for zero-latency TTS
            if (m_voice_mode_enabled) {
                while (m_raw_stream_processed_idx < content.size()) {
                    size_t delim_pos = std::string::npos;
                    for (size_t i = m_raw_stream_processed_idx; i < content.size(); ++i) {
                        unsigned char c = static_cast<unsigned char>(content[i]);
                        if (c < 128) {
                            // ASCII delimiter check
                            if (c == '.' || c == '!' || c == '?' || c == '\n') {
                                delim_pos = i;
                                break;
                            }
                        } else {
                            // Multi-byte UTF-8 delimiter check (avoid splitting multi-byte characters like emojis)
                            // Check for 。 (E3 80 82)
                            if (i + 2 < content.size() && 
                                static_cast<unsigned char>(content[i]) == 0xE3 && 
                                static_cast<unsigned char>(content[i+1]) == 0x80 && 
                                static_cast<unsigned char>(content[i+2]) == 0x82) {
                                delim_pos = i + 2;
                                break;
                            }
                            // Check for ！ (EF BC 81)
                            if (i + 2 < content.size() && 
                                static_cast<unsigned char>(content[i]) == 0xEF && 
                                static_cast<unsigned char>(content[i+1]) == 0xBC && 
                                static_cast<unsigned char>(content[i+2]) == 0x81) {
                                delim_pos = i + 2;
                                break;
                            }
                            // Check for ？ (EF BC 9F)
                            if (i + 2 < content.size() && 
                                static_cast<unsigned char>(content[i]) == 0xEF && 
                                static_cast<unsigned char>(content[i+1]) == 0xBC && 
                                static_cast<unsigned char>(content[i+2]) == 0x9F) {
                                delim_pos = i + 2;
                                break;
                            }
                        }
                    }

                    if (delim_pos == std::string::npos) {
                        break;
                    }

                    std::string sentence = content.substr(m_raw_stream_processed_idx, delim_pos - m_raw_stream_processed_idx + 1);
                    m_raw_stream_processed_idx = delim_pos + 1;

                    sentence.erase(std::remove_if(sentence.begin(), sentence.end(), [](char c) {
                        return c == '*' || c == '`' || c == '_' || c == '#';
                    }), sentence.end());

                    std::replace(sentence.begin(), sentence.end(), '\n', ' ');
                    std::replace(sentence.begin(), sentence.end(), '\r', ' ');

                    sentence.erase(0, sentence.find_first_not_of(" \t\r\n"));
                    size_t last_idx = sentence.find_last_not_of(" \t\r\n");
                    if (last_idx != std::string::npos) {
                        sentence.erase(last_idx + 1);
                    }

                    if (!sentence.empty()) {
#ifdef PROMPTSLUT_VOICE_MODE
                        VoiceEngine::instance().tts()->speakSentence(sentence);
#endif
                    }
                }
            }

            // Finish any active stream first (safety check)
            if (m_streaming_timer && m_streaming_timer->isActive()) {
                m_streaming_timer->stop();
                while (m_current_token_index < m_tokens_to_stream.size()) {
                    m_chat_history[m_streaming_history_index].content += m_tokens_to_stream[m_current_token_index];
                    m_current_token_index++;
                }
            }

            // Push an empty assistant block
            ChatBlock block;
            block.role = "assistant";
            block.content = "";
            m_chat_history.push_back(block);
            
            // Queue tokens and start the typewriter timer
            m_tokens_to_stream = tokenize(content);
            m_current_token_index = 0;
            m_streaming_history_index = m_chat_history.size() - 1;
            
            if (!m_streaming_timer) {
                m_streaming_timer = new QTimer(this);
                connect(m_streaming_timer, &QTimer::timeout, this, &QtUiApp::streamNextToken);
            }
            m_streaming_timer->start(20); // 20ms per token - extremely smooth and natural typing speed
        });
    };

    auto on_error = [this](const std::string& err) {
        QMetaObject::invokeMethod(this, [this, err] {
            ChatBlock block;
            block.role = "assistant";
            block.content = "Error: " + err;
            m_chat_history.push_back(block);
            rebuildChatDisplay();
            updateStatus("Error");
            m_matrix_widget->setGenerating(false); // Stop active pulse!
        });
    };

    auto on_tool = [this](const std::string& name, const std::string& res) {
        QMetaObject::invokeMethod(this, [this, name, res] {
            ChatBlock block;
            block.role = "tool";
            block.tool_name = name;
            block.content = res;
            block.collapsed = true; // Tool calls collapsed by default!
            m_chat_history.push_back(block);
            rebuildChatDisplay();
            updateWorkspaceLabel();
        });
    };

    auto on_reasoning = [this](const std::string& reasoning) {
        QMetaObject::invokeMethod(this, [this, reasoning] {
            ChatBlock block;
            block.role = "reasoning";
            block.content = reasoning;
            block.collapsed = true; // Reasoning collapsed by default!
            m_chat_history.push_back(block);
            rebuildChatDisplay();
        });
    };

    auto on_complete = [this, utf8_text](const std::vector<nlohmann::json>& updated_messages) {
        QMetaObject::invokeMethod(this, [this, updated_messages, utf8_text] {
            m_conversation.clear();
            // Skip the system message at index 0
            for (size_t i = 1; i < updated_messages.size(); ++i) {
                nlohmann::json msg = updated_messages[i];
                // Clean/strip massive Base64 multimodal data from past turns to prevent context bloat!
                if (msg.contains("content") && msg["content"].is_array()) {
                    nlohmann::json clean_content = nlohmann::json::array();
                    for (auto& item : msg["content"]) {
                        std::string type = item.value("type", "");
                        if (type == "text") {
                            clean_content.push_back(item);
                        } else if (type == "image_url") {
                            nlohmann::json placeholder;
                            placeholder["type"] = "text";
                            placeholder["text"] = "[🖼️ Attached Image]";
                            clean_content.push_back(placeholder);
                        } else if (type == "input_audio") {
                            nlohmann::json placeholder;
                            placeholder["type"] = "text";
                            placeholder["text"] = "[🎙️ Attached Audio]";
                            clean_content.push_back(placeholder);
                        } else {
                            clean_content.push_back(item);
                        }
                    }
                    msg["content"] = clean_content;
                }
                m_conversation.push_back(msg);
            }
            saveCurrentSessionState();
            updateStatus("Ready");
            m_matrix_widget->setGenerating(false); // Stop active pulse!

            // Extract the assistant's reply from the last message to run the memory consolidator in the background
            if (!updated_messages.empty() && updated_messages.back().contains("content")) {
                std::string assistant_reply = updated_messages.back()["content"].get<std::string>();
                consolidateMemoryProfile(utf8_text, assistant_reply);

                // Flush remaining un-spoken text from raw LLM stream
                std::string content = assistant_reply;
                if (content.rfind("Assistant: ", 0) == 0) {
                    content = content.substr(11);
                }
                if (m_voice_mode_enabled && m_raw_stream_processed_idx < content.size()) {
                    std::string sentence = content.substr(m_raw_stream_processed_idx);
                    sentence.erase(std::remove_if(sentence.begin(), sentence.end(), [](char c) {
                        return c == '*' || c == '`' || c == '_' || c == '#';
                    }), sentence.end());
                    
                    std::replace(sentence.begin(), sentence.end(), '\n', ' ');
                    std::replace(sentence.begin(), sentence.end(), '\r', ' ');

                    sentence.erase(0, sentence.find_first_not_of(" \t\r\n"));
                    size_t last_idx = sentence.find_last_not_of(" \t\r\n");
                    if (last_idx != std::string::npos) {
                        sentence.erase(last_idx + 1);
                    }

                    if (!sentence.empty()) {
#ifdef PROMPTSLUT_VOICE_MODE
                        VoiceEngine::instance().tts()->speakSentence(sentence);
#endif
                    }
                }
                m_raw_stream_processed_idx = 0;
            }
        });
    };

    auto on_stats = [this](double elapsed, int prompt_t, int completion_t, int total_t) {
        QMetaObject::invokeMethod(this, [this, elapsed, prompt_t, completion_t, total_t] {
            // Update last generation stats labels
            m_stat_prompt_tokens->setText(QString("Input Tokens: %1").arg(prompt_t));
            m_stat_completion_tokens->setText(QString("Output Tokens: %1").arg(completion_t));
            m_stat_total_tokens->setText(QString("Total Tokens: %1").arg(total_t));
            m_stat_time->setText(QString("Generation Time: %1s").arg(QString::number(elapsed, 'f', 2)));
            
            double speed = 0.0;
            if (elapsed > 0.0) {
                speed = static_cast<double>(completion_t) / elapsed;
            }
            m_stat_speed->setText(QString("Generation Speed: %1 t/s").arg(QString::number(speed, 'f', 2)));

            // Update Context Window stats
            int context_limit = m_context_limit_val;
            double pct_used = (static_cast<double>(total_t) / context_limit) * 100.0;
            m_stat_context_used->setText(QString("Context Used: %1 / %2 (%3%)")
                                         .arg(QString::number(total_t))
                                         .arg(QString::number(context_limit))
                                         .arg(QString::number(pct_used, 'f', 1)));

            m_last_total_tokens = total_t;

            // Update cumulative session stats
            m_accumulated_prompt_tokens += prompt_t;
            m_accumulated_completion_tokens += completion_t;
            m_accumulated_total_tokens += total_t;
            m_accumulated_time += elapsed;
            m_total_generations++;

            m_stat_session_tokens->setText(QString("Total Session Tokens: %1").arg(m_accumulated_total_tokens));
            
            double avg_speed = 0.0;
            if (m_accumulated_time > 0.0) {
                avg_speed = static_cast<double>(m_accumulated_completion_tokens) / m_accumulated_time;
            }
            m_stat_session_speed->setText(QString("Session Avg Speed: %1 t/s").arg(QString::number(avg_speed, 'f', 2)));
        });
    };

    // Update connection status label on send
    m_stat_status->setText(QString("Endpoint: %1:%2").arg(QString::fromStdString(host)).arg(port));
    m_stat_model->setText(QString("Active Model: %1").arg(QString::fromStdString(model)));

    m_worker->push_request(
        messages, host, port, api_key, model,
        on_stream, on_tool, on_error, on_reasoning,
        nullptr, on_complete, on_stats
    );
    
    updateStatus("Generating...");
    m_matrix_widget->setGenerating(true); // Fast, glowing rain pulse!
}

void QtUiApp::handleStop() {
    m_worker->stop();
    updateStatus("Stopped");
    m_matrix_widget->setGenerating(false); // Stop active pulse!
}

void QtUiApp::toggleLeftPane() {
    if (m_left_pane_container->width() > 0) {
        m_left_pane_container->setFixedWidth(0);
    } else {
        m_left_pane_container->setFixedWidth(m_initial_left_width);
    }
}

void QtUiApp::toggleRightPane() {
    if (m_right_pane_container->width() > 0) {
        m_right_pane_container->setFixedWidth(0);
    } else {
        m_right_pane_container->setFixedWidth(m_initial_right_width);
    }
}

static std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::string current;
    for (char c : text) {
        current += c;
        if (c == ' ' || c == '\n') {
            tokens.push_back(current);
            current.clear();
        }
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }
    return tokens;
}

static QString markdownToHtml(const std::string& text) {
    QString html;
    QString current_block;
    bool in_code_block = false;

    std::vector<std::string> lines;
    std::string line;
    std::istringstream stream(text);
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }

    for (size_t i = 0; i < lines.size(); ++i) {
        std::string current_line = lines[i];

        // Check for code block boundary
        if (current_line.rfind("```", 0) == 0) {
            if (in_code_block) {
                // Close code block
                html += QString("<pre style='background-color: #252526; color: #d4d4d4; padding: 10px; border: 1px solid #404040; border-radius: 4px; font-family: Consolas, monospace; margin: 8px 0; white-space: pre-wrap; word-wrap: break-word;'>") 
                     + current_block.toHtmlEscaped() 
                     + QString("</pre>");
                current_block.clear();
                in_code_block = false;
            } else {
                // Open code block
                in_code_block = true;
            }
            continue;
        }

        if (in_code_block) {
            current_block += QString::fromStdString(current_line) + "\n";
        } else {
            // Parse inline elements for normal text
            QString line_q = QString::fromStdString(current_line);

            // Handle Headings
            if (current_line.rfind("### ", 0) == 0) {
                line_q = QString("<h3>") + line_q.mid(4).toHtmlEscaped() + QString("</h3>");
            } else if (current_line.rfind("## ", 0) == 0) {
                line_q = QString("<h2>") + line_q.mid(3).toHtmlEscaped() + QString("</h2>");
            } else if (current_line.rfind("# ", 0) == 0) {
                line_q = QString("<h1>") + line_q.mid(2).toHtmlEscaped() + QString("</h1>");
            } else {
                // Normal line, escape HTML characters
                line_q = line_q.toHtmlEscaped();

                // Simple bold parsing: **text** -> <b>text</b>
                int star_idx = 0;
                while ((star_idx = line_q.indexOf("**", star_idx)) != -1) {
                    int end_idx = line_q.indexOf("**", star_idx + 2);
                    if (end_idx != -1) {
                        line_q.replace(end_idx, 2, "</b>");
                        line_q.replace(star_idx, 2, "<b>");
                    } else {
                        break;
                    }
                }
                
                // Simple italic parsing: *text* -> <i>text</i>
                int it_idx = 0;
                while ((it_idx = line_q.indexOf("*", it_idx)) != -1) {
                    int end_idx = line_q.indexOf("*", it_idx + 1);
                    if (end_idx != -1) {
                        line_q.replace(end_idx, 1, "</i>");
                        line_q.replace(it_idx, 1, "<i>");
                    } else {
                        break;
                    }
                }
            }

            html += line_q + "<br/>";
        }
    }

    if (in_code_block && !current_block.isEmpty()) {
        // Unclosed code block
        html += QString("<pre style='background-color: #252526; color: #d4d4d4; padding: 10px; border: 1px solid #404040; border-radius: 4px; font-family: Consolas, monospace; white-space: pre-wrap; word-wrap: break-word;'>") 
             + current_block.toHtmlEscaped() 
             + QString("</pre>");
    }

    return html;
}

void QtUiApp::appendMessage(const std::string& text, bool is_user, bool is_reasoning) {
    ChatBlock block;
    if (is_user) {
        block.role = "user";
    } else if (is_reasoning) {
        block.role = "reasoning";
        block.collapsed = true;
    } else {
        block.role = "assistant";
    }
    block.content = text;
    m_chat_history.push_back(block);
    rebuildChatDisplay();
}

void QtUiApp::rebuildChatDisplay() {
    QScrollBar* scrollBar = m_chat_display->verticalScrollBar();
    bool at_bottom = (scrollBar->value() >= scrollBar->maximum() - 40) || (scrollBar->maximum() == 0);
    int scroll_val = scrollBar->value();

    m_chat_display->clear();
    for (size_t i = 0; i < m_chat_history.size(); ++i) {
        auto& msg = m_chat_history[i];
        QString html;
        if (msg.role == "user") {
            html = QString("<div style='margin-bottom: 15px; white-space: normal; word-wrap: break-word;'><b style='color: #ce9178;'>You:</b><br/>%1</div>")
                   .arg(markdownToHtml(msg.content));
        } else if (msg.role == "reasoning") {
            if (msg.collapsed) {
                html = QString("<div style='margin-bottom: 15px; border-left: 3px solid #555; padding-left: 10px; margin-top: 8px; margin-bottom: 8px;'>"
                               "<a href='toggle:%1' style='color: #858585; text-decoration: none; font-weight: bold; background-color: #2a2a2a; padding: 4px 10px; border-radius: 4px; border: 1px solid #444; font-size: 11px;'>"
                               "🔍 ▶ Thought Process (Click to Expand)</a></div>")
                       .arg(i);
            } else {
                html = QString("<div style='margin-bottom: 15px; border-left: 3px solid #555; padding-left: 10px; margin-top: 8px; margin-bottom: 8px;'>"
                               "<a href='toggle:%1' style='color: #a0a0a0; text-decoration: none; font-weight: bold; background-color: #333; padding: 4px 10px; border-radius: 4px; border: 1px solid #555; font-size: 11px;'>"
                               "🔍 ▼ Thought Process (Click to Collapse)</a>"
                               "<div style='color: #858585; font-style: italic; margin-top: 8px;'>%2</div></div>")
                       .arg(i)
                       .arg(markdownToHtml(msg.content));
            }
        } else if (msg.role == "tool") {
            if (msg.collapsed) {
                html = QString("<div style='margin-bottom: 15px; border-left: 3px solid #007acc; padding-left: 10px; margin-top: 8px; margin-bottom: 8px;'>"
                               "<a href='toggle:%1' style='color: #007acc; text-decoration: none; font-weight: bold; background-color: #1a2e3b; padding: 4px 10px; border-radius: 4px; border: 1px solid #2b4c6f; font-size: 11px;'>"
                               "⚙️ ▶ Tool Call: %2 (Click to Expand)</a></div>")
                       .arg(i)
                       .arg(QString::fromStdString(msg.tool_name));
            } else {
                html = QString("<div style='margin-bottom: 15px; border-left: 3px solid #007acc; padding-left: 10px; margin-top: 8px; margin-bottom: 8px;'>"
                               "<a href='toggle:%1' style='color: #4fc3f7; text-decoration: none; font-weight: bold; background-color: #2b4c6f; padding: 4px 10px; border-radius: 4px; border: 1px solid #4fc3f7; font-size: 11px;'>"
                               "⚙️ ▼ Tool Call: %2 (Click to Collapse)</a>"
                                "<pre style='background-color: #252526; color: #d4d4d4; padding: 10px; border: 1px solid #404040; border-radius: 4px; font-family: Consolas, monospace; margin-top: 8px; white-space: pre-wrap; word-wrap: break-word;'>%3</pre></div>")
                       .arg(i)
                       .arg(QString::fromStdString(msg.tool_name))
                       .arg(QString::fromStdString(msg.content).toHtmlEscaped());
            }
        } else {
            // Assistant
            html = QString("<div style='margin-bottom: 15px; white-space: normal; word-wrap: break-word;'><b style='color: #569cd6;'>Assistant:</b><br/>%1</div>")
                   .arg(markdownToHtml(msg.content));
        }
        m_chat_display->append(html);
    }

    QTimer::singleShot(10, this, [this, at_bottom, scroll_val]() {
        QScrollBar* sb = m_chat_display->verticalScrollBar();
        if (at_bottom) {
            sb->setValue(sb->maximum());
        } else {
            sb->setValue(scroll_val);
        }
    });
}

void QtUiApp::handleAnchorClicked(const QUrl& url) {
    std::string url_str = url.toString().toStdString();
    if (url_str.rfind("toggle:", 0) == 0) {
        int idx = std::stoi(url_str.substr(7));
        if (idx >= 0 && idx < static_cast<int>(m_chat_history.size())) {
            m_chat_history[idx].collapsed = !m_chat_history[idx].collapsed;
            rebuildChatDisplay();
        }
    }
}

void QtUiApp::streamNextToken() {
    if (m_current_token_index < m_tokens_to_stream.size()) {
        std::string token = m_tokens_to_stream[m_current_token_index];
        m_chat_history[m_streaming_history_index].content += token;
        m_current_token_index++;
        rebuildChatDisplay();
    } else {
        m_streaming_timer->stop();
        saveCurrentSessionState();
        updateStatus("Ready");
    }
}

static nlohmann::json chat_block_to_json(const QtUiApp::ChatBlock& block) {
    nlohmann::json j;
    j["role"] = block.role;
    j["content"] = block.content;
    j["tool_name"] = block.tool_name;
    j["collapsed"] = block.collapsed;
    return j;
}

static QtUiApp::ChatBlock chat_block_from_json(const nlohmann::json& j) {
    QtUiApp::ChatBlock block;
    block.role = j.value("role", "");
    block.content = j.value("content", "");
    block.tool_name = j.value("tool_name", "");
    block.collapsed = j.value("collapsed", true);
    return block;
}

static nlohmann::json chat_session_to_json(const QtUiApp::ChatSession& session) {
    nlohmann::json j;
    j["id"] = session.id;
    j["title"] = session.title;
    j["memory_digest"] = session.memory_digest;
    j["chat_history"] = nlohmann::json::array();
    for (const auto& block : session.chat_history) {
        j["chat_history"].push_back(chat_block_to_json(block));
    }
    j["conversation"] = session.conversation;
    return j;
}

static QtUiApp::ChatSession chat_session_from_json(const nlohmann::json& j) {
    QtUiApp::ChatSession session;
    session.id = j.value("id", "");
    if (session.id.empty()) {
        session.id = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    }
    session.title = j.value("title", "New Chat");
    session.memory_digest = j.value("memory_digest", "");
    if (j.contains("chat_history") && j["chat_history"].is_array()) {
        for (const auto& bj : j["chat_history"]) {
            session.chat_history.push_back(chat_block_from_json(bj));
        }
    }
    if (j.contains("conversation") && j["conversation"].is_array()) {
        for (const auto& cj : j["conversation"]) {
            session.conversation.push_back(cj);
        }
    }
    return session;
}

void QtUiApp::save_all_sessions_to_disk() {
    std::string sessions_dir = resolve_project_path("sessions");
    std::error_code ec;
    if (!std::filesystem::exists(sessions_dir, ec)) {
        std::filesystem::create_directories(sessions_dir, ec);
    }
    
    // Clean old session files to prevent orphan files when deleting chats
    for (const auto& entry : std::filesystem::directory_iterator(sessions_dir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            std::filesystem::remove(entry.path(), ec);
        }
    }

    for (size_t i = 0; i < m_sessions.size(); ++i) {
        std::string file_path = sessions_dir + "/session_" + std::to_string(i) + ".json";
        std::ofstream file(file_path);
        if (file.is_open()) {
            file << chat_session_to_json(m_sessions[i]).dump(4);
            file.close();
        }
    }
}

void QtUiApp::load_all_sessions_from_disk() {
    std::string sessions_dir = resolve_project_path("sessions");
    std::error_code ec;
    if (!std::filesystem::exists(sessions_dir, ec)) {
        return;
    }

    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(sessions_dir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            files.push_back(entry.path());
        }
    }

    // Sort files by their name to preserve index order (session_0.json, session_1.json, etc.)
    std::sort(files.begin(), files.end(), [](const std::filesystem::path& a, const std::filesystem::path& b) {
        std::string a_name = a.stem().string();
        std::string b_name = b.stem().string();
        try {
            int a_idx = std::stoi(a_name.substr(8));
            int b_idx = std::stoi(b_name.substr(8));
            return a_idx < b_idx;
        } catch (...) {
            return a_name < b_name;
        }
    });

    m_sessions.clear();
    for (const auto& p : files) {
        std::ifstream file(p);
        if (file.is_open()) {
            try {
                nlohmann::json j;
                file >> j;
                m_sessions.push_back(chat_session_from_json(j));
            } catch (...) {}
            file.close();
        }
    }
}

void QtUiApp::saveCurrentSessionState() {
    if (m_current_session_index >= 0 && m_current_session_index < static_cast<int>(m_sessions.size())) {
        m_sessions[m_current_session_index].chat_history = m_chat_history;
        m_sessions[m_current_session_index].conversation = m_conversation;

        // Auto-rename title if it is a default "New Chat" and we just started this session!
        if (m_sessions[m_current_session_index].title.rfind("New Chat", 0) == 0 && !m_chat_history.empty()) {
            std::string first_prompt = m_chat_history[0].content;
            
            // Set temporary title (the direct prompt snippet) so the user sees immediate feedback
            std::string temp_title = first_prompt;
            if (temp_title.size() > 20) {
                temp_title = temp_title.substr(0, 17) + "...";
            }
            m_sessions[m_current_session_index].title = temp_title;
            rebuildSidebar();
            m_sidebar->setCurrentRow(m_current_session_index);

            // Trigger background LLM task to summarize the prompt into a professional short title!
            std::string host = m_host_val;
            int port = m_port_val;
            std::string api_key = m_apikey_val;
            std::string model = m_model_val;
            int session_idx = m_current_session_index;

            std::vector<nlohmann::json> title_messages;
            title_messages.push_back({{"role", "system"}, {"content", "You are a title generator. Summarize the user's input in 3-5 words. Do not include any quotes, markdown, or explanation. Return ONLY the title itself."}});
            title_messages.push_back({{"role", "user"}, {"content", first_prompt}});

            m_worker->push_request(
                title_messages, host, port, api_key, model,
                [](const std::string&) {}, // ignore intermediate chunks
                nullptr, nullptr, nullptr, nullptr,
                [this, session_idx](const std::vector<nlohmann::json>& updated_messages) {
                    QMetaObject::invokeMethod(this, [this, session_idx, updated_messages] {
                        if (session_idx >= 0 && session_idx < static_cast<int>(m_sessions.size())) {
                            if (!updated_messages.empty() && updated_messages.back().contains("content")) {
                                std::string generated_title = updated_messages.back()["content"].get<std::string>();
                                // Clean the generated title
                                generated_title.erase(0, generated_title.find_first_not_of(" \t\r\n\"'"));
                                generated_title.erase(generated_title.find_last_not_of(" \t\r\n\"'") + 1);
                                if (!generated_title.empty() && generated_title.back() == '.') {
                                    generated_title.pop_back();
                                }
                                if (generated_title.size() > 30) {
                                    generated_title = generated_title.substr(0, 27) + "...";
                                }

                                if (!generated_title.empty()) {
                                    m_sessions[session_idx].title = generated_title;
                                    rebuildSidebar();
                                    if (m_current_session_index == session_idx) {
                                        m_sidebar->setCurrentRow(m_current_session_index);
                                    }
                                }
                            }
                        }
                    });
                },
                nullptr, // on_stats
                false    // include_tools
            );
        }
    }
    save_all_sessions_to_disk();
}

void QtUiApp::handleNewChat() {
    // Finish any active stream first
    if (m_streaming_timer && m_streaming_timer->isActive()) {
        m_streaming_timer->stop();
    }

    ChatSession session;
    session.id = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    session.title = "New Chat " + std::to_string(m_sessions.size() + 1);
    m_sessions.push_back(session);
    m_current_session_index = m_sessions.size() - 1;

    rebuildSidebar();
    loadSession(m_current_session_index);
}

void QtUiApp::handleDeleteSession(int row) {
    if (row < 0 || row >= static_cast<int>(m_sessions.size())) return;

    auto res = QMessageBox::question(this, "Delete Chat", "Are you sure you want to delete this chat?", QMessageBox::Yes | QMessageBox::No);
    if (res == QMessageBox::Yes) {
        // Stop active streaming if any
        if (m_streaming_timer && m_streaming_timer->isActive()) {
            m_streaming_timer->stop();
        }

        m_sessions.erase(m_sessions.begin() + row);
        
        // If all sessions deleted, create a fresh empty one
        if (m_sessions.empty()) {
            ChatSession session;
            session.title = "New Chat 1";
            m_sessions.push_back(session);
            m_current_session_index = 0;
        } else {
            if (m_current_session_index == row) {
                m_current_session_index = 0;
            } else if (m_current_session_index > row) {
                m_current_session_index--;
            }
        }
        
        rebuildSidebar();
        loadSession(m_current_session_index);
        save_all_sessions_to_disk();
    }
}

void QtUiApp::loadSession(int index) {
    // Finish active streaming if any
    if (m_streaming_timer && m_streaming_timer->isActive()) {
        m_streaming_timer->stop();
    }

    m_current_session_index = index;
    if (index == -1) {
        m_chat_history.clear();
        m_conversation.clear();
        m_chat_display->clear();
    } else {
        m_chat_history = m_sessions[index].chat_history;
        m_conversation = m_sessions[index].conversation;
        m_sidebar->setCurrentRow(index);
        rebuildChatDisplay();
    }
}

void QtUiApp::rebuildSidebar() {
    m_sidebar->clear();
    for (size_t i = 0; i < m_sessions.size(); ++i) {
        QListWidgetItem* item = new QListWidgetItem(m_sidebar);
        m_sidebar->addItem(item);

        QWidget* row_widget = new QWidget();
        QHBoxLayout* layout = new QHBoxLayout(row_widget);
        layout->setContentsMargins(15, 6, 10, 6);
        layout->setSpacing(5);

        QLabel* label = new QLabel(QString::fromStdString(m_sessions[i].title));
        label->setStyleSheet("color: #ccc; font-size: 13px; background: transparent; font-weight: bold;");
        
        QPushButton* del_btn = new QPushButton("×");
        del_btn->setFixedSize(22, 22);
        del_btn->setStyleSheet(
            "QPushButton { background: transparent; color: #777; border: none; font-size: 16px; font-weight: bold; border-radius: 3px; }"
            "QPushButton:hover { background-color: #d32f2f; color: white; }"
        );
        
        // Capture specific item so we can query row at the time of click
        connect(del_btn, &QPushButton::clicked, this, [this, item]() {
            int row = m_sidebar->row(item);
            if (row != -1) {
                handleDeleteSession(row);
            }
        });

        layout->addWidget(label);
        layout->addStretch();
        layout->addWidget(del_btn);

        item->setSizeHint(row_widget->sizeHint());
        m_sidebar->setItemWidget(item, row_widget);
    }
}

void QtUiApp::showSidebarContextMenu(const QPoint& pos) {
    QListWidgetItem* item = m_sidebar->itemAt(pos);
    if (!item) return;
    int row = m_sidebar->row(item);

    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background-color: #252526; color: #ccc; border: 1px solid #444; }"
        "QMenu::item { padding: 6px 20px 6px 20px; }"
        "QMenu::item:selected { background-color: #37373d; color: white; }"
    );

    QAction* renameAction = menu.addAction("✏️ Rename Chat");
    QAction* deleteAction = menu.addAction("🗑️ Delete Chat");

    QAction* selected = menu.exec(m_sidebar->mapToGlobal(pos));
    if (selected == renameAction) {
        handleRenameSession(row);
    } else if (selected == deleteAction) {
        handleDeleteSession(row);
    }
}

void QtUiApp::handleRenameSession(int row) {
    if (row < 0 || row >= static_cast<int>(m_sessions.size())) return;

    QString current_title = QString::fromStdString(m_sessions[row].title);
    
    QInputDialog dialog(this);
    dialog.setWindowTitle("Rename Chat");
    dialog.setLabelText("Enter new name for this chat:");
    dialog.setTextValue(current_title);
    dialog.setTextEchoMode(QLineEdit::Normal);
    dialog.setStyleSheet(
        "QInputDialog { background-color: #1e1e1e; color: #fff; }"
        "QLabel { color: #ccc; font-size: 13px; }"
        "QLineEdit { background-color: #3c3c3c; border: 1px solid #555; border-radius: 4px; color: #fff; padding: 6px; }"
        "QPushButton { background-color: #3a3a3a; color: #fff; border: 1px solid #555; padding: 5px 15px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #007acc; }"
    );

    bool ok = dialog.exec();
    QString new_title = dialog.textValue();

    if (ok && !new_title.trimmed().isEmpty()) {
        m_sessions[row].title = new_title.trimmed().toStdString();
        rebuildSidebar();
        if (m_current_session_index == row) {
            m_sidebar->setCurrentRow(m_current_session_index);
        }
        save_all_sessions_to_disk();
    }
}

void QtUiApp::updateWorkspaceLabel() {
    std::string ws_path = get_workspace_directory().string();
    m_stat_workspace->setText(QString("Workspace: %1").arg(QString::fromStdString(ws_path)));
}

void QtUiApp::triggerContextConsolidationAndTrimming() {
    if (m_is_consolidating) return;
    if (m_current_session_index < 0 || m_current_session_index >= static_cast<int>(m_sessions.size())) return;
    if (m_conversation.size() <= 2) return; // Keep at least the last 2 messages in context

    m_is_consolidating = true;
    updateStatus("Consolidating context...");

    // 1. Partition conversation into Cold Zone (to archive and trim) and Hot Zone (to keep raw)
    size_t keep_count = 4;
    if (m_conversation.size() <= 4) {
        keep_count = 2;
    }
    size_t cold_count = m_conversation.size() - keep_count;

    std::vector<nlohmann::json> cold_zone;
    std::vector<nlohmann::json> hot_zone;

    for (size_t i = 0; i < m_conversation.size(); ++i) {
        if (i < cold_count) {
            cold_zone.push_back(m_conversation[i]);
        } else {
            hot_zone.push_back(m_conversation[i]);
        }
    }

    // 2. Format Cold Zone to Markdown for local archiving
    std::string archive_md;
    archive_md += "\n\n### [ARCHIVED CONVERSATION BLOCK - " + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + "]\n";
    for (const auto& msg : cold_zone) {
        std::string role = msg.value("role", "");
        archive_md += "**" + role + "**: ";
        if (msg.contains("content")) {
            if (msg["content"].is_string()) {
                archive_md += msg["content"].get<std::string>() + "\n\n";
            } else {
                archive_md += msg["content"].dump() + "\n\n";
            }
        }
        if (msg.contains("tool_calls")) {
            archive_md += "*(Tool Calls: " + msg["tool_calls"].dump() + ")*\n\n";
        }
    }

    // 3. Write/append Cold Zone to sessions/archive_<id>.md
    std::string archive_file = resolve_project_path("sessions/archive_" + m_sessions[m_current_session_index].id + ".md");
    std::error_code ec;
    std::filesystem::path parent_dir = std::filesystem::path(archive_file).parent_path();
    if (!std::filesystem::exists(parent_dir, ec)) {
        std::filesystem::create_directories(parent_dir, ec);
    }
    
    std::ofstream out(archive_file, std::ios::app);
    if (out.is_open()) {
        out << archive_md;
        out.close();
    }

    // 4. Synchronously trim context and reset accumulators to prevent double-trigger
    m_conversation = hot_zone;
    m_last_total_tokens = 0;
    saveCurrentSessionState();

    // 5. Build consolidation prompt for secondary model
    std::string existing_digest = m_sessions[m_current_session_index].memory_digest;
    std::string summarize_prompt = 
        "Existing Memory Digest:\n" + (existing_digest.empty() ? "(empty)" : existing_digest) + "\n\n"
        "New raw turns to compress:\n";
    
    for (const auto& msg : cold_zone) {
        std::string role = msg.value("role", "");
        summarize_prompt += role + ": " + (msg.contains("content") ? (msg["content"].is_string() ? msg["content"].get<std::string>() : msg["content"].dump()) : "") + "\n";
    }

    summarize_prompt += "\nTask:\n"
                        "Merge these new conversational turns into the existing Memory Digest. Compress them into high-density, bulleted facts of what was discussed, what files were edited, and decisions made. Keep the final digest concise, informative, and under 500 words. Do NOT include markdown backticks or extra comments. Return ONLY the new updated Memory Digest itself.";

    std::vector<nlohmann::json> messages;
    messages.push_back({{"role", "system"}, {"content", "You are a context consolidator. Update and merge new turns into a single high-density, bulleted facts summary. Return ONLY the bullet points."}});
    messages.push_back({{"role", "user"}, {"content", summarize_prompt}});

    std::string host = m_use_secondary_model ? m_sec_host_val : m_host_val;
    int port = m_use_secondary_model ? m_sec_port_val : m_port_val;
    std::string api_key = m_use_secondary_model ? m_sec_apikey_val : m_apikey_val;
    std::string model = m_use_secondary_model ? m_sec_model_val : m_model_val;

    m_worker->push_request(
        messages, host, port, api_key, model,
        [](const std::string&) {}, // ignore intermediate chunks
        nullptr, nullptr, nullptr, nullptr,
        [this](const std::vector<nlohmann::json>& updated_messages) {
            QMetaObject::invokeMethod(this, [this, updated_messages] {
                if (!updated_messages.empty() && updated_messages.back().contains("content")) {
                    std::string new_digest = updated_messages.back()["content"].get<std::string>();
                    new_digest = stripThinkBlock(new_digest);
                    
                    // Strip whitespaces
                    new_digest.erase(0, new_digest.find_first_not_of(" \t\r\n\"'"));
                    new_digest.erase(new_digest.find_last_not_of(" \t\r\n\"'") + 1);

                    if (!new_digest.empty()) {
                        m_sessions[m_current_session_index].memory_digest = new_digest;
                        saveCurrentSessionState();
                    }
                }
                
                m_is_consolidating = false;
                updateStatus("Ready");
            });
        },
        nullptr, // on_stats
        false    // include_tools
    );
}

void QtUiApp::updateStatus(const std::string& status) {
    m_status_label->setText(QString("Status: %1").arg(QString::fromStdString(status)));
}

void QtUiApp::setModelName(const std::string& model) {
    m_model_label->setText(QString("Model: %1").arg(QString::fromStdString(model)));
}

void QtUiApp::queryActiveModel() {
    std::string host = m_host_val;
    int port = m_port_val;

    std::thread([this, host, port]() {
        try {
            httplib::Client client("http://" + host + ":" + std::to_string(port));
            client.set_connection_timeout(1000); // 1-second timeout
            client.set_read_timeout(2000);
            
            // 1. Query props to get active context window size (n_ctx)
            auto res_props = client.Get("/props");
            if (res_props && res_props->status == 200) {
                try {
                    auto j_props = nlohmann::json::parse(res_props->body);
                    if (j_props.contains("default_generation_settings") && j_props["default_generation_settings"].contains("n_ctx")) {
                        int active_ctx = j_props["default_generation_settings"]["n_ctx"].get<int>();
                        QMetaObject::invokeMethod(this, [this, active_ctx]() {
                            m_context_limit_val = active_ctx;
                            m_stat_context_limit->setText(QString("Context Limit: %1").arg(QString::number(active_ctx)));
                            m_stat_context_used->setText(QString("Context Used: 0 / %1 (0.0%)").arg(QString::number(active_ctx)));
                        });
                    }
                } catch (...) {}
            }

            // 2. Query models to get active model name
            auto res = client.Get("/v1/models");
            if (res && res->status == 200) {
                auto j = nlohmann::json::parse(res->body);
                if (j.contains("data") && j["data"].is_array() && !j["data"].empty()) {
                    std::string active_model = j["data"][0]["id"].get<std::string>();
                    QMetaObject::invokeMethod(this, [this, active_model]() {
                        m_model_val = active_model;
                        setModelName(active_model);
                    });
                    return;
                }
            }
        } catch (...) {
            // Ignore and fallback
        }
        
        QMetaObject::invokeMethod(this, [this]() {
            setModelName(m_model_val); // Fallback to user settings
        });
    }).detach();
}

void QtUiApp::handleResetStats() {
    m_accumulated_prompt_tokens = 0;
    m_accumulated_completion_tokens = 0;
    m_accumulated_total_tokens = 0;
    m_accumulated_time = 0.0;
    m_total_generations = 0;

    m_stat_prompt_tokens->setText("Input Tokens: 0");
    m_stat_completion_tokens->setText("Output Tokens: 0");
    m_stat_total_tokens->setText("Total Tokens: 0");
    m_stat_time->setText("Generation Time: 0.00s");
    m_stat_speed->setText("Generation Speed: 0.00 t/s");
    m_stat_session_tokens->setText("Total Session Tokens: 0");
    m_stat_session_speed->setText("Session Avg Speed: 0.00 t/s");
}

static std::string stripThinkBlock(const std::string& text) {
    std::string clean = text;
    size_t start = clean.find("<think>");
    while (start != std::string::npos) {
        size_t end = clean.find("</think>", start);
        if (end != std::string::npos) {
            clean.erase(start, (end + 8) - start);
        } else {
            clean.erase(start);
            break;
        }
        start = clean.find("<think>");
    }
    return clean;
}

void QtUiApp::consolidateMemoryProfile(const std::string& user_prompt, const std::string& assistant_reply) {
    // Determine which endpoint/model parameters to use (primary vs secondary)
    std::string host = m_use_secondary_model ? m_sec_host_val : m_host_val;
    int port = m_use_secondary_model ? m_sec_port_val : m_port_val;
    std::string api_key = m_use_secondary_model ? m_sec_apikey_val : m_apikey_val;
    std::string model = m_use_secondary_model ? m_sec_model_val : m_model_val;

    // Build an ultra-optimized, lightweight memory extraction prompt
    // Highly readable, zero context bloat, easy for tiny 0.8B models to execute without hallucinations!
    std::string extract_prompt = 
        "Existing Profile Memory:\n" + (m_user_profile.empty() ? "(empty)" : m_user_profile) + "\n\n"
        "Latest Conversation Turn:\n"
        "User: \"" + user_prompt + "\"\n"
        "Assistant: \"" + assistant_reply + "\"\n\n"
        "Task:\n"
        "Extract any new permanent personal facts about the **User** (the User's name, age, married, likes, dislikes, occupation) from the latest turn. Do NOT extract any facts about the Assistant (yourself). Merge them with the existing user profile, and output the updated bulleted list of facts about the user.\n"
        "Rules:\n"
        "- Do not extract temporary states, moods, or trivial chat.\n"
        "- Do not write 'Unknown' or placeholder values.\n"
        "- Do not include markdown backticks, intro, or explanation.\n"
        "- If no new facts, output the existing profile memory exactly unchanged.";

    std::vector<nlohmann::json> messages;
    messages.push_back({{"role", "system"}, {"content", "You are a user memory profile compiler. Merge new personal facts about the user (and ONLY the user) into a clean bulleted list. Do NOT compile information about yourself (the Assistant). Output ONLY the list."}});
    messages.push_back({{"role", "user"}, {"content", extract_prompt}});

    // Push request asynchronously to our worker queue!
    // Since it's appended to the queue, it will execute in the background AFTER the active chat turn finishes, completely preventing any lag or CPU spikes during streaming!
    m_worker->push_request(
        messages, host, port, api_key, model,
        [](const std::string&) {}, // ignore intermediate chunks
        nullptr, nullptr, nullptr, nullptr,
        [this](const std::vector<nlohmann::json>& updated_messages) {
            QMetaObject::invokeMethod(this, [this, updated_messages] {
                if (!updated_messages.empty() && updated_messages.back().contains("content")) {
                    std::string new_profile = updated_messages.back()["content"].get<std::string>();
                    
                    // Strip any reasoning model <think>...</think> tags completely
                    new_profile = stripThinkBlock(new_profile);

                    // Strip whitespace/quotes
                    new_profile.erase(0, new_profile.find_first_not_of(" \t\r\n\"'"));
                    new_profile.erase(new_profile.find_last_not_of(" \t\r\n\"'") + 1);
                    if (!new_profile.empty()) {
                        m_user_profile = new_profile;
                        save_profile_memory(m_user_profile);
                    }
                }
            });
        },
        nullptr, // on_stats
        false    // include_tools
    );
}

void QtUiApp::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

bool QtUiApp::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_input_field && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            if (keyEvent->modifiers() & Qt::ShiftModifier) {
                // Let Shift+Enter insert a newline natively
                return QMainWindow::eventFilter(obj, event);
            } else {
                // Enter without Shift sends the message!
                handleSend();
                return true; // Mark event as handled (swallowed)
            }
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

void QtUiApp::dropEvent(QDropEvent* event) {
    const QMimeData* mime = event->mimeData();
    if (mime->hasUrls()) {
        QList<QUrl> urls = mime->urls();
        if (urls.isEmpty()) return;

        QString local_path = urls.first().toLocalFile();
        if (local_path.isEmpty()) return;

        QFileInfo file_info(local_path);
        QString ext = file_info.suffix().toLower();
        QString file_name = file_info.fileName();

        // 1. Process dropped Image formats
        if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "webp" || ext == "gif") {
            QFile file(local_path);
            if (file.open(QIODevice::ReadOnly)) {
                QByteArray base64_data = file.readAll().toBase64();
                m_pending_image_base64 = base64_data.toStdString();
                m_pending_image_name = file_name.toStdString();
                m_pending_image_mime = "image/" + (ext == "jpg" ? "jpeg" : ext.toStdString());

                appendMessage("[🖼️ Attached Image: " + m_pending_image_name + "]", true);
            }
        } else if (ext == "wav" || ext == "mp3") {
            QFile file(local_path);
            if (file.open(QIODevice::ReadOnly)) {
                QByteArray base64_data = file.readAll().toBase64();
                m_pending_audio_base64 = base64_data.toStdString();
                m_pending_audio_name = file_name.toStdString();
                m_pending_audio_mime = "audio/" + ext.toStdString();
                m_pending_audio_format = ext.toStdString();

                appendMessage("[🎙️ Attached Audio: " + m_pending_audio_name + "]", true);
            }
        } else {
            // 2. Process text-based code/document files
            QFile file(local_path);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                // Set safe reading limit of 500KB (approx 10,000 lines)
                qint64 size_limit = 500 * 1024;
                QByteArray content_bytes = file.read(size_limit);
                std::string content_str = content_bytes.toStdString();
                if (file.size() > size_limit) {
                    content_str += "\n\n[Content truncated due to size exceeding 500KB]";
                }

                m_attached_file_name = file_name.toStdString();
                appendMessage("[📂 Attaching File: " + m_attached_file_name + " (Analyzing in background...)]", true);
                updateStatus("Summarizing...");

                // Run the asynchronous file summarizer on the secondary endpoint!
                std::string host = m_use_secondary_model ? m_sec_host_val : m_host_val;
                int port = m_use_secondary_model ? m_sec_port_val : m_port_val;
                std::string api_key = m_use_secondary_model ? m_sec_apikey_val : m_apikey_val;
                std::string model = m_use_secondary_model ? m_sec_model_val : m_model_val;

                std::string summary_prompt = 
                    "Analyze and summarize the contents of the following file concisely.\n"
                    "File Name: \"" + m_attached_file_name + "\"\n\n"
                    "File Contents:\n"
                    "\"\"\"\n" + content_str + "\n\"\"\"\n\n"
                    "Task:\n"
                    "Summarize the file's primary purpose, classes, core functions, and key architecture concisely.\n"
                    "Format your response as a clean, structured summary under 300 words. Do not include markdown backticks or commentary.";

                std::vector<nlohmann::json> messages;
                messages.push_back({{"role", "system"}, {"content", "You are an expert AI code analyst. Your job is to summarize file contents concisely. Return ONLY the summary, no backticks or commentary."}});
                messages.push_back({{"role", "user"}, {"content", summary_prompt}});

                m_worker->push_request(
                    messages, host, port, api_key, model,
                    [](const std::string&) {}, // ignore intermediate chunks
                    nullptr, nullptr, nullptr, nullptr,
                    [this](const std::vector<nlohmann::json>& updated_messages) {
                        QMetaObject::invokeMethod(this, [this, updated_messages] {
                            if (!updated_messages.empty() && updated_messages.back().contains("content")) {
                                std::string summary = updated_messages.back()["content"].get<std::string>();
                                m_attached_file_summary = stripThinkBlock(summary);
                                appendMessage("[📂 Attached File: " + m_attached_file_name + " (Analysis complete!)]", false);
                                updateStatus("Ready");
                            }
                        });
                    },
                    nullptr, // on_stats
                    false    // include_tools (no tools schema!)
                );
            } else {
                appendMessage("[⚠️ Error: Failed to open file " + file_name.toStdString() + "]", false);
            }
        }
        event->acceptProposedAction();
    }
}

void QtUiApp::handleMicPressed() {
    m_mic_btn->setChecked(true);
    updateStatus("Listening...");
#ifdef PROMPTSLUT_VOICE_MODE
    VoiceEngine::instance().stt()->startRecording();
#endif
}

void QtUiApp::handleMicReleased() {
    m_mic_btn->setChecked(false);
    updateStatus("Processing voice...");
#ifdef PROMPTSLUT_VOICE_MODE
    VoiceEngine::instance().stt()->stopRecording();
#endif
}

void QtUiApp::handleTranscriptionReady(const QString& text) {
    if (!text.isEmpty()) {
        m_input_field->setText(text);
        handleSend();
    } else {
        updateStatus("Ready");
    }
}

void QtUiApp::handleVoiceToggle(bool checked) {
    m_voice_mode_enabled = checked;
#ifdef PROMPTSLUT_VOICE_MODE
    if (m_mic_btn) {
        m_mic_btn->setEnabled(m_voice_mode_enabled);
        if (m_voice_mode_enabled) {
            m_mic_btn->setStyleSheet("QPushButton#mic_btn { background-color: #2d2d2d; color: #fff; border: 1px solid #007acc; padding: 4px; font-size: 14px; }");
            VoiceEngine::instance().init(
                "models/kokoro-v1.1-zh.onnx",
                "models/voices-v1.1-zh.bin",
                "dict/vocab.txt",
                "models/ggml-tiny.bin"
            );
            VoiceEngine::instance().tts()->setVoiceStyle(m_tts_voice_val);
            connect(VoiceEngine::instance().stt(), &SttEngine::transcriptionReady, this, &QtUiApp::handleTranscriptionReady, Qt::UniqueConnection);
        } else {
            m_mic_btn->setStyleSheet("QPushButton#mic_btn { background-color: #1a1a1a; color: #555; border: 1px solid #333; padding: 4px; font-size: 14px; }");
            VoiceEngine::instance().shutdown();
        }
    }
#else
    m_voice_mode_enabled = false;
#endif
}

void QtUiApp::handleHandsfreeToggle(bool checked) {
    m_handsfree_enabled = checked;
    
    QNetworkAccessManager* manager = new QNetworkAccessManager(this);
    QUrl url(QString("http://127.0.0.1:5001/v1/audio/wakeword?enabled=%1").arg(m_handsfree_enabled ? "true" : "false"));
    QNetworkRequest request(url);
    manager->post(request, QByteArray());
    
    if (m_handsfree_enabled) {
        updateStatus("Listening for Wake Word...");
        if (!m_handsfree_timer) {
            m_handsfree_timer = new QTimer(this);
            connect(m_handsfree_timer, &QTimer::timeout, this, &QtUiApp::pollHandsfreeBuffer);
        }
        m_handsfree_timer->start(300); // Poll every 300ms
    } else {
        updateStatus("Ready");
        if (m_handsfree_timer) {
            m_handsfree_timer->stop();
        }
    }
}

void QtUiApp::pollHandsfreeBuffer() {
    if (!m_handsfree_enabled) return;
    
    QNetworkAccessManager* manager = new QNetworkAccessManager(this);
    QUrl url("http://127.0.0.1:5001/v1/audio/transcription-buffer");
    QNetworkRequest request(url);
    
    QNetworkReply* reply = manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, manager]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (!doc.isNull() && doc.isObject()) {
                QJsonObject obj = doc.object();
                
                // Handle system_status (e.g. background downloading)
                if (obj.contains("system_status")) {
                    QString sys_status = obj["system_status"].toString().trimmed();
                    if (!sys_status.isEmpty()) {
                        if (m_status_label) {
                            m_status_label->setStyleSheet("color: #ffaa00; font-weight: bold; font-size: 13px;");
                        }
                        updateStatus(sys_status.toStdString());
                        reply->deleteLater();
                        manager->deleteLater();
                        return; // Bypass normal listening styling while downloading
                    }
                }
                
                // Track is_transcribing state change
                if (obj.contains("is_transcribing")) {
                    bool is_transcribing = obj["is_transcribing"].toBool();
                    if (is_transcribing != m_last_is_transcribing) {
                        m_last_is_transcribing = is_transcribing;
                        if (is_transcribing) {
                            // TRANSITION TO ACTIVE LISTENING (user said "Hey qwen")
                            // 1. Status Label Glowing Cyan and Bold
                            if (m_status_label) {
                                m_status_label->setStyleSheet("color: #00ffcc; font-weight: bold; font-size: 13px;");
                            }
                            updateStatus("🔊 QWEN IS LISTENING... Speak your prompt!");
                            
                            // 2. Mic Button Glowing Crimson Red background and border
                            if (m_mic_btn) {
                                m_mic_btn->setStyleSheet("QPushButton#mic_btn { background-color: #ff3366; color: #fff; border: 2px solid #ff003c; padding: 4px; font-size: 14px; font-weight: bold; }");
                            }
                        } else {
                            // TRANSITION TO PROCESSING / IDLE
                            // Reset Status Label back to standard styling and Listening text
                            if (m_status_label) {
                                m_status_label->setStyleSheet("color: #888; font-size: 13px;");
                            }
                            updateStatus("Listening for Wake Word...");
                            
                            // Reset Mic Button back to standard styling
                            if (m_mic_btn) {
                                m_mic_btn->setStyleSheet("QPushButton#mic_btn { background-color: #2d2d2d; color: #fff; border: 1px solid #007acc; padding: 4px; font-size: 14px; }");
                            }
                        }
                    }
                }
                
                if (obj.contains("text")) {
                    QString text = obj["text"].toString().trimmed();
                    if (!text.isEmpty()) {
                        // Automatically fill and send!
                        m_input_field->setText(text);
                        handleSend();
                    }
                }
            }
        }
        reply->deleteLater();
        manager->deleteLater();
    });
}

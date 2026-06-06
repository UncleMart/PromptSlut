#include "ProjectWatcher.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QDirIterator>
#include <QFile>
#include <QTextStream>
#include <QDebug>

// --- ProjectWatcherWorker Implementation ---

ProjectWatcherWorker::ProjectWatcherWorker(QObject* parent)
    : QObject(parent), m_hDir(INVALID_HANDLE_VALUE), m_hStopEvent(NULL), m_running(false)
{
    m_hStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
}

ProjectWatcherWorker::~ProjectWatcherWorker() {
    stop();
    if (m_hStopEvent != NULL) {
        CloseHandle(m_hStopEvent);
    }
}

void ProjectWatcherWorker::setDirectory(const QString& path) {
    QMutexLocker locker(&m_mutex);
    m_dirPath = path;
}

void ProjectWatcherWorker::stop() {
    {
        QMutexLocker locker(&m_mutex);
        if (!m_running) return;
        m_running = false;
    }
    
    if (m_hStopEvent != NULL) {
        SetEvent(m_hStopEvent);
    }
    
    // Close the handle to immediately unblock any pending ReadDirectoryChangesW blocking call
    if (m_hDir != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hDir);
        m_hDir = INVALID_HANDLE_VALUE;
    }
}

QString ProjectWatcherWorker::readFileContent(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return "";
    }
    QTextStream in(&file);
    return in.readAll();
}

void ProjectWatcherWorker::performBackgroundScan(const QString& dirPath) {
    QDirIterator it(dirPath, QStringList() << "*.cpp" << "*.h" << "*.py" << "*.md" << "*.txt", QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString fPath = it.next();
        QString content = readFileContent(fPath);
        emit fileDiscovered(fPath, content);
    }
}

void ProjectWatcherWorker::startWatching() {
    QString path;
    {
        QMutexLocker locker(&m_mutex);
        path = m_dirPath;
        m_running = true;
    }

    // 1. Initial quick background directory scan
    performBackgroundScan(path);

    // 2. Open directory handle for recursive watching
    std::wstring wPath = QDir::toNativeSeparators(path).toStdWString();
    m_hDir = CreateFileW(
        wPath.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        NULL
    );

    if (m_hDir == INVALID_HANDLE_VALUE) {
        qWarning() << "[ProjectWatcher] Failed to open directory for watching:" << path;
        return;
    }

    watchLoop();
}

void ProjectWatcherWorker::watchLoop() {
    const DWORD bufferSize = 65536;
    QByteArray buffer(bufferSize, 0);
    OVERLAPPED overlapped = {0};
    overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    HANDLE handles[2] = { m_hStopEvent, overlapped.hEvent };

    while (true) {
        {
            QMutexLocker locker(&m_mutex);
            if (!m_running) break;
        }

        ResetEvent(overlapped.hEvent);

        BOOL success = ReadDirectoryChangesW(
            m_hDir,
            buffer.data(),
            bufferSize,
            TRUE, // watch recursively
            FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME,
            NULL,
            &overlapped,
            NULL
        );

        if (!success) {
            DWORD err = GetLastError();
            if (err != ERROR_IO_PENDING) {
                qWarning() << "[ProjectWatcher] ReadDirectoryChangesW failed with error:" << err;
                break;
            }
        }

        // Dual event wait structure
        DWORD waitResult = WaitForMultipleObjects(2, handles, FALSE, INFINITE);

        if (waitResult == WAIT_OBJECT_0) {
            // Stop event triggered
            break;
        } else if (waitResult == WAIT_OBJECT_0 + 1) {
            // Directory changes event triggered
            DWORD bytesTransferred = 0;
            if (GetOverlappedResult(m_hDir, &overlapped, &bytesTransferred, FALSE) && bytesTransferred > 0) {
                BYTE* pBase = reinterpret_cast<BYTE*>(buffer.data());
                DWORD offset = 0;

                do {
                    FILE_NOTIFY_INFORMATION* pInfo = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(pBase + offset);
                    
                    std::wstring wFilename(pInfo->FileName, pInfo->FileNameLength / sizeof(wchar_t));
                    QString relPath = QString::fromStdWString(wFilename);
                    QString absPath = QDir(m_dirPath).absoluteFilePath(relPath);

                    QFileInfo fileInfo(absPath);
                    QString suffix = fileInfo.suffix().toLower();
                    if (suffix == "cpp" || suffix == "h" || suffix == "py" || suffix == "md" || suffix == "txt") {
                        if (pInfo->Action == FILE_ACTION_MODIFIED || pInfo->Action == FILE_ACTION_ADDED || pInfo->Action == FILE_ACTION_RENAMED_NEW_NAME) {
                            QString content = readFileContent(absPath);
                            emit fileModified(absPath, content);
                        }
                    }

                    offset = pInfo->NextEntryOffset;
                } while (offset != 0);
            }
        } else {
            break;
        }
    }

    CloseHandle(overlapped.hEvent);
}

// --- ProjectWatcher Implementation ---

ProjectWatcher::ProjectWatcher(QObject* parent)
    : QObject(parent), m_state(IDLE), m_workerThread(nullptr), m_worker(nullptr)
{
}

ProjectWatcher::~ProjectWatcher() {
    if (m_workerThread) {
        if (m_worker) {
            m_worker->stop();
        }
        m_workerThread->quit();
        m_workerThread->wait();
    }
}

bool ProjectWatcher::isGatekeeperRefused(const QString& absolutePath) const {
    QString cleanPath = QDir::cleanPath(absolutePath);
    
    // Safety verification check 1: Targeted path matches application directory
    QString appDir = QDir::cleanPath(QCoreApplication::applicationDirPath());
    if (cleanPath == appDir) {
        return true;
    }

    // Safety verification check 2: Targeted path evaluates to drive root partition (e.g. C:/)
    QDir dir(cleanPath);
    if (dir.isRoot()) {
        return true;
    }

    return false;
}

void ProjectWatcher::setWorkingDirectory(const QString &path) {
    QMutexLocker locker(&m_stateMutex);

    // Stop and tear down existing thread system safely if active
    if (m_workerThread) {
        m_worker->stop();
        m_workerThread->quit();
        m_workerThread->wait();
        m_workerThread->deleteLater();
        m_worker->deleteLater();
        m_workerThread = nullptr;
        m_worker = nullptr;
    }

    QString absPath = QDir(path).absolutePath();

    // Gatekeeper verification validation
    if (isGatekeeperRefused(absPath)) {
        m_state = IDLE;
        m_workingDir.clear();
        qDebug() << "[ProjectWatcher] Gatekeeper REFUSED directory path:" << path << "- subsystem switched to IDLE.";
        return;
    }

    m_state = ACTIVE;
    m_workingDir = absPath;
    qDebug() << "[ProjectWatcher] Gatekeeper APPROVED directory path:" << absPath << "- subsystem switched to ACTIVE.";

    // Spin up non-blocking Win32 thread system
    m_workerThread = new QThread(this);
    m_worker = new ProjectWatcherWorker();
    m_worker->setDirectory(absPath);
    m_worker->moveToThread(m_workerThread);

    connect(m_workerThread, &QThread::started, m_worker, &ProjectWatcherWorker::startWatching);
    connect(m_worker, &ProjectWatcherWorker::fileModified, this, &ProjectWatcher::handleFileEvent);
    connect(m_worker, &ProjectWatcherWorker::fileDiscovered, this, &ProjectWatcher::handleFileEvent);

    m_workerThread->start();
}

ProjectWatcher::State ProjectWatcher::state() const {
    QMutexLocker locker(&m_stateMutex);
    return m_state;
}

QString ProjectWatcher::currentDirectory() const {
    QMutexLocker locker(&m_stateMutex);
    return m_workingDir;
}

void ProjectWatcher::handleFileEvent(const QString& filePath, const QString& content) {
    emit fileChangedOrDiscovered(filePath, content);
}
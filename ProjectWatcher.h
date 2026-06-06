#ifndef PROJECTWATCHER_H
#define PROJECTWATCHER_H

#include <QObject>
#include <QString>
#include <QThread>
#include <QMutex>
#include <windows.h>

class ProjectWatcherWorker : public QObject {
    Q_OBJECT
public:
    explicit ProjectWatcherWorker(QObject* parent = nullptr);
    ~ProjectWatcherWorker() override;

    void setDirectory(const QString& path);
    void stop();

signals:
    void fileModified(const QString& filePath, const QString& content);
    void fileDiscovered(const QString& filePath, const QString& content);

public slots:
    void startWatching();

private:
    void performBackgroundScan(const QString& dirPath);
    void watchLoop();
    QString readFileContent(const QString& filePath);

    QString m_dirPath;
    HANDLE m_hDir;
    HANDLE m_hStopEvent;
    bool m_running;
    QMutex m_mutex;
};

class ProjectWatcher : public QObject {
    Q_OBJECT
public:
    explicit ProjectWatcher(QObject* parent = nullptr);
    ~ProjectWatcher() override;

    enum State {
        IDLE,
        ACTIVE
    };

    void setWorkingDirectory(const QString &path);
    State state() const;
    QString currentDirectory() const;

signals:
    void fileChangedOrDiscovered(const QString &filePath, const QString &content);

private slots:
    void handleFileEvent(const QString& filePath, const QString& content);

private:
    bool isGatekeeperRefused(const QString& absolutePath) const;

    State m_state;
    QString m_workingDir;
    QThread* m_workerThread;
    ProjectWatcherWorker* m_worker;
    mutable QMutex m_stateMutex;
};

#endif // PROJECTWATCHER_H
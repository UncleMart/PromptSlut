#ifndef CHRONOSENGINE_H
#define CHRONOSENGINE_H

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QMap>
#include <QMutex>
#include <QTimer>

struct ChronosTask {
    QString id;
    QString systemInstruction;
    QString userContext;
    bool requiresVoiceAlert = false;
    QDateTime targetTime;
};

class ChronosEngine : public QObject {
    Q_OBJECT
public:
    explicit ChronosEngine(QObject* parent = nullptr);
    ~ChronosEngine() override;

    // Thread-safe registration of tasks
    void scheduleTask(const ChronosTask& task);
    bool cancelTask(const QString& id);
    QList<ChronosTask> getActiveTasks() const;

signals:
    void eventTriggered(const ChronosTask& task);

private slots:
    void pollTasks();

private:
    QTimer* m_pollTimer;
    mutable QMutex m_mutex;
    QMap<QDateTime, ChronosTask> m_tasks;
};

#endif // CHRONOSENGINE_H
#include "ChronosEngine.h"
#include <QMutexLocker>
#include <QDebug>

ChronosEngine::ChronosEngine(QObject* parent)
    : QObject(parent), m_pollTimer(nullptr)
{
    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &ChronosEngine::pollTasks);
    m_pollTimer->start(1000); // Check map front every 1000ms
}

ChronosEngine::~ChronosEngine() {
    m_pollTimer->stop();
}

void ChronosEngine::scheduleTask(const ChronosTask& task) {
    QMutexLocker locker(&m_mutex);
    QDateTime target = task.targetTime;
    
    // Safety guard for key collisions: if target time already exists in the QMap,
    // incrementally add 1 millisecond until we resolve a unique key position.
    while (m_tasks.contains(target)) {
        target = target.addMSecs(1);
    }
    
    ChronosTask uniqueTask = task;
    uniqueTask.targetTime = target;
    m_tasks.insert(target, uniqueTask);
    
    qDebug() << "[ChronosEngine] Scheduled task:" << uniqueTask.id << "at" << uniqueTask.targetTime.toString();
}

bool ChronosEngine::cancelTask(const QString& id) {
    QMutexLocker locker(&m_mutex);
    for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
        if (it.value().id == id) {
            m_tasks.erase(it);
            qDebug() << "[ChronosEngine] Cancelled task:" << id;
            return true;
        }
    }
    return false;
}

QList<ChronosTask> ChronosEngine::getActiveTasks() const {
    QMutexLocker locker(&m_mutex);
    return m_tasks.values();
}

void ChronosEngine::pollTasks() {
    QList<ChronosTask> triggeredTasks;
    {
        QMutexLocker locker(&m_mutex);
        QDateTime now = QDateTime::currentDateTime();
        
        while (!m_tasks.isEmpty()) {
            // Since QMap is naturally sorted by QDateTime, the first key is the earliest task
            QDateTime earliestTime = m_tasks.firstKey();
            if (now >= earliestTime) {
                triggeredTasks.append(m_tasks.take(earliestTime));
            } else {
                break; // Earliest task is in the future; remaining tasks are also in the future
            }
        }
    }

    // Emit signals outside of mutex scope to ensure safety against recursive lock deadlocks
    for (const auto& task : triggeredTasks) {
        qDebug() << "[ChronosEngine] Event Triggered:" << task.id;
        emit eventTriggered(task);
    }
}
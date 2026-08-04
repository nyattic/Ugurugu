#pragma once

#include "app/RecoveryStore.hpp"
#include "io/DocumentSerializer.hpp"

#include <QMutex>
#include <QObject>
#include <QString>
#include <QThread>
#include <QWaitCondition>

#include <functional>
#include <optional>
#include <variant>

namespace ugurugu
{

// Serializes and writes recovery snapshots on a private thread. Every public
// method is called from the owning thread; only performWrite runs on the
// worker. At most one write is pending, so a newer snapshot replaces an
// unstarted one instead of queueing behind it.
class RecoveryWriter final : public QObject
{
    Q_OBJECT

public:
    using DocumentPayload =
        std::variant<DocumentSerializer::PreparedDocument, Document>;

    explicit RecoveryWriter(QObject *parent = nullptr);
    ~RecoveryWriter() override;

    void submitWrite(
        DocumentPayload payload, const RecoveryStore::Metadata &metadata);
    bool runExclusiveFileOperation(const std::function<bool()> &operation);
    bool waitForIdle(int timeoutMilliseconds = -1);
    void setSuspendedForTesting(bool suspended);

signals:
    void writeFinished(bool success, quint64 revision, const QString &error);

private:
    struct PendingWrite
    {
        DocumentPayload payload;
        RecoveryStore::Metadata metadata;
        quint64 generation = 0;
    };

    void scheduleProcessing();
    void processPending();
    bool performWrite(const PendingWrite &request, QString *error);

    QThread m_thread;
    QObject m_workerContext;
    DocumentSerializer::SerializationCache m_workerCache;
    QMutex m_mutex;
    QWaitCondition m_idleCondition;
    std::optional<PendingWrite> m_pending;
    quint64 m_generation = 0;
    bool m_busy = false;
    bool m_suspended = false;
};

}

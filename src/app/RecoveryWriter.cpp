#include "app/RecoveryWriter.hpp"

#include "document/DocumentLimits.hpp"

#include <QDeadlineTimer>
#include <QJsonObject>
#include <QMutexLocker>
#include <QSaveFile>

#include <utility>

namespace wobble
{

RecoveryWriter::RecoveryWriter(QObject *parent)
    : QObject(parent)
{
    m_thread.setObjectName(QStringLiteral("RecoveryWriter"));
    m_workerContext.moveToThread(&m_thread);
    m_thread.start();
}

RecoveryWriter::~RecoveryWriter()
{
    {
        QMutexLocker locker(&m_mutex);
        m_suspended = false;
    }
    scheduleProcessing();
    waitForIdle();
    m_thread.quit();
    m_thread.wait();
}

void RecoveryWriter::submitWrite(
    DocumentPayload payload, const RecoveryStore::Metadata &metadata)
{
    {
        QMutexLocker locker(&m_mutex);
        m_pending = PendingWrite{std::move(payload), metadata, m_generation};
        if (m_suspended)
        {
            return;
        }
    }
    scheduleProcessing();
}

bool RecoveryWriter::runExclusiveFileOperation(
    const std::function<bool()> &operation)
{
    QMutexLocker locker(&m_mutex);
    ++m_generation;
    m_pending.reset();
    const bool result = operation();
    m_idleCondition.wakeAll();
    return result;
}

bool RecoveryWriter::waitForIdle(int timeoutMilliseconds)
{
    QDeadlineTimer deadline = timeoutMilliseconds < 0
                                  ? QDeadlineTimer(QDeadlineTimer::Forever)
                                  : QDeadlineTimer(timeoutMilliseconds);
    QMutexLocker locker(&m_mutex);
    while (m_pending || m_busy)
    {
        if (!m_idleCondition.wait(&m_mutex, deadline))
        {
            return false;
        }
    }
    return true;
}

void RecoveryWriter::setSuspendedForTesting(bool suspended)
{
    {
        QMutexLocker locker(&m_mutex);
        m_suspended = suspended;
        if (suspended)
        {
            return;
        }
    }
    scheduleProcessing();
}

void RecoveryWriter::scheduleProcessing()
{
    QMetaObject::invokeMethod(
        &m_workerContext,
        [this]()
        {
            processPending();
        },
        Qt::QueuedConnection);
}

void RecoveryWriter::processPending()
{
    while (true)
    {
        PendingWrite request;
        {
            QMutexLocker locker(&m_mutex);
            if (m_suspended || !m_pending)
            {
                m_busy = false;
                m_idleCondition.wakeAll();
                return;
            }
            request = std::move(*m_pending);
            m_pending.reset();
            m_busy = true;
        }

        QString error;
        const bool success = performWrite(request, &error);
        if (success || !error.isEmpty())
        {
            emit writeFinished(success, request.metadata.revision, error);
        }
    }
}

bool RecoveryWriter::performWrite(const PendingWrite &request, QString *error)
{
    if (!RecoveryStore::isValidMetadata(request.metadata))
    {
        *error = QStringLiteral("Recovery metadata is invalid.");
        return false;
    }
    if (!RecoveryStore::ensureParentDirectory(error))
    {
        return false;
    }

    const QJsonObject rootFields =
        RecoveryStore::metadataRootFields(request.metadata);
    QByteArray data;
    if (const auto *prepared =
            std::get_if<DocumentSerializer::PreparedDocument>(&request.payload))
    {
        data = DocumentSerializer::toJson(*prepared, m_workerCache, rootFields);
        if (data.isEmpty() || data.size() > DocumentLimits::maximumProjectBytes)
        {
            data = DocumentSerializer::toJson(*prepared, m_workerCache);
        }
    }
    else
    {
        const Document &document = std::get<Document>(request.payload);
        data = DocumentSerializer::toJson(document, rootFields);
        if (data.isEmpty() || data.size() > DocumentLimits::maximumProjectBytes)
        {
            data = DocumentSerializer::toJson(document);
        }
    }
    if (data.isEmpty())
    {
        *error = QStringLiteral("Could not serialize the recovery project.");
        return false;
    }
    if (data.size() > DocumentLimits::maximumProjectBytes)
    {
        *error = QStringLiteral("The recovery project is too large.");
        return false;
    }

    QSaveFile file(RecoveryStore::filePath());
    if (!file.open(QIODevice::WriteOnly))
    {
        *error = file.errorString();
        return false;
    }
    if (file.write(data) != data.size())
    {
        *error = file.errorString();
        file.cancelWriting();
        return false;
    }

    // runExclusiveFileOperation bumps the generation while holding the mutex,
    // so a write that started before it must discard its QSaveFile here. The
    // commit is what the generation guards: without this check a write that
    // began earlier could resurrect the recovery file after a delete.
    QMutexLocker locker(&m_mutex);
    if (request.generation != m_generation)
    {
        file.cancelWriting();
        return false;
    }
    if (!file.commit())
    {
        *error = file.errorString();
        return false;
    }
    return true;
}

}

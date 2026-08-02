#include "app/ApplicationInstanceLock.hpp"

#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QStandardPaths>

namespace wobble
{

namespace
{

QString defaultLockPath()
{
    const QString configured =
        qEnvironmentVariable("WAGLEWAGLEPAINT_INSTANCE_LOCK_PATH");
    if (!configured.isEmpty())
    {
        return QFileInfo(configured).absoluteFilePath();
    }
    return QDir(
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("instance.lock"));
}

void setError(QString *error, const QString &message)
{
    if (error)
    {
        *error = message;
    }
}

}

ApplicationInstanceLock::ApplicationInstanceLock(QString filePath)
    : m_filePath(filePath.isEmpty() ? defaultLockPath()
                                    : QFileInfo(filePath).absoluteFilePath())
{
}

ApplicationInstanceLock::~ApplicationInstanceLock() = default;

ApplicationInstanceLock::AcquireResult ApplicationInstanceLock::acquire(
    QString *error)
{
    if (m_acquired)
    {
        return AcquireResult::Acquired;
    }
    const QString directory = QFileInfo(m_filePath).absolutePath();
    if (!QDir().mkpath(directory))
    {
        setError(error,
            QStringLiteral("Could not create the instance lock directory: %1")
                .arg(directory));
        return AcquireResult::Failed;
    }

    m_lock = std::make_unique<QLockFile>(m_filePath);
    m_lock->setStaleLockTime(0);
    if (m_lock->tryLock())
    {
        m_acquired = true;
        return AcquireResult::Acquired;
    }
    if (m_lock->error() == QLockFile::LockFailedError)
    {
        return AcquireResult::AlreadyRunning;
    }

    setError(error,
        QStringLiteral("Could not acquire the application instance lock: %1")
            .arg(m_filePath));
    return AcquireResult::Failed;
}

bool ApplicationInstanceLock::isAcquired() const
{
    return m_acquired;
}

QString ApplicationInstanceLock::filePath() const
{
    return m_filePath;
}

}

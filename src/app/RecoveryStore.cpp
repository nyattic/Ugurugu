#include "app/RecoveryStore.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace wobble
{

namespace
{

void setError(QString *error, const QString &message)
{
    if (error)
    {
        *error = message;
    }
}

}

QString RecoveryStore::filePath()
{
    const QString configured =
        qEnvironmentVariable("WAGLEWAGLEPAINT_RECOVERY_PATH");
    if (!configured.isEmpty())
    {
        return QFileInfo(configured).absoluteFilePath();
    }
    return QDir(
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("recovery.wagle"));
}

bool RecoveryStore::ensureParentDirectory(QString *error)
{
    const QString directory = QFileInfo(filePath()).absolutePath();
    if (QDir().mkpath(directory))
    {
        return true;
    }
    setError(error,
        QStringLiteral("Could not create recovery directory: %1")
            .arg(directory));
    return false;
}

bool RecoveryStore::discard(QString *error)
{
    const QString path = filePath();
    if (!QFileInfo::exists(path) || QFile::remove(path))
    {
        return true;
    }
    setError(
        error, QStringLiteral("Could not remove recovery file: %1").arg(path));
    return false;
}

QString RecoveryStore::quarantine(QString *error)
{
    const QString source = filePath();
    if (!QFileInfo::exists(source))
    {
        setError(error,
            QStringLiteral("Recovery file does not exist: %1").arg(source));
        return {};
    }

    const QString stamp = QDateTime::currentDateTimeUtc().toString(
        QStringLiteral("yyyyMMdd-HHmmss"));
    const QString base = source + QStringLiteral(".failed-") + stamp;
    QString destination = base;
    for (int suffix = 2; QFileInfo::exists(destination); ++suffix)
    {
        destination = base + QStringLiteral("-%1").arg(suffix);
    }
    if (QFile::rename(source, destination))
    {
        return destination;
    }

    setError(error,
        QStringLiteral("Could not preserve recovery file as: %1")
            .arg(destination));
    return {};
}

}

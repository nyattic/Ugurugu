#include "TestSuites.hpp"
#include "app/ApplicationInstanceLock.hpp"
#include "ui/Theme.hpp"

#include <QApplication>
#include <QEventLoop>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

#include <array>

namespace
{

struct Suite
{
    const char *name;
    int (*run)(int, char **);
};

constexpr std::array suites{
    Suite{"app", wobble::runAppPolicyTests},
    Suite{"document", wobble::runDocumentTests},
    Suite{"render", wobble::runRenderEngineTests},
    Suite{"gif", wobble::runGifWriterTests},
    Suite{"webp", wobble::runWebPWriterTests},
    Suite{"mask", wobble::runMaskRegressionTests},
    Suite{"release_notes", wobble::runReleaseNotesTests},
    Suite{"stabilizer", wobble::runStrokeStabilizerTests},
    Suite{"ui", wobble::runUiTests},
};

}

int main(int argc, char **argv)
{
    QTemporaryDir settingsDirectory;
    if (!settingsDirectory.isValid())
    {
        qCritical("Could not create an isolated test settings directory.");
        return 1;
    }

    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(
        QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QSettings::setPath(
        QSettings::IniFormat, QSettings::SystemScope, settingsDirectory.path());
    if (!qputenv("WAGLEWAGLEPAINT_RECOVERY_PATH",
            settingsDirectory.filePath(QStringLiteral("recovery.wagle"))
                .toUtf8()))
    {
        qCritical("Could not isolate the test recovery file.");
        return 1;
    }

    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("WagleWaglePaint"));
    QApplication::setApplicationDisplayName(QStringLiteral("WagleWaglePaint"));
    QApplication::setApplicationVersion(
        QStringLiteral(WAGLEWAGLEPAINT_VERSION));
    QApplication::setOrganizationName(QStringLiteral("WagleWaglePaint"));
    QApplication::setOrganizationDomain(QStringLiteral("waglewaglepaint.dev"));
    wobble::Theme::apply(application);

    const QString lockProbePath =
        qEnvironmentVariable("WOBBLEPAINT_INSTANCE_LOCK_PROBE_PATH");
    if (!lockProbePath.isEmpty())
    {
        wobble::ApplicationInstanceLock lock(lockProbePath);
        const auto result = lock.acquire();
        QFile ready(
            qEnvironmentVariable("WOBBLEPAINT_INSTANCE_LOCK_PROBE_READY_PATH"));
        if (!ready.open(QIODevice::WriteOnly))
        {
            return 3;
        }
        const QByteArray status =
            result == wobble::ApplicationInstanceLock::AcquireResult::Acquired
                ? QByteArrayLiteral("acquired")
                : QByteArrayLiteral("failed");
        if (ready.write(status) != status.size())
        {
            return 3;
        }
        ready.close();
        if (result != wobble::ApplicationInstanceLock::AcquireResult::Acquired)
        {
            return 4;
        }
        QEventLoop loop;
        QTimer::singleShot(30000, &loop, &QEventLoop::quit);
        return loop.exec();
    }

    const QByteArray requested = qgetenv("WOBBLEPAINT_TEST_SUITE");
    int result = 0;
    bool matched = requested.isEmpty();
    for (const Suite &suite : suites)
    {
        if (!requested.isEmpty() && requested != suite.name)
        {
            continue;
        }
        matched = true;
        result |= suite.run(argc, argv);
    }
    if (!matched)
    {
        qCritical("Unknown WOBBLEPAINT_TEST_SUITE: %s", requested.constData());
        return 2;
    }
    return result;
}

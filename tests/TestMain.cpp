#include "TestSuites.hpp"
#include "ui/Theme.hpp"

#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>

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

    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("WagleWaglePaint"));
    QApplication::setApplicationDisplayName(QStringLiteral("WagleWaglePaint"));
    QApplication::setApplicationVersion(
        QStringLiteral(WAGLEWAGLEPAINT_VERSION));
    QApplication::setOrganizationName(QStringLiteral("WagleWaglePaint"));
    QApplication::setOrganizationDomain(QStringLiteral("waglewaglepaint.dev"));
    wobble::Theme::apply(application);

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

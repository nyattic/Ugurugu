#include "app/ApplicationInstanceLock.hpp"
#include "app/Logging.hpp"
#include "app/UpdateController.hpp"
#include "ui/FileOpenEventRouter.hpp"
#include "ui/MainWindow.hpp"
#include "ui/SettingsDialog.hpp"
#include "ui/Theme.hpp"

#include <QAction>
#include <QApplication>
#include <QFileInfo>
#include <QImageReader>
#include <QInputDevice>
#include <QLibraryInfo>
#include <QLocale>
#include <QMessageBox>
#include <QObject>
#include <QPointingDevice>
#include <QSettings>
#include <QTranslator>

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstdlib>
#include <exception>

namespace
{

void configureApplicationMetadata()
{
    QApplication::setApplicationName(QStringLiteral("Ugurugu"));
    QApplication::setApplicationDisplayName(QStringLiteral("Ugurugu"));
    QApplication::setApplicationVersion(QStringLiteral(UGURUGU_VERSION));
    QApplication::setOrganizationName(QStringLiteral("Ugurugu"));
    QApplication::setOrganizationDomain(QStringLiteral("ugurugu.dev"));
}

// Turning Windows Ink off moves a pen from the pointer API onto WinTab, and
// WinTab only reaches Qt when the driver has installed a 64-bit wintab32.dll.
// When it has not, the pen arrives as plain mouse input and every stroke draws
// at full width. That is indistinguishable from a broken app, so the log says
// which devices were actually found and whether any reports pressure.
void logPointingDevices()
{
    int styluses = 0;
    int withPressure = 0;
    for (const QInputDevice *device : QInputDevice::devices())
    {
        const auto *pointing = qobject_cast<const QPointingDevice *>(device);
        if (!pointing
            || (pointing->type() != QInputDevice::DeviceType::Stylus
                && pointing->type() != QInputDevice::DeviceType::Airbrush
                && pointing->type() != QInputDevice::DeviceType::Puck))
        {
            continue;
        }
        ++styluses;
        const bool pressure = pointing->capabilities().testFlag(
            QInputDevice::Capability::Pressure);
        withPressure += pressure ? 1 : 0;
        spdlog::info("Tablet device: {} (system id {}), pressure {}",
            pointing->name().toUtf8().constData(),
            pointing->systemId(),
            pressure ? "yes" : "no");
    }
    if (styluses == 0)
    {
        spdlog::info("No tablet device reported by Qt. A pen will draw at a "
                     "constant width. With Windows Ink off, check that the "
                     "tablet driver installed a 64-bit wintab32.dll.");
    }
    else if (withPressure == 0)
    {
        spdlog::warn(
            "{} tablet device(s) found but none reports pressure.", styluses);
    }
}

struct LegacySettingsIdentity
{
    QString applicationName;
    QString organizationName;
    QString organizationDomain;
};

void migrateLegacySettings()
{
    QSettings currentSettings;
    const QString migrationKey =
        QStringLiteral("migration/legacySettingsImported");
    if (currentSettings.value(migrationKey).toBool())
    {
        return;
    }

    // Every rename of the application leaves its settings behind under the
    // previous identity, so all of them are visited. Newest first, because the
    // first identity holding a key wins and the most recent copy is the one
    // the reader last used.
    const QList<LegacySettingsIdentity> legacyIdentities = {
        {QStringLiteral("WagleWaglePaint"),
            QStringLiteral("WagleWaglePaint"),
            QStringLiteral("waglewaglepaint.dev")},
        {QStringLiteral("WobblePaint"),
            QStringLiteral("WobblePaint"),
            QStringLiteral("wobblepaint.dev")}};

    for (const LegacySettingsIdentity &identity : legacyIdentities)
    {
        // QSettings resolves its backing store from the application and
        // organization names, so the only way to read a pre-rename store is to
        // adopt that identity for the length of one construction and restore
        // the current one immediately afterwards.
        QApplication::setApplicationName(identity.applicationName);
        QApplication::setOrganizationName(identity.organizationName);
        QApplication::setOrganizationDomain(identity.organizationDomain);
        const QSettings legacySettings;
        configureApplicationMetadata();

        for (const QString &key : legacySettings.allKeys())
        {
            if (!currentSettings.contains(key))
            {
                currentSettings.setValue(key, legacySettings.value(key));
            }
        }
    }
    currentSettings.setValue(migrationKey, true);
    currentSettings.sync();
}

int runApplication(int argc, char *argv[])
{
    ugurugu::UpdateController::initialize();
    QApplication application(argc, argv);
    QImageReader::setAllocationLimit(64);
    configureApplicationMetadata();
    ugurugu::ApplicationInstanceLock instanceLock;
    QString instanceError;
    const ugurugu::ApplicationInstanceLock::AcquireResult instanceResult =
        instanceLock.acquire(&instanceError);
    if (instanceResult
        == ugurugu::ApplicationInstanceLock::AcquireResult::Acquired)
    {
        migrateLegacySettings();
    }
    ugurugu::Theme::apply(application);
    logPointingDevices();

    QTranslator qtBaseTranslator;
    const QString configuredLanguage = ugurugu::SettingsDialog::uiLanguage();
    const QLocale interfaceLocale =
        configuredLanguage == QStringLiteral("system")
            ? QLocale::system()
            : QLocale(configuredLanguage);
    if (qtBaseTranslator.load(interfaceLocale,
            QStringLiteral("qtbase"),
            QStringLiteral("_"),
            QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
    {
        QApplication::installTranslator(&qtBaseTranslator);
    }
    QTranslator appTranslator;
    if (appTranslator.load(interfaceLocale,
            QStringLiteral("ugurugu"),
            QStringLiteral("_"),
            QStringLiteral(":/i18n")))
    {
        QApplication::installTranslator(&appTranslator);
    }

    if (instanceResult
        == ugurugu::ApplicationInstanceLock::AcquireResult::AlreadyRunning)
    {
        QMessageBox::information(nullptr,
            QObject::tr("Ugurugu"),
            QObject::tr("Ugurugu is already running."));
        return EXIT_SUCCESS;
    }
    if (instanceResult
        == ugurugu::ApplicationInstanceLock::AcquireResult::Failed)
    {
        QMessageBox::critical(nullptr,
            QObject::tr("Ugurugu"),
            QObject::tr("Ugurugu could not start.\n\n%1").arg(instanceError));
        return EXIT_FAILURE;
    }

    ugurugu::Logging::initialize();
    spdlog::info("Ugurugu {} starting",
        QApplication::applicationVersion().toStdString());

    int result = EXIT_FAILURE;
    try
    {
        ugurugu::MainWindow window;
        ugurugu::UpdateController updateController(&window);
        QAction *checkForUpdatesAction = window.findChild<QAction *>(
            QStringLiteral("checkForUpdatesAction"));
        QObject::connect(checkForUpdatesAction,
            &QAction::triggered,
            &updateController,
            &ugurugu::UpdateController::checkForUpdates);
        ugurugu::FileOpenEventRouter fileOpenRouter(
            [&window](const QString &filePath)
            {
                if (window.openFile(filePath))
                {
                    window.showNormal();
                    window.raise();
                    window.activateWindow();
                }
            });
        application.installEventFilter(&fileOpenRouter);
        application.processEvents();
        QString requestedFilePath;
        if (application.arguments().size() > 1)
        {
            requestedFilePath =
                QFileInfo(application.arguments().at(1)).absoluteFilePath();
            fileOpenRouter.discardPendingFile(requestedFilePath);
        }
        else
        {
            requestedFilePath = fileOpenRouter.takePendingFile();
        }
        const ugurugu::MainWindow::StartupResult startupResult =
            window.initializeSession(requestedFilePath);
        if (!requestedFilePath.isEmpty())
        {
            fileOpenRouter.discardPendingFile(requestedFilePath);
        }
        if (startupResult == ugurugu::MainWindow::StartupResult::Canceled)
        {
            fileOpenRouter.discardPendingFiles();
            result = EXIT_SUCCESS;
        }
        else if (startupResult == ugurugu::MainWindow::StartupResult::Failed)
        {
            fileOpenRouter.discardPendingFiles();
            result = EXIT_FAILURE;
        }
        else
        {
            window.show();
            fileOpenRouter.setReady(true);
            result = application.exec();
            spdlog::info("Ugurugu exiting with code {}", result);
        }
    }
    catch (const std::exception &error)
    {
        spdlog::critical("Unhandled exception: {}", error.what());
        QMessageBox::critical(nullptr,
            QObject::tr("Ugurugu"),
            QObject::tr("The application encountered an unexpected error."));
    }

    ugurugu::Logging::shutdown();
    return result;
}

}

int main(int argc, char *argv[]) noexcept
{
    try
    {
        return runApplication(argc, argv);
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "Fatal startup exception: %s\n", error.what());
    }
    catch (...)
    {
        std::fputs("Fatal unknown startup exception.\n", stderr);
    }
    return EXIT_FAILURE;
}

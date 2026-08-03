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
#include <QLibraryInfo>
#include <QLocale>
#include <QMessageBox>
#include <QObject>
#include <QSettings>
#include <QTranslator>

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <exception>

namespace
{

void configureApplicationMetadata()
{
    QApplication::setApplicationName(QStringLiteral("WagleWaglePaint"));
    QApplication::setApplicationDisplayName(QStringLiteral("WagleWaglePaint"));
    QApplication::setApplicationVersion(
        QStringLiteral(WAGLEWAGLEPAINT_VERSION));
    QApplication::setOrganizationName(QStringLiteral("WagleWaglePaint"));
    QApplication::setOrganizationDomain(QStringLiteral("waglewaglepaint.dev"));
}

void migrateLegacySettings()
{
    QSettings currentSettings;
    const QString migrationKey =
        QStringLiteral("migration/wobblePaintSettingsImported");
    if (currentSettings.value(migrationKey).toBool())
    {
        return;
    }

    // QSettings resolves its backing store from the application and
    // organization names, so the only way to read the pre-rename WobblePaint
    // settings is to adopt that identity for the length of one construction
    // and restore the current one immediately afterwards.
    QApplication::setApplicationName(QStringLiteral("WobblePaint"));
    QApplication::setOrganizationName(QStringLiteral("WobblePaint"));
    QApplication::setOrganizationDomain(QStringLiteral("wobblepaint.dev"));
    const QSettings legacySettings;
    configureApplicationMetadata();

    for (const QString &key : legacySettings.allKeys())
    {
        if (!currentSettings.contains(key))
        {
            currentSettings.setValue(key, legacySettings.value(key));
        }
    }
    currentSettings.setValue(migrationKey, true);
    currentSettings.sync();
}

}

int main(int argc, char *argv[])
{
    wobble::UpdateController::initialize();
    QApplication application(argc, argv);
    configureApplicationMetadata();
    wobble::ApplicationInstanceLock instanceLock;
    QString instanceError;
    const wobble::ApplicationInstanceLock::AcquireResult instanceResult =
        instanceLock.acquire(&instanceError);
    if (instanceResult
        == wobble::ApplicationInstanceLock::AcquireResult::Acquired)
    {
        migrateLegacySettings();
    }
    wobble::Theme::apply(application);

    QTranslator qtBaseTranslator;
    const QString configuredLanguage = wobble::SettingsDialog::uiLanguage();
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
            QStringLiteral("wobblepaint"),
            QStringLiteral("_"),
            QStringLiteral(":/i18n")))
    {
        QApplication::installTranslator(&appTranslator);
    }

    if (instanceResult
        == wobble::ApplicationInstanceLock::AcquireResult::AlreadyRunning)
    {
        QMessageBox::information(nullptr,
            QObject::tr("WagleWaglePaint"),
            QObject::tr("WagleWaglePaint is already running."));
        return EXIT_SUCCESS;
    }
    if (instanceResult
        == wobble::ApplicationInstanceLock::AcquireResult::Failed)
    {
        QMessageBox::critical(nullptr,
            QObject::tr("WagleWaglePaint"),
            QObject::tr("WagleWaglePaint could not start.\n\n%1")
                .arg(instanceError));
        return EXIT_FAILURE;
    }

    wobble::Logging::initialize();
    spdlog::info("WagleWaglePaint {} starting",
        QApplication::applicationVersion().toStdString());

    int result = EXIT_FAILURE;
    try
    {
        wobble::MainWindow window;
        wobble::UpdateController updateController(&window);
        QAction *checkForUpdatesAction = window.findChild<QAction *>(
            QStringLiteral("checkForUpdatesAction"));
        QObject::connect(checkForUpdatesAction,
            &QAction::triggered,
            &updateController,
            &wobble::UpdateController::checkForUpdates);
        wobble::FileOpenEventRouter fileOpenRouter(
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
        const wobble::MainWindow::StartupResult startupResult =
            window.initializeSession(requestedFilePath);
        if (!requestedFilePath.isEmpty())
        {
            fileOpenRouter.discardPendingFile(requestedFilePath);
        }
        if (startupResult == wobble::MainWindow::StartupResult::Canceled)
        {
            fileOpenRouter.discardPendingFiles();
            result = EXIT_SUCCESS;
        }
        else if (startupResult == wobble::MainWindow::StartupResult::Failed)
        {
            fileOpenRouter.discardPendingFiles();
            result = EXIT_FAILURE;
        }
        else
        {
            window.show();
            fileOpenRouter.setReady(true);
            result = application.exec();
            spdlog::info("WagleWaglePaint exiting with code {}", result);
        }
    }
    catch (const std::exception &error)
    {
        spdlog::critical("Unhandled exception: {}", error.what());
        QMessageBox::critical(nullptr,
            QObject::tr("WagleWaglePaint"),
            QObject::tr("The application encountered an unexpected error."));
    }

    wobble::Logging::shutdown();
    return result;
}

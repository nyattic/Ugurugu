#include "app/Logging.hpp"
#include "app/UpdateController.hpp"
#include "ui/MainWindow.hpp"
#include "ui/SettingsDialog.hpp"
#include "ui/Theme.hpp"

#include <QAction>
#include <QApplication>
#include <QFileOpenEvent>
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

namespace {

void configureApplicationMetadata()
{
    QApplication::setApplicationName(QStringLiteral("WagleWaglePaint"));
    QApplication::setApplicationDisplayName(QStringLiteral("WagleWaglePaint"));
    QApplication::setApplicationVersion(
        QStringLiteral(WAGLEWAGLEPAINT_VERSION));
    QApplication::setOrganizationName(QStringLiteral("WagleWaglePaint"));
    QApplication::setOrganizationDomain(
        QStringLiteral("waglewaglepaint.dev"));
}

void migrateLegacySettings()
{
    QSettings currentSettings;
    const QString migrationKey =
        QStringLiteral("migration/wobblePaintSettingsImported");
    if (currentSettings.value(migrationKey).toBool()) {
        return;
    }

    QApplication::setApplicationName(QStringLiteral("WobblePaint"));
    QApplication::setOrganizationName(QStringLiteral("WobblePaint"));
    QApplication::setOrganizationDomain(QStringLiteral("wobblepaint.dev"));
    const QSettings legacySettings;
    configureApplicationMetadata();

    for (const QString &key : legacySettings.allKeys()) {
        if (!currentSettings.contains(key)) {
            currentSettings.setValue(key, legacySettings.value(key));
        }
    }
    currentSettings.setValue(migrationKey, true);
    currentSettings.sync();
}

class FileOpenEventFilter final : public QObject
{
public:
    explicit FileOpenEventFilter(wobble::MainWindow *window)
        : m_window(window)
    {
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::FileOpen) {
            const auto *fileEvent = static_cast<QFileOpenEvent *>(event);
            if (!fileEvent->file().isEmpty()) {
                m_window->openFile(fileEvent->file());
                return true;
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    wobble::MainWindow *m_window;
};

}

int main(int argc, char *argv[])
{
    wobble::UpdateController::initialize();
    QApplication application(argc, argv);
    configureApplicationMetadata();
    migrateLegacySettings();
    wobble::Theme::apply(application);

    QTranslator qtBaseTranslator;
    const QString configuredLanguage =
        wobble::SettingsDialog::uiLanguage();
    const QLocale interfaceLocale =
        configuredLanguage == QStringLiteral("system")
        ? QLocale::system()
        : QLocale(configuredLanguage);
    if (qtBaseTranslator.load(
            interfaceLocale,
            QStringLiteral("qtbase"),
            QStringLiteral("_"),
            QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        QApplication::installTranslator(&qtBaseTranslator);
    }
    QTranslator appTranslator;
    if (appTranslator.load(
            interfaceLocale,
            QStringLiteral("wobblepaint"),
            QStringLiteral("_"),
            QStringLiteral(":/i18n"))) {
        QApplication::installTranslator(&appTranslator);
    }

    wobble::Logging::initialize();
    spdlog::info(
        "WagleWaglePaint {} starting",
        QApplication::applicationVersion().toStdString());

    int result = EXIT_FAILURE;
    try {
        wobble::MainWindow window;
        wobble::UpdateController updateController(&window);
        QAction *checkForUpdatesAction =
            window.findChild<QAction *>(
                QStringLiteral("checkForUpdatesAction"));
        QObject::connect(
            checkForUpdatesAction,
            &QAction::triggered,
            &updateController,
            &wobble::UpdateController::checkForUpdates);
        FileOpenEventFilter fileOpenFilter(&window);
        application.installEventFilter(&fileOpenFilter);
        if (application.arguments().size() > 1) {
            const QString filePath =
                QFileInfo(application.arguments().at(1)).absoluteFilePath();
            window.openFile(filePath);
        }
        window.show();
        result = application.exec();
        spdlog::info("WagleWaglePaint exiting with code {}", result);
    } catch (const std::exception &error) {
        spdlog::critical("Unhandled exception: {}", error.what());
        QMessageBox::critical(
            nullptr,
            QObject::tr("WagleWaglePaint"),
            QObject::tr("The application encountered an unexpected error."));
    }

    wobble::Logging::shutdown();
    return result;
}

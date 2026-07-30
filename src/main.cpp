#include "app/Logging.hpp"
#include "ui/MainWindow.hpp"
#include "ui/Theme.hpp"

#include <QApplication>
#include <QFileOpenEvent>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QLocale>
#include <QMessageBox>
#include <QObject>
#include <QTranslator>

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <exception>

namespace {

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
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("WobblePaint"));
    QApplication::setApplicationDisplayName(QStringLiteral("WobblePaint"));
    QApplication::setApplicationVersion(QStringLiteral(WOBBLEPAINT_VERSION));
    QApplication::setOrganizationName(QStringLiteral("WobblePaint"));
    QApplication::setOrganizationDomain(QStringLiteral("wobblepaint.dev"));
    wobble::Theme::apply(application);

    QTranslator qtBaseTranslator;
    if (qtBaseTranslator.load(
            QLocale::system(),
            QStringLiteral("qtbase"),
            QStringLiteral("_"),
            QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        QApplication::installTranslator(&qtBaseTranslator);
    }
    QTranslator appTranslator;
    if (appTranslator.load(
            QLocale::system(),
            QStringLiteral("wobblepaint"),
            QStringLiteral("_"),
            QStringLiteral(":/i18n"))) {
        QApplication::installTranslator(&appTranslator);
    }

    wobble::Logging::initialize();
    spdlog::info("WobblePaint {} starting", QApplication::applicationVersion().toStdString());

    int result = EXIT_FAILURE;
    try {
        wobble::MainWindow window;
        FileOpenEventFilter fileOpenFilter(&window);
        application.installEventFilter(&fileOpenFilter);
        if (application.arguments().size() > 1) {
            const QString filePath =
                QFileInfo(application.arguments().at(1)).absoluteFilePath();
            window.openFile(filePath);
        }
        window.show();
        result = application.exec();
        spdlog::info("WobblePaint exiting with code {}", result);
    } catch (const std::exception &error) {
        spdlog::critical("Unhandled exception: {}", error.what());
        QMessageBox::critical(
            nullptr,
            QObject::tr("WobblePaint"),
            QObject::tr("The application encountered an unexpected error."));
    }

    wobble::Logging::shutdown();
    return result;
}

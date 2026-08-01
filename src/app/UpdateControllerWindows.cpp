#include "app/ReleaseNotes.hpp"
#include "app/UpdateCheckPolicy.hpp"
#include "app/UpdateController.hpp"
#include "ui/SettingsDialog.hpp"

#include <QApplication>
#include <QDateTime>
#include <QFutureWatcher>
#include <QLocale>
#include <QMessageBox>
#include <QPointer>
#include <QProgressDialog>
#include <QPromise>
#include <QSettings>
#include <QTimer>
#include <QtConcurrentRun>

#include <spdlog/spdlog.h>

#include <Velopack.hpp>
#include <algorithm>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>

namespace wobble
{

namespace
{

constexpr auto repositoryUrl = "https://github.com/nyattic/WagleWaglePaint";
constexpr auto lastAutomaticCheckKey = "updates/lastAutomaticCheck";

std::unique_ptr<Velopack::UpdateManager> createUpdateManager()
{
    auto source = std::make_unique<Velopack::GithubSource>(repositoryUrl);
    return std::make_unique<Velopack::UpdateManager>(std::move(source));
}

struct CheckResult
{
    std::optional<Velopack::UpdateInfo> update;
    QString error;
};

struct DownloadResult
{
    std::optional<Velopack::UpdateInfo> update;
    QString error;
};

}

class UpdateController::Impl
{
public:
    explicit Impl(UpdateController *owner, QWidget *window)
        : owner(owner)
        , window(window)
    {
    }

    void startAutomaticCheckIfDue()
    {
        QSettings settings;
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const QDateTime lastCheck =
            settings.value(QString::fromLatin1(lastAutomaticCheckKey))
                .toDateTime();
        if (!UpdateCheckPolicy::isAutomaticCheckDue(lastCheck, now))
        {
            return;
        }

        // Claim the interval before starting the network request. A temporary
        // outage must not turn every application launch into another poll;
        // the explicit Check for Updates action remains available.
        settings.setValue(QString::fromLatin1(lastAutomaticCheckKey), now);
        startCheck(false);
    }

    void startCheck(bool interactive)
    {
        if (busy)
        {
            return;
        }
        busy = true;

        auto *watcher = new QFutureWatcher<CheckResult>(owner);
        QObject::connect(watcher,
            &QFutureWatcher<CheckResult>::finished,
            owner,
            [this, watcher, interactive]()
            {
                const CheckResult result = watcher->result();
                watcher->deleteLater();
                busy = false;

                if (!result.error.isEmpty())
                {
                    spdlog::warn("Update check failed: {}",
                        result.error.toUtf8().constData());
                    if (interactive)
                    {
                        QMessageBox::warning(window,
                            UpdateController::tr("Update failed"),
                            UpdateController::tr(
                                "Could not check for updates.\n\n%1")
                                .arg(result.error));
                    }
                    return;
                }

                if (!result.update)
                {
                    if (interactive)
                    {
                        QMessageBox::information(window,
                            UpdateController::tr("You're up to date"),
                            UpdateController::tr(
                                "WagleWaglePaint %1 is the latest version.")
                                .arg(QApplication::applicationVersion()));
                    }
                    return;
                }

                offerUpdate(*result.update);
            });

        watcher->setFuture(QtConcurrent::run(
            []()
            {
                try
                {
                    auto manager = createUpdateManager();
                    return CheckResult{manager->CheckForUpdates(), {}};
                }
                catch (const std::exception &error)
                {
                    return CheckResult{{}, QString::fromUtf8(error.what())};
                }
            }));
    }

    void offerUpdate(const Velopack::UpdateInfo &update)
    {
        const QString version =
            QString::fromStdString(update.TargetFullRelease.Version);
        QMessageBox message(QMessageBox::Information,
            UpdateController::tr("Update available"),
            UpdateController::tr("WagleWaglePaint %1 is available. "
                                 "Download and install it now?")
                .arg(version),
            QMessageBox::Yes | QMessageBox::No,
            window);
        message.setDefaultButton(QMessageBox::Yes);
        if (!update.TargetFullRelease.NotesMarkdown.empty())
        {
            const QString configured = SettingsDialog::uiLanguage();
            const QString language = configured == QStringLiteral("system")
                                         ? QLocale::system().name()
                                         : configured;
            message.setDetailedText(localizedReleaseNotes(
                QString::fromStdString(update.TargetFullRelease.NotesMarkdown),
                language));
        }
        if (message.exec() == QMessageBox::Yes)
        {
            startDownload(update);
        }
    }

    void startDownload(const Velopack::UpdateInfo &update)
    {
        busy = true;
        auto *progress =
            new QProgressDialog(UpdateController::tr("Downloading update…"),
                QString(),
                0,
                100,
                window);
        progress->setWindowTitle(
            UpdateController::tr("WagleWaglePaint update"));
        progress->setCancelButton(nullptr);
        progress->setMinimumDuration(0);
        progress->setValue(0);
        progress->show();

        auto *watcher = new QFutureWatcher<DownloadResult>(owner);
        QObject::connect(watcher,
            &QFutureWatcher<DownloadResult>::progressValueChanged,
            progress,
            &QProgressDialog::setValue);
        QObject::connect(watcher,
            &QFutureWatcher<DownloadResult>::finished,
            owner,
            [this, watcher, progress]()
            {
                const DownloadResult result = watcher->result();
                watcher->deleteLater();
                progress->close();
                progress->deleteLater();
                busy = false;

                if (!result.error.isEmpty())
                {
                    spdlog::error("Update download failed: {}",
                        result.error.toUtf8().constData());
                    QMessageBox::warning(window,
                        UpdateController::tr("Update failed"),
                        UpdateController::tr(
                            "Could not download the update.\n\n%1")
                            .arg(result.error));
                    return;
                }
                if (result.update)
                {
                    applyUpdate(*result.update);
                }
            });

        watcher->setFuture(QtConcurrent::run(
            [update](QPromise<DownloadResult> &promise)
            {
                promise.setProgressRange(0, 100);
                try
                {
                    auto manager = createUpdateManager();
                    manager->DownloadUpdates(
                        update,
                        [](void *data, size_t value)
                        {
                            auto *downloadPromise =
                                static_cast<QPromise<DownloadResult> *>(data);
                            downloadPromise->setProgressValue(
                                std::clamp(static_cast<int>(value), 0, 100));
                        },
                        &promise);
                    promise.addResult(DownloadResult{update, {}});
                }
                catch (const std::exception &error)
                {
                    promise.addResult(
                        DownloadResult{{}, QString::fromUtf8(error.what())});
                }
            }));
    }

    void applyUpdate(const Velopack::UpdateInfo &update)
    {
        std::unique_ptr<Velopack::UpdateManager> manager;
        try
        {
            manager = createUpdateManager();
        }
        catch (const std::exception &error)
        {
            QMessageBox::warning(window,
                UpdateController::tr("Update failed"),
                UpdateController::tr(
                    "The update was downloaded but could not be installed."
                    "\n\n%1")
                    .arg(QString::fromUtf8(error.what())));
            return;
        }

        const bool quitOnLastWindowClosed = qApp->quitOnLastWindowClosed();
        qApp->setQuitOnLastWindowClosed(false);
        if (window && !window->close())
        {
            qApp->setQuitOnLastWindowClosed(quitOnLastWindowClosed);
            return;
        }

        try
        {
            manager->WaitExitThenApplyUpdates(update);
            qApp->quit();
        }
        catch (const std::exception &error)
        {
            qApp->setQuitOnLastWindowClosed(quitOnLastWindowClosed);
            if (window)
            {
                window->show();
            }
            QMessageBox::warning(window,
                UpdateController::tr("Update failed"),
                UpdateController::tr(
                    "The update was downloaded but could not be installed."
                    "\n\n%1")
                    .arg(QString::fromUtf8(error.what())));
        }
    }

    UpdateController *owner = nullptr;
    QPointer<QWidget> window;
    bool busy = false;
};

void UpdateController::initialize()
{
    Velopack::VelopackApp::Build().Run();
}

UpdateController::UpdateController(QWidget *window, QObject *parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>(this, window))
{
    QTimer::singleShot(2000,
        this,
        [this]()
        {
            m_impl->startAutomaticCheckIfDue();
        });
}

UpdateController::~UpdateController() = default;

void UpdateController::checkForUpdates()
{
    m_impl->startCheck(true);
}

}

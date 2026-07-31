#include "app/UpdateController.hpp"

#include <QTimer>

#import <Sparkle/Sparkle.h>

namespace wobble {

class UpdateController::Impl
{
public:
    void start(bool checkInBackground)
    {
        if (controller) {
            return;
        }
        controller = [
            [SPUStandardUpdaterController alloc]
            initWithStartingUpdater:YES
            updaterDelegate:nil
            userDriverDelegate:nil
        ];
        SPUUpdater *updater = controller.updater;
        if (checkInBackground && updater.automaticallyChecksForUpdates) {
            // A scheduled check may still be hours away when a release is
            // published. Sparkle permits one background check immediately
            // after the updater starts.
            [updater checkForUpdatesInBackground];
        }
    }

    SPUStandardUpdaterController *controller = nil;
};

void UpdateController::initialize()
{
}

UpdateController::UpdateController(QWidget *, QObject *parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>())
{
    QTimer::singleShot(0, this, [this]() {
        m_impl->start(true);
    });
}

UpdateController::~UpdateController() = default;

void UpdateController::checkForUpdates()
{
    m_impl->start(false);
    [m_impl->controller checkForUpdates:nil];
}

}

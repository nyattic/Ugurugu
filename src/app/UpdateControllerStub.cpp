#include "app/UpdateController.hpp"

namespace wobble
{

class UpdateController::Impl
{
};

void UpdateController::initialize()
{
}

UpdateController::UpdateController(QWidget *, QObject *parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>())
{
}

UpdateController::~UpdateController() = default;

void UpdateController::checkForUpdates()
{
}

}

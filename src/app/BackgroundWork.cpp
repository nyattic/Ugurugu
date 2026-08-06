#include "app/BackgroundWork.hpp"

#include <QThreadPool>

namespace ugurugu
{

void joinDetachedBackgroundWork()
{
    QThreadPool::globalInstance()->waitForDone();
}

}

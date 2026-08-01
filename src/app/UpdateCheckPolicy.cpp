#include "app/UpdateCheckPolicy.hpp"

namespace wobble
{

bool UpdateCheckPolicy::isAutomaticCheckDue(
    const QDateTime &lastCheck, const QDateTime &now)
{
    if (!now.isValid())
    {
        return false;
    }
    if (!lastCheck.isValid() || lastCheck > now)
    {
        return true;
    }
    return lastCheck.secsTo(now) >= automaticCheckIntervalSeconds;
}

}

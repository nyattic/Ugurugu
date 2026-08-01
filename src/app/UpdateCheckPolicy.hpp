#pragma once

#include <QDateTime>

namespace wobble
{

class UpdateCheckPolicy final
{
public:
    static constexpr qint64 automaticCheckIntervalSeconds = 24 * 60 * 60;

    static bool isAutomaticCheckDue(
        const QDateTime &lastCheck, const QDateTime &now);
};

}

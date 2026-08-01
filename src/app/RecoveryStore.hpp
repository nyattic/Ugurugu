#pragma once

#include <QString>

namespace wobble
{

class RecoveryStore final
{
public:
    static QString filePath();
    static bool ensureParentDirectory(QString *error = nullptr);
    static bool discard(QString *error = nullptr);

    // Moves an unreadable recovery aside so it can be inspected or recovered
    // manually. On failure, the original file is deliberately left intact.
    static QString quarantine(QString *error = nullptr);
};

}

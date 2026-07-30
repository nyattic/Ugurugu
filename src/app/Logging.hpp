#pragma once

#include <QString>

namespace wobble
{

class Logging final
{
public:
    static void initialize();
    static void shutdown();
    static QString logFilePath();
};

}

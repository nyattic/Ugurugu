#pragma once

#include <QString>

namespace ugurugu
{

class Logging final
{
public:
    static void initialize();
    static void shutdown();
    static QString logFilePath();
};

}

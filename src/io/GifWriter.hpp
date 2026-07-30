#pragma once

#include <QImage>
#include <QString>
#include <QVector>

namespace wobble
{

class GifWriter final
{
public:
    static bool write(
        const QString &path,
        const QVector<QImage> &frames,
        int delayCentiseconds,
        QString *error = nullptr);

    static bool write(
        const QString &path,
        const QVector<QImage> &frames,
        const QVector<int> &delaysCentiseconds,
        QString *error = nullptr);
};

}

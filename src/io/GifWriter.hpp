#pragma once

#include <QCoreApplication>
#include <QImage>
#include <QString>
#include <QVector>

namespace wobble
{

class GifWriter final
{
    Q_DECLARE_TR_FUNCTIONS(wobble::GifWriter)

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

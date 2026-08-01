#pragma once

#include "document/Document.hpp"

#include <QString>
#include <QVector>

namespace wobble
{

struct EraserPreset
{
    QString id;
    const char *sourceName;
    BrushSettings settings;
    qreal defaultSize = 6.0;
};

struct EraserPresetCatalog final
{
    static const QVector<EraserPreset> &builtIns();
    static const EraserPreset &defaultPreset();
    static const EraserPreset *find(const QString &id);
    static QString displayName(const EraserPreset &preset);
};

}

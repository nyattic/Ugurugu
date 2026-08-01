#pragma once

#include <QWidget>

namespace wobble
{

class CanvasWidget;

class StrokeStabilizationRow final : public QWidget
{
    Q_OBJECT

public:
    enum class Target
    {
        Brush,
        Eraser
    };

    StrokeStabilizationRow(CanvasWidget *canvas,
        Target target,
        const QString &objectNamePrefix,
        QWidget *parent = nullptr);
};

}

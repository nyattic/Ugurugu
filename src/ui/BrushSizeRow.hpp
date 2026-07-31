#pragma once

#include <QWidget>

namespace wobble {

class CanvasWidget;

class BrushSizeRow final : public QWidget
{
    Q_OBJECT

public:
    enum class Target {
        Brush,
        Eraser
    };

    BrushSizeRow(
        CanvasWidget *canvas,
        Target target,
        const QString &objectNamePrefix,
        QWidget *parent = nullptr);
};

}

#pragma once

#include <QWidget>

namespace wobble {

class CanvasWidget;

class BrushSizeRow final : public QWidget
{
    Q_OBJECT

public:
    BrushSizeRow(
        CanvasWidget *canvas,
        const QString &objectNamePrefix,
        QWidget *parent = nullptr);
};

}

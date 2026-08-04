#pragma once

#include <QWidget>

namespace wobble
{

class CanvasWidget;

class BucketPopoverPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit BucketPopoverPanel(
        CanvasWidget *canvas, QWidget *parent = nullptr);
};

}

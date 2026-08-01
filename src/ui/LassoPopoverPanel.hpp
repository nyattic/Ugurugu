#pragma once

#include <QWidget>

namespace wobble
{

class CanvasWidget;

class LassoPopoverPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit LassoPopoverPanel(CanvasWidget *canvas, QWidget *parent = nullptr);
};

}

#pragma once

#include <QWidget>

namespace wobble
{

class CanvasWidget;

class WandPopoverPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit WandPopoverPanel(CanvasWidget *canvas, QWidget *parent = nullptr);
};

}

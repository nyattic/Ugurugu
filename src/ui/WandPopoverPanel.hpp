#pragma once

#include <QWidget>

namespace ugurugu
{

class CanvasWidget;

class WandPopoverPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit WandPopoverPanel(CanvasWidget *canvas, QWidget *parent = nullptr);
};

}

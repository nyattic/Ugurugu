#pragma once

#include <QWidget>

namespace ugurugu
{

class CanvasWidget;

class EraserPopoverPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit EraserPopoverPanel(
        CanvasWidget *canvas, QWidget *parent = nullptr);
};

}

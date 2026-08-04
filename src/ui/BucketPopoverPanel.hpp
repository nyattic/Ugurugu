#pragma once

#include <QWidget>

namespace ugurugu
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

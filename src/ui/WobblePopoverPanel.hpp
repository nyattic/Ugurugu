#pragma once

#include <QWidget>

namespace wobble
{

class DocumentController;

class WobblePopoverPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit WobblePopoverPanel(
        DocumentController *controller, QWidget *parent = nullptr);
};

}

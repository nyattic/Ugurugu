#pragma once

#include <QWidget>

namespace ugurugu
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

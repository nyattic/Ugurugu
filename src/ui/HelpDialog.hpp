#pragma once

#include <QDialog>

namespace wobble
{

class HelpDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit HelpDialog(QWidget *parent = nullptr);
};

}

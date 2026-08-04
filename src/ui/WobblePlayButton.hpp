#pragma once

#include <QToolButton>

namespace ugurugu
{

class WobblePlayButton final : public QToolButton
{
    Q_OBJECT

public:
    explicit WobblePlayButton(QWidget *parent = nullptr);

private:
    void refreshIcon();
};

}

#pragma once

#include <QTimer>
#include <QToolButton>

namespace wobble {

class ToolPopover;

class PopoverToolButton final : public QToolButton
{
    Q_OBJECT

public:
    explicit PopoverToolButton(QWidget *parent = nullptr);

    void setPopover(ToolPopover *popover);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void openPopover();

    ToolPopover *m_popover = nullptr;
    QTimer m_longPressTimer;
    bool m_checkedAtPress = false;
};

}

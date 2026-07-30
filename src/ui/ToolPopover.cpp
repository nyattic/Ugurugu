#include "ui/ToolPopover.hpp"

#include "ui/Theme.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QVBoxLayout>

#include <algorithm>

namespace wobble {

ToolPopover::ToolPopover(QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 12, 14, 12);
}

void ToolPopover::setContentWidget(QWidget *content)
{
    layout()->addWidget(content);
}

void ToolPopover::popupBeside(QWidget *anchor)
{
    if (m_lastHide.isValid() && m_lastHide.elapsed() < 160) {
        m_lastHide.invalidate();
        return;
    }
    adjustSize();
    QPoint target =
        anchor->mapToGlobal(QPoint(anchor->width() + 6, -6));
    if (const QScreen *screen = anchor->screen()) {
        const QRect available = screen->availableGeometry();
        target.setX(std::clamp(
            target.x(),
            available.left(),
            available.right() - width() + 1));
        target.setY(std::clamp(
            target.y(),
            available.top(),
            available.bottom() - height() + 1));
    }
    move(target);
    show();
}

void ToolPopover::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF frame =
        QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(frame, 10.0, 10.0);
    painter.fillPath(path, Theme::panelBackground());
    painter.setPen(QPen(Theme::border(), 1.0));
    painter.drawPath(path);
}

void ToolPopover::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    emit popoverShown();
}

void ToolPopover::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    m_lastHide.start();
    emit popoverHidden();
}

}

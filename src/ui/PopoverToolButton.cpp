#include "ui/PopoverToolButton.hpp"

#include "ui/Theme.hpp"
#include "ui/ToolPopover.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace wobble {

PopoverToolButton::PopoverToolButton(QWidget *parent)
    : QToolButton(parent)
{
    m_longPressTimer.setSingleShot(true);
    m_longPressTimer.setInterval(480);
    connect(&m_longPressTimer, &QTimer::timeout, this, [this]() {
        if (!isDown()) {
            return;
        }
        setDown(false);
        if (defaultAction() && !defaultAction()->isChecked()) {
            defaultAction()->trigger();
        }
        openPopover();
    });
}

void PopoverToolButton::setPopover(ToolPopover *popover)
{
    m_popover = popover;
    update();
}

void PopoverToolButton::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_checkedAtPress = isChecked();
        if (m_popover) {
            m_longPressTimer.start();
        }
    }
    QToolButton::mousePressEvent(event);
}

void PopoverToolButton::mouseReleaseEvent(QMouseEvent *event)
{
    m_longPressTimer.stop();
    const bool insideRelease =
        event->button() == Qt::LeftButton
        && rect().contains(event->position().toPoint());
    QToolButton::mouseReleaseEvent(event);
    if (m_popover && insideRelease && m_checkedAtPress) {
        openPopover();
    }
}

void PopoverToolButton::paintEvent(QPaintEvent *event)
{
    QToolButton::paintEvent(event);
    if (!m_popover) {
        return;
    }
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPointF corner(width() - 4.0, height() - 4.0);
    QPainterPath indicator;
    indicator.moveTo(corner);
    indicator.lineTo(corner + QPointF(-5.0, 0.0));
    indicator.lineTo(corner + QPointF(0.0, -5.0));
    indicator.closeSubpath();
    painter.fillPath(
        indicator,
        isChecked() ? Theme::accentText() : Theme::textMuted());
}

void PopoverToolButton::openPopover()
{
    if (m_popover) {
        m_popover->popupBeside(this);
    }
}

}

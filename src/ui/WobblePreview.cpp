#include "ui/WobblePreview.hpp"

#include "document/DocumentController.hpp"
#include "ui/Theme.hpp"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace wobble {

WobblePreview::WobblePreview(
    DocumentController *controller,
    QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
{
    setFixedSize(sizeHint());
    setAccessibleName(tr("Wobble preview"));
    setToolTip(tr("Live preview of the wobble strength"));

    m_timer.setInterval(120);
    connect(&m_timer, &QTimer::timeout, this, [this]() {
        m_phase += 1.0;
        if (m_controller->document().wobbleAmount > 0.0) {
            update();
        }
    });
    connect(
        m_controller,
        &DocumentController::documentChanged,
        this,
        [this]() { update(); });
}

QSize WobblePreview::sizeHint() const
{
    return QSize(58, 24);
}

void WobblePreview::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qreal amount = m_controller->document().wobbleAmount;
    const qreal amplitude = std::min(6.5, amount * 0.55);
    const qreal centerY = height() / 2.0;

    QPainterPath line;
    bool started = false;
    for (qreal x = 5.0; x <= width() - 5.0; x += 2.0) {
        const qreal y = centerY
            + amplitude
                * (0.62 * std::sin(x * 0.16 + m_phase)
                   + 0.38
                       * std::sin(
                           x * 0.37
                           + m_phase * 1.4
                           + std::numbers::pi_v<qreal> / 3.0));
        if (!started) {
            line.moveTo(x, y);
            started = true;
        } else {
            line.lineTo(x, y);
        }
    }

    painter.setPen(
        QPen(
            Theme::accent(),
            2.0,
            Qt::SolidLine,
            Qt::RoundCap,
            Qt::RoundJoin));
    painter.drawPath(line);
}

void WobblePreview::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    m_timer.start();
}

void WobblePreview::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    m_timer.stop();
}

}

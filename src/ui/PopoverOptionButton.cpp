#include "ui/PopoverOptionButton.hpp"

#include "ui/Theme.hpp"

#include <QPainter>
#include <QPainterPath>

#include <utility>

namespace wobble
{

namespace
{

constexpr int cardWidth = 264;
constexpr int cardHeight = 62;

}

PopoverOptionButton::PopoverOptionButton(
    QString title, QString description, QWidget *parent)
    : QAbstractButton(parent)
    , m_title(std::move(title))
    , m_description(std::move(description))
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setToolTip(m_description);
    setAccessibleName(m_title);
    setAccessibleDescription(m_description);
    setAttribute(Qt::WA_Hover);
}

QSize PopoverOptionButton::sizeHint() const
{
    return QSize(cardWidth, cardHeight);
}

void PopoverOptionButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF frame = QRectF(rect()).adjusted(0.75, 0.75, -0.75, -0.75);
    QPainterPath card;
    card.addRoundedRect(frame, 8.0, 8.0);

    QColor background =
        isChecked() ? Theme::hoverBackground() : Theme::canvasBackground();
    if (underMouse() && !isChecked())
    {
        background = Theme::hoverBackground();
    }
    painter.fillPath(card, background);
    painter.setPen(isChecked() || hasFocus() ? QPen(Theme::accent(), 1.5)
                                             : QPen(Theme::border(), 1.0));
    painter.drawPath(card);

    paintPreview(painter, QRectF(12.0, 11.0, 42.0, 40.0));

    QFont titleFont = font();
    titleFont.setPixelSize(12);
    titleFont.setWeight(QFont::DemiBold);
    painter.setFont(titleFont);
    painter.setPen(Theme::textPrimary());
    painter.drawText(QRectF(66.0, 11.0, width() - 78.0, 19.0),
        Qt::AlignLeft | Qt::AlignVCenter,
        painter.fontMetrics().elidedText(
            m_title, Qt::ElideRight, width() - 78));

    QFont descriptionFont = font();
    descriptionFont.setPixelSize(10);
    painter.setFont(descriptionFont);
    painter.setPen(Theme::textMuted());
    painter.drawText(QRectF(66.0, 31.0, width() - 78.0, 18.0),
        Qt::AlignLeft | Qt::AlignVCenter,
        painter.fontMetrics().elidedText(
            m_description, Qt::ElideRight, width() - 78));
}

}

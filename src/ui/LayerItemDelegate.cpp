#include "ui/LayerItemDelegate.hpp"

#include "ui/Icons.hpp"
#include "ui/Theme.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace wobble
{

namespace
{

constexpr int rowHeight = 46;
constexpr int rowPadding = 8;
constexpr int accentBarWidth = 3;
constexpr int thumbnailWidth = 48;
constexpr int thumbnailHeight = 32;
constexpr int eyeSize = 20;

const QPixmap &eyeOpenPixmap()
{
    static const QPixmap pixmap = Icons::pixmap(
        IconGlyph::EyeOpen, eyeSize, Theme::textMuted(), 0.0, 2.0);
    return pixmap;
}

const QPixmap &eyeClosedPixmap()
{
    static const QPixmap pixmap = Icons::pixmap(
        IconGlyph::EyeClosed, eyeSize, Theme::textDisabled(), 0.0, 2.0);
    return pixmap;
}

}

LayerItemDelegate::LayerItemDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

QRect LayerItemDelegate::thumbnailRect(const QRect &rowRect) const
{
    const int y = rowRect.top() + (rowRect.height() - thumbnailHeight) / 2;
    return QRect(rowRect.left() + rowPadding + accentBarWidth + 8,
        y,
        thumbnailWidth,
        thumbnailHeight);
}

QRect LayerItemDelegate::eyeRect(const QRect &rowRect) const
{
    const int y = rowRect.top() + (rowRect.height() - eyeSize) / 2;
    return QRect(rowRect.right() - rowPadding - eyeSize, y, eyeSize, eyeSize);
}

QRect LayerItemDelegate::nameRect(const QRect &rowRect) const
{
    const QRect thumb = thumbnailRect(rowRect);
    const QRect eye = eyeRect(rowRect);
    return QRect(thumb.right() + 10,
        rowRect.top(),
        eye.left() - thumb.right() - 16,
        rowRect.height());
}

void LayerItemDelegate::paint(QPainter *painter,
    const QStyleOptionViewItem &option,
    const QModelIndex &index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const bool selected = option.state.testFlag(QStyle::State_Selected);
    const bool visible = index.data(LayerItemRoles::Visible).toBool();

    if (selected)
    {
        const QRectF card = QRectF(option.rect).adjusted(4.0, 2.0, -4.0, -2.0);
        painter->setPen(Qt::NoPen);
        painter->setBrush(Theme::controlBackground());
        painter->drawRoundedRect(card, 7.0, 7.0);
        painter->setBrush(Theme::accent());
        painter->drawRoundedRect(QRectF(card.left() + 4.0,
                                     card.center().y() - 10.0,
                                     accentBarWidth,
                                     20.0),
            1.5,
            1.5);
    }

    painter->setOpacity(visible ? 1.0 : 0.4);

    const QRect thumbRect = thumbnailRect(option.rect);
    const QPixmap thumbnail =
        index.data(LayerItemRoles::Thumbnail).value<QPixmap>();
    QPainterPath thumbClip;
    thumbClip.addRoundedRect(QRectF(thumbRect), 4.0, 4.0);
    painter->save();
    painter->setClipPath(thumbClip);
    painter->fillRect(thumbRect, Theme::canvasBackground());
    if (!thumbnail.isNull())
    {
        const QSizeF drawnSize = thumbnail.deviceIndependentSize().scaled(
            thumbRect.size(), Qt::KeepAspectRatio);
        QRectF drawnRect(QPointF(), drawnSize);
        drawnRect.moveCenter(QRectF(thumbRect).center());
        painter->drawPixmap(drawnRect.toRect(), thumbnail);
    }
    painter->restore();
    painter->setPen(QPen(Theme::border(), 1.0));
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(
        QRectF(thumbRect).adjusted(0.5, 0.5, -0.5, -0.5), 4.0, 4.0);

    painter->setPen(visible ? Theme::textPrimary() : Theme::textMuted());
    const QString name = index.data(Qt::DisplayRole).toString();
    const QRect textRect = nameRect(option.rect);
    painter->drawText(textRect,
        Qt::AlignLeft | Qt::AlignVCenter,
        option.fontMetrics.elidedText(name, Qt::ElideRight, textRect.width()));

    painter->setOpacity(1.0);
    const QPixmap &eye = visible ? eyeOpenPixmap() : eyeClosedPixmap();
    painter->drawPixmap(eyeRect(option.rect), eye);

    painter->restore();
}

QSize LayerItemDelegate::sizeHint(
    const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    Q_UNUSED(index);
    return QSize(option.rect.width(), rowHeight);
}

bool LayerItemDelegate::editorEvent(QEvent *event,
    QAbstractItemModel *model,
    const QStyleOptionViewItem &option,
    const QModelIndex &index)
{
    if (event->type() == QEvent::MouseButtonPress
        || event->type() == QEvent::MouseButtonRelease)
    {
        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton
            && eyeRect(option.rect)
                .adjusted(-3, -3, 3, 3)
                .contains(mouseEvent->position().toPoint()))
        {
            if (event->type() == QEvent::MouseButtonRelease)
            {
                emit visibilityToggled(index);
            }
            return true;
        }
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

void LayerItemDelegate::updateEditorGeometry(QWidget *editor,
    const QStyleOptionViewItem &option,
    const QModelIndex &index) const
{
    Q_UNUSED(index);
    QRect rect = nameRect(option.rect);
    rect.adjust(-4, 6, 4, -6);
    editor->setGeometry(rect);
}

}

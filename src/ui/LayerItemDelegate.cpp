#include "ui/LayerItemDelegate.hpp"

#include "document/Document.hpp"
#include "document/DocumentLimits.hpp"
#include "ui/Icons.hpp"
#include "ui/Theme.hpp"

#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

#include <algorithm>

namespace ugurugu
{

namespace
{

constexpr int rowHeight = 56;
constexpr int rowPadding = 8;
constexpr int accentBarWidth = 3;
constexpr int thumbnailWidth = 48;
constexpr int thumbnailHeight = 32;
constexpr int eyeSize = 20;
constexpr int wobbleSize = 18;
constexpr int metaHeight = 15;
constexpr int metaFontPixelSize = 10;
constexpr int depthIndent = 14;

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

const QPixmap &wobbleOnPixmap()
{
    static const QPixmap pixmap =
        Icons::pixmap(IconGlyph::Wobble, wobbleSize, Theme::accent(), 0.0, 2.0);
    return pixmap;
}

const QPixmap &wobbleOffPixmap()
{
    static const QPixmap pixmap = Icons::pixmap(
        IconGlyph::Wobble, wobbleSize, Theme::textDisabled(), 0.0, 2.0);
    return pixmap;
}

}

LayerItemDelegate::LayerItemDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

QRect LayerItemDelegate::thumbnailRect(const QRect &rowRect, int depth) const
{
    const int y = rowRect.top() + (rowRect.height() - thumbnailHeight) / 2;
    return QRect(rowRect.left() + rowPadding + accentBarWidth + 8
                     + std::max(0, depth) * depthIndent,
        y,
        thumbnailWidth,
        thumbnailHeight);
}

QRect LayerItemDelegate::eyeRect(const QRect &rowRect) const
{
    const int y = rowRect.top() + (rowRect.height() - eyeSize) / 2;
    return QRect(rowRect.right() - rowPadding - eyeSize, y, eyeSize, eyeSize);
}

QRect LayerItemDelegate::wobbleRect(const QRect &rowRect) const
{
    const QRect eye = eyeRect(rowRect);
    return QRect(eye.left() - 6 - wobbleSize,
        rowRect.top() + (rowRect.height() - wobbleSize) / 2,
        wobbleSize,
        wobbleSize);
}

QRect LayerItemDelegate::textColumnRect(const QRect &rowRect, int depth) const
{
    const QRect thumb = thumbnailRect(rowRect, depth);
    const QRect wobble = wobbleRect(rowRect);
    return QRect(thumb.right() + 10,
        rowRect.top() + 4,
        wobble.left() - thumb.right() - 16,
        rowRect.height() - 8);
}

QRect LayerItemDelegate::metaRect(const QRect &rowRect, int depth) const
{
    QRect column = textColumnRect(rowRect, depth);
    column.setHeight(metaHeight);
    return column;
}

QRect LayerItemDelegate::nameRect(const QRect &rowRect, int depth) const
{
    QRect column = textColumnRect(rowRect, depth);
    column.setTop(column.top() + metaHeight);
    return column;
}

void LayerItemDelegate::paint(QPainter *painter,
    const QStyleOptionViewItem &option,
    const QModelIndex &index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const bool selected = option.state.testFlag(QStyle::State_Selected);
    const bool visible = index.data(LayerItemRoles::Visible).toBool();
    const int depth = index.data(LayerItemRoles::Depth).toInt();
    const bool group = index.data(LayerItemRoles::Kind).toInt()
                       == static_cast<int>(LayerKind::Group);
    const bool clipped = index.data(LayerItemRoles::Clipped).toBool();
    const bool reference = index.data(LayerItemRoles::Reference).toBool();

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

    const QRect thumbRect = thumbnailRect(option.rect, depth);
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

    const QString blendModeName =
        index.data(LayerItemRoles::BlendModeName).toString();
    if (!blendModeName.isEmpty())
    {
        QFont metaFont = option.font;
        metaFont.setPixelSize(metaFontPixelSize);
        painter->save();
        painter->setFont(metaFont);
        painter->setPen(Theme::textMuted());
        const QRect meta = metaRect(option.rect, depth);
        const QString metaText =
            QStringLiteral("%1 %  %2")
                .arg(index.data(LayerItemRoles::OpacityPercent).toInt())
                .arg(blendModeName);
        painter->drawText(meta,
            Qt::AlignLeft | Qt::AlignVCenter,
            QFontMetrics(metaFont).elidedText(
                metaText, Qt::ElideRight, meta.width()));
        painter->restore();
    }

    painter->setPen(visible ? Theme::textPrimary() : Theme::textMuted());
    const QString name = index.data(Qt::DisplayRole).toString();
    QRect textRect = nameRect(option.rect, depth);
    if (group || clipped || reference)
    {
        QStringList badges;
        if (group)
        {
            badges.append(QStringLiteral("G"));
        }
        if (clipped)
        {
            badges.append(QStringLiteral("↳"));
        }
        if (reference)
        {
            badges.append(QStringLiteral("R"));
        }
        const QString badge = badges.join(QLatin1Char(' '));
        const QRect badgeRect(textRect.left(),
            textRect.top(),
            option.fontMetrics.horizontalAdvance(badge) + 8,
            textRect.height());
        painter->setPen(Theme::accent());
        painter->drawText(badgeRect, Qt::AlignLeft | Qt::AlignVCenter, badge);
        textRect.setLeft(badgeRect.right());
        painter->setPen(visible ? Theme::textPrimary() : Theme::textMuted());
    }
    painter->drawText(textRect,
        Qt::AlignLeft | Qt::AlignVCenter,
        option.fontMetrics.elidedText(name, Qt::ElideRight, textRect.width()));

    painter->setOpacity(1.0);
    if (index.data(LayerItemRoles::WobbleToggleable).toBool())
    {
        const bool stopped = index.data(LayerItemRoles::WobbleStopped).toBool();
        painter->drawPixmap(wobbleRect(option.rect),
            stopped ? wobbleOffPixmap() : wobbleOnPixmap());
    }
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
        const QPoint position = mouseEvent->position().toPoint();
        if (mouseEvent->button() == Qt::LeftButton
            && eyeRect(option.rect).adjusted(-3, -3, 3, 3).contains(position))
        {
            if (event->type() == QEvent::MouseButtonRelease)
            {
                emit visibilityToggled(index);
            }
            return true;
        }
        if (mouseEvent->button() == Qt::LeftButton
            && index.data(LayerItemRoles::WobbleToggleable).toBool()
            && wobbleRect(option.rect)
                .adjusted(-3, -3, 3, 3)
                .contains(position))
        {
            if (event->type() == QEvent::MouseButtonRelease)
            {
                emit wobbleToggled(index);
            }
            return true;
        }
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

QWidget *LayerItemDelegate::createEditor(QWidget *parent,
    const QStyleOptionViewItem &option,
    const QModelIndex &index) const
{
    QWidget *editor = QStyledItemDelegate::createEditor(parent, option, index);
    if (auto *lineEdit = qobject_cast<QLineEdit *>(editor))
    {
        lineEdit->setMaxLength(DocumentLimits::maximumLayerNameLength);
    }
    return editor;
}

void LayerItemDelegate::updateEditorGeometry(QWidget *editor,
    const QStyleOptionViewItem &option,
    const QModelIndex &index) const
{
    Q_UNUSED(index);
    const int depth = index.data(LayerItemRoles::Depth).toInt();
    QRect rect = nameRect(option.rect, depth);
    rect.adjust(-4, 6, 4, -6);
    editor->setGeometry(rect);
}

}

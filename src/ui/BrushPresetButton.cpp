#include "ui/BrushPresetButton.hpp"

#include "brush/BrushPreset.hpp"
#include "render/RenderEngine.hpp"
#include "ui/Theme.hpp"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace wobble
{

namespace
{

constexpr int cardWidth = 132;
constexpr int cardHeight = 64;
constexpr int previewWidth = 116;
constexpr int previewHeight = 36;
constexpr qreal previewScale = 2.0;
constexpr qreal previewWobbleAmount = 2.2;

Stroke previewStroke(const BrushPreset &preset)
{
    Stroke stroke;
    stroke.seed = qHash(preset.id);
    stroke.color = Theme::textPrimary();
    stroke.width = std::min(preset.defaultSize, 16.0) * previewScale;
    stroke.brush = preset.settings;

    constexpr int sampleCount = 26;
    stroke.points.reserve(sampleCount);
    for (int index = 0; index < sampleCount; ++index)
    {
        const qreal t = static_cast<qreal>(index) / (sampleCount - 1);
        StrokePoint point;
        point.position = QPointF(
            (10.0 + t * (previewWidth - 20.0)) * previewScale,
            (previewHeight * 0.5 - std::sin(t * std::numbers::pi * 1.5) * 5.0)
                * previewScale);
        point.pressure = 0.2 + 0.8 * std::sin(t * std::numbers::pi);
        stroke.points.append(point);
    }
    return stroke;
}

}

BrushPresetButton::BrushPresetButton(const BrushPreset &preset, QWidget *parent)
    : QAbstractButton(parent)
    , m_preset(&preset)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    const QString displayName = BrushPresetCatalog::displayName(preset);
    setToolTip(displayName);
    setAccessibleName(displayName);
    setAttribute(Qt::WA_Hover);
    m_frames.resize(previewFrameCount);
}

QString BrushPresetButton::presetId() const
{
    return m_preset->id;
}

void BrushPresetButton::setPreviewFrame(int frame)
{
    const int normalized = frame % previewFrameCount;
    if (m_frame == normalized)
    {
        return;
    }
    m_frame = normalized;
    update();
}

QSize BrushPresetButton::sizeHint() const
{
    return QSize(cardWidth, cardHeight);
}

void BrushPresetButton::paintEvent(QPaintEvent *event)
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
    painter.setPen(
        isChecked() ? QPen(Theme::accent(), 1.5) : QPen(Theme::border(), 1.0));
    painter.drawPath(card);

    painter.drawImage(
        QPointF((width() - previewWidth) * 0.5, 5.0), frameImage(m_frame));

    QFont nameFont = font();
    nameFont.setPixelSize(11);
    painter.setFont(nameFont);
    painter.setPen(isChecked() ? Theme::textPrimary() : Theme::textMuted());
    const QString name = painter.fontMetrics().elidedText(
        BrushPresetCatalog::displayName(*m_preset),
        Qt::ElideRight,
        width() - 12);
    painter.drawText(QRectF(6.0, height() - 20.0, width() - 12.0, 16.0),
        Qt::AlignHCenter | Qt::AlignVCenter,
        name);
}

const QImage &BrushPresetButton::frameImage(int frame)
{
    QImage &cached = m_frames[frame];
    if (!cached.isNull())
    {
        return cached;
    }

    Document document;
    document.size = QSize(qRound(previewWidth * previewScale),
        qRound(previewHeight * previewScale));
    document.background = QColor(0, 0, 0, 0);
    document.animationFrames = previewFrameCount;
    document.wobbleAmount = previewWobbleAmount;

    Layer layer;
    layer.strokes.append(previewStroke(*m_preset));
    document.layers.append(layer);

    cached = RenderEngine::render(document, frame);
    cached.setDevicePixelRatio(previewScale);
    return cached;
}

}

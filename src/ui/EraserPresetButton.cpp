#include "ui/EraserPresetButton.hpp"

#include "brush/EraserPreset.hpp"
#include "render/RenderEngine.hpp"
#include "ui/Theme.hpp"

#include <QPainter>
#include <QPainterPath>

#include <utility>

namespace ugurugu
{

namespace
{

constexpr int cardWidth = 86;
constexpr int cardHeight = 64;
constexpr int previewWidth = 70;
constexpr int previewHeight = 36;
constexpr qreal previewScale = 2.0;

Stroke previewBackground()
{
    Stroke stroke;
    stroke.color = Theme::textPrimary();
    stroke.width = 20.0 * previewScale;
    stroke.brush.sizeDynamics = 0.0;
    stroke.brush.antialiasing = true;
    stroke.points = {{QPointF(4.0, 18.0) * previewScale, 1.0},
        {QPointF(66.0, 18.0) * previewScale, 1.0}};
    return stroke;
}

Stroke previewErase(const EraserPreset &preset)
{
    Stroke stroke;
    stroke.seed = qHash(preset.id);
    stroke.mode = StrokeMode::Erase;
    stroke.width = 16.0 * previewScale;
    stroke.brush = preset.settings;
    constexpr int sampleCount = 18;
    stroke.points.reserve(sampleCount);
    for (int index = 0; index < sampleCount; ++index)
    {
        const qreal progress = static_cast<qreal>(index) / (sampleCount - 1);
        stroke.points.append(
            {QPointF(26.0 + progress * 18.0, 4.0 + progress * 28.0)
                    * previewScale,
                1.0});
    }
    return stroke;
}

}

EraserPresetButton::EraserPresetButton(
    const EraserPreset &preset, QWidget *parent)
    : QAbstractButton(parent)
    , m_preset(&preset)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    const QString displayName = EraserPresetCatalog::displayName(preset);
    setToolTip(displayName);
    setAccessibleName(displayName);
    setAttribute(Qt::WA_Hover);
}

QString EraserPresetButton::presetId() const
{
    return m_preset->id;
}

QSize EraserPresetButton::sizeHint() const
{
    return QSize(cardWidth, cardHeight);
}

void EraserPresetButton::paintEvent(QPaintEvent *event)
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
        QPointF((width() - previewWidth) * 0.5, 5.0), previewImage());

    QFont nameFont = font();
    nameFont.setPixelSize(11);
    painter.setFont(nameFont);
    painter.setPen(isChecked() ? Theme::textPrimary() : Theme::textMuted());
    const QString name = painter.fontMetrics().elidedText(
        EraserPresetCatalog::displayName(*m_preset),
        Qt::ElideRight,
        width() - 10);
    painter.drawText(QRectF(5.0, height() - 20.0, width() - 10.0, 16.0),
        Qt::AlignHCenter | Qt::AlignVCenter,
        name);
}

const QImage &EraserPresetButton::previewImage()
{
    if (!m_preview.isNull())
    {
        return m_preview;
    }
    Document document;
    document.size = QSize(qRound(previewWidth * previewScale),
        qRound(previewHeight * previewScale));
    document.background = Qt::transparent;
    document.wobbleAmount = 0.0;
    Layer layer;
    layer.strokes = {previewBackground(), previewErase(*m_preset)};
    document.layers.append(std::move(layer));
    m_preview = RenderEngine::render(document, 0);
    m_preview.setDevicePixelRatio(previewScale);
    return m_preview;
}

}

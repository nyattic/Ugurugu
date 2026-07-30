#pragma once

#include <QColor>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QUuid>
#include <QVector>

namespace wobble
{

enum class StrokeMode {
    Paint,
    Erase,
    Fill
};

enum class BrushEngine {
    Line,
    Airbrush,
    Spray
};

enum class BrushTipShape {
    Round,
    Square
};

struct BrushSettings {
    BrushEngine engine = BrushEngine::Line;
    BrushTipShape tipShape = BrushTipShape::Round;
    qreal opacity = 1.0;
    qreal flow = 1.0;
    qreal hardness = 1.0;
    qreal spacing = 0.15;
    qreal scatter = 0.0;
    qreal particleSize = 0.08;
    qreal density = 1.0;
    qreal sizeDynamics = 0.8;
    qreal opacityDynamics = 0.0;
    qreal sizeJitter = 0.0;
    bool animatedJitter = false;

    bool operator==(const BrushSettings &) const = default;
};

bool isValidBrushSettings(const BrushSettings &settings);

struct StrokePoint {
    QPointF position;
    qreal pressure = 1.0;
};

struct Stroke {
    QUuid id = QUuid::createUuid();
    quint64 seed = 0;
    StrokeMode mode = StrokeMode::Paint;
    QColor color = Qt::black;
    qreal width = 6.0;
    BrushSettings brush;
    QVector<StrokePoint> points;
};

struct Layer {
    QUuid id = QUuid::createUuid();
    QString name;
    bool visible = true;
    qreal opacity = 1.0;
    QVector<Stroke> strokes;
};

struct Document {
    QSize size = QSize(1024, 768);
    QColor background = Qt::white;
    int animationFrames = 30;
    qreal framesPerSecond = 25.0;
    qreal wobbleAmount = 1.6;
    QVector<Layer> layers;
    QUuid activeLayerId;

    static Document createDefault(const QSize &size = QSize(1024, 768));
    Layer *layer(const QUuid &id);
    const Layer *layer(const QUuid &id) const;
    int layerIndex(const QUuid &id) const;
};

}

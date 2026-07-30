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

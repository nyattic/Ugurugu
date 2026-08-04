#pragma once

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QVector>

#include <optional>

namespace wobble
{

struct WawaStroke
{
    QColor color;
    int size = 0;
    int opacity = 0;
    bool airbrush = false;
    qint32 seed = 0;
    int order = 0;
    QVector<QPointF> points;
};

struct WawaFill
{
    QColor color;
    int opacity = 0;
    qint32 seed = 0;
    int order = 0;
    QVector<QPointF> points;
};

struct WawaLayer
{
    QString name;
    bool visible = true;
    QImage baseImage;
    QVector<WawaStroke> paintStrokes;
    QVector<WawaStroke> eraserStrokes;
    QVector<WawaFill> fills;
};

struct WawaSettings
{
    int activeLayer = 0;
    int brushSize = 0;
    int opacity = 0;
    int tolerance = 0;
    int wobbleAmount = 0;
    int wobbleSpeed = 0;
    int wobbleDetail = 0;
    int wobbleMode = 0;
    int wobbleHoldFrames = 0;
    int wobbleStepSpeed = 0;
    int wobbleRandomness = 0;
    bool linkedWiggle = false;
    bool brokenLine = false;
    int breakAmount = 0;
    int breakRange = 0;
    bool wobbleEraser = false;
    QColor brushColor;
    QColor backgroundColor;
    bool bucketUseLayerAlpha = false;
    bool bucketAntialias = false;
};

struct WawaProject
{
    QSize canvasSize;
    WawaSettings settings;
    QVector<WawaLayer> layers;
};

class WawaV10Reader final
{
public:
    static std::optional<WawaProject> read(
        const QByteArray &data, QString *error = nullptr);
};

}

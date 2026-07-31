#include "brush/BrushPreset.hpp"
#include "render/RenderEngine.hpp"

#include <QSet>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <limits>

namespace wobble {

namespace {

Stroke makeStroke(
    StrokeMode mode,
    const QColor &color,
    qreal width,
    quint64 seed,
    const QVector<QPointF> &positions)
{
    Stroke stroke;
    stroke.mode = mode;
    stroke.color = color;
    stroke.width = width;
    stroke.seed = seed;
    for (const QPointF &position : positions) {
        stroke.points.append({position, 1.0});
    }
    return stroke;
}

Document animatedDocument()
{
    Document document = Document::createDefault(QSize(96, 72));
    document.animationFrames = 12;
    document.wobbleAmount = 6.0;
    document.layers.first().strokes.append(makeStroke(
        StrokeMode::Paint,
        QColor(25, 40, 70),
        9.0,
        0x123456789abcdef0ULL,
        {
            QPointF(8.0, 52.0),
            QPointF(24.0, 18.0),
            QPointF(48.0, 46.0),
            QPointF(72.0, 14.0),
            QPointF(88.0, 48.0)
        }));
    return document;
}

}

class RenderEngineTests final : public QObject
{
    Q_OBJECT

private slots:
    void rendersDeterministically()
    {
        const Document document = animatedDocument();
        const QImage first = RenderEngine::render(document, 5);
        const QImage second = RenderEngine::render(document, 5);

        QVERIFY(!first.isNull());
        QCOMPARE(first.size(), document.size);
        QVERIFY(first == second);
    }

    void rendersScaledPreview()
    {
        const Document document = animatedDocument();
        const QSize previewSize(48, 36);
        const QImage preview =
            RenderEngine::renderScaled(document, 5, previewSize);

        QVERIFY(!preview.isNull());
        QCOMPARE(preview.size(), previewSize);
        bool containsPaint = false;
        for (int y = 0; y < preview.height() && !containsPaint; ++y) {
            for (int x = 0; x < preview.width(); ++x) {
                if (preview.pixelColor(x, y) != document.background) {
                    containsPaint = true;
                    break;
                }
            }
        }
        QVERIFY(containsPaint);
        QVERIFY(RenderEngine::renderScaled(document, 0, {}).isNull());
    }

    void loopsAtFrameCount()
    {
        const Document document = animatedDocument();
        const QImage first = RenderEngine::render(document, 0);
        const QImage looped =
            RenderEngine::render(document, document.animationFrames);

        QVERIFY(first == looped);
    }

    void changesAcrossWobbleFrames()
    {
        const Document document = animatedDocument();
        const QImage first = RenderEngine::render(document, 0);
        const QImage second = RenderEngine::render(document, 1);

        QVERIFY(first != second);
    }

    void staysStillWhenWobbleIsZero()
    {
        Document document = animatedDocument();
        document.wobbleAmount = 0.0;
        document.layers.first().strokes.first().width = 40.0;

        const QImage first = RenderEngine::render(document, 0);
        QVERIFY(!first.isNull());
        for (int frame = 1; frame < document.animationFrames; ++frame) {
            QCOMPARE(RenderEngine::render(document, frame), first);
        }
    }

    void staysStillWhenBrushWobbleScaleIsZero()
    {
        Document document = animatedDocument();
        document.layers.first().strokes.first().brush.wobbleScale = 0.0;

        const QImage first = RenderEngine::render(document, 0);
        QVERIFY(!first.isNull());
        for (int frame = 1; frame < document.animationFrames; ++frame) {
            QCOMPARE(RenderEngine::render(document, frame), first);
        }
    }

    void scalesWobbleByBrushWobbleScale()
    {
        Document document = animatedDocument();
        const QImage normal = RenderEngine::render(document, 0);
        document.layers.first().strokes.first().brush.wobbleScale = 2.0;
        const QImage rough = RenderEngine::render(document, 0);

        QVERIFY(!normal.isNull());
        QVERIFY(normal != rough);
    }

    void displacesWobbleLinearlyWithAmount()
    {
        QVector<QPointF> positions;
        for (int index = 0; index <= 20; ++index) {
            positions.append(QPointF(10.0 + index * 9.0, 60.0));
        }
        const Stroke stroke = makeStroke(
            StrokeMode::Paint,
            QColor(10, 20, 30),
            6.0,
            0xfeedbeefULL,
            positions);

        const QPainterPath still =
            RenderEngine::strokePath(stroke, 3, 12, 0.0);
        const QPainterPath single =
            RenderEngine::strokePath(stroke, 3, 12, 2.0);
        const QPainterPath doubled =
            RenderEngine::strokePath(stroke, 3, 12, 4.0);

        QCOMPARE(single.elementCount(), still.elementCount());
        QCOMPARE(doubled.elementCount(), still.elementCount());
        qreal largestOffset = 0.0;
        for (int index = 0; index < still.elementCount(); ++index) {
            const QPointF base(
                still.elementAt(index).x,
                still.elementAt(index).y);
            const QPointF offsetSingle =
                QPointF(single.elementAt(index).x, single.elementAt(index).y)
                - base;
            const QPointF offsetDouble =
                QPointF(doubled.elementAt(index).x, doubled.elementAt(index).y)
                - base;
            largestOffset = std::max(
                largestOffset,
                std::hypot(offsetSingle.x(), offsetSingle.y()));
            QVERIFY(
                std::abs(offsetDouble.x() - offsetSingle.x() * 2.0) < 1e-6);
            QVERIFY(
                std::abs(offsetDouble.y() - offsetSingle.y() * 2.0) < 1e-6);
        }
        QVERIFY(largestOffset > 0.05);
    }

    void rendersCrispPixelEdges()
    {
        Document document = Document::createDefault(QSize(64, 64));
        document.wobbleAmount = 0.0;
        const QColor strokeColor(255, 120, 120);
        document.layers.first().strokes.append(makeStroke(
            StrokeMode::Paint,
            strokeColor,
            3.0,
            45,
            {
                QPointF(7.25, 51.75),
                QPointF(22.5, 11.25),
                QPointF(55.75, 44.5)
            }));

        const QImage image = RenderEngine::render(document, 0);
        QVERIFY(!image.isNull());
        int paintedPixels = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QColor pixel = image.pixelColor(x, y);
                QVERIFY(
                    pixel == document.background
                    || pixel == strokeColor);
                if (pixel == strokeColor) {
                    ++paintedPixels;
                }
            }
        }
        QVERIFY(paintedPixels > 0);
    }

    void rejectsUnallocatableCanvas()
    {
        Document document = Document::createDefault(QSize(
            std::numeric_limits<int>::max(),
            std::numeric_limits<int>::max()));
        QVERIFY(RenderEngine::render(document, 0).isNull());
    }

    void ignoresUnsafeStrokeCoordinates()
    {
        Document document = Document::createDefault(QSize(64, 64));
        Stroke stroke;
        stroke.points = {
            {
                QPointF(
                    std::numeric_limits<qreal>::infinity(),
                    16.0),
                1.0
            }
        };
        document.layers.first().strokes.append(stroke);

        const QImage image = RenderEngine::render(document, 0);
        QVERIFY(!image.isNull());
        QCOMPARE(image.pixelColor(16, 16), QColor(Qt::white));
        QVERIFY(RenderEngine::strokePath(
            stroke,
            0,
            document.animationFrames,
            document.wobbleAmount).isEmpty());
    }

    void keepsErasersLocalToTheirLayer()
    {
        Document document;
        document.size = QSize(80, 64);
        document.background = QColor(10, 20, 30);
        document.animationFrames = 8;
        document.wobbleAmount = 0.0;

        Layer bottom;
        bottom.name = QStringLiteral("Bottom");
        bottom.strokes.append(makeStroke(
            StrokeMode::Paint,
            QColor(20, 180, 80),
            20.0,
            10,
            {QPointF(8.0, 32.0), QPointF(72.0, 32.0)}));

        Layer middle;
        middle.name = QStringLiteral("Middle");
        middle.strokes.append(makeStroke(
            StrokeMode::Paint,
            QColor(220, 40, 50),
            20.0,
            20,
            {QPointF(8.0, 32.0), QPointF(72.0, 32.0)}));
        middle.strokes.append(makeStroke(
            StrokeMode::Erase,
            Qt::black,
            14.0,
            30,
            {QPointF(48.0, 32.0)}));

        Layer top;
        top.name = QStringLiteral("Top");
        top.strokes.append(makeStroke(
            StrokeMode::Erase,
            Qt::black,
            14.0,
            40,
            {QPointF(20.0, 32.0)}));

        document.layers = {bottom, middle, top};
        document.activeLayerId = top.id;

        const QImage image = RenderEngine::render(document, 0);
        QCOMPARE(image.pixelColor(48, 32), QColor(20, 180, 80));
        QCOMPARE(image.pixelColor(20, 32), QColor(220, 40, 50));
    }

    void doesNotFillOpenStrokes()
    {
        Document document = Document::createDefault(QSize(64, 64));
        document.wobbleAmount = 0.0;
        document.layers.first().strokes.append(makeStroke(
            StrokeMode::Paint,
            Qt::black,
            4.0,
            50,
            {
                QPointF(8.0, 52.0),
                QPointF(32.0, 8.0),
                QPointF(56.0, 52.0)
            }));

        const QImage image = RenderEngine::render(document, 0);
        QCOMPARE(image.pixelColor(32, 46), QColor(Qt::white));
        QCOMPARE(image.pixelColor(32, 8), QColor(Qt::black));
    }

    void clipsPaintAndFillToSelectionMasks()
    {
        QImage clipMask(QSize(64, 64), QImage::Format_Grayscale8);
        clipMask.fill(0);
        for (int y = 12; y < 52; ++y) {
            std::fill_n(clipMask.scanLine(y) + 12, 20, 255);
        }

        Document paintDocument =
            Document::createDefault(QSize(64, 64));
        paintDocument.wobbleAmount = 0.0;
        Stroke paint = makeStroke(
            StrokeMode::Paint,
            QColor(220, 30, 40),
            16.0,
            60,
            {QPointF(4.0, 32.0), QPointF(60.0, 32.0)});
        paint.clipMask = clipMask;
        paintDocument.layers.first().strokes.append(paint);
        const QImage painted = RenderEngine::render(paintDocument, 0);
        QCOMPARE(painted.pixelColor(20, 32), paint.color);
        QCOMPARE(painted.pixelColor(45, 32), QColor(Qt::white));

        Document fillDocument =
            Document::createDefault(QSize(64, 64));
        Stroke fill;
        fill.mode = StrokeMode::Fill;
        fill.color = QColor(30, 80, 220);
        fill.points = {{QPointF(20.0, 32.0), 1.0}};
        fill.clipMask = clipMask;
        fillDocument.layers.first().strokes.append(fill);
        const QImage filled = RenderEngine::render(fillDocument, 0);
        QCOMPARE(filled.pixelColor(20, 32), fill.color);
        QCOMPARE(filled.pixelColor(45, 32), QColor(Qt::white));

        const QImage scaled =
            RenderEngine::renderScaled(fillDocument, 0, QSize(32, 32));
        QCOMPARE(scaled.pixelColor(10, 16), fill.color);
        QCOMPARE(scaled.pixelColor(22, 16), QColor(Qt::white));
    }

    void usesTabletPressureForWidth()
    {
        auto renderPressure = [](qreal pressure) {
            Document document = Document::createDefault(QSize(80, 64));
            document.wobbleAmount = 0.0;
            Stroke stroke;
            stroke.color = Qt::black;
            stroke.width = 20.0;
            stroke.points = {
                {QPointF(10.0, 32.0), pressure},
                {QPointF(70.0, 32.0), pressure}
            };
            document.layers.first().strokes.append(stroke);
            return RenderEngine::render(document, 0);
        };

        const QImage light = renderPressure(0.1);
        const QImage heavy = renderPressure(1.0);
        int lightPixels = 0;
        int heavyPixels = 0;
        for (int y = 0; y < light.height(); ++y) {
            for (int x = 0; x < light.width(); ++x) {
                if (light.pixelColor(x, y) != QColor(Qt::white)) {
                    ++lightPixels;
                }
                if (heavy.pixelColor(x, y) != QColor(Qt::white)) {
                    ++heavyPixels;
                }
            }
        }
        QVERIFY(heavyPixels > lightPixels * 2);
    }

    void rendersEveryBuiltInBrushDeterministically()
    {
        QSet<QString> ids;
        for (const BrushPreset &preset : BrushPresetCatalog::builtIns()) {
            QVERIFY2(!ids.contains(preset.id), qPrintable(preset.id));
            ids.insert(preset.id);
            QVERIFY(isValidBrushSettings(preset.settings));

            Document document = Document::createDefault(QSize(128, 96));
            document.wobbleAmount = 0.0;
            Stroke stroke;
            stroke.seed = 0x123456789abcdef0ULL;
            stroke.color = QColor(20, 40, 80);
            stroke.width = std::min(64.0, preset.defaultSize);
            stroke.brush = preset.settings;
            stroke.points = {
                {QPointF(24.0, 48.0), 0.45},
                {QPointF(104.0, 48.0), 1.0}
            };
            document.layers.first().strokes.append(stroke);

            const QImage first = RenderEngine::render(document, 3);
            const QImage second = RenderEngine::render(document, 3);
            QVERIFY2(!first.isNull(), qPrintable(preset.id));
            QVERIFY2(first == second, qPrintable(preset.id));
            QVERIFY2(
                std::any_of(
                    first.constBits(),
                    first.constBits() + first.sizeInBytes(),
                    [](uchar value) { return value != 255; }),
                qPrintable(preset.id));
        }
        QCOMPARE(ids.size(), BrushPresetCatalog::builtIns().size());
    }

    void rendersSoftAirbrushWithPartialAlpha()
    {
        Document document = Document::createDefault(QSize(80, 80));
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;
        Stroke stroke;
        stroke.seed = 77;
        stroke.color = Qt::black;
        stroke.width = 48.0;
        stroke.brush =
            BrushPresetCatalog::find(QStringLiteral("soft-airbrush"))->settings;
        stroke.points = {
            {QPointF(40.0, 40.0), 1.0}
        };
        document.layers.first().strokes.append(stroke);

        const QImage image = RenderEngine::render(document, 0);
        QVERIFY(!image.isNull());
        const int centerAlpha = image.pixelColor(40, 40).alpha();
        const int middleAlpha = image.pixelColor(52, 40).alpha();
        const int edgeAlpha = image.pixelColor(64, 40).alpha();
        QVERIFY(centerAlpha > middleAlpha);
        QVERIFY(middleAlpha > edgeAlpha);
        QVERIFY(centerAlpha > 0 && centerAlpha < 255);
    }

    void onlyAnimatedSprayChangesWithoutWobble()
    {
        auto renderPreset = [](const QString &presetId, int frame) {
            Document document = Document::createDefault(QSize(96, 72));
            document.wobbleAmount = 0.0;
            Stroke stroke;
            stroke.seed = 91;
            stroke.color = Qt::black;
            stroke.width = 44.0;
            stroke.brush = BrushPresetCatalog::find(presetId)->settings;
            stroke.points = {
                {QPointF(18.0, 36.0), 1.0},
                {QPointF(78.0, 36.0), 1.0}
            };
            document.layers.first().strokes.append(stroke);
            return RenderEngine::render(document, frame);
        };

        QCOMPARE(
            renderPreset(QStringLiteral("pixel-spray"), 0),
            renderPreset(QStringLiteral("pixel-spray"), 1));
        QVERIFY(
            renderPreset(QStringLiteral("wobble-spray"), 0)
            != renderPreset(QStringLiteral("wobble-spray"), 1));
    }

    void handlesDotsAndDuplicatePoints()
    {
        Document document = Document::createDefault(QSize(64, 64));
        document.wobbleAmount = 5.0;

        const Stroke dot = makeStroke(
            StrokeMode::Paint,
            Qt::black,
            8.0,
            100,
            {QPointF(16.0, 16.0)});
        const Stroke duplicates = makeStroke(
            StrokeMode::Paint,
            QColor(200, 20, 40),
            8.0,
            200,
            {
                QPointF(40.0, 40.0),
                QPointF(40.0, 40.0),
                QPointF(40.0, 40.0)
            });
        document.layers.first().strokes = {dot, duplicates};

        const QPainterPath dotPath =
            RenderEngine::strokePath(dot, 3, document.animationFrames, 5.0);
        const QPainterPath duplicatePath =
            RenderEngine::strokePath(duplicates, 3, document.animationFrames, 5.0);
        QVERIFY(dotPath.elementCount() > 0);
        QVERIFY(duplicatePath.elementCount() > 0);
        const QPainterPath::Element dotElement = dotPath.elementAt(0);
        const QPainterPath::Element duplicateElement = duplicatePath.elementAt(0);
        QVERIFY(std::isfinite(dotElement.x));
        QVERIFY(std::isfinite(dotElement.y));
        QVERIFY(std::isfinite(duplicateElement.x));
        QVERIFY(std::isfinite(duplicateElement.y));

        const QImage image = RenderEngine::render(document, 3);
        QVERIFY(!image.isNull());
        QCOMPARE(image.size(), document.size);
        bool containsPaint = false;
        for (int y = 0; y < image.height() && !containsPaint; ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (image.pixelColor(x, y) != document.background) {
                    containsPaint = true;
                    break;
                }
            }
        }
        QVERIFY(containsPaint);
    }
};

int runRenderEngineTests(int argc, char **argv)
{
    RenderEngineTests tests;
    return QTest::qExec(&tests, argc, argv);
}

}

#include "RenderEngineTests.moc"

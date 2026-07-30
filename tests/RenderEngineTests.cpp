#include "render/RenderEngine.hpp"

#include <QtTest>

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

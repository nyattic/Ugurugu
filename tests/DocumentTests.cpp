#include "brush/BrushPreset.hpp"
#include "document/Document.hpp"
#include "document/DocumentController.hpp"
#include "document/DocumentLimits.hpp"
#include "io/DocumentSerializer.hpp"
#include "ui/Theme.hpp"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <limits>

namespace wobble {

int runRenderEngineTests(int argc, char **argv);
int runGifWriterTests(int argc, char **argv);
int runMaskRegressionTests(int argc, char **argv);
int runReleaseNotesTests(int argc, char **argv);
int runUiTests(int argc, char **argv);

QByteArray pointArray(int count)
{
    QByteArray points = QByteArrayLiteral("[0,0],").repeated(count);
    if (!points.isEmpty()) {
        points.chop(1);
    }
    points.prepend('[');
    points.append(']');
    return points;
}

class DocumentTests final : public QObject
{
    Q_OBJECT

private slots:
    void createsDefaultDocument()
    {
        const QSize size(640, 360);
        const Document document = Document::createDefault(size);

        QCOMPARE(document.size, size);
        QCOMPARE(document.background, QColor(Qt::white));
        QCOMPARE(document.animationFrames, 30);
        QCOMPARE(document.framesPerSecond, 25.0);
        QCOMPARE(document.wobbleAmount, 1.6);
        QCOMPARE(document.layers.size(), 1);
        QCOMPARE(document.layers.first().name, QStringLiteral("Layer 1"));
        QVERIFY(document.layers.first().visible);
        QCOMPARE(document.layers.first().opacity, 1.0);
        QVERIFY(document.layers.first().strokes.isEmpty());
        QCOMPARE(document.activeLayerId, document.layers.first().id);
        QCOMPARE(document.layerIndex(document.activeLayerId), 0);
        QVERIFY(document.layer(document.activeLayerId) != nullptr);
        QVERIFY(document.layer(QUuid::createUuid()) == nullptr);
    }

    void undoesAndRedoesStrokeWithCleanState()
    {
        DocumentController controller;
        controller.newDocument(QSize(320, 240));
        const QUuid layerId = controller.document().activeLayerId;

        QVERIFY(!controller.isModified());
        QVERIFY(controller.undoStack()->isClean());

        Stroke stroke;
        stroke.id = QUuid(QStringLiteral("{11111111-1111-1111-1111-111111111111}"));
        stroke.seed = 42;
        stroke.color = QColor(20, 40, 60);
        stroke.width = 8.0;
        stroke.points = {
            {QPointF(10.0, 12.0), 0.5},
            {QPointF(40.0, 52.0), 1.0}
        };

        controller.addStroke(layerId, stroke);
        QCOMPARE(controller.document().layer(layerId)->strokes.size(), 1);
        QCOMPARE(controller.document().layer(layerId)->strokes.first().id, stroke.id);
        QVERIFY(controller.isModified());
        QVERIFY(controller.undoStack()->canUndo());

        controller.undoStack()->undo();
        QVERIFY(controller.document().layer(layerId)->strokes.isEmpty());
        QVERIFY(!controller.isModified());
        QVERIFY(controller.undoStack()->isClean());

        controller.undoStack()->redo();
        QCOMPARE(controller.document().layer(layerId)->strokes.size(), 1);
        QVERIFY(controller.isModified());

        controller.markSaved();
        QVERIFY(!controller.isModified());
        QVERIFY(controller.undoStack()->isClean());

        controller.undoStack()->undo();
        QVERIFY(controller.document().layer(layerId)->strokes.isEmpty());
        QVERIFY(controller.isModified());

        controller.undoStack()->redo();
        QCOMPARE(controller.document().layer(layerId)->strokes.size(), 1);
        QVERIFY(!controller.isModified());
        QVERIFY(controller.undoStack()->isClean());
    }

    void transientCommandsDoNotModifyDocument()
    {
        DocumentController controller;
        controller.newDocument(QSize(320, 240));
        int transientState = 0;

        controller.pushTransientCommand(
            QStringLiteral("Transient"),
            [&transientState]() {
                transientState = 1;
            },
            [&transientState]() {
                transientState = 0;
            });
        QCOMPARE(transientState, 1);
        QVERIFY(!controller.isModified());

        controller.undoStack()->undo();
        QCOMPARE(transientState, 0);
        QVERIFY(!controller.isModified());

        controller.undoStack()->redo();
        QCOMPARE(transientState, 1);
        QVERIFY(!controller.isModified());

        const QUuid layerId = controller.document().activeLayerId;
        Stroke stroke;
        stroke.points = {
            {QPointF(10.0, 12.0), 1.0},
            {QPointF(40.0, 52.0), 1.0}
        };
        controller.addStroke(layerId, stroke);
        QVERIFY(controller.isModified());
        controller.markSaved();
        QVERIFY(!controller.isModified());

        controller.pushTransientCommand(
            QStringLiteral("Transient 2"),
            [&transientState]() {
                transientState = 2;
            },
            [&transientState]() {
                transientState = 1;
            });
        QCOMPARE(transientState, 2);
        QVERIFY(!controller.isModified());

        controller.undoStack()->undo();
        QCOMPARE(transientState, 1);
        QVERIFY(!controller.isModified());
        controller.undoStack()->undo();
        QVERIFY(controller.isModified());
        controller.undoStack()->redo();
        QVERIFY(!controller.isModified());
    }

    void limitsUndoHistory()
    {
        DocumentController controller;
        int value = 0;
        for (int index = 1; index <= 80; ++index) {
            controller.pushTransientCommand(
                QStringLiteral("Transient"),
                [&value, index]() {
                    value = index;
                },
                [&value, index]() {
                    value = index - 1;
                });
        }

        QCOMPARE(value, 80);
        QCOMPARE(controller.undoStack()->undoLimit(), 64);
        QCOMPARE(controller.undoStack()->count(), 64);
    }

    void marksRecoveredDocumentsAsModified()
    {
        DocumentController controller;
        Document recovered = Document::createDefault(QSize(480, 320));
        recovered.wobbleAmount = 4.0;

        controller.loadRecoveredDocument(recovered);

        QCOMPARE(controller.document().size, recovered.size);
        QCOMPARE(controller.document().wobbleAmount, recovered.wobbleAmount);
        QVERIFY(controller.isModified());
        controller.markSaved();
        QVERIFY(!controller.isModified());
    }

    void undoesLayerAdditionAndRemoval()
    {
        DocumentController controller;
        controller.newDocument(QSize(256, 256));
        const QUuid originalId = controller.document().activeLayerId;

        controller.addLayer();
        QCOMPARE(controller.document().layers.size(), 2);
        const QUuid addedId = controller.document().activeLayerId;
        QVERIFY(addedId != originalId);
        QCOMPARE(controller.document().layers.constLast().id, addedId);

        controller.undoStack()->undo();
        QCOMPARE(controller.document().layers.size(), 1);
        QCOMPARE(controller.document().activeLayerId, originalId);
        QVERIFY(controller.document().layer(addedId) == nullptr);

        controller.undoStack()->redo();
        QCOMPARE(controller.document().layers.size(), 2);
        QCOMPARE(controller.document().activeLayerId, addedId);

        controller.removeLayer(addedId);
        QCOMPARE(controller.document().layers.size(), 1);
        QCOMPARE(controller.document().activeLayerId, originalId);
        QVERIFY(controller.document().layer(addedId) == nullptr);

        controller.undoStack()->undo();
        QCOMPARE(controller.document().layers.size(), 2);
        QCOMPARE(controller.document().activeLayerId, addedId);
        QCOMPARE(controller.document().layers.constLast().id, addedId);
    }

    void supportsDocumentsWithoutLayers()
    {
        DocumentController controller;
        controller.newDocument(QSize(256, 256));
        const Layer originalLayer = controller.document().layers.first();

        controller.removeLayer(originalLayer.id);
        QVERIFY(controller.document().layers.isEmpty());
        QVERIFY(controller.document().activeLayerId.isNull());
        QVERIFY(controller.isModified());

        QString error;
        const QByteArray serialized =
            DocumentSerializer::toJson(controller.document());
        QVERIFY(!serialized.isEmpty());
        const QJsonObject root =
            QJsonDocument::fromJson(serialized).object();
        QVERIFY(root.value(QStringLiteral("activeLayerId")).isNull());
        const std::optional<Document> loaded =
            DocumentSerializer::fromJson(serialized, &error);
        QVERIFY2(loaded.has_value(), qPrintable(error));
        QVERIFY(loaded->layers.isEmpty());
        QVERIFY(loaded->activeLayerId.isNull());

        DocumentController loadedController;
        loadedController.loadDocument(*loaded);
        QVERIFY(loadedController.document().layers.isEmpty());
        QVERIFY(loadedController.document().activeLayerId.isNull());

        controller.undoStack()->undo();
        QCOMPARE(controller.document().layers.size(), 1);
        QCOMPARE(controller.document().layers.first().id, originalLayer.id);
        QCOMPARE(controller.document().activeLayerId, originalLayer.id);

        controller.undoStack()->redo();
        QVERIFY(controller.document().layers.isEmpty());
        QVERIFY(controller.document().activeLayerId.isNull());

        controller.addLayer();
        QCOMPARE(controller.document().layers.size(), 1);
        const QUuid addedId = controller.document().activeLayerId;
        QVERIFY(!addedId.isNull());
        QCOMPARE(
            controller.document().layers.first().name,
            QStringLiteral("Layer 1"));

        controller.undoStack()->undo();
        QVERIFY(controller.document().layers.isEmpty());
        QVERIFY(controller.document().activeLayerId.isNull());

        controller.undoStack()->redo();
        QCOMPARE(controller.document().layers.size(), 1);
        QCOMPARE(controller.document().activeLayerId, addedId);
    }

    void activatesAdjacentLayerWhenRemovingFirstLayer()
    {
        DocumentController controller;
        controller.newDocument(QSize(64, 64));
        controller.addLayer();
        controller.addLayer();
        QCOMPARE(controller.document().layers.size(), 3);
        const QUuid first = controller.document().layers[0].id;
        const QUuid second = controller.document().layers[1].id;
        const QUuid third = controller.document().layers[2].id;

        controller.setActiveLayer(first);
        controller.removeLayer(first);
        QCOMPARE(controller.document().layers.size(), 2);
        QCOMPARE(controller.document().activeLayerId, second);

        controller.undoStack()->undo();
        QCOMPARE(controller.document().layers.size(), 3);
        QCOMPARE(controller.document().activeLayerId, first);

        controller.setActiveLayer(third);
        controller.removeLayer(second);
        QCOMPARE(controller.document().layers.size(), 2);
        QCOMPARE(controller.document().activeLayerId, third);
    }

    void roundTripsJson()
    {
        Document source = Document::createDefault(QSize(321, 123));
        source.background = QColor(12, 34, 56, 78);
        source.animationFrames = 17;
        source.framesPerSecond = 12.5;
        source.wobbleAmount = 4.25;

        Layer &firstLayer = source.layers.first();
        firstLayer.id = QUuid(QStringLiteral("{22222222-2222-2222-2222-222222222222}"));
        firstLayer.name = QStringLiteral("Ink");
        firstLayer.opacity = 0.625;

        Stroke paint;
        paint.id = QUuid(QStringLiteral("{33333333-3333-3333-3333-333333333333}"));
        paint.seed = 9876543210123456789ULL;
        paint.mode = StrokeMode::Paint;
        paint.color = QColor(1, 2, 3, 4);
        paint.width = 9.75;
        paint.brush =
            BrushPresetCatalog::find(QStringLiteral("soft-airbrush"))->settings;
        paint.brush.wobbleScale = 0.35;
        paint.brush.antialiasing = true;
        paint.points = {
            {QPointF(1.25, 2.5), 0.2},
            {QPointF(30.0, 40.0), 0.9}
        };
        paint.clipMask = QImage(source.size, QImage::Format_Grayscale8);
        paint.clipMask.fill(0);
        for (int y = 10; y < 40; ++y) {
            std::fill_n(paint.clipMask.scanLine(y) + 20, 60, 255);
        }
        firstLayer.strokes.append(paint);

        Layer secondLayer;
        secondLayer.id = QUuid(QStringLiteral("{44444444-4444-4444-4444-444444444444}"));
        secondLayer.name = QStringLiteral("Erase");
        secondLayer.visible = false;
        secondLayer.opacity = 0.4;

        Stroke erase;
        erase.id = QUuid(QStringLiteral("{55555555-5555-5555-5555-555555555555}"));
        erase.seed = 123456789;
        erase.mode = StrokeMode::Erase;
        erase.color = QColor(200, 150, 100, 50);
        erase.width = 13.5;
        erase.points = {
            {QPointF(6.0, 7.0), 1.0}
        };
        secondLayer.strokes.append(erase);
        source.layers.append(secondLayer);
        source.activeLayerId = secondLayer.id;

        QString error;
        const std::optional<Document> loaded =
            DocumentSerializer::fromJson(DocumentSerializer::toJson(source), &error);
        QVERIFY2(loaded.has_value(), qPrintable(error));
        QCOMPARE(loaded->size, source.size);
        QCOMPARE(loaded->background, source.background);
        QCOMPARE(loaded->animationFrames, source.animationFrames);
        QCOMPARE(loaded->framesPerSecond, source.framesPerSecond);
        QCOMPARE(loaded->wobbleAmount, source.wobbleAmount);
        QCOMPARE(loaded->activeLayerId, source.activeLayerId);
        QCOMPARE(loaded->layers.size(), source.layers.size());

        for (int layerIndex = 0; layerIndex < source.layers.size(); ++layerIndex) {
            const Layer &actualLayer = loaded->layers[layerIndex];
            const Layer &expectedLayer = source.layers[layerIndex];
            QCOMPARE(actualLayer.id, expectedLayer.id);
            QCOMPARE(actualLayer.name, expectedLayer.name);
            QCOMPARE(actualLayer.visible, expectedLayer.visible);
            QCOMPARE(actualLayer.opacity, expectedLayer.opacity);
            QCOMPARE(actualLayer.strokes.size(), expectedLayer.strokes.size());

            for (int strokeIndex = 0;
                 strokeIndex < expectedLayer.strokes.size();
                 ++strokeIndex) {
                const Stroke &actualStroke = actualLayer.strokes[strokeIndex];
                const Stroke &expectedStroke = expectedLayer.strokes[strokeIndex];
                QCOMPARE(actualStroke.id, expectedStroke.id);
                QCOMPARE(actualStroke.seed, expectedStroke.seed);
                QCOMPARE(actualStroke.mode, expectedStroke.mode);
                QCOMPARE(actualStroke.color, expectedStroke.color);
                QCOMPARE(actualStroke.width, expectedStroke.width);
                QVERIFY(actualStroke.brush == expectedStroke.brush);
                QCOMPARE(actualStroke.clipMask, expectedStroke.clipMask);
                QCOMPARE(actualStroke.points.size(), expectedStroke.points.size());

                for (int pointIndex = 0;
                     pointIndex < expectedStroke.points.size();
                     ++pointIndex) {
                    QCOMPARE(
                        actualStroke.points[pointIndex].position,
                        expectedStroke.points[pointIndex].position);
                    QCOMPARE(
                        actualStroke.points[pointIndex].pressure,
                        expectedStroke.points[pointIndex].pressure);
                }
            }
        }
    }

    void loadsLegacyBrushAsInk()
    {
        const QByteArray json = QByteArrayLiteral(
            R"({"schemaVersion":1,"algorithmVersion":1,"canvas":{"width":20,"height":20,"background":"#ffffffff"},"animation":{"frames":2,"fps":25,"wobble":1},"activeLayerId":"11111111-1111-1111-1111-111111111111","layers":[{"id":"11111111-1111-1111-1111-111111111111","name":"Layer","visible":true,"opacity":1,"strokes":[{"id":"22222222-2222-2222-2222-222222222222","seed":"1","mode":"paint","color":"#ff000000","width":6,"points":[[5,5,0.5],[15,15,1]]}]}]})");

        QString error;
        const std::optional<Document> document =
            DocumentSerializer::fromJson(json, &error);
        QVERIFY2(document.has_value(), qPrintable(error));
        const Stroke &stroke = document->layers.first().strokes.first();
        QCOMPARE(stroke.brush.engine, BrushEngine::Line);
        QCOMPARE(stroke.brush.tipShape, BrushTipShape::Round);
        QCOMPARE(stroke.brush.sizeDynamics, 0.8);
        QCOMPARE(stroke.brush.wobbleScale, 1.0);
        QVERIFY(isValidBrushSettings(stroke.brush));
    }

    void resizesCanvasUndoably()
    {
        Document document = Document::createDefault(QSize(100, 50));
        Stroke stroke;
        stroke.width = 10.0;
        stroke.points = {
            {QPointF(10.0, 10.0), 0.5},
            {QPointF(90.0, 40.0), 1.0}
        };
        stroke.clipMask = QImage(
            document.size,
            QImage::Format_Grayscale8);
        stroke.clipMask.fill(255);
        document.layers.first().strokes.append(stroke);

        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.resizeCanvas(QSize(200, 100)));
        QCOMPARE(controller.document().size, QSize(200, 100));
        const Stroke &resized =
            controller.document().layers.first().strokes.first();
        QCOMPARE(resized.points.first().position, QPointF(20.0, 20.0));
        QCOMPARE(resized.points.last().position, QPointF(180.0, 80.0));
        QCOMPARE(resized.width, 20.0);
        QCOMPARE(resized.clipMask.size(), QSize(200, 100));

        controller.undoStack()->undo();
        QCOMPARE(controller.document().size, QSize(100, 50));
        const Stroke &restored =
            controller.document().layers.first().strokes.first();
        QCOMPARE(restored.points.first().position, QPointF(10.0, 10.0));
        QCOMPARE(restored.points.last().position, QPointF(90.0, 40.0));
        QCOMPARE(restored.width, 10.0);
        QCOMPARE(restored.clipMask, stroke.clipMask);

        controller.undoStack()->redo();
        QCOMPARE(controller.document().size, QSize(200, 100));
        QVERIFY(!controller.resizeCanvas(QSize(0, 100)));
    }

    void clampsEdgePointsWhenResizingCanvas()
    {
        Document document = Document::createDefault(QSize(100, 100));
        Stroke stroke;
        stroke.width = 6.0;
        stroke.points = {
            {QPointF(50.0, 50.0), 1.0},
            {QPointF(100.0, 100.0), 1.0}
        };
        document.layers.first().strokes.append(stroke);

        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.resizeCanvas(QSize(109, 109)));

        const QSize size = controller.document().size;
        for (const StrokePoint &point :
            controller.document().layers.first().strokes.first().points) {
            QVERIFY(point.position.x() >= 0.0);
            QVERIFY(point.position.y() >= 0.0);
            QVERIFY(point.position.x() <= size.width());
            QVERIFY(point.position.y() <= size.height());
        }

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString filePath = directory.filePath(
            QStringLiteral("resized.wagle"));
        QString error;
        QVERIFY2(
            DocumentSerializer::save(
                filePath,
                controller.document(),
                &error),
            qPrintable(error));
    }

    void transformsAndDuplicatesStrokesUndoably()
    {
        Document document = Document::createDefault(QSize(100, 100));
        Stroke stroke;
        stroke.width = 6.0;
        stroke.points = {
            {QPointF(40.0, 50.0), 1.0},
            {QPointF(60.0, 50.0), 1.0}
        };
        const QUuid strokeId = stroke.id;
        const QUuid layerId = document.activeLayerId;
        document.layers.first().strokes.append(stroke);

        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.scaleStrokes(
            layerId,
            {strokeId},
            QPointF(50.0, 50.0),
            2.0));
        const Stroke &scaled =
            controller.document().layers.first().strokes.first();
        QCOMPARE(scaled.points.first().position, QPointF(30.0, 50.0));
        QCOMPARE(scaled.points.last().position, QPointF(70.0, 50.0));
        QCOMPARE(scaled.width, 12.0);

        controller.undoStack()->undo();
        QCOMPARE(
            controller.document().layers.first().strokes.first().points,
            stroke.points);

        QVERIFY(controller.rotateStrokes(
            layerId,
            {strokeId},
            QPointF(50.0, 50.0),
            90.0));
        const Stroke &rotated =
            controller.document().layers.first().strokes.first();
        QVERIFY(qAbs(rotated.points.first().position.x() - 50.0) < 0.0001);
        QVERIFY(qAbs(rotated.points.last().position.x() - 50.0) < 0.0001);
        QVERIFY(qAbs(
            rotated.points.first().position.y()
                - rotated.points.last().position.y()) > 19.999);

        controller.undoStack()->undo();
        QVERIFY(controller.duplicateStrokes(
            layerId,
            {strokeId},
            QPointF(10.0, 5.0)));
        QCOMPARE(controller.document().layers.first().strokes.size(), 2);
        const Stroke &copy =
            controller.document().layers.first().strokes.last();
        QVERIFY(copy.id != strokeId);
        QCOMPARE(copy.points.first().position, QPointF(50.0, 55.0));
        QCOMPARE(copy.points.last().position, QPointF(70.0, 55.0));

        controller.undoStack()->undo();
        QCOMPARE(controller.document().layers.first().strokes.size(), 1);
        controller.undoStack()->redo();
        QCOMPARE(controller.document().layers.first().strokes.size(), 2);

        QVERIFY(!controller.scaleStrokes(
            layerId,
            {strokeId},
            QPointF(50.0, 50.0),
            6.0));
        QCOMPARE(controller.document().layers.first().strokes.size(), 2);
    }

    void rotatesOnlyTheSelectedPartOfAnIntersectingStroke()
    {
        Document document = Document::createDefault(QSize(100, 100));
        Stroke stroke;
        stroke.width = 4.0;
        stroke.points = {
            {QPointF(10.0, 50.0), 1.0},
            {QPointF(90.0, 50.0), 1.0}
        };
        const QUuid strokeId = stroke.id;
        const QUuid layerId = document.activeLayerId;
        document.layers.first().strokes.append(stroke);

        QImage selection(document.size, QImage::Format_Grayscale8);
        selection.fill(0);
        for (int y = 40; y <= 60; ++y) {
            uchar *line = selection.scanLine(y);
            std::fill(line + 40, line + 61, 255);
        }

        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.rotateStrokes(
            layerId,
            {strokeId},
            QPointF(50.0, 50.0),
            90.0,
            selection));

        const Layer &rotatedLayer =
            controller.document().layers.first();
        QCOMPARE(rotatedLayer.strokes.size(), 2);
        const auto selected = std::find_if(
            rotatedLayer.strokes.cbegin(),
            rotatedLayer.strokes.cend(),
            [strokeId](const Stroke &candidate) {
                return candidate.id == strokeId;
            });
        QVERIFY(selected != rotatedLayer.strokes.cend());
        const auto remainder = std::find_if(
            rotatedLayer.strokes.cbegin(),
            rotatedLayer.strokes.cend(),
            [strokeId](const Stroke &candidate) {
                return candidate.id != strokeId;
            });
        QVERIFY(remainder != rotatedLayer.strokes.cend());

        QCOMPARE(remainder->points, stroke.points);
        QCOMPARE(remainder->clipMask.constScanLine(50)[20], 255);
        QCOMPARE(remainder->clipMask.constScanLine(50)[50], 0);
        QCOMPARE(selected->clipMask.constScanLine(50)[50], 255);
        QCOMPARE(selected->clipMask.constScanLine(50)[20], 0);
        QVERIFY(qAbs(selected->points.first().position.x() - 50.0) < 0.0001);
        QVERIFY(qAbs(selected->points.last().position.x() - 50.0) < 0.0001);

        controller.undoStack()->undo();
        const Layer &restoredLayer =
            controller.document().layers.first();
        QCOMPARE(restoredLayer.strokes.size(), 1);
        QCOMPARE(restoredLayer.strokes.first().id, strokeId);
        QCOMPARE(restoredLayer.strokes.first().points, stroke.points);
        QVERIFY(restoredLayer.strokes.first().clipMask.isNull());

        controller.undoStack()->redo();
        QCOMPARE(controller.document().layers.first().strokes.size(), 2);
    }

    void movesOnlyTheSelectedPartOfAnIntersectingStroke()
    {
        Document document = Document::createDefault(QSize(100, 100));
        Stroke stroke;
        stroke.width = 4.0;
        stroke.points = {
            {QPointF(10.0, 50.0), 1.0},
            {QPointF(90.0, 50.0), 1.0}
        };
        const QUuid strokeId = stroke.id;
        const QUuid layerId = document.activeLayerId;
        document.layers.first().strokes.append(stroke);

        QImage selection(document.size, QImage::Format_Grayscale8);
        selection.fill(0);
        for (int y = 40; y <= 60; ++y) {
            uchar *line = selection.scanLine(y);
            std::fill(line + 40, line + 61, 255);
        }

        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.moveStrokes(
            layerId,
            {strokeId},
            QPointF(0.0, 20.0),
            selection));

        const Layer &movedLayer = controller.document().layers.first();
        QCOMPARE(movedLayer.strokes.size(), 2);
        const auto moved = std::find_if(
            movedLayer.strokes.cbegin(),
            movedLayer.strokes.cend(),
            [strokeId](const Stroke &candidate) {
                return candidate.id == strokeId;
            });
        QVERIFY(moved != movedLayer.strokes.cend());
        const auto remainder = std::find_if(
            movedLayer.strokes.cbegin(),
            movedLayer.strokes.cend(),
            [strokeId](const Stroke &candidate) {
                return candidate.id != strokeId;
            });
        QVERIFY(remainder != movedLayer.strokes.cend());

        QCOMPARE(remainder->points, stroke.points);
        QCOMPARE(remainder->clipMask.constScanLine(50)[20], 255);
        QCOMPARE(remainder->clipMask.constScanLine(50)[50], 0);
        QCOMPARE(moved->points.first().position, QPointF(10.0, 70.0));
        QCOMPARE(moved->points.last().position, QPointF(90.0, 70.0));
        QCOMPARE(moved->clipMask.constScanLine(70)[50], 255);
        QCOMPARE(moved->clipMask.constScanLine(50)[50], 0);

        controller.undoStack()->undo();
        const Layer &restoredLayer =
            controller.document().layers.first();
        QCOMPARE(restoredLayer.strokes.size(), 1);
        QCOMPARE(restoredLayer.strokes.first().points, stroke.points);
        QVERIFY(restoredLayer.strokes.first().clipMask.isNull());
    }

    void movesClipMaskAlongWithFullyContainedStroke()
    {
        Document document = Document::createDefault(QSize(100, 100));
        Stroke stroke;
        stroke.width = 4.0;
        stroke.points = {
            {QPointF(45.0, 50.0), 1.0},
            {QPointF(55.0, 50.0), 1.0}
        };
        stroke.clipMask =
            QImage(document.size, QImage::Format_Grayscale8);
        stroke.clipMask.fill(0);
        for (int y = 40; y <= 60; ++y) {
            uchar *line = stroke.clipMask.scanLine(y);
            std::fill(line + 40, line + 61, 255);
        }
        const QUuid strokeId = stroke.id;
        const QUuid layerId = document.activeLayerId;
        document.layers.first().strokes.append(stroke);

        QImage selection(document.size, QImage::Format_Grayscale8);
        selection.fill(0);
        for (int y = 30; y <= 70; ++y) {
            uchar *line = selection.scanLine(y);
            std::fill(line + 30, line + 71, 255);
        }

        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.moveStrokes(
            layerId,
            {strokeId},
            QPointF(20.0, 0.0),
            selection));

        const Layer &movedLayer = controller.document().layers.first();
        QCOMPARE(movedLayer.strokes.size(), 1);
        const Stroke &moved = movedLayer.strokes.first();
        QCOMPARE(moved.points.first().position, QPointF(65.0, 50.0));
        QCOMPARE(moved.points.last().position, QPointF(75.0, 50.0));
        QCOMPARE(moved.clipMask.constScanLine(50)[70], 255);
        QCOMPARE(moved.clipMask.constScanLine(50)[50], 0);
    }

    void loadsBundledExample()
    {
        QString error;
        const QString path =
            QStringLiteral(WOBBLEPAINT_SOURCE_DIR)
            + QStringLiteral("/examples/Wave.wagle");
        const std::optional<Document> document =
            DocumentSerializer::load(path, &error);
        QVERIFY2(document.has_value(), qPrintable(error));
        QCOMPARE(document->size, QSize(640, 400));
        QCOMPARE(document->animationFrames, 30);
        QCOMPARE(document->framesPerSecond, 25.0);
        QCOMPARE(document->layers.size(), 1);
        QCOMPARE(document->layers.first().strokes.size(), 2);
    }

    void savesAndLoadsProjectFile()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path =
            directory.filePath(QStringLiteral("project.wagle"));
        Document source = Document::createDefault(QSize(222, 111));
        source.wobbleAmount = 3.4;

        QString error;
        QVERIFY2(
            DocumentSerializer::save(path, source, &error),
            qPrintable(error));
        const std::optional<Document> loaded =
            DocumentSerializer::load(path, &error);
        QVERIFY2(loaded.has_value(), qPrintable(error));
        QCOMPARE(loaded->size, source.size);
        QCOMPARE(loaded->wobbleAmount, source.wobbleAmount);
        QCOMPARE(loaded->activeLayerId, source.activeLayerId);
    }

    void savesMaximumPointBudget()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path =
            directory.filePath(QStringLiteral("maximum-points.wagle"));
        Document source = Document::createDefault(QSize(4096, 4096));
        const StrokePoint point {
            QPointF(1234.56789012345, 3987.65432109876),
            0.543210987654321
        };

        Stroke first;
        first.points.fill(
            point,
            DocumentLimits::maximumPointsPerStroke);
        Stroke second;
        second.points.fill(
            point,
            DocumentLimits::maximumTotalPoints
                - DocumentLimits::maximumPointsPerStroke);
        source.layers.first().strokes.append(std::move(first));
        source.layers.first().strokes.append(std::move(second));

        QString error;
        QVERIFY2(
            DocumentSerializer::save(path, source, &error),
            qPrintable(error));
        QVERIFY(
            QFileInfo(path).size()
            <= DocumentLimits::maximumProjectBytes);

        const std::optional<Document> loaded =
            DocumentSerializer::load(path, &error);
        QVERIFY2(loaded.has_value(), qPrintable(error));
        QCOMPARE(loaded->layers.first().strokes.size(), 2);
        QCOMPARE(
            loaded->layers.first().strokes.first().points.size()
                + loaded->layers.first().strokes.last().points.size(),
            DocumentLimits::maximumTotalPoints);
    }

    void rejectsUnsafeDocumentsBeforeSaving()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path =
            directory.filePath(QStringLiteral("unsafe.wagle"));

        Document document = Document::createDefault(QSize(100, 100));
        Stroke stroke;
        stroke.points = {
            {QPointF(101.0, 50.0), 0.5}
        };
        document.layers.first().strokes.append(stroke);

        QString error;
        QVERIFY(!DocumentSerializer::save(path, document, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!QFile::exists(path));

        document.layers.first().strokes.first().points.first().position =
            QPointF(50.0, 50.0);
        document.framesPerSecond =
            std::numeric_limits<qreal>::quiet_NaN();
        error.clear();
        QVERIFY(!DocumentSerializer::save(path, document, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!QFile::exists(path));
    }

    void rejectsOversizedProjectFile()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path =
            directory.filePath(QStringLiteral("oversized.wagle"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.resize(DocumentLimits::maximumProjectBytes + 1));
        file.close();

        QString error;
        const std::optional<Document> document =
            DocumentSerializer::load(path, &error);
        QVERIFY(!document.has_value());
        QVERIFY(!error.isEmpty());
    }

    void rejectsExcessivePointCollection()
    {
        const QByteArray json =
            QByteArrayLiteral(
                R"({"schemaVersion":1,"algorithmVersion":1,"canvas":{"width":100,"height":100,"background":"#ffffffff"},"animation":{"frames":30,"fps":25,"wobble":1.6},"activeLayerId":"11111111-1111-1111-1111-111111111111","layers":[{"id":"11111111-1111-1111-1111-111111111111","name":"Layer","visible":true,"opacity":1,"strokes":[{"id":"22222222-2222-2222-2222-222222222222","seed":"1","mode":"paint","color":"#ff000000","width":6,"points":)")
            + pointArray(DocumentLimits::maximumPointsPerStroke)
            + QByteArrayLiteral(
                R"(},{"id":"33333333-3333-3333-3333-333333333333","seed":"2","mode":"paint","color":"#ff000000","width":6,"points":)")
            + pointArray(
                DocumentLimits::maximumTotalPoints
                - DocumentLimits::maximumPointsPerStroke
                + 1)
            + QByteArrayLiteral(R"(}]}]})");

        QString error;
        const std::optional<Document> document =
            DocumentSerializer::fromJson(json, &error);
        QVERIFY(!document.has_value());
        QVERIFY(!error.isEmpty());
    }

    void enforcesEditingLimits()
    {
        DocumentController controller;
        controller.newDocument(QSize(
            0,
            DocumentLimits::maximumCanvasEdge + 100));
        QCOMPARE(
            controller.document().size,
            QSize(
                DocumentLimits::minimumCanvasEdge,
                DocumentLimits::maximumCanvasEdge));

        controller.newDocument(QSize(100, 100));
        const QUuid layerId = controller.document().activeLayerId;
        Stroke stroke;
        stroke.points.fill(
            {QPointF(50.0, 50.0), 0.5},
            DocumentLimits::maximumPointsPerStroke + 1);
        controller.addStroke(layerId, std::move(stroke));
        QCOMPARE(
            controller.document().layer(layerId)->strokes.first().points.size(),
            DocumentLimits::maximumPointsPerStroke);

        Stroke invalid;
        invalid.points = {
            {QPointF(150.0, 50.0), 0.5}
        };
        controller.addStroke(layerId, std::move(invalid));
        QCOMPARE(controller.document().layer(layerId)->strokes.size(), 1);

        const int undoCount = controller.undoStack()->count();
        controller.setFramesPerSecond(
            std::numeric_limits<qreal>::quiet_NaN());
        QCOMPARE(controller.undoStack()->count(), undoCount);

        Document full = Document::createDefault(QSize(100, 100));
        while (full.layers.size() < DocumentLimits::maximumLayers) {
            Layer layer;
            layer.name = QStringLiteral("Layer %1").arg(full.layers.size() + 1);
            full.layers.append(layer);
        }
        full.activeLayerId = full.layers.constLast().id;
        controller.loadDocument(std::move(full));
        const int layerCount = controller.document().layers.size();
        controller.addLayer();
        QCOMPARE(controller.document().layers.size(), layerCount);
        controller.duplicateLayer(controller.document().activeLayerId);
        QCOMPARE(controller.document().layers.size(), layerCount);
        QCOMPARE(controller.undoStack()->count(), 0);
    }

    void rejectsInvalidJson_data()
    {
        QTest::addColumn<QByteArray>("json");

        QTest::newRow("malformed") << QByteArrayLiteral("{");
        QTest::newRow("unsupported-version")
            << QByteArrayLiteral(
                   R"({"schemaVersion":5,"canvas":{"width":10,"height":10},"layers":[{}]})");
        QTest::newRow("unsupported-algorithm")
            << QByteArrayLiteral(
                   R"({"schemaVersion":2,"algorithmVersion":3,"canvas":{"width":10,"height":10},"layers":[{}]})");
        QTest::newRow("invalid-canvas")
            << QByteArrayLiteral(
                   R"({"schemaVersion":1,"canvas":{"width":0,"height":10},"layers":[{}]})");
        QTest::newRow("missing-layers")
            << QByteArrayLiteral(
                   R"({"schemaVersion":1,"canvas":{"width":10,"height":10},"layers":[]})");
        QTest::newRow("duplicate-layers")
            << QByteArrayLiteral(
                   R"({"schemaVersion":1,"canvas":{"width":10,"height":10},"layers":[{"id":"11111111-1111-1111-1111-111111111111","strokes":[]},{"id":"11111111-1111-1111-1111-111111111111","strokes":[]}]})");
        QTest::newRow("empty-stroke")
            << QByteArrayLiteral(
                   R"({"schemaVersion":1,"canvas":{"width":10,"height":10},"layers":[{"name":"Layer","strokes":[{"points":[]}]}]})");
        QTest::newRow("invalid-point")
            << QByteArrayLiteral(
                   R"({"schemaVersion":1,"canvas":{"width":10,"height":10},"layers":[{"name":"Layer","strokes":[{"points":["bad"]}]}]})");
        QTest::newRow("fractional-canvas")
            << QByteArrayLiteral(
                   R"({"schemaVersion":1,"algorithmVersion":1,"canvas":{"width":10.5,"height":10,"background":"#ffffffff"},"animation":{"frames":2,"fps":25,"wobble":1},"activeLayerId":"11111111-1111-1111-1111-111111111111","layers":[{"id":"11111111-1111-1111-1111-111111111111","name":"Layer","visible":true,"opacity":1,"strokes":[]}]})");
        QTest::newRow("invalid-active-layer")
            << QByteArrayLiteral(
                   R"({"schemaVersion":1,"algorithmVersion":1,"canvas":{"width":10,"height":10,"background":"#ffffffff"},"animation":{"frames":2,"fps":25,"wobble":1},"activeLayerId":"22222222-2222-2222-2222-222222222222","layers":[{"id":"11111111-1111-1111-1111-111111111111","name":"Layer","visible":true,"opacity":1,"strokes":[]}]})");
        QTest::newRow("active-layer-without-layers")
            << QByteArrayLiteral(
                   R"({"schemaVersion":4,"algorithmVersion":2,"canvas":{"width":10,"height":10,"background":"#ffffffff"},"animation":{"frames":2,"fps":25,"wobble":1},"activeLayerId":"11111111-1111-1111-1111-111111111111","layers":[],"clipMasks":[]})");
        QTest::newRow("malformed-active-layer-without-layers")
            << QByteArrayLiteral(
                   R"({"schemaVersion":4,"algorithmVersion":2,"canvas":{"width":10,"height":10,"background":"#ffffffff"},"animation":{"frames":2,"fps":25,"wobble":1},"activeLayerId":"garbage","layers":[],"clipMasks":[]})");
        QTest::newRow("outside-point")
            << QByteArrayLiteral(
                   R"({"schemaVersion":1,"algorithmVersion":1,"canvas":{"width":10,"height":10,"background":"#ffffffff"},"animation":{"frames":2,"fps":25,"wobble":1},"activeLayerId":"11111111-1111-1111-1111-111111111111","layers":[{"id":"11111111-1111-1111-1111-111111111111","name":"Layer","visible":true,"opacity":1,"strokes":[{"id":"22222222-2222-2222-2222-222222222222","seed":"1","mode":"paint","color":"#ff000000","width":1,"points":[[11,5,1]]}]}]})");
        QTest::newRow("invalid-pressure")
            << QByteArrayLiteral(
                   R"({"schemaVersion":1,"algorithmVersion":1,"canvas":{"width":10,"height":10,"background":"#ffffffff"},"animation":{"frames":2,"fps":25,"wobble":1},"activeLayerId":"11111111-1111-1111-1111-111111111111","layers":[{"id":"11111111-1111-1111-1111-111111111111","name":"Layer","visible":true,"opacity":1,"strokes":[{"id":"22222222-2222-2222-2222-222222222222","seed":"1","mode":"paint","color":"#ff000000","width":1,"points":[[5,5,1.1]]}]}]})");
        QTest::newRow("missing-brush")
            << QByteArrayLiteral(
                   R"({"schemaVersion":2,"algorithmVersion":2,"canvas":{"width":10,"height":10,"background":"#ffffffff"},"animation":{"frames":2,"fps":25,"wobble":1},"activeLayerId":"11111111-1111-1111-1111-111111111111","layers":[{"id":"11111111-1111-1111-1111-111111111111","name":"Layer","visible":true,"opacity":1,"strokes":[{"id":"22222222-2222-2222-2222-222222222222","seed":"1","mode":"paint","color":"#ff000000","width":1,"points":[[5,5,1]]}]}]})");
        QTest::newRow("wrong-field-type")
            << QByteArrayLiteral(
                   R"({"schemaVersion":1,"algorithmVersion":1,"canvas":{"width":10,"height":10,"background":"#ffffffff"},"animation":{"frames":2,"fps":25,"wobble":1},"activeLayerId":"11111111-1111-1111-1111-111111111111","layers":[{"id":"11111111-1111-1111-1111-111111111111","name":"Layer","visible":1,"opacity":1,"strokes":[]}]})");
        QTest::newRow("fps-too-high")
            << QByteArrayLiteral(
                   R"({"schemaVersion":1,"algorithmVersion":1,"canvas":{"width":10,"height":10,"background":"#ffffffff"},"animation":{"frames":2,"fps":51,"wobble":1},"activeLayerId":"11111111-1111-1111-1111-111111111111","layers":[{"id":"11111111-1111-1111-1111-111111111111","name":"Layer","visible":true,"opacity":1,"strokes":[]}]})");
    }

    void rejectsInvalidJson()
    {
        QFETCH(QByteArray, json);

        QString error;
        const std::optional<Document> document =
            DocumentSerializer::fromJson(json, &error);
        QVERIFY(!document.has_value());
        QVERIFY(!error.isEmpty());
    }
};

}

int main(int argc, char **argv)
{
    QTemporaryDir settingsDirectory;
    if (!settingsDirectory.isValid()) {
        qCritical("Could not create an isolated test settings directory.");
        return 1;
    }

    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(
        QSettings::IniFormat,
        QSettings::UserScope,
        settingsDirectory.path());
    QSettings::setPath(
        QSettings::IniFormat,
        QSettings::SystemScope,
        settingsDirectory.path());

    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("WagleWaglePaint"));
    QApplication::setApplicationDisplayName(
        QStringLiteral("WagleWaglePaint"));
    QApplication::setApplicationVersion(
        QStringLiteral(WAGLEWAGLEPAINT_VERSION));
    QApplication::setOrganizationName(QStringLiteral("WagleWaglePaint"));
    QApplication::setOrganizationDomain(
        QStringLiteral("waglewaglepaint.dev"));

    wobble::Theme::apply(application);
    wobble::DocumentTests documentTests;
    int result = QTest::qExec(&documentTests, argc, argv);
    result |= wobble::runRenderEngineTests(argc, argv);
    result |= wobble::runGifWriterTests(argc, argv);
    result |= wobble::runMaskRegressionTests(argc, argv);
    result |= wobble::runReleaseNotesTests(argc, argv);
    result |= wobble::runUiTests(argc, argv);
    return result;
}

#include "DocumentTests.moc"

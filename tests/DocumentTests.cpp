#include "TestSuites.hpp"
#include "app/RecoveryStore.hpp"
#include "brush/BrushPreset.hpp"
#include "document/Document.hpp"
#include "document/DocumentController.hpp"
#include "document/DocumentLimits.hpp"
#include "document/SelectionOperation.hpp"
#include "io/DocumentSerializer.hpp"
#include "render/RenderEngine.hpp"

#include <QAction>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <limits>
#include <memory>

namespace wobble
{

QByteArray pointArray(int count)
{
    QByteArray points = QByteArrayLiteral("[0,0],").repeated(count);
    if (!points.isEmpty())
    {
        points.chop(1);
    }
    points.prepend('[');
    points.append(']');
    return points;
}

class DocumentControllerTestAccess final
{
public:
    static void failHistoryPrepareAfter(
        DocumentController &controller, int successfulStages)
    {
        controller.m_historyPrepareFailureCountdownForTesting =
            successfulStages;
    }

    static quint64 contentRevision(const DocumentController &controller)
    {
        return controller.m_currentContentRevision;
    }

    static quint64 historyNode(const DocumentController &controller)
    {
        return controller.m_currentHistoryNode;
    }

    static void setHistoryResidentLimit(
        DocumentController &controller, qint64 bytes)
    {
        controller.m_undoStack.m_maximumResidentBytes = bytes;
        controller.m_undoStack.enforceLimits();
    }
};

Document documentWithStrokeCount(int count)
{
    Document document = Document::createDefault(QSize(64, 64));
    Layer &layer = document.layers.first();
    layer.strokes.reserve(count);
    for (int index = 0; index < count; ++index)
    {
        Stroke stroke;
        stroke.seed = static_cast<quint64>(index + 1);
        stroke.points = {{QPointF(1.0 + static_cast<qreal>(index % 62),
                              1.0 + static_cast<qreal>((index / 62) % 62)),
            1.0}};
        layer.strokes.append(std::move(stroke));
    }
    return document;
}

class DocumentTests final : public QObject
{
    Q_OBJECT

private slots:
    void quarantinesUnreadableRecoveryWithoutDeletingIt()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QByteArray variable =
            QByteArrayLiteral("WAGLEWAGLEPAINT_RECOVERY_PATH");
        const bool existed = qEnvironmentVariableIsSet(variable.constData());
        const QByteArray previous = qgetenv(variable.constData());
        [[maybe_unused]] const auto restoreEnvironment = qScopeGuard(
            [&]()
            {
                if (existed)
                {
                    qputenv(variable.constData(), previous);
                }
                else
                {
                    qunsetenv(variable.constData());
                }
            });

        const QString source =
            directory.filePath(QStringLiteral("recovery.wagle"));
        QVERIFY(qputenv(variable.constData(), source.toUtf8()));
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray contents("not a project");
        QCOMPARE(file.write(contents), contents.size());
        file.close();

        QString error;
        const QString preserved = RecoveryStore::quarantine(&error);
        QVERIFY2(!preserved.isEmpty(), qPrintable(error));
        QVERIFY(!QFileInfo::exists(source));
        QVERIFY(QFileInfo::exists(preserved));
        QFile preservedFile(preserved);
        QVERIFY(preservedFile.open(QIODevice::ReadOnly));
        QCOMPARE(preservedFile.readAll(), contents);
    }

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
        stroke.id =
            QUuid(QStringLiteral("{11111111-1111-1111-1111-111111111111}"));
        stroke.seed = 42;
        stroke.color = QColor(20, 40, 60);
        stroke.width = 8.0;
        stroke.points = {
            {QPointF(10.0, 12.0), 0.5}, {QPointF(40.0, 52.0), 1.0}};

        controller.addStroke(layerId, stroke);
        QCOMPARE(controller.document().layer(layerId)->strokes.size(), 1);
        QCOMPARE(controller.document().layer(layerId)->strokes.first().id,
            stroke.id);
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
        QObject::connect(&controller,
            &DocumentController::selectionHistoryStateRequested,
            &controller,
            [&transientState](const QUuid &, const QImage &mask)
            {
                transientState = mask.isNull() ? 0 : mask.width();
            });

        QImage firstState(1, 1, QImage::Format_Grayscale8);
        firstState.fill(255);

        controller.pushSelectionStateCommand(
            QStringLiteral("Transient"), {}, {}, {}, firstState);
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
            {QPointF(10.0, 12.0), 1.0}, {QPointF(40.0, 52.0), 1.0}};
        controller.addStroke(layerId, stroke);
        QVERIFY(controller.isModified());
        controller.markSaved();
        QVERIFY(!controller.isModified());

        QImage secondState(2, 1, QImage::Format_Grayscale8);
        secondState.fill(255);
        controller.pushSelectionStateCommand(
            QStringLiteral("Transient 2"), {}, firstState, {}, secondState);
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
        QObject::connect(&controller,
            &DocumentController::selectionHistoryStateRequested,
            &controller,
            [&value](const QUuid &, const QImage &mask)
            {
                value = mask.isNull() ? 0 : mask.width();
            });
        for (int index = 1; index <= 80; ++index)
        {
            QImage before;
            if (index > 1)
            {
                before = QImage(index - 1, 1, QImage::Format_Grayscale8);
                before.fill(255);
            }
            QImage after(index, 1, QImage::Format_Grayscale8);
            after.fill(255);
            controller.pushSelectionStateCommand(
                QStringLiteral("Transient"), {}, before, {}, after);
        }

        QCOMPARE(value, 80);
        QCOMPARE(controller.undoStack()->undoLimit(), 64);
        QCOMPARE(controller.undoStack()->count(), 64);
    }

    void removesMergedScalarRoundTripFromHistory()
    {
        DocumentController controller;
        controller.newDocument(QSize(96, 96));
        const qreal original = controller.document().wobbleAmount;
        const quint64 originalRevision =
            DocumentControllerTestAccess::contentRevision(controller);
        const quint64 originalNode =
            DocumentControllerTestAccess::historyNode(controller);

        controller.setWobbleAmount(original + 1.0);
        QCOMPARE(controller.undoStack()->count(), 1);
        QVERIFY(controller.isModified());

        controller.setWobbleAmount(original);
        QCOMPARE(controller.undoStack()->count(), 0);
        QCOMPARE(controller.undoStack()->index(), 0);
        QVERIFY(!controller.undoStack()->canUndo());
        QVERIFY(!controller.undoStack()->canRedo());
        QVERIFY(controller.undoStack()->isClean());
        QVERIFY(!controller.isModified());
        QCOMPARE(DocumentControllerTestAccess::contentRevision(controller),
            originalRevision);
        QCOMPARE(DocumentControllerTestAccess::historyNode(controller),
            originalNode);
    }

    void discardsFailedMacroWithoutUndoingEarlierHistory()
    {
        DocumentController controller;
        controller.newDocument(QSize(96, 96));
        const QUuid layerId = controller.document().activeLayerId;
        controller.setWobbleAmount(2.0);
        const int beforeCount = controller.undoStack()->count();
        const int beforeIndex = controller.undoStack()->index();
        const quint64 beforeRevision =
            DocumentControllerTestAccess::contentRevision(controller);
        const quint64 beforeNode =
            DocumentControllerTestAccess::historyNode(controller);
        QSignalSpy documentChangedSpy(
            &controller, &DocumentController::documentChanged);

        controller.undoStack()->beginMacro(
            QStringLiteral("Rejected canvas transaction"));
        controller.renameLayer(layerId, QStringLiteral("Temporary"));
        QVERIFY(!controller.resizeCanvas(QSize(96, 96), QPoint()));
        controller.undoStack()->endMacro();

        QCOMPARE(controller.undoStack()->count(), beforeCount);
        QCOMPARE(controller.undoStack()->index(), beforeIndex);
        QCOMPARE(controller.document().layer(layerId)->name,
            QStringLiteral("Layer 1"));
        QCOMPARE(controller.document().wobbleAmount, 2.0);
        QCOMPARE(documentChangedSpy.count(), 0);
        QCOMPARE(DocumentControllerTestAccess::contentRevision(controller),
            beforeRevision);
        QCOMPARE(
            DocumentControllerTestAccess::historyNode(controller), beforeNode);

        controller.undoStack()->undo();
        QCOMPARE(controller.document().wobbleAmount,
            Document::createDefault(QSize(96, 96)).wobbleAmount);
    }

    void publishesNestedMacroOnce()
    {
        DocumentController controller;
        controller.newDocument(QSize(96, 96));
        const QUuid layerId = controller.document().activeLayerId;
        QSignalSpy documentChangedSpy(
            &controller, &DocumentController::documentChanged);

        controller.undoStack()->beginMacro(QStringLiteral("Outer"));
        controller.renameLayer(layerId, QStringLiteral("Nested"));
        controller.undoStack()->beginMacro(QStringLiteral("Inner"));
        controller.setLayerVisible(layerId, false);
        controller.undoStack()->endMacro();
        QCOMPARE(controller.undoStack()->count(), 0);
        QCOMPARE(documentChangedSpy.count(), 0);
        controller.undoStack()->endMacro();

        QCOMPARE(controller.undoStack()->count(), 1);
        QCOMPARE(controller.undoStack()->index(), 1);
        QCOMPARE(documentChangedSpy.count(), 1);
        QCOMPARE(controller.document().layer(layerId)->name,
            QStringLiteral("Nested"));
        QVERIFY(!controller.document().layer(layerId)->visible);

        controller.undoStack()->undo();
        QCOMPARE(controller.document().layer(layerId)->name,
            QStringLiteral("Layer 1"));
        QVERIFY(controller.document().layer(layerId)->visible);
    }

    void evictsFarthestHistoryAndMaintainsActions()
    {
        DocumentController controller;
        controller.newDocument(QSize(96, 96));
        const QUuid layerId = controller.document().activeLayerId;
        controller.undoStack()->setUndoLimit(3);
        QAction *undoAction =
            controller.undoStack()->createUndoAction(&controller);
        QAction *redoAction =
            controller.undoStack()->createRedoAction(&controller);
        QVERIFY(!undoAction->isEnabled());
        QVERIFY(!redoAction->isEnabled());

        for (int index = 0; index < 4; ++index)
        {
            controller.setLayerVisible(layerId, (index % 2) != 0);
        }
        QCOMPARE(controller.undoStack()->count(), 3);
        QCOMPARE(controller.undoStack()->index(), 3);
        QVERIFY(undoAction->isEnabled());
        QVERIFY(!redoAction->isEnabled());

        while (controller.undoStack()->canUndo())
        {
            controller.undoStack()->undo();
        }
        QCOMPARE(controller.undoStack()->index(), 0);
        QVERIFY(!controller.undoStack()->isClean());
        QVERIFY(!undoAction->isEnabled());
        QVERIFY(redoAction->isEnabled());

        controller.undoStack()->redo();
        QVERIFY(controller.undoStack()->canRedo());
        controller.renameLayer(layerId, QStringLiteral("New branch"));
        QVERIFY(!controller.undoStack()->canRedo());
        QVERIFY(!redoAction->isEnabled());
    }

    void evictsRedoTailWhenLimitShrinksNearCursor()
    {
        DocumentController controller;
        controller.newDocument(QSize(96, 96));
        const QUuid layerId = controller.document().activeLayerId;

        for (int index = 0; index < 6; ++index)
        {
            controller.setLayerVisible(layerId, (index % 2) != 0);
        }
        controller.markSaved();
        QVERIFY(controller.undoStack()->isClean());
        QCOMPARE(controller.undoStack()->count(), 6);

        for (int index = 0; index < 4; ++index)
        {
            controller.undoStack()->undo();
        }
        QCOMPARE(controller.undoStack()->index(), 2);
        QVERIFY(controller.document().layer(layerId)->visible);

        controller.undoStack()->setUndoLimit(3);

        QCOMPARE(controller.undoStack()->count(), 3);
        QCOMPARE(controller.undoStack()->index(), 1);
        QVERIFY(!controller.undoStack()->isClean());
        QVERIFY(controller.undoStack()->canUndo());
        QVERIFY(controller.undoStack()->canRedo());
        QVERIFY(controller.document().layer(layerId)->visible);

        controller.undoStack()->undo();
        QVERIFY(!controller.document().layer(layerId)->visible);
        QVERIFY(!controller.undoStack()->canUndo());

        controller.undoStack()->redo();
        controller.undoStack()->redo();
        controller.undoStack()->redo();
        QVERIFY(!controller.undoStack()->canRedo());
        QVERIFY(controller.document().layer(layerId)->visible);
        QVERIFY(!controller.undoStack()->isClean());
        QVERIFY(controller.isModified());
    }

    void invalidatesCleanMarkerInsideDeletedRedoTail()
    {
        DocumentController controller;
        controller.newDocument(QSize(96, 96));
        const QUuid layerId = controller.document().activeLayerId;

        controller.renameLayer(layerId, QStringLiteral("First"));
        controller.setLayerVisible(layerId, false);
        controller.markSaved();
        QVERIFY(controller.undoStack()->isClean());
        QVERIFY(!controller.isModified());
        QCOMPARE(controller.undoStack()->count(), 2);

        controller.undoStack()->undo();
        QVERIFY(!controller.undoStack()->isClean());
        QVERIFY(controller.isModified());

        controller.setLayerOpacity(layerId, 0.5);
        QCOMPARE(controller.undoStack()->count(), 2);
        QCOMPARE(controller.undoStack()->index(), 2);
        QVERIFY(!controller.undoStack()->canRedo());
        QVERIFY(!controller.undoStack()->isClean());
        QVERIFY(controller.isModified());

        controller.undoStack()->undo();
        QVERIFY(!controller.undoStack()->isClean());
        QVERIFY(controller.isModified());
        controller.undoStack()->undo();
        QCOMPARE(controller.document().layer(layerId)->name,
            QStringLiteral("Layer 1"));
        QVERIFY(!controller.undoStack()->isClean());
        QVERIFY(controller.isModified());
        controller.undoStack()->redo();
        controller.undoStack()->redo();
        QCOMPARE(controller.document().layer(layerId)->name,
            QStringLiteral("First"));
        QCOMPARE(controller.document().layer(layerId)->opacity, 0.5);
        QVERIFY(!controller.undoStack()->isClean());
        QVERIFY(controller.isModified());

        controller.markSaved();
        QVERIFY(controller.undoStack()->isClean());
        QVERIFY(!controller.isModified());
        controller.undoStack()->undo();
        QVERIFY(controller.isModified());
        controller.undoStack()->redo();
        QVERIFY(controller.undoStack()->isClean());
        QVERIFY(!controller.isModified());
    }

    void reusesTrustedBackingsWhenRestoringClearedAndRemovedLayers()
    {
        Document source = Document::createDefault(QSize(64, 64));
        Stroke stroke;
        stroke.points = {
            {QPointF(10.0, 10.0), 1.0}, {QPointF(20.0, 20.0), 1.0}};
        stroke.clipMask = QImage(source.size, QImage::Format_Grayscale8);
        stroke.clipMask.fill(0);
        for (int y = 8; y < 24; ++y)
        {
            std::fill_n(stroke.clipMask.scanLine(y) + 8, 16, 255);
        }
        source.layers.first().strokes.append(stroke);

        QImage selection(source.size, QImage::Format_Grayscale8);
        selection.fill(0);
        for (int y = 16; y < 32; ++y)
        {
            std::fill_n(selection.scanLine(y) + 16, 8, 255);
        }
        const std::optional<PixelSelectionOp> selectionOp =
            makePixelSelectionOp(
                selection, QTransform::fromTranslate(4.0, 2.0), true, true);
        QVERIFY(selectionOp.has_value());
        Stroke selectionStroke;
        selectionStroke.mode = StrokeMode::PixelSelection;
        selectionStroke.pixelSelectionOp = *selectionOp;
        source.layers.first().strokes.append(selectionStroke);

        DocumentController controller;
        controller.loadDocument(source);
        const QUuid layerId = controller.document().activeLayerId;
        const Stroke &prepared =
            controller.document().layer(layerId)->strokes.first();
        const qint64 maskKey = prepared.clipMask.cacheKey();
        const StrokePoint *pointBacking = prepared.points.constData();
        const Stroke &preparedSelection =
            controller.document().layer(layerId)->strokes.last();
        QVERIFY(preparedSelection.pixelSelectionOp.has_value());
        const char *selectionBacking =
            preparedSelection.pixelSelectionOp->packedMask.constData();

        controller.clearLayer(layerId);
        controller.undoStack()->undo();
        const Stroke &clearRestored =
            controller.document().layer(layerId)->strokes.first();
        QCOMPARE(clearRestored.clipMask.cacheKey(), maskKey);
        QCOMPARE(clearRestored.points.constData(), pointBacking);
        const Stroke &clearRestoredSelection =
            controller.document().layer(layerId)->strokes.last();
        QVERIFY(clearRestoredSelection.pixelSelectionOp.has_value());
        QCOMPARE(static_cast<const void *>(clearRestoredSelection
                         .pixelSelectionOp->packedMask.constData()),
            static_cast<const void *>(selectionBacking));

        controller.loadDocument(source);
        const Stroke &removePrepared =
            controller.document().layer(layerId)->strokes.first();
        const qint64 removeMaskKey = removePrepared.clipMask.cacheKey();
        const StrokePoint *removePointBacking =
            removePrepared.points.constData();
        QVERIFY(controller.document()
                .layer(layerId)
                ->strokes.last()
                .pixelSelectionOp.has_value());
        const char *removeSelectionBacking =
            controller.document()
                .layer(layerId)
                ->strokes.last()
                .pixelSelectionOp->packedMask.constData();
        controller.removeLayer(layerId);
        QVERIFY(controller.document().layers.isEmpty());
        controller.undoStack()->undo();
        const Stroke &removeRestored =
            controller.document().layer(layerId)->strokes.first();
        QCOMPARE(removeRestored.clipMask.cacheKey(), removeMaskKey);
        QCOMPARE(removeRestored.points.constData(), removePointBacking);
        QVERIFY(controller.document()
                .layer(layerId)
                ->strokes.last()
                .pixelSelectionOp.has_value());
        QCOMPARE(static_cast<const void *>(controller.document()
                         .layer(layerId)
                         ->strokes.last()
                         .pixelSelectionOp->packedMask.constData()),
            static_cast<const void *>(removeSelectionBacking));
    }

    void refusesTrustedLeaseForExternalAliases()
    {
        Document source = Document::createDefault(QSize(64, 64));
        Stroke stroke;
        stroke.points = {{QPointF(4.0, 4.0), 1.0}, {QPointF(40.0, 40.0), 1.0}};
        stroke.clipMask = QImage(source.size, QImage::Format_Grayscale8);
        stroke.clipMask.fill(0);
        for (int y = 4; y < 20; ++y)
        {
            std::fill_n(stroke.clipMask.scanLine(y) + 4, 16, 255);
        }
        source.layers.first().strokes.append(stroke);

        DocumentSerializer::SerializationCache cache;
        const std::optional<DocumentSerializer::PreparedDocument> prepared =
            DocumentSerializer::prepare(source, cache);
        QVERIFY(prepared.has_value());
        const Stroke &trusted =
            prepared->document().layers.first().strokes.first();

        QVERIFY(
            DocumentSerializer::retainImmutableBackings(*prepared, {trusted})
                .isValid());

        Stroke pointAlias = trusted;
        pointAlias.points.detach();
        QVERIFY(!DocumentSerializer::retainImmutableBackings(
            *prepared, {pointAlias})
                .isValid());

        Stroke maskAlias = trusted;
        maskAlias.clipMask = trusted.clipMask.copy();
        QVERIFY(
            !DocumentSerializer::retainImmutableBackings(*prepared, {maskAlias})
                .isValid());

        Stroke foreign = trusted;
        foreign.id = QUuid::createUuid();
        QVERIFY(
            !DocumentSerializer::retainImmutableBackings(*prepared, {foreign})
                .isValid());

        QVERIFY(!DocumentSerializer::ImmutableBackingLease().isValid());
    }

    void commitsLargeDocumentMacroAsOnePreparedStage()
    {
        DocumentController controller;
        controller.loadDocument(documentWithStrokeCount(20000));
        const QUuid layerId = controller.document().activeLayerId;
        QSignalSpy documentChangedSpy(
            &controller, &DocumentController::documentChanged);

        controller.undoStack()->beginMacro(
            QStringLiteral("Large metadata transaction"));
        for (int index = 0; index < 64; ++index)
        {
            controller.renameLayer(
                layerId, QStringLiteral("Large layer %1").arg(index));
            QVERIFY(
                controller.undoStack()->storageStats().macroPreparedDocuments
                <= qsizetype(2));
        }
        QCOMPARE(documentChangedSpy.count(), 0);
        QCOMPARE(controller.undoStack()->storageStats().macroPreparedDocuments,
            qsizetype(2));
        controller.undoStack()->endMacro();

        QCOMPARE(controller.undoStack()->count(), 1);
        QCOMPARE(documentChangedSpy.count(), 1);
        DocumentUndoStack::StorageStats stats =
            controller.undoStack()->storageStats();
        QCOMPARE(stats.entryCount, qsizetype(1));
        QCOMPARE(stats.macroPreparedDocuments, qsizetype(0));
        QCOMPARE(stats.retainedPreparedDocuments, qsizetype(0));
        QCOMPARE(stats.stagedPreparedDocuments, qsizetype(0));
        QCOMPARE(stats.peakTransientPreparedDocuments, qsizetype(0));

        controller.undoStack()->undo();
        stats = controller.undoStack()->storageStats();
        QCOMPARE(stats.stagedPreparedDocuments, qsizetype(0));
        QCOMPARE(stats.peakTransientPreparedDocuments, qsizetype(1));
        QCOMPARE(controller.document().layer(layerId)->name,
            QStringLiteral("Layer 1"));
    }

    void boundsSixtyFourUniqueFourKSelectionSnapshots()
    {
        constexpr int edge = 4096;
        constexpr int rows = 64;
        constexpr int editCount = 64;
        constexpr qint64 testResidentLimit = 2LL * 1024LL * 1024LL;
        QImage state(QSize(edge, rows), QImage::Format_Grayscale8);
        QVERIFY(!state.isNull());
        state.fill(255);

        DocumentController controller;
        controller.newDocument(state.size());
        DocumentControllerTestAccess::setHistoryResidentLimit(
            controller, testResidentLimit);
        const QUuid layerId = controller.document().activeLayerId;
        int observedLastPixel = -1;
        QObject::connect(&controller,
            &DocumentController::selectionHistoryStateRequested,
            &controller,
            [&observedLastPixel](const QUuid &, const QImage &mask)
            {
                observedLastPixel =
                    mask.isNull() ? -1 : mask.constScanLine(0)[editCount - 1];
            });

        for (int index = 0; index < editCount; ++index)
        {
            const QImage before = state;
            state.detach();
            state.scanLine(0)[index] = 0;
            controller.pushSelectionStateCommand(
                QStringLiteral("4K selection transform %1").arg(index),
                layerId,
                before,
                layerId,
                state);
        }

        const DocumentUndoStack::StorageStats stats =
            controller.undoStack()->storageStats();
        QCOMPARE(stats.entryCount, qsizetype(controller.undoStack()->count()));
        QVERIFY(stats.retainedBytes <= testResidentLimit);
        QVERIFY(!stats.residentBudgetSoftExceeded);
        QVERIFY(controller.undoStack()->count() < editCount);
        QCOMPARE(observedLastPixel, 0);

        const int retainedCount = controller.undoStack()->count();
        controller.undoStack()->undo();
        QCOMPARE(observedLastPixel, 255);
        controller.undoStack()->redo();
        QCOMPARE(observedLastPixel, 0);
        QCOMPARE(controller.undoStack()->index(), retainedCount);
        QCOMPARE(observedLastPixel, 0);
    }

    void boundsProductionBudgetForSixtyFourUniqueFourKMasks()
    {
        constexpr int edge = 4096;
        constexpr int editCount = 64;
        QImage state(edge, edge, QImage::Format_Grayscale8);
        QVERIFY(!state.isNull());
        state.fill(255);

        DocumentController controller;
        controller.newDocument(state.size());
        const QUuid layerId = controller.document().activeLayerId;
        int observedLastPixel = -1;
        QObject::connect(&controller,
            &DocumentController::selectionHistoryStateRequested,
            &controller,
            [&observedLastPixel](const QUuid &, const QImage &mask)
            {
                observedLastPixel =
                    mask.isNull() ? -1
                                  : mask.constScanLine(edge - 1)[editCount - 1];
            });

        for (int index = 0; index < editCount; ++index)
        {
            const QImage before = state;
            state.detach();
            state.scanLine(edge - 1)[index] = 0;
            controller.pushSelectionStateCommand(
                QStringLiteral("Native 4K selection %1").arg(index),
                layerId,
                before,
                layerId,
                state);
        }

        const DocumentUndoStack::StorageStats stats =
            controller.undoStack()->storageStats();
        QCOMPARE(stats.entryCount, qsizetype(controller.undoStack()->count()));
        QVERIFY(stats.retainedBytes <= DocumentUndoStack::maximumResidentBytes);
        QVERIFY(!stats.residentBudgetSoftExceeded);
        QVERIFY(controller.undoStack()->count() >= editCount / 2);
        QCOMPARE(
            controller.undoStack()->index(), controller.undoStack()->count());
        QCOMPARE(observedLastPixel, 0);

        const int retainedCount = controller.undoStack()->count();
        controller.undoStack()->undo();
        QCOMPARE(observedLastPixel, 255);
        controller.undoStack()->redo();
        QCOMPARE(observedLastPixel, 0);
        QCOMPARE(controller.undoStack()->index(), retainedCount);
    }

    void softRetainsSingleOversizedHistoryEntry()
    {
        DocumentController controller;
        controller.newDocument(QSize(256, 256));
        DocumentControllerTestAccess::setHistoryResidentLimit(
            controller, 4LL * 1024LL);
        const QUuid layerId = controller.document().activeLayerId;
        int observedWidth = -2;
        QObject::connect(&controller,
            &DocumentController::selectionHistoryStateRequested,
            &controller,
            [&observedWidth](const QUuid &, const QImage &mask)
            {
                observedWidth = mask.isNull() ? -1 : mask.width();
            });

        QImage oversized(256, 256, QImage::Format_Grayscale8);
        oversized.fill(255);
        controller.pushSelectionStateCommand(
            QStringLiteral("Oversized selection"),
            layerId,
            {},
            layerId,
            oversized);

        DocumentUndoStack::StorageStats stats =
            controller.undoStack()->storageStats();
        QCOMPARE(controller.undoStack()->count(), 1);
        QVERIFY(stats.retainedBytes > 4LL * 1024LL);
        QVERIFY(stats.residentBudgetSoftExceeded);
        QVERIFY(controller.undoStack()->canUndo());

        controller.undoStack()->undo();
        QCOMPARE(observedWidth, -1);
        controller.undoStack()->redo();
        QCOMPARE(observedWidth, 256);

        QImage second(192, 256, QImage::Format_Grayscale8);
        second.fill(255);
        controller.pushSelectionStateCommand(
            QStringLiteral("Second oversized selection"),
            layerId,
            oversized,
            layerId,
            second);
        stats = controller.undoStack()->storageStats();
        QCOMPARE(controller.undoStack()->count(), 1);
        QCOMPARE(controller.undoStack()->index(), 1);
        QVERIFY(stats.residentBudgetSoftExceeded);
        controller.undoStack()->undo();
        QCOMPARE(observedWidth, 256);
    }

    void retainsOnlyChangedPayloadForLargeDocumentHistory()
    {
        constexpr int strokeLimit = DocumentLimits::maximumTotalStrokes;

        DocumentController controller;
        controller.loadDocument(documentWithStrokeCount(strokeLimit));
        const QUuid layerId = controller.document().activeLayerId;
        QCOMPARE(
            controller.document().layer(layerId)->strokes.size(), strokeLimit);

        QElapsedTimer timer;
        timer.start();
        for (int index = 0; index < 64; ++index)
        {
            controller.renameLayer(
                layerId, QStringLiteral("Large layer %1").arg(index));
        }
        const qint64 renameMs = timer.elapsed();
        const DocumentUndoStack::StorageStats renameStorage =
            controller.undoStack()->storageStats();
        QCOMPARE(controller.undoStack()->count(), 64);
        QCOMPARE(renameStorage.retainedLayers, qsizetype(0));
        QCOMPARE(renameStorage.retainedStrokes, qsizetype(0));
        QCOMPARE(renameStorage.retainedPreparedDocuments, qsizetype(0));

        Document addBase = controller.document();
        addBase.layers.first().strokes.resize(strokeLimit - 64);
        controller.loadDocument(std::move(addBase));
        const QUuid addLayerId = controller.document().activeLayerId;
        timer.restart();
        for (int index = 0; index < 64; ++index)
        {
            Stroke stroke;
            stroke.seed = static_cast<quint64>(strokeLimit + index);
            stroke.points = {
                {QPointF(1.0 + static_cast<qreal>(index % 62), 32.0), 1.0}};
            controller.addStroke(addLayerId, std::move(stroke));
        }
        const qint64 addMs = timer.elapsed();
        const DocumentUndoStack::StorageStats addStorage =
            controller.undoStack()->storageStats();
        QCOMPARE(controller.undoStack()->count(), 64);
        QCOMPARE(addStorage.retainedLayers, qsizetype(0));
        QCOMPARE(addStorage.retainedStrokes, qsizetype(64));
        QCOMPARE(addStorage.retainedPreparedDocuments, qsizetype(0));
        QCOMPARE(controller.document().layer(addLayerId)->strokes.size(),
            strokeLimit);

        controller.loadDocument(controller.document());
        const qreal originalWobble = controller.document().wobbleAmount;
        timer.restart();
        for (int index = 0; index < 64; ++index)
        {
            controller.setWobbleAmount(2.0 + static_cast<qreal>(index) / 100.0);
        }
        const qint64 scalarMs = timer.elapsed();
        const qreal finalWobble = controller.document().wobbleAmount;
        const DocumentUndoStack::StorageStats scalarStorage =
            controller.undoStack()->storageStats();
        QCOMPARE(controller.undoStack()->count(), 1);
        QCOMPARE(scalarStorage.retainedLayers, qsizetype(0));
        QCOMPARE(scalarStorage.retainedStrokes, qsizetype(0));
        QCOMPARE(scalarStorage.retainedPreparedDocuments, qsizetype(0));
        controller.undoStack()->undo();
        QCOMPARE(controller.document().wobbleAmount, originalWobble);
        controller.undoStack()->redo();
        QCOMPARE(controller.document().wobbleAmount, finalWobble);

        controller.loadDocument(controller.document());
        const QUuid clearLayerId = controller.document().activeLayerId;
        timer.restart();
        controller.clearLayer(clearLayerId);
        const qint64 clearMs = timer.elapsed();
        QCOMPARE(controller.undoStack()->storageStats().retainedStrokes,
            qsizetype(strokeLimit));
        QCOMPARE(controller.document().layer(clearLayerId)->strokes.size(), 0);
        timer.restart();
        controller.undoStack()->undo();
        const qint64 restoreClearMs = timer.elapsed();
        QCOMPARE(controller.document().layer(clearLayerId)->strokes.size(),
            strokeLimit);

        controller.loadDocument(controller.document());
        const QUuid removedLayerId = controller.document().activeLayerId;
        timer.restart();
        controller.removeLayer(removedLayerId);
        const qint64 removeMs = timer.elapsed();
        QCOMPARE(controller.document().layers.size(), 0);
        QCOMPARE(controller.undoStack()->storageStats().retainedStrokes,
            qsizetype(strokeLimit));
        timer.restart();
        controller.undoStack()->undo();
        const qint64 restoreRemoveMs = timer.elapsed();
        QCOMPARE(controller.document().layer(removedLayerId)->strokes.size(),
            strokeLimit);

        qInfo().nospace() << "large history latency ms: rename=" << renameMs
                          << ", add=" << addMs << ", scalar=" << scalarMs
                          << ", clear=" << clearMs
                          << ", restore-clear=" << restoreClearMs
                          << ", remove=" << removeMs
                          << ", restore-remove=" << restoreRemoveMs;
    }

    void preflightsWholeMacroBeforeMovingHistory()
    {
        DocumentController controller;
        controller.newDocument(QSize(128, 128));
        const QUuid layerId = controller.document().activeLayerId;
        int transientState = 0;
        QObject::connect(&controller,
            &DocumentController::selectionHistoryStateRequested,
            &controller,
            [&transientState](const QUuid &, const QImage &mask)
            {
                transientState = mask.isNull() ? 0 : 1;
            });
        QImage selected(1, 1, QImage::Format_Grayscale8);
        selected.fill(255);

        controller.undoStack()->beginMacro(QStringLiteral("Atomic macro"));
        controller.pushSelectionStateCommand(
            QStringLiteral("Selection"), {}, {}, layerId, selected);
        controller.renameLayer(layerId, QStringLiteral("Renamed"));
        controller.setLayerVisible(layerId, false);
        controller.undoStack()->endMacro();
        controller.markSaved();

        const int beforeIndex = controller.undoStack()->index();
        const int beforeCount = controller.undoStack()->count();
        const quint64 beforeRevision =
            DocumentControllerTestAccess::contentRevision(controller);
        const quint64 beforeNode =
            DocumentControllerTestAccess::historyNode(controller);
        QSignalSpy documentChangedSpy(
            &controller, &DocumentController::documentChanged);
        QSignalSpy modifiedChangedSpy(
            &controller, &DocumentController::modifiedChanged);

        // The aggregate command has one prepared target. Rejecting that
        // target must leave the entire document/selection transaction still.
        DocumentControllerTestAccess::failHistoryPrepareAfter(controller, 0);
        controller.undoStack()->undo();

        QCOMPARE(controller.undoStack()->index(), beforeIndex);
        QCOMPARE(controller.undoStack()->count(), beforeCount);
        QCOMPARE(transientState, 1);
        QCOMPARE(controller.document().layer(layerId)->name,
            QStringLiteral("Renamed"));
        QVERIFY(!controller.document().layer(layerId)->visible);
        QVERIFY(!controller.isModified());
        QCOMPARE(DocumentControllerTestAccess::contentRevision(controller),
            beforeRevision);
        QCOMPARE(
            DocumentControllerTestAccess::historyNode(controller), beforeNode);
        QCOMPARE(documentChangedSpy.count(), 0);
        QCOMPARE(modifiedChangedSpy.count(), 0);
        QCOMPARE(
            controller.undoStack()->storageStats().retainedPreparedDocuments,
            qsizetype(0));

        controller.undoStack()->undo();
        QCOMPARE(controller.undoStack()->index(), 0);
        QCOMPARE(transientState, 0);
        QCOMPARE(controller.document().layer(layerId)->name,
            QStringLiteral("Layer 1"));
        QVERIFY(controller.document().layer(layerId)->visible);
        QVERIFY(controller.isModified());

        controller.undoStack()->redo();
        QCOMPARE(controller.undoStack()->index(), 1);
        QCOMPARE(transientState, 1);
        QCOMPARE(controller.document().layer(layerId)->name,
            QStringLiteral("Renamed"));
        QVERIFY(!controller.document().layer(layerId)->visible);
        QVERIFY(!controller.isModified());
    }

    void blocksHistoryMutationDuringSynchronousCallbacks()
    {
        Document document = Document::createDefault(QSize(160, 120));
        Layer secondLayer;
        secondLayer.name = QStringLiteral("Layer 2");
        secondLayer.initialCanvasSize = document.size;
        const QUuid firstLayerId = document.activeLayerId;
        const QUuid secondLayerId = secondLayer.id;
        document.layers.append(secondLayer);

        DocumentController controller;
        controller.loadDocument(std::move(document));
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const QString rejectedSavePath =
            tempDir.filePath(QStringLiteral("rejected-save.wagle"));
        int phase = 0;
        int transientState = 0;
        bool pushStateWasAtomic = false;
        bool undoStateWasAtomic = false;
        bool reentrantSaveSucceeded = true;
        QObject::connect(&controller,
            &DocumentController::documentChanged,
            &controller,
            [&]()
            {
                if (phase == 1)
                {
                    phase = 0;
                    pushStateWasAtomic =
                        controller.document()
                                .layer(firstLayerId)
                                ->strokes.size()
                            == 1
                        && controller.isModified()
                        && DocumentControllerTestAccess::contentRevision(
                               controller)
                               == 1
                        && DocumentControllerTestAccess::historyNode(controller)
                               != 0;
                    controller.undoStack()->undo();
                    controller.undoStack()->redo();
                    controller.renameLayer(
                        firstLayerId, QStringLiteral("Rejected nested rename"));
                    QImage selected(1, 1, QImage::Format_Grayscale8);
                    selected.fill(255);
                    controller.pushSelectionStateCommand(
                        QStringLiteral("Rejected transient"),
                        {},
                        {},
                        firstLayerId,
                        selected);
                    controller.newDocument(QSize(32, 32));
                    controller.loadDocument(
                        Document::createDefault(QSize(48, 48)));
                    controller.loadRecoveredDocument(
                        Document::createDefault(QSize(48, 48)));
                    reentrantSaveSucceeded =
                        controller.saveDocument(rejectedSavePath);
                    controller.markSaved();
                    // Active-layer selection is deliberately non-history UI
                    // state and remains legal after the target is installed.
                    controller.setActiveLayer(secondLayerId);
                }
                else if (phase == 2)
                {
                    phase = 0;
                    undoStateWasAtomic =
                        controller.document()
                            .layer(firstLayerId)
                            ->strokes.isEmpty()
                        && !controller.isModified()
                        && DocumentControllerTestAccess::contentRevision(
                               controller)
                               == 0
                        && DocumentControllerTestAccess::historyNode(controller)
                               == 0;
                    controller.undoStack()->undo();
                    controller.undoStack()->redo();
                    controller.renameLayer(
                        firstLayerId, QStringLiteral("Also rejected"));
                }
            });

        Stroke stroke;
        stroke.points = {
            {QPointF(20.0, 20.0), 1.0}, {QPointF(40.0, 40.0), 1.0}};
        phase = 1;
        controller.addStroke(firstLayerId, std::move(stroke));
        QVERIFY(pushStateWasAtomic);
        QCOMPARE(controller.undoStack()->count(), 1);
        QCOMPARE(controller.undoStack()->index(), 1);
        QCOMPARE(controller.document().size, QSize(160, 120));
        QCOMPARE(controller.document().layer(firstLayerId)->name,
            QStringLiteral("Layer 1"));
        QCOMPARE(controller.document().layer(firstLayerId)->strokes.size(), 1);
        QCOMPARE(controller.document().activeLayerId, secondLayerId);
        QCOMPARE(transientState, 0);
        QVERIFY(controller.isModified());
        QVERIFY(!reentrantSaveSucceeded);
        QVERIFY(!QFile::exists(rejectedSavePath));

        phase = 2;
        controller.undoStack()->undo();
        QVERIFY(undoStateWasAtomic);
        QCOMPARE(controller.undoStack()->index(), 0);
        QVERIFY(controller.document().layer(firstLayerId)->strokes.isEmpty());
        QCOMPARE(controller.document().layer(firstLayerId)->name,
            QStringLiteral("Layer 1"));
        QCOMPARE(controller.document().activeLayerId, secondLayerId);
        QVERIFY(!controller.isModified());
    }

    void commitsAccumulatedSelectionTransformAsOneCommand()
    {
        Document document = Document::createDefault(QSize(240, 180));
        Stroke stroke;
        stroke.width = 8.0;
        stroke.points = {
            {QPointF(70.0, 70.0), 0.5}, {QPointF(110.0, 80.0), 1.0}};
        const QUuid layerId = document.activeLayerId;
        const QUuid strokeId = stroke.id;
        document.layers.first().strokes.append(stroke);

        DocumentController controller;
        controller.loadDocument(document);
        QSignalSpy transformedSpy(
            &controller, &DocumentController::strokesTransformed);
        QTransform accumulated;
        accumulated.translate(12.0, 6.0);
        accumulated.translate(90.0, 75.0);
        accumulated.rotate(15.0);
        accumulated.scale(1.25, 1.25);
        accumulated.translate(-90.0, -75.0);

        QVERIFY(
            controller.transformSelection(layerId, {strokeId}, accumulated));
        QCOMPARE(controller.undoStack()->count(), 1);
        QCOMPARE(controller.undoStack()->index(), 1);
        const Stroke transformed =
            controller.document().layer(layerId)->strokes.first();
        QCOMPARE(transformed.points.first().position,
            accumulated.map(stroke.points.first().position));
        QCOMPARE(transformed.points.last().position,
            accumulated.map(stroke.points.last().position));
        QVERIFY(qAbs(transformed.width - 10.0) < 0.000001);
        QCOMPARE(transformedSpy.count(), 1);
        QCOMPARE(qvariant_cast<QTransform>(transformedSpy[0][2]), accumulated);

        QVERIFY(
            !controller.transformSelection(layerId, {strokeId}, QTransform()));
        QCOMPARE(controller.undoStack()->count(), 1);

        QTransform perspective;
        perspective.setMatrix(1.0, 0.0, 0.001, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0);
        QVERIFY(!perspective.isAffine());
        QVERIFY(
            !controller.transformSelection(layerId, {strokeId}, perspective));
        QCOMPARE(controller.undoStack()->count(), 1);
        QCOMPARE(controller.document().layer(layerId)->strokes.first().points,
            transformed.points);

        controller.undoStack()->undo();
        QCOMPARE(controller.undoStack()->index(), 0);
        const Stroke restored =
            controller.document().layer(layerId)->strokes.first();
        QCOMPARE(restored.points, stroke.points);
        QCOMPARE(restored.width, stroke.width);
        QCOMPARE(transformedSpy.count(), 2);
        bool invertible = false;
        const QTransform inverse = accumulated.inverted(&invertible);
        QVERIFY(invertible);
        QCOMPARE(qvariant_cast<QTransform>(transformedSpy[1][2]), inverse);

        controller.undoStack()->redo();
        QCOMPARE(controller.undoStack()->index(), 1);
        QCOMPARE(controller.document().layer(layerId)->strokes.first().points,
            transformed.points);
    }

    void roundTripsLayerAndStrokeSequenceDeltas()
    {
        Document layers = Document::createDefault(QSize(96, 96));
        layers.layers.first().name = QStringLiteral("A");
        Layer second;
        second.name = QStringLiteral("B");
        second.initialCanvasSize = layers.size;
        Layer third;
        third.name = QStringLiteral("C");
        third.initialCanvasSize = layers.size;
        const QUuid firstId = layers.layers.first().id;
        const QUuid secondId = second.id;
        const QUuid thirdId = third.id;
        layers.layers.append(second);
        layers.layers.append(third);
        layers.activeLayerId = secondId;

        const auto layerOrder = [](const Document &document)
        {
            QVector<QUuid> ids;
            for (const Layer &layer : document.layers)
            {
                ids.append(layer.id);
            }
            return ids;
        };

        DocumentController controller;
        controller.loadDocument(std::move(layers));
        controller.moveLayer(firstId, 2);
        QCOMPARE(layerOrder(controller.document()),
            QVector<QUuid>({secondId, thirdId, firstId}));
        QCOMPARE(controller.document().activeLayerId, secondId);

        controller.setActiveLayer(thirdId);
        controller.undoStack()->undo();
        QCOMPARE(layerOrder(controller.document()),
            QVector<QUuid>({firstId, secondId, thirdId}));
        QCOMPARE(controller.document().activeLayerId, thirdId);
        controller.undoStack()->redo();
        QCOMPARE(layerOrder(controller.document()),
            QVector<QUuid>({secondId, thirdId, firstId}));
        QCOMPARE(controller.document().activeLayerId, thirdId);

        Document strokes = Document::createDefault(QSize(96, 96));
        QVector<QUuid> originalIds;
        for (int index = 0; index < 5; ++index)
        {
            Stroke item;
            item.seed = static_cast<quint64>(index);
            item.points = {{QPointF(10.0 + index * 10.0, 40.0), 1.0}};
            originalIds.append(item.id);
            strokes.layers.first().strokes.append(std::move(item));
        }
        const QUuid strokeLayerId = strokes.activeLayerId;
        const auto strokeOrder = [strokeLayerId](const Document &document)
        {
            QVector<QUuid> ids;
            for (const Stroke &item : document.layer(strokeLayerId)->strokes)
            {
                ids.append(item.id);
            }
            return ids;
        };
        controller.loadDocument(std::move(strokes));
        controller.removeStrokes(
            strokeLayerId, {originalIds[1], originalIds[3]});
        QCOMPARE(strokeOrder(controller.document()),
            QVector<QUuid>({originalIds[0], originalIds[2], originalIds[4]}));
        controller.undoStack()->undo();
        QCOMPARE(strokeOrder(controller.document()), originalIds);
        controller.undoStack()->redo();
        QCOMPARE(strokeOrder(controller.document()),
            QVector<QUuid>({originalIds[0], originalIds[2], originalIds[4]}));
    }

    void mergesScalarCommandsFromEarliestToLatestValue()
    {
        DocumentController controller;
        controller.newDocument(QSize(96, 96));
        const QUuid layerId = controller.document().activeLayerId;

        controller.setWobbleAmount(2.0);
        controller.setWobbleAmount(3.0);
        controller.setAnimationFrames(12);
        controller.setAnimationFrames(18);
        controller.setFramesPerSecond(10.0);
        controller.setFramesPerSecond(20.0);
        controller.setLayerOpacity(layerId, 0.4);
        controller.setLayerOpacity(layerId, 0.7);
        QCOMPARE(controller.undoStack()->count(), 4);
        QCOMPARE(controller.undoStack()->storageStats().retainedStrokes,
            qsizetype(0));

        controller.undoStack()->undo();
        QCOMPARE(controller.document().layer(layerId)->opacity, 1.0);
        controller.undoStack()->undo();
        QCOMPARE(controller.document().framesPerSecond, 25.0);
        controller.undoStack()->undo();
        QCOMPARE(controller.document().animationFrames, 30);
        controller.undoStack()->undo();
        QCOMPARE(controller.document().wobbleAmount, 1.6);

        controller.undoStack()->redo();
        QCOMPARE(controller.document().wobbleAmount, 3.0);
        controller.undoStack()->redo();
        QCOMPARE(controller.document().animationFrames, 18);
        controller.undoStack()->redo();
        QCOMPARE(controller.document().framesPerSecond, 20.0);
        controller.undoStack()->redo();
        QCOMPARE(controller.document().layer(layerId)->opacity, 0.7);
    }

    void undoActionsCannotBypassHistoryPreflight()
    {
        DocumentController controller;
        controller.newDocument(QSize(96, 96));
        const QUuid layerId = controller.document().activeLayerId;
        controller.renameLayer(layerId, QStringLiteral("Renamed"));

        std::unique_ptr<QAction> undoAction(
            controller.undoStack()->createUndoAction(nullptr));
        std::unique_ptr<QAction> redoAction(
            controller.undoStack()->createRedoAction(nullptr));
        QVERIFY(undoAction->isEnabled());
        QVERIFY(!redoAction->isEnabled());

        DocumentControllerTestAccess::failHistoryPrepareAfter(controller, 0);
        undoAction->trigger();
        QCOMPARE(controller.undoStack()->index(), 1);
        QCOMPARE(controller.document().layer(layerId)->name,
            QStringLiteral("Renamed"));
        QCOMPARE(
            controller.undoStack()->storageStats().retainedPreparedDocuments,
            qsizetype(0));

        undoAction->trigger();
        QCOMPARE(controller.undoStack()->index(), 0);
        QCOMPARE(controller.document().layer(layerId)->name,
            QStringLiteral("Layer 1"));
        QVERIFY(redoAction->isEnabled());
        redoAction->trigger();
        QCOMPARE(controller.undoStack()->index(), 1);
        QCOMPARE(controller.document().layer(layerId)->name,
            QStringLiteral("Renamed"));
    }

    void keepsCleanRevisionAcrossScalarMergeBoundary()
    {
        DocumentController controller;
        controller.newDocument(QSize(96, 96));
        controller.setWobbleAmount(2.0);
        controller.markSaved();
        QVERIFY(!controller.isModified());
        QVERIFY(controller.undoStack()->isClean());

        controller.setWobbleAmount(3.0);
        QVERIFY(controller.isModified());
        QCOMPARE(controller.undoStack()->count(), 2);
        controller.undoStack()->undo();
        QCOMPARE(controller.document().wobbleAmount, 2.0);
        QVERIFY(!controller.isModified());
        QVERIFY(controller.undoStack()->isClean());
        controller.undoStack()->redo();
        QCOMPARE(controller.document().wobbleAmount, 3.0);
        QVERIFY(controller.isModified());
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
        const QJsonObject root = QJsonDocument::fromJson(serialized).object();
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
        QCOMPARE(controller.document().layers.first().name,
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
        firstLayer.id =
            QUuid(QStringLiteral("{22222222-2222-2222-2222-222222222222}"));
        firstLayer.name = QStringLiteral("Ink");
        firstLayer.opacity = 0.625;

        Stroke paint;
        paint.id =
            QUuid(QStringLiteral("{33333333-3333-3333-3333-333333333333}"));
        paint.seed = 9876543210123456789ULL;
        paint.mode = StrokeMode::Paint;
        paint.color = QColor(1, 2, 3, 4);
        paint.width = 9.75;
        paint.brush =
            BrushPresetCatalog::find(QStringLiteral("soft-airbrush"))->settings;
        paint.brush.wobbleScale = 0.35;
        paint.brush.antialiasing = true;
        paint.points = {{QPointF(1.25, 2.5), 0.2}, {QPointF(30.0, 40.0), 0.9}};
        paint.clipMask = QImage(source.size, QImage::Format_Grayscale8);
        paint.clipMask.fill(0);
        for (int y = 10; y < 40; ++y)
        {
            std::fill_n(paint.clipMask.scanLine(y) + 20, 60, 255);
        }
        firstLayer.strokes.append(paint);

        Layer secondLayer;
        secondLayer.id =
            QUuid(QStringLiteral("{44444444-4444-4444-4444-444444444444}"));
        secondLayer.name = QStringLiteral("Erase");
        secondLayer.visible = false;
        secondLayer.opacity = 0.4;

        Stroke erase;
        erase.id =
            QUuid(QStringLiteral("{55555555-5555-5555-5555-555555555555}"));
        erase.seed = 123456789;
        erase.mode = StrokeMode::Erase;
        erase.color = QColor(200, 150, 100, 50);
        erase.width = 13.5;
        erase.points = {{QPointF(6.0, 7.0), 1.0}};
        secondLayer.strokes.append(erase);
        source.layers.append(secondLayer);
        source.activeLayerId = secondLayer.id;

        QString error;
        const std::optional<Document> loaded = DocumentSerializer::fromJson(
            DocumentSerializer::toJson(source), &error);
        QVERIFY2(loaded.has_value(), qPrintable(error));
        QCOMPARE(loaded->size, source.size);
        QCOMPARE(loaded->background, source.background);
        QCOMPARE(loaded->animationFrames, source.animationFrames);
        QCOMPARE(loaded->framesPerSecond, source.framesPerSecond);
        QCOMPARE(loaded->wobbleAmount, source.wobbleAmount);
        QCOMPARE(loaded->activeLayerId, source.activeLayerId);
        QCOMPARE(loaded->layers.size(), source.layers.size());

        for (int layerIndex = 0; layerIndex < source.layers.size();
            ++layerIndex)
        {
            const Layer &actualLayer = loaded->layers[layerIndex];
            const Layer &expectedLayer = source.layers[layerIndex];
            QCOMPARE(actualLayer.id, expectedLayer.id);
            QCOMPARE(actualLayer.name, expectedLayer.name);
            QCOMPARE(actualLayer.visible, expectedLayer.visible);
            QCOMPARE(actualLayer.opacity, expectedLayer.opacity);
            QCOMPARE(actualLayer.strokes.size(), expectedLayer.strokes.size());

            for (int strokeIndex = 0;
                strokeIndex < expectedLayer.strokes.size();
                ++strokeIndex)
            {
                const Stroke &actualStroke = actualLayer.strokes[strokeIndex];
                const Stroke &expectedStroke =
                    expectedLayer.strokes[strokeIndex];
                QCOMPARE(actualStroke.id, expectedStroke.id);
                QCOMPARE(actualStroke.seed, expectedStroke.seed);
                QCOMPARE(actualStroke.mode, expectedStroke.mode);
                QCOMPARE(actualStroke.color, expectedStroke.color);
                QCOMPARE(actualStroke.width, expectedStroke.width);
                QVERIFY(actualStroke.brush == expectedStroke.brush);
                QCOMPARE(actualStroke.clipMask, expectedStroke.clipMask);
                QCOMPARE(
                    actualStroke.points.size(), expectedStroke.points.size());

                for (int pointIndex = 0;
                    pointIndex < expectedStroke.points.size();
                    ++pointIndex)
                {
                    QCOMPARE(actualStroke.points[pointIndex].position,
                        expectedStroke.points[pointIndex].position);
                    QCOMPARE(actualStroke.points[pointIndex].pressure,
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

    void preservesProceduralFillAcrossSchemaFourMigration()
    {
        Document source = Document::createDefault(QSize(64, 64));
        source.background = Qt::transparent;
        source.animationFrames = 8;
        source.wobbleAmount = 2.4;

        Stroke boundary;
        boundary.seed = 91234;
        boundary.width = 3.0;
        boundary.brush.antialiasing = false;
        boundary.points = {{QPointF(15.0, 15.0), 1.0},
            {QPointF(49.0, 15.0), 1.0},
            {QPointF(49.0, 49.0), 1.0},
            {QPointF(15.0, 49.0), 1.0},
            {QPointF(15.0, 20.0), 1.0}};
        Stroke fill;
        fill.seed = 5678;
        fill.mode = StrokeMode::Fill;
        fill.color = QColor(220, 40, 70);
        fill.points = {{QPointF(32.0, 32.0), 1.0}};
        source.layers.first().strokes = {boundary, fill};

        QJsonObject schemaFour =
            QJsonDocument::fromJson(DocumentSerializer::toJson(source))
                .object();
        schemaFour.insert(QStringLiteral("schemaVersion"), 4);
        schemaFour.remove(QStringLiteral("binaryMasks"));
        QJsonArray layers =
            schemaFour.value(QStringLiteral("layers")).toArray();
        QJsonObject layer = layers.first().toObject();
        layer.remove(QStringLiteral("initialCanvasSize"));
        layers[0] = layer;
        schemaFour.insert(QStringLiteral("layers"), layers);

        QString error;
        const std::optional<Document> migrated = DocumentSerializer::fromJson(
            QJsonDocument(schemaFour).toJson(QJsonDocument::Compact), &error);
        QVERIFY2(migrated.has_value(), qPrintable(error));
        QVERIFY(migrated->layers.first().strokes.last().fillMask.isNull());

        const QByteArray currentJson = DocumentSerializer::toJson(*migrated);
        QVERIFY(!currentJson.isEmpty());
        const QJsonObject currentRoot =
            QJsonDocument::fromJson(currentJson).object();
        QCOMPARE(currentRoot.value(QStringLiteral("schemaVersion")).toInt(), 6);
        QCOMPARE(
            currentRoot.value(QStringLiteral("algorithmVersion")).toInt(), 2);
        const std::optional<Document> reloaded =
            DocumentSerializer::fromJson(currentJson, &error);
        QVERIFY2(reloaded.has_value(), qPrintable(error));
        QVERIFY(reloaded->layers.first().strokes.last().fillMask.isNull());
        for (int frame = 0; frame < source.animationFrames; ++frame)
        {
            QCOMPARE(RenderEngine::render(*migrated, frame),
                RenderEngine::render(*reloaded, frame));
            QCOMPARE(RenderEngine::render(source, frame),
                RenderEngine::render(*reloaded, frame));
        }

        Document changed = *reloaded;
        changed.animationFrames = 12;
        changed.wobbleAmount = 5.5;
        const QByteArray changedJson = DocumentSerializer::toJson(changed);
        const std::optional<Document> changedReloaded =
            DocumentSerializer::fromJson(changedJson, &error);
        QVERIFY2(changedReloaded.has_value(), qPrintable(error));
        bool changedAtLeastOneFrame = false;
        for (int frame = 0; frame < changed.animationFrames; ++frame)
        {
            const QImage changedFrame = RenderEngine::render(changed, frame);
            QCOMPARE(
                changedFrame, RenderEngine::render(*changedReloaded, frame));
            if (changedFrame
                != RenderEngine::render(
                    *reloaded, frame % reloaded->animationFrames))
            {
                changedAtLeastOneFrame = true;
            }
        }
        QVERIFY(changedAtLeastOneFrame);
    }

    void preservesFrozenSchemaFiveFillAcrossSchemaSixRoundTrip()
    {
        Document source = Document::createDefault(QSize(32, 24));
        source.background = Qt::transparent;
        source.animationFrames = 6;
        source.wobbleAmount = 3.0;
        Stroke fill;
        fill.mode = StrokeMode::Fill;
        fill.color = QColor(20, 120, 230, 190);
        fill.points = {{QPointF(8.0, 8.0), 1.0}};
        fill.fillMask = QImage(source.size, QImage::Format_Grayscale8);
        fill.fillMask.fill(0);
        for (int y = 4; y < 18; ++y)
        {
            std::fill(fill.fillMask.scanLine(y) + 5,
                fill.fillMask.scanLine(y) + 21,
                255);
        }
        source.layers.first().strokes = {fill};

        QJsonObject schemaFive =
            QJsonDocument::fromJson(DocumentSerializer::toJson(source))
                .object();
        schemaFive.insert(QStringLiteral("schemaVersion"), 5);
        schemaFive.remove(QStringLiteral("binaryMasks"));
        QJsonArray layers =
            schemaFive.value(QStringLiteral("layers")).toArray();
        QJsonObject layer = layers.first().toObject();
        layer.remove(QStringLiteral("initialCanvasSize"));
        layers[0] = layer;
        schemaFive.insert(QStringLiteral("layers"), layers);

        QString error;
        const std::optional<Document> migrated = DocumentSerializer::fromJson(
            QJsonDocument(schemaFive).toJson(QJsonDocument::Compact), &error);
        QVERIFY2(migrated.has_value(), qPrintable(error));
        QCOMPARE(
            migrated->layers.first().strokes.first().fillMask, fill.fillMask);

        const QByteArray schemaSixJson = DocumentSerializer::toJson(*migrated);
        QVERIFY(!schemaSixJson.isEmpty());
        const QJsonObject schemaSixRoot =
            QJsonDocument::fromJson(schemaSixJson).object();
        QCOMPARE(
            schemaSixRoot.value(QStringLiteral("algorithmVersion")).toInt(), 2);
        const QJsonObject schemaSixStroke =
            schemaSixRoot.value(QStringLiteral("layers"))
                .toArray()
                .first()
                .toObject()
                .value(QStringLiteral("strokes"))
                .toArray()
                .first()
                .toObject();
        QVERIFY(schemaSixStroke.contains(QStringLiteral("fillMaskId")));
        const std::optional<Document> reloaded =
            DocumentSerializer::fromJson(schemaSixJson, &error);
        QVERIFY2(reloaded.has_value(), qPrintable(error));
        QCOMPARE(
            reloaded->layers.first().strokes.first().fillMask, fill.fillMask);
        for (int frame = 0; frame < source.animationFrames; ++frame)
        {
            QCOMPARE(RenderEngine::render(*migrated, frame),
                RenderEngine::render(*reloaded, frame));
        }
    }

    void roundTripsOrderedFramebufferOperations()
    {
        Document document = Document::createDefault(QSize(48, 40));
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;
        Stroke stroke;
        stroke.width = 5.0;
        stroke.points = {{QPointF(8.0, 18.0), 1.0}, {QPointF(28.0, 18.0), 1.0}};
        const QUuid strokeId = stroke.id;
        const QUuid layerId = document.activeLayerId;
        document.layers.first().strokes = {stroke};

        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.resizeCanvas(QSize(56, 44), QPoint(4, 2)));
        QImage selection(controller.document().size, QImage::Format_Grayscale8);
        selection.fill(0);
        for (int y = 14; y < 27; ++y)
        {
            std::fill(
                selection.scanLine(y) + 10, selection.scanLine(y) + 37, 255);
        }
        QVERIFY(controller.moveStrokes(
            layerId, {strokeId}, QPointF(6.0, 5.0), selection));

        const QVector<Stroke> &operations =
            controller.document().layers.first().strokes;
        QCOMPARE(operations.size(), 3);
        QCOMPARE(operations[1].mode, StrokeMode::Reframe);
        QCOMPARE(operations[2].mode, StrokeMode::PixelSelection);
        const QByteArray json =
            DocumentSerializer::toJson(controller.document());
        QVERIFY(!json.isEmpty());
        QString error;
        const std::optional<Document> loaded =
            DocumentSerializer::fromJson(json, &error);
        QVERIFY2(loaded.has_value(), qPrintable(error));
        const QVector<Stroke> &loadedOperations =
            loaded->layers.first().strokes;
        QCOMPARE(loadedOperations.size(), operations.size());
        QVERIFY(loadedOperations[1].reframeOp == operations[1].reframeOp);
        QVERIFY(loadedOperations[2].pixelSelectionOp
                == operations[2].pixelSelectionOp);
        for (int frame = 0; frame < controller.document().animationFrames;
            ++frame)
        {
            QCOMPARE(RenderEngine::render(controller.document(), frame),
                RenderEngine::render(*loaded, frame));
        }
    }

    void rejectsUnsafeSelectionTransformsAtomically()
    {
        QImage selection(QSize(32, 32), QImage::Format_Grayscale8);
        selection.fill(0);
        for (int y = 8; y < 24; ++y)
        {
            std::fill(
                selection.scanLine(y) + 8, selection.scanLine(y) + 24, 255);
        }

        QTransform huge;
        huge.scale(1.0e300, 1.0e300);
        QVERIFY(!makePixelSelectionOp(selection, huge, true, true));
        const QTransform projective(
            1.0, 0.0, 0.01, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0);
        QVERIFY(!makePixelSelectionOp(selection, projective, true, true));
        QTransform singular;
        singular.scale(0.0, 1.0);
        QVERIFY(!makePixelSelectionOp(selection, singular, true, true));

        Document document = Document::createDefault(selection.size());
        Stroke stroke;
        stroke.width = 4.0;
        stroke.points = {{QPointF(6.0, 16.0), 1.0}, {QPointF(26.0, 16.0), 1.0}};
        const QUuid strokeId = stroke.id;
        const QUuid layerId = document.activeLayerId;
        document.layers.first().strokes = {stroke};
        DocumentController controller;
        controller.loadDocument(document);
        const QByteArray before =
            DocumentSerializer::toJson(controller.document());
        const int undoCount = controller.undoStack()->count();
        QVERIFY(!controller.scaleStrokes(
            layerId, {strokeId}, QPointF(16.0, 16.0), 1.0e300, selection));
        QCOMPARE(controller.undoStack()->count(), undoCount);
        QCOMPARE(DocumentSerializer::toJson(controller.document()), before);

        QTransform safeTransform;
        safeTransform.translate(2.0, 1.0);
        const std::optional<PixelSelectionOp> safeOperation =
            makePixelSelectionOp(selection, safeTransform, true, true);
        QVERIFY(safeOperation.has_value());
        Stroke operationStroke;
        operationStroke.mode = StrokeMode::PixelSelection;
        operationStroke.points.clear();
        operationStroke.pixelSelectionOp = *safeOperation;
        Document operationDocument = document;
        operationDocument.layers.first().strokes.append(operationStroke);
        QJsonObject unsafeRoot = QJsonDocument::fromJson(
            DocumentSerializer::toJson(operationDocument))
                                     .object();
        QJsonArray layers =
            unsafeRoot.value(QStringLiteral("layers")).toArray();
        QJsonObject layer = layers.first().toObject();
        QJsonArray strokes = layer.value(QStringLiteral("strokes")).toArray();
        QJsonObject serializedOperation = strokes.last().toObject();
        QJsonObject payload =
            serializedOperation.value(QStringLiteral("pixelSelection"))
                .toObject();
        payload.insert(QStringLiteral("transform"),
            QJsonArray{1.0e300, 0.0, 0.0, 0.0, 1.0e300, 0.0, 0.0, 0.0, 1.0});
        serializedOperation.insert(QStringLiteral("pixelSelection"), payload);
        strokes[strokes.size() - 1] = serializedOperation;
        layer.insert(QStringLiteral("strokes"), strokes);
        layers[0] = layer;
        unsafeRoot.insert(QStringLiteral("layers"), layers);
        QString error;
        QVERIFY(!DocumentSerializer::fromJson(
            QJsonDocument(unsafeRoot).toJson(QJsonDocument::Compact), &error));
    }

    void clearsResizedLayerWithCurrentFramebufferEpoch()
    {
        Document document = Document::createDefault(QSize(30, 24));
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;
        Stroke stroke;
        stroke.width = 4.0;
        stroke.points = {{QPointF(4.0, 12.0), 1.0}, {QPointF(24.0, 12.0), 1.0}};
        const QUuid layerId = document.activeLayerId;
        document.layers.first().strokes = {stroke};
        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.resizeCanvas(QSize(42, 32), QPoint(6, 4)));
        const QImage beforeClear =
            RenderEngine::render(controller.document(), 0);

        controller.clearLayer(layerId);
        const Layer &cleared = controller.document().layers.first();
        QVERIFY(cleared.strokes.isEmpty());
        QCOMPARE(cleared.initialCanvasSize, controller.document().size);
        const QByteArray clearedJson =
            DocumentSerializer::toJson(controller.document());
        QVERIFY(!clearedJson.isEmpty());
        QString error;
        const std::optional<Document> loaded =
            DocumentSerializer::fromJson(clearedJson, &error);
        QVERIFY2(loaded.has_value(), qPrintable(error));

        DocumentController loadedController;
        loadedController.loadDocument(*loaded);
        Stroke newStroke;
        newStroke.color = Qt::red;
        newStroke.points = {
            {QPointF(8.0, 8.0), 1.0}, {QPointF(30.0, 20.0), 1.0}};
        loadedController.addStroke(layerId, newStroke);
        QVERIFY(!RenderEngine::render(loadedController.document(), 0).isNull());
        QVERIFY(
            !DocumentSerializer::toJson(loadedController.document()).isEmpty());

        controller.undoStack()->undo();
        QCOMPARE(RenderEngine::render(controller.document(), 0), beforeClear);
        controller.undoStack()->redo();
        QVERIFY(controller.document().layers.first().strokes.isEmpty());
        QCOMPARE(controller.document().layers.first().initialCanvasSize,
            QSize(42, 32));
    }

    void resizesTransparentLayerWithoutFramebufferHistory()
    {
        Document document = Document::createDefault(QSize(64, 48));
        Stroke erase;
        erase.mode = StrokeMode::Erase;
        erase.points = {{QPointF(8.0, 20.0), 1.0}, {QPointF(50.0, 20.0), 1.0}};
        document.layers.first().strokes = {erase};

        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.resizeCanvas(QSize(80, 60), QPoint(5, 4)));
        const Layer &resized = controller.document().layers.first();
        QVERIFY(resized.strokes.isEmpty());
        QCOMPARE(resized.initialCanvasSize, QSize(80, 60));
        QVERIFY(!DocumentSerializer::toJson(controller.document()).isEmpty());

        controller.undoStack()->undo();
        const Layer &restored = controller.document().layers.first();
        QCOMPARE(restored.strokes.size(), 1);
        QCOMPARE(restored.strokes.first().mode, StrokeMode::Erase);
        QCOMPARE(restored.initialCanvasSize, QSize(64, 48));
        controller.undoStack()->redo();
        QVERIFY(controller.document().layers.first().strokes.isEmpty());
    }

    void rejectsLegacyVectorEditsAfterFramebufferOperations()
    {
        Document document = Document::createDefault(QSize(30, 24));
        Stroke stroke;
        stroke.points = {{QPointF(4.0, 12.0), 1.0}, {QPointF(24.0, 12.0), 1.0}};
        const QUuid strokeId = stroke.id;
        const QUuid layerId = document.activeLayerId;
        document.layers.first().strokes = {stroke};
        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.resizeImage(QSize(45, 36)));
        const QByteArray before =
            DocumentSerializer::toJson(controller.document());
        const int undoCount = controller.undoStack()->count();

        QVERIFY(!controller.scaleStrokes(
            layerId, {strokeId}, QPointF(15.0, 12.0), 1.5));
        QVERIFY(!controller.duplicateStrokes(
            layerId, {strokeId}, QPointF(4.0, 3.0)));
        QCOMPARE(controller.undoStack()->count(), undoCount);
        QCOMPARE(DocumentSerializer::toJson(controller.document()), before);
    }

    void rejectsOverflowingSchemaSixBounds()
    {
        Document document = Document::createDefault(QSize(32, 32));
        Stroke stroke;
        stroke.points = {{QPointF(4.0, 16.0), 1.0}, {QPointF(28.0, 16.0), 1.0}};
        stroke.visibilityClip = QRect(2, 2, 28, 28);
        const QUuid strokeId = stroke.id;
        document.layers.first().strokes = {stroke};

        DocumentController controller;
        controller.loadDocument(document);
        QImage selection(document.size, QImage::Format_Grayscale8);
        selection.fill(0);
        for (int y = 8; y < 24; ++y)
        {
            std::fill(
                selection.scanLine(y) + 8, selection.scanLine(y) + 24, 255);
        }
        QVERIFY(controller.moveStrokes(
            document.activeLayerId, {strokeId}, QPointF(2.0, 0.0), selection));
        const QJsonObject validRoot = QJsonDocument::fromJson(
            DocumentSerializer::toJson(controller.document()))
                                          .object();

        QJsonObject badBinaryRoot = validRoot;
        QJsonArray binaryMasks =
            badBinaryRoot.value(QStringLiteral("binaryMasks")).toArray();
        QJsonObject binaryMask = binaryMasks.first().toObject();
        binaryMask.insert(QStringLiteral("bounds"),
            QJsonArray{std::numeric_limits<int>::max(), 0, 16, 16});
        binaryMasks[0] = binaryMask;
        badBinaryRoot.insert(QStringLiteral("binaryMasks"), binaryMasks);
        QString error;
        QVERIFY(!DocumentSerializer::fromJson(
            QJsonDocument(badBinaryRoot).toJson(QJsonDocument::Compact),
            &error));

        QJsonObject badClipRoot = validRoot;
        QJsonArray layers =
            badClipRoot.value(QStringLiteral("layers")).toArray();
        QJsonObject layer = layers.first().toObject();
        QJsonArray strokes = layer.value(QStringLiteral("strokes")).toArray();
        QJsonObject firstStroke = strokes.first().toObject();
        firstStroke.insert(QStringLiteral("visibilityClip"),
            QJsonArray{std::numeric_limits<int>::max(), 0, 16, 16});
        strokes[0] = firstStroke;
        layer.insert(QStringLiteral("strokes"), strokes);
        layers[0] = layer;
        badClipRoot.insert(QStringLiteral("layers"), layers);
        QVERIFY(!DocumentSerializer::fromJson(
            QJsonDocument(badClipRoot).toJson(QJsonDocument::Compact), &error));
    }

    void resizesImageUndoably()
    {
        Document document = Document::createDefault(QSize(100, 50));
        Stroke stroke;
        stroke.width = 10.0;
        stroke.points = {
            {QPointF(10.0, 10.0), 0.5}, {QPointF(90.0, 40.0), 1.0}};
        stroke.clipMask = QImage(document.size, QImage::Format_Grayscale8);
        stroke.clipMask.fill(255);
        document.layers.first().strokes.append(stroke);

        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.resizeImage(QSize(200, 100)));
        QCOMPARE(controller.document().size, QSize(200, 100));
        const Layer &resizedLayer = controller.document().layers.first();
        QCOMPARE(resizedLayer.strokes.size(), 2);
        QCOMPARE(resizedLayer.strokes.first().points, stroke.points);
        QCOMPARE(resizedLayer.strokes.first().width, stroke.width);
        QCOMPARE(resizedLayer.strokes.first().clipMask, stroke.clipMask);
        const Stroke &resizeOperation = resizedLayer.strokes.last();
        QCOMPARE(resizeOperation.mode, StrokeMode::Reframe);
        QVERIFY(resizeOperation.reframeOp.has_value());
        QCOMPARE(resizeOperation.reframeOp->mode, ReframeMode::Image);
        QCOMPARE(resizeOperation.reframeOp->sourceSize, QSize(100, 50));
        QCOMPARE(resizeOperation.reframeOp->targetSize, QSize(200, 100));
        QCOMPARE(resizeOperation.reframeOp->sampling, SamplingMode::Smooth);

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
        QCOMPARE(controller.document().layers.first().strokes.size(), 2);
        QVERIFY(!controller.resizeImage(QSize(0, 100)));
    }

    void keepsEdgePointsSafeWhenResizingImage()
    {
        Document document = Document::createDefault(QSize(100, 100));
        Stroke stroke;
        stroke.width = 6.0;
        stroke.points = {
            {QPointF(50.0, 50.0), 1.0}, {QPointF(100.0, 100.0), 1.0}};
        document.layers.first().strokes.append(stroke);

        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.resizeImage(QSize(109, 109)));

        const QSize size = controller.document().size;
        for (const StrokePoint &point :
            controller.document().layers.first().strokes.first().points)
        {
            QVERIFY(point.position.x() >= 0.0);
            QVERIFY(point.position.y() >= 0.0);
            QVERIFY(point.position.x() <= size.width());
            QVERIFY(point.position.y() <= size.height());
        }

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString filePath =
            directory.filePath(QStringLiteral("resized.wagle"));
        QString error;
        QVERIFY2(
            DocumentSerializer::save(filePath, controller.document(), &error),
            qPrintable(error));
    }

    void resizesCanvasByTranslatingContentUndoably()
    {
        Document document = Document::createDefault(QSize(100, 80));
        Stroke first;
        first.width = 10.0;
        first.points = {{QPointF(10.0, 20.0), 0.5}, {QPointF(90.0, 60.0), 1.0}};
        Stroke second;
        second.width = 4.0;
        second.points = {
            {QPointF(25.0, 35.0), 1.0}, {QPointF(50.0, 45.0), 1.0}};
        document.layers.first().strokes = {first, second};

        DocumentController controller;
        controller.loadDocument(document);
        QSignalSpy resizeSpy(&controller, &DocumentController::canvasResized);

        const QSize targetSize(120, 100);
        const QPoint offset(10, 15);
        QVERIFY(controller.resizeCanvas(targetSize, offset));

        QCOMPARE(controller.document().size, targetSize);
        const QVector<Stroke> &resized =
            controller.document().layers.first().strokes;
        QCOMPARE(resized.size(), 3);
        QCOMPARE(resized[0].points, first.points);
        QCOMPARE(resized[1].points, second.points);
        QCOMPARE(resized[0].width, first.width);
        QCOMPARE(resized[1].width, second.width);
        QVERIFY(resized[0].clipMask.isNull());
        QVERIFY(resized[1].clipMask.isNull());
        QCOMPARE(resized.last().mode, StrokeMode::Reframe);
        QVERIFY(resized.last().reframeOp.has_value());
        QCOMPARE(resized.last().reframeOp->mode, ReframeMode::Canvas);
        QCOMPARE(resized.last().reframeOp->sourceSize, document.size);
        QCOMPARE(resized.last().reframeOp->targetSize, targetSize);
        QCOMPARE(resized.last().reframeOp->contentOffset, offset);

        QCOMPARE(resizeSpy.size(), 1);
        QCOMPARE(resizeSpy[0][0].toSize(), document.size);
        QCOMPARE(resizeSpy[0][1].toSize(), targetSize);
        const QTransform forward = qvariant_cast<QTransform>(resizeSpy[0][2]);
        QCOMPARE(forward.map(QPointF(3.0, 4.0)), QPointF(13.0, 19.0));

        controller.undoStack()->undo();
        QCOMPARE(controller.document().size, document.size);
        const QVector<Stroke> &restored =
            controller.document().layers.first().strokes;
        QCOMPARE(restored[0].points, first.points);
        QCOMPARE(restored[1].points, second.points);
        QVERIFY(restored[0].clipMask.isNull());
        QVERIFY(restored[1].clipMask.isNull());
        QCOMPARE(resizeSpy.size(), 2);
        const QTransform inverse = qvariant_cast<QTransform>(resizeSpy[1][2]);
        QCOMPARE(inverse.map(QPointF(13.0, 19.0)), QPointF(3.0, 4.0));

        controller.undoStack()->redo();
        QCOMPARE(controller.document().size, targetSize);
        QCOMPARE(controller.document().layers.first().strokes[0].points,
            first.points);
        QCOMPARE(controller.document().layers.first().strokes.size(), 3);
    }

    void supportsSameSizeCanvasOffsetsAndOffCanvasRoundTrip()
    {
        Document document = Document::createDefault(QSize(100, 80));
        Stroke stroke;
        stroke.width = 7.0;
        stroke.points = {{QPointF(5.0, 20.0), 1.0}, {QPointF(40.0, 30.0), 1.0}};
        document.layers.first().strokes.append(stroke);

        DocumentController controller;
        controller.loadDocument(document);
        const int undoCount = controller.undoStack()->count();
        QVERIFY(!controller.resizeCanvas(document.size, QPoint()));
        QCOMPARE(controller.undoStack()->count(), undoCount);

        QVERIFY(controller.resizeCanvas(document.size, QPoint(-20, 5)));
        const Layer &translatedLayer = controller.document().layers.first();
        QCOMPARE(translatedLayer.strokes.size(), 2);
        QCOMPARE(translatedLayer.strokes.first().points, stroke.points);
        QCOMPARE(translatedLayer.strokes.first().width, stroke.width);
        QVERIFY(translatedLayer.strokes.first().clipMask.isNull());
        const Stroke &translated = translatedLayer.strokes.last();
        QCOMPARE(translated.mode, StrokeMode::Reframe);
        QVERIFY(translated.reframeOp.has_value());
        QCOMPARE(translated.reframeOp->contentOffset, QPoint(-20, 5));

        QString error;
        const QByteArray json =
            DocumentSerializer::toJson(controller.document());
        QVERIFY(!json.isEmpty());
        QCOMPARE(QJsonDocument::fromJson(json)
                     .object()
                     .value(QStringLiteral("schemaVersion"))
                     .toInt(),
            6);
        const std::optional<Document> loaded =
            DocumentSerializer::fromJson(json, &error);
        QVERIFY2(loaded.has_value(), qPrintable(error));
        QCOMPARE(loaded->layers.first().strokes.first().points, stroke.points);
        QCOMPARE(loaded->layers.first().strokes.last().reframeOp,
            translated.reframeOp);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path =
            directory.filePath(QStringLiteral("off-canvas.wagle"));
        QVERIFY2(DocumentSerializer::save(path, controller.document(), &error),
            qPrintable(error));
        const std::optional<Document> loadedFile =
            DocumentSerializer::load(path, &error);
        QVERIFY2(loadedFile.has_value(), qPrintable(error));
        QCOMPARE(
            loadedFile->layers.first().strokes.first().points, stroke.points);
    }

    void imageResizePreservesHiddenOffCanvasGeometry()
    {
        Document document = Document::createDefault(QSize(100, 80));
        Stroke stroke;
        stroke.width = 6.0;
        stroke.points = {{QPointF(5.0, 20.0), 1.0}, {QPointF(40.0, 30.0), 1.0}};
        document.layers.first().strokes.append(stroke);

        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.resizeCanvas(document.size, QPoint(-20, 0)));
        QCOMPARE(controller.document()
                     .layers.first()
                     .strokes.first()
                     .points.first()
                     .position,
            QPointF(5.0, 20.0));

        QVERIFY(controller.resizeImage(QSize(200, 160)));
        const Layer &scaledLayer = controller.document().layers.first();
        QCOMPARE(scaledLayer.strokes.size(), 3);
        const Stroke &sourceStroke = scaledLayer.strokes.first();
        QCOMPARE(sourceStroke.points, stroke.points);
        QCOMPARE(sourceStroke.width, stroke.width);
        QVERIFY(sourceStroke.clipMask.isNull());
        QCOMPARE(
            scaledLayer.strokes[1].reframeOp->contentOffset, QPoint(-20, 0));
        QCOMPARE(scaledLayer.strokes[2].reframeOp->mode, ReframeMode::Image);
    }

    void resizesCanvasWithoutLayers()
    {
        Document document = Document::createDefault(QSize(90, 70));
        document.layers.clear();
        document.activeLayerId = {};

        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.resizeCanvas(QSize(120, 100), QPoint(12, 8)));
        QCOMPARE(controller.document().size, QSize(120, 100));
        QVERIFY(controller.document().layers.isEmpty());
        QVERIFY(controller.document().activeLayerId.isNull());

        controller.undoStack()->undo();
        QCOMPARE(controller.document().size, document.size);
        QVERIFY(controller.document().layers.isEmpty());
        QVERIFY(controller.document().activeLayerId.isNull());
    }

    void canvasCropMatchesTranslatedPaintRendering()
    {
        Document document = Document::createDefault(QSize(64, 64));
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;
        Stroke stroke;
        stroke.seed = 42;
        stroke.width = 8.0;
        stroke.brush.antialiasing = false;
        stroke.points = {{QPointF(5.0, 30.0), 1.0}, {QPointF(55.0, 30.0), 1.0}};
        document.layers.first().strokes.append(stroke);

        const QImage before = RenderEngine::render(document, 0);
        QVERIFY(!before.isNull());

        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.resizeCanvas(QSize(40, 40), QPoint(-12, -8)));
        const Layer &croppedLayer = controller.document().layers.first();
        QCOMPARE(croppedLayer.strokes.size(), 2);
        QCOMPARE(croppedLayer.strokes.first().points, stroke.points);
        QCOMPARE(croppedLayer.strokes.first().width, stroke.width);
        QVERIFY(croppedLayer.strokes.last().reframeOp.has_value());
        QCOMPARE(croppedLayer.strokes.last().reframeOp->contentOffset,
            QPoint(-12, -8));

        const QImage after = RenderEngine::render(controller.document(), 0);
        QCOMPARE(after, before.copy(QRect(12, 8, 40, 40)));
    }

    void validatesStoredOffCanvasCoordinatesBySchema()
    {
        Document document = Document::createDefault(QSize(20, 20));
        Stroke stroke;
        stroke.points = {{QPointF(-1.0, 5.0), 1.0}, {QPointF(10.0, 5.0), 1.0}};
        document.layers.first().strokes.append(stroke);

        const QByteArray currentJson = DocumentSerializer::toJson(document);
        QVERIFY(!currentJson.isEmpty());
        QString error;
        const std::optional<Document> current =
            DocumentSerializer::fromJson(currentJson, &error);
        QVERIFY2(current.has_value(), qPrintable(error));

        QJsonObject legacyRoot = QJsonDocument::fromJson(currentJson).object();
        legacyRoot.insert(QStringLiteral("schemaVersion"), 4);
        const std::optional<Document> legacy = DocumentSerializer::fromJson(
            QJsonDocument(legacyRoot).toJson(QJsonDocument::Compact), &error);
        QVERIFY(!legacy.has_value());

        Document unsafe = document;
        unsafe.layers.first().strokes.first().points.first().position.setX(
            DocumentLimits::maximumStoredCoordinateMagnitude + 1.0);
        const QByteArray unsafeJson = DocumentSerializer::toJson(unsafe);
        QVERIFY(unsafeJson.isEmpty());

        QJsonObject unsafeRoot = QJsonDocument::fromJson(currentJson).object();
        QJsonArray layers =
            unsafeRoot.value(QStringLiteral("layers")).toArray();
        QJsonObject layer = layers.first().toObject();
        QJsonArray strokes = layer.value(QStringLiteral("strokes")).toArray();
        QJsonObject unsafeStroke = strokes.first().toObject();
        QJsonArray points =
            unsafeStroke.value(QStringLiteral("points")).toArray();
        QJsonArray unsafePoint = points.first().toArray();
        unsafePoint[0] = DocumentLimits::maximumStoredCoordinateMagnitude + 1.0;
        points[0] = unsafePoint;
        unsafeStroke.insert(QStringLiteral("points"), points);
        strokes[0] = unsafeStroke;
        layer.insert(QStringLiteral("strokes"), strokes);
        layers[0] = layer;
        unsafeRoot.insert(QStringLiteral("layers"), layers);
        const std::optional<Document> rejected = DocumentSerializer::fromJson(
            QJsonDocument(unsafeRoot).toJson(QJsonDocument::Compact), &error);
        QVERIFY(!rejected.has_value());
    }

    void rejectsCanvasResizeThatWouldExceedCoordinateLimitAtomically()
    {
        Document document = Document::createDefault(QSize(20, 20));
        Stroke stroke;
        stroke.points = {
            {QPointF(
                 DocumentLimits::maximumStoredCoordinateMagnitude - 1.0, 5.0),
                1.0}};
        document.layers.first().strokes.append(stroke);

        DocumentController controller;
        controller.loadDocument(document);
        const int undoCount = controller.undoStack()->count();
        QVERIFY(!controller.resizeCanvas(QSize(30, 30),
            QPoint(static_cast<int>(
                       DocumentLimits::maximumStoredCoordinateMagnitude)
                       + 1,
                0)));
        QCOMPARE(controller.undoStack()->count(), undoCount);
        QCOMPARE(controller.document().size, document.size);
        QCOMPARE(controller.document().layers.first().strokes.first().points,
            stroke.points);
        QVERIFY(controller.document()
                .layers.first()
                .strokes.first()
                .clipMask.isNull());
    }

    void transformsAndDuplicatesStrokesUndoably()
    {
        Document document = Document::createDefault(QSize(100, 100));
        Stroke stroke;
        stroke.width = 6.0;
        stroke.points = {
            {QPointF(40.0, 50.0), 1.0}, {QPointF(60.0, 50.0), 1.0}};
        const QUuid strokeId = stroke.id;
        const QUuid layerId = document.activeLayerId;
        document.layers.first().strokes.append(stroke);

        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.scaleStrokes(
            layerId, {strokeId}, QPointF(50.0, 50.0), 2.0));
        const Stroke &scaled =
            controller.document().layers.first().strokes.first();
        QCOMPARE(scaled.points.first().position, QPointF(30.0, 50.0));
        QCOMPARE(scaled.points.last().position, QPointF(70.0, 50.0));
        QCOMPARE(scaled.width, 12.0);

        controller.undoStack()->undo();
        QCOMPARE(controller.document().layers.first().strokes.first().points,
            stroke.points);

        QVERIFY(controller.rotateStrokes(
            layerId, {strokeId}, QPointF(50.0, 50.0), 90.0));
        const Stroke &rotated =
            controller.document().layers.first().strokes.first();
        QVERIFY(qAbs(rotated.points.first().position.x() - 50.0) < 0.0001);
        QVERIFY(qAbs(rotated.points.last().position.x() - 50.0) < 0.0001);
        QVERIFY(qAbs(rotated.points.first().position.y()
                     - rotated.points.last().position.y())
                > 19.999);

        controller.undoStack()->undo();
        QVERIFY(controller.duplicateStrokes(
            layerId, {strokeId}, QPointF(10.0, 5.0)));
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
            layerId, {strokeId}, QPointF(50.0, 50.0), 6.0));
        QCOMPARE(controller.document().layers.first().strokes.size(), 2);
    }

    void rotatesOnlyTheSelectedPartOfAnIntersectingStroke()
    {
        Document document = Document::createDefault(QSize(100, 100));
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;
        Stroke stroke;
        stroke.width = 4.0;
        stroke.points = {
            {QPointF(10.0, 50.0), 1.0}, {QPointF(90.0, 50.0), 1.0}};
        const QUuid strokeId = stroke.id;
        const QUuid layerId = document.activeLayerId;
        document.layers.first().strokes.append(stroke);

        QImage selection(document.size, QImage::Format_Grayscale8);
        selection.fill(0);
        for (int y = 40; y <= 60; ++y)
        {
            uchar *line = selection.scanLine(y);
            std::fill(line + 40, line + 61, 255);
        }

        const QImage before = RenderEngine::render(document, 0);
        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.rotateStrokes(
            layerId, {strokeId}, QPointF(50.0, 50.0), 90.0, selection));

        const Layer &rotatedLayer = controller.document().layers.first();
        QCOMPARE(rotatedLayer.strokes.size(), 2);
        QCOMPARE(rotatedLayer.strokes.first().id, strokeId);
        QCOMPARE(rotatedLayer.strokes.first().points, stroke.points);
        const Stroke &operation = rotatedLayer.strokes.last();
        QCOMPARE(operation.mode, StrokeMode::PixelSelection);
        QVERIFY(operation.pixelSelectionOp.has_value());
        QVERIFY(operation.pixelSelectionOp->clearSource);
        QVERIFY(operation.pixelSelectionOp->drawDestination);
        QCOMPARE(operation.pixelSelectionOp->sampling, SamplingMode::Nearest);
        const QImage after = RenderEngine::render(controller.document(), 0);
        QVERIFY(after != before);

        controller.undoStack()->undo();
        const Layer &restoredLayer = controller.document().layers.first();
        QCOMPARE(restoredLayer.strokes.size(), 1);
        QCOMPARE(restoredLayer.strokes.first().id, strokeId);
        QCOMPARE(restoredLayer.strokes.first().points, stroke.points);
        QVERIFY(restoredLayer.strokes.first().clipMask.isNull());
        QCOMPARE(RenderEngine::render(controller.document(), 0), before);

        controller.undoStack()->redo();
        QCOMPARE(controller.document().layers.first().strokes.size(), 2);
        QCOMPARE(RenderEngine::render(controller.document(), 0), after);
    }

    void movesOnlyTheSelectedPartOfAnIntersectingStroke()
    {
        Document document = Document::createDefault(QSize(100, 100));
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;
        Stroke stroke;
        stroke.width = 4.0;
        stroke.points = {
            {QPointF(10.0, 50.0), 1.0}, {QPointF(90.0, 50.0), 1.0}};
        const QUuid strokeId = stroke.id;
        const QUuid layerId = document.activeLayerId;
        document.layers.first().strokes.append(stroke);

        QImage selection(document.size, QImage::Format_Grayscale8);
        selection.fill(0);
        for (int y = 40; y <= 60; ++y)
        {
            uchar *line = selection.scanLine(y);
            std::fill(line + 40, line + 61, 255);
        }

        const QImage before = RenderEngine::render(document, 0);
        DocumentController controller;
        controller.loadDocument(document);
        QSignalSpy transitionSpy(
            &controller, &DocumentController::selectionOverlayTransition);
        QVERIFY(controller.moveStrokes(
            layerId, {strokeId}, QPointF(0.0, 20.0), selection));

        const Layer &movedLayer = controller.document().layers.first();
        QCOMPARE(movedLayer.strokes.size(), 2);
        QCOMPARE(movedLayer.strokes.first().id, strokeId);
        QCOMPARE(movedLayer.strokes.first().points, stroke.points);
        const Stroke &operation = movedLayer.strokes.last();
        QCOMPARE(operation.mode, StrokeMode::PixelSelection);
        QVERIFY(operation.pixelSelectionOp.has_value());
        QVERIFY(operation.pixelSelectionOp->clearSource);
        QVERIFY(operation.pixelSelectionOp->drawDestination);
        QCOMPARE(operation.pixelSelectionOp->transform.map(QPointF(50.0, 50.0)),
            QPointF(50.0, 70.0));
        const QImage after = RenderEngine::render(controller.document(), 0);
        QVERIFY(after != before);
        QCOMPARE(transitionSpy.size(), 1);
        QCOMPARE(qvariant_cast<QImage>(transitionSpy[0][3]), selection);
        const QImage expectedNextMask = transformedSelectionSupport(selection,
            document.size,
            operation.pixelSelectionOp->transform,
            operation.pixelSelectionOp->sampling);
        QCOMPARE(qvariant_cast<QImage>(transitionSpy[0][4]), expectedNextMask);
        const std::optional<PackedMaskRegion> packedSource =
            packBinaryMask(selection);
        const std::optional<PackedMaskRegion> packedNext =
            packBinaryMask(expectedNextMask);
        QVERIFY(packedSource.has_value());
        QVERIFY(packedNext.has_value());
        QVERIFY(packedSource->packedMask.size() + packedNext->packedMask.size()
                < selection.sizeInBytes() + expectedNextMask.sizeInBytes());

        controller.undoStack()->undo();
        const Layer &restoredLayer = controller.document().layers.first();
        QCOMPARE(restoredLayer.strokes.size(), 1);
        QCOMPARE(restoredLayer.strokes.first().points, stroke.points);
        QVERIFY(restoredLayer.strokes.first().clipMask.isNull());
        QCOMPARE(RenderEngine::render(controller.document(), 0), before);
        QCOMPARE(transitionSpy.size(), 2);
        QCOMPARE(qvariant_cast<QImage>(transitionSpy[1][3]), expectedNextMask);
        QCOMPARE(qvariant_cast<QImage>(transitionSpy[1][4]), selection);
        controller.undoStack()->redo();
        QCOMPARE(RenderEngine::render(controller.document(), 0), after);
        QCOMPARE(transitionSpy.size(), 3);
    }

    void duplicatesOnlyTheSelectedPartOfAnIntersectingStroke()
    {
        Document document = Document::createDefault(QSize(100, 100));
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;
        Stroke stroke;
        stroke.width = 4.0;
        stroke.points = {
            {QPointF(10.0, 50.0), 1.0}, {QPointF(90.0, 50.0), 1.0}};
        const QUuid strokeId = stroke.id;
        const QUuid layerId = document.activeLayerId;
        document.layers.first().strokes.append(stroke);

        QImage selection(document.size, QImage::Format_Grayscale8);
        selection.fill(0);
        for (int y = 40; y <= 60; ++y)
        {
            std::fill(
                selection.scanLine(y) + 40, selection.scanLine(y) + 61, 255);
        }

        const QImage before = RenderEngine::render(document, 0);
        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.duplicateStrokes(
            layerId, {strokeId}, QPointF(0.0, 20.0), selection));

        const Layer &duplicatedLayer = controller.document().layers.first();
        QCOMPARE(duplicatedLayer.strokes.size(), 2);
        const Stroke &original = duplicatedLayer.strokes.first();
        const Stroke &copyOperation = duplicatedLayer.strokes.last();
        QCOMPARE(original.id, strokeId);
        QCOMPARE(original.points, stroke.points);
        QVERIFY(original.clipMask.isNull());
        QCOMPARE(copyOperation.mode, StrokeMode::PixelSelection);
        QVERIFY(copyOperation.pixelSelectionOp.has_value());
        QVERIFY(!copyOperation.pixelSelectionOp->clearSource);
        QVERIFY(copyOperation.pixelSelectionOp->drawDestination);
        QCOMPARE(
            copyOperation.pixelSelectionOp->transform.map(QPointF(50.0, 50.0)),
            QPointF(50.0, 70.0));
        const QUuid copyId = copyOperation.id;
        const QImage after = RenderEngine::render(controller.document(), 0);
        QVERIFY(after != before);

        controller.undoStack()->undo();
        const Layer &restoredLayer = controller.document().layers.first();
        QCOMPARE(restoredLayer.strokes.size(), 1);
        QCOMPARE(restoredLayer.strokes.first().id, strokeId);
        QCOMPARE(restoredLayer.strokes.first().points, stroke.points);
        QVERIFY(restoredLayer.strokes.first().clipMask.isNull());
        QCOMPARE(RenderEngine::render(controller.document(), 0), before);

        controller.undoStack()->redo();
        const Layer &redoneLayer = controller.document().layers.first();
        QCOMPARE(redoneLayer.strokes.size(), 2);
        QCOMPARE(redoneLayer.strokes.first().id, strokeId);
        QCOMPARE(redoneLayer.strokes.first().points, stroke.points);
        QVERIFY(redoneLayer.strokes.first().clipMask.isNull());
        QCOMPARE(redoneLayer.strokes.last().id, copyId);
        QCOMPARE(redoneLayer.strokes.last().mode, StrokeMode::PixelSelection);
        QCOMPARE(RenderEngine::render(controller.document(), 0), after);
    }

    void removesOnlySelectedContentFromAnIntersectingStroke()
    {
        Document document = Document::createDefault(QSize(100, 100));
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;
        Stroke stroke;
        stroke.width = 4.0;
        stroke.points = {
            {QPointF(10.0, 50.0), 1.0}, {QPointF(90.0, 50.0), 1.0}};
        const QUuid strokeId = stroke.id;
        const QUuid layerId = document.activeLayerId;
        document.layers.first().strokes.append(stroke);

        QImage selection(document.size, QImage::Format_Grayscale8);
        selection.fill(0);
        for (int y = 40; y <= 60; ++y)
        {
            std::fill(
                selection.scanLine(y) + 40, selection.scanLine(y) + 61, 255);
        }

        const QImage before = RenderEngine::render(document, 0);
        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(
            controller.removeSelectedContent(layerId, {strokeId}, selection));

        const Layer &trimmedLayer = controller.document().layers.first();
        QCOMPARE(trimmedLayer.strokes.size(), 2);
        const Stroke &remainder = trimmedLayer.strokes.first();
        QCOMPARE(remainder.id, strokeId);
        QCOMPARE(remainder.points, stroke.points);
        QVERIFY(remainder.clipMask.isNull());
        const Stroke &deleteOperation = trimmedLayer.strokes.last();
        QCOMPARE(deleteOperation.mode, StrokeMode::PixelSelection);
        QVERIFY(deleteOperation.pixelSelectionOp.has_value());
        QVERIFY(deleteOperation.pixelSelectionOp->clearSource);
        QVERIFY(!deleteOperation.pixelSelectionOp->drawDestination);
        const QImage after = RenderEngine::render(controller.document(), 0);
        QVERIFY(after != before);

        controller.undoStack()->undo();
        const Layer &restoredLayer = controller.document().layers.first();
        QCOMPARE(restoredLayer.strokes.size(), 1);
        QCOMPARE(restoredLayer.strokes.first().id, strokeId);
        QCOMPARE(restoredLayer.strokes.first().points, stroke.points);
        QVERIFY(restoredLayer.strokes.first().clipMask.isNull());
        QCOMPARE(RenderEngine::render(controller.document(), 0), before);

        controller.undoStack()->redo();
        const Layer &redoneLayer = controller.document().layers.first();
        QCOMPARE(redoneLayer.strokes.size(), 2);
        QCOMPARE(redoneLayer.strokes.first().id, strokeId);
        QCOMPARE(redoneLayer.strokes.first().points, stroke.points);
        QCOMPARE(redoneLayer.strokes.last().mode, StrokeMode::PixelSelection);
        QCOMPARE(RenderEngine::render(controller.document(), 0), after);
    }

    void flipsOnlySelectedContentHorizontallyAndVertically()
    {
        Document horizontalDocument = Document::createDefault(QSize(100, 100));
        horizontalDocument.background = Qt::transparent;
        horizontalDocument.wobbleAmount = 0.0;
        Stroke horizontalStroke;
        horizontalStroke.width = 4.0;
        horizontalStroke.points = {
            {QPointF(10.0, 45.0), 1.0}, {QPointF(90.0, 55.0), 1.0}};
        const QUuid horizontalId = horizontalStroke.id;
        const QUuid horizontalLayer = horizontalDocument.activeLayerId;
        horizontalDocument.layers.first().strokes.append(horizontalStroke);
        QImage horizontalSelection(
            horizontalDocument.size, QImage::Format_Grayscale8);
        horizontalSelection.fill(0);
        for (int y = 35; y < 65; ++y)
        {
            std::fill(horizontalSelection.scanLine(y) + 20,
                horizontalSelection.scanLine(y) + 41,
                255);
        }

        const QImage horizontalBefore =
            RenderEngine::render(horizontalDocument, 0);
        DocumentController horizontalController;
        horizontalController.loadDocument(horizontalDocument);
        QVERIFY(horizontalController.flipStrokes(horizontalLayer,
            {horizontalId},
            QPointF(50.0, 50.0),
            true,
            horizontalSelection));
        const Layer &horizontallyFlipped =
            horizontalController.document().layers.first();
        QCOMPARE(horizontallyFlipped.strokes.size(), 2);
        QCOMPARE(horizontallyFlipped.strokes.first().points,
            horizontalStroke.points);
        const Stroke &horizontalOperation = horizontallyFlipped.strokes.last();
        QCOMPARE(horizontalOperation.mode, StrokeMode::PixelSelection);
        QVERIFY(horizontalOperation.pixelSelectionOp.has_value());
        QVERIFY(horizontalOperation.pixelSelectionOp->clearSource);
        QVERIFY(horizontalOperation.pixelSelectionOp->drawDestination);
        QCOMPARE(horizontalOperation.pixelSelectionOp->transform.map(
                     QPointF(30.0, 50.0)),
            QPointF(70.0, 50.0));
        const QImage horizontalRendered =
            RenderEngine::render(horizontalController.document(), 0);
        QVERIFY(horizontalRendered != horizontalBefore);
        const QByteArray horizontalAfter =
            DocumentSerializer::toJson(horizontalController.document());
        QVERIFY(!horizontalAfter.isEmpty());

        horizontalController.undoStack()->undo();
        QCOMPARE(
            horizontalController.document().layers.first().strokes.size(), 1);
        QCOMPARE(horizontalController.document()
                     .layers.first()
                     .strokes.first()
                     .points,
            horizontalStroke.points);
        QVERIFY(horizontalController.document()
                .layers.first()
                .strokes.first()
                .clipMask.isNull());
        QCOMPARE(RenderEngine::render(horizontalController.document(), 0),
            horizontalBefore);
        horizontalController.undoStack()->redo();
        QCOMPARE(DocumentSerializer::toJson(horizontalController.document()),
            horizontalAfter);

        Document verticalDocument = Document::createDefault(QSize(100, 100));
        verticalDocument.background = Qt::transparent;
        verticalDocument.wobbleAmount = 0.0;
        Stroke verticalStroke;
        verticalStroke.width = 4.0;
        verticalStroke.points = {
            {QPointF(45.0, 10.0), 1.0}, {QPointF(55.0, 90.0), 1.0}};
        const QUuid verticalId = verticalStroke.id;
        const QUuid verticalLayer = verticalDocument.activeLayerId;
        verticalDocument.layers.first().strokes.append(verticalStroke);
        QImage verticalSelection(
            verticalDocument.size, QImage::Format_Grayscale8);
        verticalSelection.fill(0);
        for (int y = 20; y < 41; ++y)
        {
            std::fill(verticalSelection.scanLine(y) + 35,
                verticalSelection.scanLine(y) + 65,
                255);
        }

        const QImage verticalBefore = RenderEngine::render(verticalDocument, 0);
        DocumentController verticalController;
        verticalController.loadDocument(verticalDocument);
        QVERIFY(verticalController.flipStrokes(verticalLayer,
            {verticalId},
            QPointF(50.0, 50.0),
            false,
            verticalSelection));
        const Layer &verticallyFlipped =
            verticalController.document().layers.first();
        QCOMPARE(verticallyFlipped.strokes.size(), 2);
        QCOMPARE(
            verticallyFlipped.strokes.first().points, verticalStroke.points);
        const Stroke &verticalOperation = verticallyFlipped.strokes.last();
        QCOMPARE(verticalOperation.mode, StrokeMode::PixelSelection);
        QVERIFY(verticalOperation.pixelSelectionOp.has_value());
        QVERIFY(verticalOperation.pixelSelectionOp->clearSource);
        QVERIFY(verticalOperation.pixelSelectionOp->drawDestination);
        QCOMPARE(verticalOperation.pixelSelectionOp->transform.map(
                     QPointF(50.0, 30.0)),
            QPointF(50.0, 70.0));
        const QImage verticalRendered =
            RenderEngine::render(verticalController.document(), 0);
        QVERIFY(verticalRendered != verticalBefore);
        const QByteArray verticalAfter =
            DocumentSerializer::toJson(verticalController.document());
        QVERIFY(!verticalAfter.isEmpty());

        verticalController.undoStack()->undo();
        QCOMPARE(
            verticalController.document().layers.first().strokes.size(), 1);
        QCOMPARE(
            verticalController.document().layers.first().strokes.first().points,
            verticalStroke.points);
        QVERIFY(verticalController.document()
                .layers.first()
                .strokes.first()
                .clipMask.isNull());
        QCOMPARE(RenderEngine::render(verticalController.document(), 0),
            verticalBefore);
        verticalController.undoStack()->redo();
        QCOMPARE(DocumentSerializer::toJson(verticalController.document()),
            verticalAfter);
    }

    void movesClipMaskAlongWithFullyContainedStroke()
    {
        Document document = Document::createDefault(QSize(100, 100));
        Stroke stroke;
        stroke.width = 4.0;
        stroke.points = {
            {QPointF(45.0, 50.0), 1.0}, {QPointF(55.0, 50.0), 1.0}};
        stroke.clipMask = QImage(document.size, QImage::Format_Grayscale8);
        stroke.clipMask.fill(0);
        for (int y = 40; y <= 60; ++y)
        {
            uchar *line = stroke.clipMask.scanLine(y);
            std::fill(line + 40, line + 61, 255);
        }
        const QUuid strokeId = stroke.id;
        const QUuid layerId = document.activeLayerId;
        document.layers.first().strokes.append(stroke);

        QImage selection(document.size, QImage::Format_Grayscale8);
        selection.fill(0);
        for (int y = 30; y <= 70; ++y)
        {
            uchar *line = selection.scanLine(y);
            std::fill(line + 30, line + 71, 255);
        }

        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.moveStrokes(
            layerId, {strokeId}, QPointF(20.0, 0.0), selection));

        const Layer &movedLayer = controller.document().layers.first();
        QCOMPARE(movedLayer.strokes.size(), 2);
        const Stroke &source = movedLayer.strokes.first();
        QCOMPARE(source.points, stroke.points);
        QCOMPARE(source.clipMask, stroke.clipMask);
        const Stroke &operation = movedLayer.strokes.last();
        QCOMPARE(operation.mode, StrokeMode::PixelSelection);
        QVERIFY(operation.pixelSelectionOp.has_value());
        QCOMPARE(operation.pixelSelectionOp->transform.map(QPointF(50.0, 50.0)),
            QPointF(70.0, 50.0));
    }

    void loadsBundledExample()
    {
        QString error;
        const QString path = QStringLiteral(WOBBLEPAINT_SOURCE_DIR)
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
            DocumentSerializer::save(path, source, &error), qPrintable(error));
        const std::optional<Document> loaded =
            DocumentSerializer::load(path, &error);
        QVERIFY2(loaded.has_value(), qPrintable(error));
        QCOMPARE(loaded->size, source.size);
        QCOMPARE(loaded->wobbleAmount, source.wobbleAmount);
        QCOMPARE(loaded->activeLayerId, source.activeLayerId);
    }

    void preparesExactSerializationAtTheByteLimit()
    {
        Document source = Document::createDefault(QSize(65, 17));
        source.layers.first().name = QStringLiteral("레이어 \"A\"\n日本語");

        QImage clipMask(source.size, QImage::Format_Grayscale8);
        clipMask.fill(0);
        for (int y = 2; y < 15; ++y)
        {
            std::fill_n(clipMask.scanLine(y) + 4, 41, static_cast<uchar>(255));
        }
        Stroke paint;
        paint.points = {
            {QPointF(4.25, 3.5), 0.5}, {QPointF(44.75, 13.25), 1.0}};
        paint.clipMask = clipMask;
        source.layers.first().strokes.append(paint);

        QImage selection(source.size, QImage::Format_Grayscale8);
        selection.fill(0);
        for (int y = 5; y < 12; ++y)
        {
            std::fill_n(
                selection.scanLine(y) + 10, 23, static_cast<uchar>(255));
        }
        const std::optional<PixelSelectionOp> operation = makePixelSelectionOp(
            selection, QTransform::fromTranslate(1.5, -0.5), true, true);
        QVERIFY(operation.has_value());
        Stroke selectionStroke;
        selectionStroke.mode = StrokeMode::PixelSelection;
        selectionStroke.points.clear();
        selectionStroke.pixelSelectionOp = *operation;
        source.layers.first().strokes.append(selectionStroke);

        DocumentSerializer::SerializationCache cache;
        QString error;
        const auto prepared =
            DocumentSerializer::prepare(source, cache, &error);
        QVERIFY2(prepared.has_value(), qPrintable(error));
        const QByteArray preparedJson =
            DocumentSerializer::toJson(*prepared, cache);
        QVERIFY(!preparedJson.isEmpty());
        QCOMPARE(
            prepared->compactSize(), static_cast<qint64>(preparedJson.size()));
        QCOMPARE(DocumentSerializer::toJson(source), preparedJson);

        DocumentSerializer::SerializationCache exactCache;
        const auto exact = DocumentSerializer::prepare(
            source, exactCache, nullptr, preparedJson.size(), &error);
        QVERIFY2(exact.has_value(), qPrintable(error));
        QCOMPARE(exact->compactSize(), prepared->compactSize());

        DocumentSerializer::SerializationCache smallCache;
        error.clear();
        const auto tooSmall = DocumentSerializer::prepare(
            source, smallCache, nullptr, preparedJson.size() - 1, &error);
        QVERIFY(!tooSmall.has_value());
        QVERIFY(!error.isEmpty());

        error.clear();
        const auto aboveHardLimit = DocumentSerializer::prepare(source,
            smallCache,
            nullptr,
            DocumentLimits::maximumProjectBytes + 1,
            &error);
        QVERIFY(!aboveHardLimit.has_value());
        QVERIFY(!error.isEmpty());
    }

    void freezesPreparedDocumentsAgainstRawCowAliases()
    {
        Document source = Document::createDefault(QSize(8, 8));
        source.layers.first().name = QStringLiteral("Layer");

        Stroke paint;
        paint.width = 5.0;
        paint.points = {{QPointF(1.0, 1.0), 0.5}, {QPointF(6.0, 6.0), 1.0}};
        paint.clipMask = QImage(source.size, QImage::Format_Grayscale8);
        paint.clipMask.fill(0);
        paint.clipMask.scanLine(0)[0] = 0xff;

        Stroke selection;
        selection.mode = StrokeMode::PixelSelection;
        selection.points.clear();
        PixelSelectionOp operation;
        operation.canvasSize = source.size;
        operation.sourceBounds = QRect(0, 0, 8, 1);
        operation.packedMask = QByteArray(1, static_cast<char>(0x80));
        operation.sampling = SamplingMode::Nearest;
        selection.pixelSelectionOp = operation;
        source.layers.first().strokes = {paint, selection};

        QChar *rawName = source.layers.first().name.data();
        Stroke *rawStrokes = source.layers.first().strokes.data();
        StrokePoint *rawPoints = rawStrokes[0].points.data();
        uchar *rawImage = rawStrokes[0].clipMask.bits();
        char *rawPacked = rawStrokes[1].pixelSelectionOp->packedMask.data();

        DocumentSerializer::SerializationCache cache;
        const auto prepared = DocumentSerializer::prepare(source, cache);
        QVERIFY(prepared.has_value());
        const QByteArray originalJson =
            DocumentSerializer::toJson(*prepared, cache);
        QVERIFY(!originalJson.isEmpty());

        rawName[0] = QChar(u'X');
        rawStrokes[0].width = 7.0;
        rawPoints[0].pressure = 0.75;
        rawImage[0] = 0;
        rawPacked[0] = static_cast<char>(0x40);

        const Layer &frozenLayer = prepared->document().layers.first();
        QCOMPARE(frozenLayer.name, QStringLiteral("Layer"));
        QCOMPARE(frozenLayer.strokes[0].width, 5.0);
        QCOMPARE(frozenLayer.strokes[0].points[0].pressure, 0.5);
        QCOMPARE(frozenLayer.strokes[0].clipMask.constScanLine(0)[0],
            static_cast<uchar>(0xff));
        QCOMPARE(frozenLayer.strokes[1].pixelSelectionOp->packedMask[0],
            static_cast<char>(0x80));

        DocumentSerializer::SerializationCache freshCache;
        const QByteArray frozenJson =
            DocumentSerializer::toJson(*prepared, freshCache);
        QCOMPARE(frozenJson, originalJson);
        QString error;
        QVERIFY2(DocumentSerializer::fromJson(frozenJson, &error).has_value(),
            qPrintable(error));
    }

    void reusesPreparedMetadataTopologyExactly()
    {
        Document source = Document::createDefault(QSize(48, 32));
        Stroke firstStroke;
        firstStroke.points = {{QPointF(4.0, 5.0), 1.0}};
        source.layers.first().strokes.append(firstStroke);

        Layer secondLayer;
        secondLayer.name = QStringLiteral("Second");
        secondLayer.initialCanvasSize = source.size;
        Stroke secondStroke;
        secondStroke.points = {{QPointF(20.0, 12.0), 0.75}};
        secondLayer.strokes.append(secondStroke);
        source.layers.append(secondLayer);

        DocumentSerializer::SerializationCache cache;
        const auto base = DocumentSerializer::prepare(source, cache);
        QVERIFY(base.has_value());

        Document changed = base->document();
        changed.layers.swapItemsAt(0, 1);
        changed.layers.first().name = QStringLiteral("이름 \"변경\"\n日本語");
        changed.layers.first().visible = false;
        changed.layers.first().opacity = 0.625;
        changed.background = QColor(QStringLiteral("#123456"));
        changed.animationFrames = 22;
        changed.framesPerSecond = 18.5;
        changed.wobbleAmount = 2.25;
        changed.activeLayerId = changed.layers.first().id;
        const QString expectedName = changed.layers.first().name;
        QChar *rawChangedName = changed.layers.first().name.data();

        cache.resetStats();
        QString error;
        const auto prepared = DocumentSerializer::prepare(changed,
            cache,
            &*base,
            DocumentLimits::maximumProjectBytes,
            &error);
        QVERIFY2(prepared.has_value(), qPrintable(error));
        const auto stats = cache.stats();
        QCOMPARE(stats.clipMaskContentHashes, 0ULL);
        QCOMPARE(stats.clipMaskCompressions, 0ULL);
        QCOMPARE(stats.binaryMaskContentHashes, 0ULL);
        QCOMPARE(stats.binaryMaskCompressions, 0ULL);
        QCOMPARE(stats.strokeSerializations, 0ULL);
        QCOMPARE(prepared->document().layers.first().id, secondLayer.id);
        QCOMPARE(prepared->document().layers.first().name, expectedName);
        QCOMPARE(prepared->document().activeLayerId, secondLayer.id);
        for (const Layer &baseLayer : base->document().layers)
        {
            const Layer *preparedLayer =
                prepared->document().layer(baseLayer.id);
            QVERIFY(preparedLayer);
            QVERIFY(preparedLayer->strokes.constData()
                    == baseLayer.strokes.constData());
        }

        const QByteArray json = DocumentSerializer::toJson(*prepared, cache);
        QVERIFY(!json.isEmpty());
        QCOMPARE(prepared->compactSize(), static_cast<qint64>(json.size()));
        const std::optional<Document> decoded =
            DocumentSerializer::fromJson(json, &error);
        QVERIFY2(decoded.has_value(), qPrintable(error));
        QCOMPARE(decoded->layers.first().name, expectedName);
        QCOMPARE(decoded->background, changed.background);
        QCOMPARE(decoded->animationFrames, 22);
        QCOMPARE(decoded->framesPerSecond, 18.5);
        QCOMPARE(decoded->wobbleAmount, 2.25);

        rawChangedName[0] = QChar(u'X');
        QCOMPARE(prepared->document().layers.first().name, expectedName);

        const auto exact = DocumentSerializer::prepare(prepared->document(),
            cache,
            &*base,
            prepared->compactSize(),
            &error);
        QVERIFY2(exact.has_value(), qPrintable(error));
        const auto tooSmall = DocumentSerializer::prepare(prepared->document(),
            cache,
            &*base,
            prepared->compactSize() - 1,
            &error);
        QVERIFY(!tooSmall.has_value());

        Document invalid = base->document();
        invalid.layers.first().opacity =
            std::numeric_limits<qreal>::quiet_NaN();
        QVERIFY(!DocumentSerializer::prepare(std::move(invalid),
            cache,
            &*base,
            DocumentLimits::maximumProjectBytes)
                .has_value());
    }

    void doesNotReuseDetachedWritableStrokeBacking()
    {
        Document source = Document::createDefault(QSize(32, 32));
        Stroke stroke;
        stroke.width = 5.0;
        stroke.points = {{QPointF(8.0, 9.0), 0.5}};
        source.layers.first().strokes.append(stroke);

        DocumentSerializer::SerializationCache cache;
        const auto base = DocumentSerializer::prepare(source, cache);
        QVERIFY(base.has_value());
        const Stroke *baseStrokes =
            base->document().layers.first().strokes.constData();

        Document changed = base->document();
        Stroke *writableStrokes = changed.layers.first().strokes.data();
        QVERIFY(writableStrokes != baseStrokes);
        writableStrokes[0].width = 8.0;

        cache.resetStats();
        const auto prepared = DocumentSerializer::prepare(
            changed, cache, &*base, DocumentLimits::maximumProjectBytes);
        QVERIFY(prepared.has_value());
        QCOMPARE(cache.stats().strokeSerializations, 1ULL);
        QCOMPARE(base->document().layers.first().strokes.first().width, 5.0);
        QCOMPARE(
            prepared->document().layers.first().strokes.first().width, 8.0);
        writableStrokes[0].width = 11.0;
        QCOMPARE(
            prepared->document().layers.first().strokes.first().width, 8.0);

        const QByteArray json = DocumentSerializer::toJson(*prepared, cache);
        QVERIFY(!json.isEmpty());
        QCOMPARE(prepared->compactSize(), static_cast<qint64>(json.size()));
    }

    void reusesPreparedMetadataWithoutLayers()
    {
        Document source;
        source.size = QSize(64, 48);
        source.layers.clear();
        source.activeLayerId = {};

        DocumentSerializer::SerializationCache cache;
        const auto base = DocumentSerializer::prepare(source, cache);
        QVERIFY(base.has_value());
        Document changed = base->document();
        changed.background = Qt::transparent;
        changed.wobbleAmount = 3.5;

        cache.resetStats();
        const auto prepared = DocumentSerializer::prepare(std::move(changed),
            cache,
            &*base,
            DocumentLimits::maximumProjectBytes);
        QVERIFY(prepared.has_value());
        QVERIFY(prepared->document().layers.isEmpty());
        QVERIFY(prepared->document().activeLayerId.isNull());
        QCOMPARE(cache.stats().strokeSerializations, 0ULL);
        const QByteArray json = DocumentSerializer::toJson(*prepared, cache);
        QVERIFY(!json.isEmpty());
        QCOMPARE(prepared->compactSize(), static_cast<qint64>(json.size()));
    }

    void reusesPreparedClipMaskAndStrokeMetadata()
    {
        Document source = Document::createDefault(QSize(257, 129));
        QImage mask(source.size, QImage::Format_Grayscale8);
        quint32 random = 0x9e3779b9U;
        for (int y = 0; y < mask.height(); ++y)
        {
            uchar *line = mask.scanLine(y);
            for (int x = 0; x < mask.width(); ++x)
            {
                random ^= random << 13U;
                random ^= random >> 17U;
                random ^= random << 5U;
                line[x] = static_cast<uchar>(random & 0xffU);
            }
        }
        Stroke masked;
        masked.points = {
            {QPointF(5.0, 5.0), 0.5}, {QPointF(200.0, 100.0), 1.0}};
        masked.clipMask = mask;
        source.layers.first().strokes.append(masked);

        DocumentSerializer::SerializationCache cache;
        const auto first = DocumentSerializer::prepare(source, cache);
        QVERIFY(first.has_value());
        const QByteArray firstJson = DocumentSerializer::toJson(*first, cache);
        QVERIFY(!firstJson.isEmpty());

        Document appended = first->document();
        Stroke plain;
        plain.points = {
            {QPointF(10.0, 20.0), 1.0}, {QPointF(40.0, 60.0), 0.75}};
        appended.layers.first().strokes.append(plain);
        cache.resetStats();
        const auto second = DocumentSerializer::prepare(
            appended, cache, &*first, DocumentLimits::maximumProjectBytes);
        QVERIFY(second.has_value());
        const auto appendStats = cache.stats();
        QCOMPARE(appendStats.clipMaskContentHashes, 0ULL);
        QCOMPARE(appendStats.clipMaskCompressions, 0ULL);
        QCOMPARE(appendStats.strokeSerializations, 1ULL);

        Document duplicated = first->document();
        Stroke duplicate = duplicated.layers.first().strokes.first();
        duplicate.id = QUuid::createUuid();
        duplicated.layers.first().strokes.append(std::move(duplicate));
        cache.resetStats();
        const auto duplicatedPrepared =
            DocumentSerializer::prepare(std::move(duplicated),
                cache,
                &*first,
                DocumentLimits::maximumProjectBytes);
        QVERIFY(duplicatedPrepared.has_value());
        const auto duplicateStats = cache.stats();
        QCOMPARE(duplicateStats.clipMaskContentHashes, 0ULL);
        QCOMPARE(duplicateStats.clipMaskCompressions, 0ULL);
        QCOMPARE(duplicateStats.strokeSerializations, 1ULL);

        Document equalContent = first->document();
        const qint64 originalKey =
            equalContent.layers.first().strokes.first().clipMask.cacheKey();
        equalContent.layers.first().strokes.first().clipMask =
            equalContent.layers.first().strokes.first().clipMask.copy();
        QVERIFY(equalContent.layers.first().strokes.first().clipMask.cacheKey()
                != originalKey);
        cache.resetStats();
        const auto equalPrepared = DocumentSerializer::prepare(
            equalContent, cache, &*first, DocumentLimits::maximumProjectBytes);
        QVERIFY(equalPrepared.has_value());
        const auto equalStats = cache.stats();
        QCOMPARE(equalStats.clipMaskContentHashes, 1ULL);
        QCOMPARE(equalStats.clipMaskCompressions, 0ULL);
        QCOMPARE(equalStats.strokeSerializations, 1ULL);
        QCOMPARE(DocumentSerializer::toJson(*equalPrepared, cache), firstJson);

        Document changed = first->document();
        QImage &changedMask = changed.layers.first().strokes.first().clipMask;
        const uchar previous = changedMask.constScanLine(0)[0];
        changedMask.scanLine(0)[0] = static_cast<uchar>(previous ^ 0xffU);
        cache.resetStats();
        const auto changedPrepared = DocumentSerializer::prepare(
            changed, cache, &*first, DocumentLimits::maximumProjectBytes);
        QVERIFY(changedPrepared.has_value());
        const auto changedStats = cache.stats();
        QCOMPARE(changedStats.clipMaskContentHashes, 1ULL);
        QCOMPARE(changedStats.clipMaskCompressions, 1ULL);
        QCOMPARE(changedStats.strokeSerializations, 1ULL);
        QCOMPARE(DocumentSerializer::toJson(*first, cache), firstJson);
        QVERIFY(
            DocumentSerializer::toJson(*changedPrepared, cache) != firstJson);
    }

    void distinguishesSharedBinaryMaskGeometryAndCowChanges()
    {
        Document source = Document::createDefault(QSize(16, 16));
        QByteArray packed(2, '\0');
        packed[0] = static_cast<char>(0x80);
        packed[1] = static_cast<char>(0x40);

        PixelSelectionOp firstOperation;
        firstOperation.canvasSize = source.size;
        firstOperation.sourceBounds = QRect(0, 0, 8, 2);
        firstOperation.packedMask = packed;
        firstOperation.sampling = SamplingMode::Nearest;
        PixelSelectionOp secondOperation = firstOperation;
        secondOperation.sourceBounds = QRect(0, 4, 16, 1);
        secondOperation.packedMask = firstOperation.packedMask;
        QVERIFY(firstOperation.packedMask.constData()
                == secondOperation.packedMask.constData());

        Stroke firstStroke;
        firstStroke.mode = StrokeMode::PixelSelection;
        firstStroke.points.clear();
        firstStroke.pixelSelectionOp = firstOperation;
        Stroke secondStroke;
        secondStroke.mode = StrokeMode::PixelSelection;
        secondStroke.points.clear();
        secondStroke.pixelSelectionOp = secondOperation;
        source.layers.first().strokes = {firstStroke, secondStroke};

        DocumentSerializer::SerializationCache cache;
        const auto prepared = DocumentSerializer::prepare(source, cache);
        QVERIFY(prepared.has_value());
        const auto initialStats = cache.stats();
        QCOMPARE(initialStats.binaryMaskContentHashes, 2ULL);
        QCOMPARE(initialStats.binaryMaskCompressions, 2ULL);
        const QByteArray originalJson =
            DocumentSerializer::toJson(*prepared, cache);
        const QJsonArray masks = QJsonDocument::fromJson(originalJson)
                                     .object()
                                     .value(QStringLiteral("binaryMasks"))
                                     .toArray();
        QCOMPARE(masks.size(), 2);

        Document changed = prepared->document();
        QByteArray &changedBytes =
            changed.layers.first().strokes.first().pixelSelectionOp->packedMask;
        changedBytes[0] = static_cast<char>(0x20);
        cache.resetStats();
        const auto changedPrepared = DocumentSerializer::prepare(
            changed, cache, &*prepared, DocumentLimits::maximumProjectBytes);
        QVERIFY(changedPrepared.has_value());
        const auto changedStats = cache.stats();
        QCOMPARE(changedStats.binaryMaskContentHashes, 1ULL);
        QCOMPARE(changedStats.binaryMaskCompressions, 1ULL);
        QCOMPARE(changedStats.strokeSerializations, 1ULL);
        QCOMPARE(DocumentSerializer::toJson(*prepared, cache), originalJson);
        QVERIFY(DocumentSerializer::toJson(*changedPrepared, cache)
                != originalJson);

        DocumentSerializer::SerializationCache tinyCache(1);
        const auto tinyPrepared =
            DocumentSerializer::prepare(source, tinyCache);
        QVERIFY(tinyPrepared.has_value());
        QCOMPARE(tinyCache.payloadBytes(), 0);
        tinyCache.resetStats();
        const QByteArray tinyJson =
            DocumentSerializer::toJson(*tinyPrepared, tinyCache);
        QCOMPARE(tinyJson, originalJson);
        QCOMPARE(tinyCache.stats().binaryMaskCompressions, 2ULL);
        QCOMPARE(tinyCache.payloadBytes(), 0);

        const qint64 onePayloadBytes = qCompress(packed, 6).size();
        DocumentSerializer::SerializationCache lruCache(onePayloadBytes);
        const auto lruPrepared = DocumentSerializer::prepare(source, lruCache);
        QVERIFY(lruPrepared.has_value());
        QVERIFY(lruCache.payloadBytes() > 0);
        QVERIFY(lruCache.payloadBytes() <= lruCache.payloadCapacityBytes());
        lruCache.resetStats();
        QCOMPARE(
            DocumentSerializer::toJson(*lruPrepared, lruCache), originalJson);
        QCOMPARE(lruCache.stats().binaryMaskCompressions, 2ULL);
        QVERIFY(lruCache.payloadBytes() <= lruCache.payloadCapacityBytes());

        DocumentSerializer::SerializationCache cappedCache(
            DocumentSerializer::SerializationCache::maximumPayloadBytes * 2);
        QCOMPARE(cappedCache.payloadCapacityBytes(),
            DocumentSerializer::SerializationCache::maximumPayloadBytes);
    }

    void cachesFourKMaskAcrossAccumulatedStrokes()
    {
        constexpr int edge = 4096;
        Document source = Document::createDefault(QSize(edge, edge));
        PixelSelectionOp operation;
        operation.canvasSize = source.size;
        operation.sourceBounds = QRect(QPoint(), source.size);
        operation.packedMask =
            QByteArray(static_cast<qsizetype>(edge) * edge / 8, '\0');
        quint32 random = 0x6d2b79f5U;
        for (char &byte : operation.packedMask)
        {
            random ^= random << 13U;
            random ^= random >> 17U;
            random ^= random << 5U;
            byte = static_cast<char>(random & 0xffU);
        }
        operation.sampling = SamplingMode::Nearest;
        Stroke selectionStroke;
        selectionStroke.mode = StrokeMode::PixelSelection;
        selectionStroke.points.clear();
        selectionStroke.pixelSelectionOp = std::move(operation);
        source.layers.first().strokes.append(std::move(selectionStroke));

        DocumentSerializer::SerializationCache cache;
        auto current = DocumentSerializer::prepare(source, cache);
        QVERIFY(current.has_value());
        QCOMPARE(cache.stats().binaryMaskCompressions, 1ULL);

        constexpr int appendedStrokeCount = 64;
        QElapsedTimer timer;
        timer.start();
        for (int index = 0; index < appendedStrokeCount; ++index)
        {
            Document candidate = current->document();
            Stroke stroke;
            stroke.points = {{QPointF(10.0 + index, 20.0 + index), 1.0}};
            candidate.layers.first().strokes.append(std::move(stroke));
            cache.resetStats();
            auto next = DocumentSerializer::prepare(std::move(candidate),
                cache,
                &*current,
                DocumentLimits::maximumProjectBytes);
            QVERIFY(next.has_value());
            const auto stats = cache.stats();
            QCOMPARE(stats.binaryMaskContentHashes, 0ULL);
            QCOMPARE(stats.binaryMaskCompressions, 0ULL);
            QCOMPARE(stats.strokeSerializations, 1ULL);
            current = std::move(next);
        }
        const qint64 elapsedMilliseconds = timer.elapsed();
        const QByteArray json = DocumentSerializer::toJson(*current, cache);
        QVERIFY(!json.isEmpty());
        QCOMPARE(current->compactSize(), static_cast<qint64>(json.size()));
        qInfo().nospace() << "4K prepared-cache benchmark: "
                          << appendedStrokeCount << " accumulated strokes in "
                          << elapsedMilliseconds << " ms";
    }

    void rebindsPreparedActiveLayerWithoutRebuildingContent()
    {
        Document source = Document::createDefault(QSize(32, 32));
        Layer second;
        second.name = QStringLiteral("Layer 2");
        second.initialCanvasSize = source.size;
        source.layers.append(second);

        DocumentSerializer::SerializationCache cache;
        const auto prepared = DocumentSerializer::prepare(source, cache);
        QVERIFY(prepared.has_value());
        cache.resetStats();
        const auto rebound =
            DocumentSerializer::rebindActiveLayer(*prepared, second.id);
        QVERIFY(rebound.has_value());
        QCOMPARE(rebound->document().activeLayerId, second.id);
        QCOMPARE(rebound->compactSize(), prepared->compactSize());
        const QJsonObject root =
            QJsonDocument::fromJson(DocumentSerializer::toJson(*rebound, cache))
                .object();
        QCOMPARE(root.value(QStringLiteral("activeLayerId")).toString(),
            second.id.toString(QUuid::WithoutBraces));
        QCOMPARE(cache.stats().strokeSerializations, 0ULL);
        QVERIFY(!DocumentSerializer::rebindActiveLayer(
            *prepared, QUuid::createUuid())
                .has_value());
    }

    void savesMaximumPointBudget()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path =
            directory.filePath(QStringLiteral("maximum-points.wagle"));
        Document source = Document::createDefault(QSize(4096, 4096));
        const StrokePoint point{
            QPointF(1234.56789012345, 3987.65432109876), 0.543210987654321};

        Stroke first;
        first.points.fill(point, DocumentLimits::maximumPointsPerStroke);
        Stroke second;
        second.points.fill(point,
            DocumentLimits::maximumTotalPoints
                - DocumentLimits::maximumPointsPerStroke);
        source.layers.first().strokes.append(std::move(first));
        source.layers.first().strokes.append(std::move(second));

        QString error;
        QVERIFY2(
            DocumentSerializer::save(path, source, &error), qPrintable(error));
        QVERIFY(QFileInfo(path).size() <= DocumentLimits::maximumProjectBytes);

        const std::optional<Document> loaded =
            DocumentSerializer::load(path, &error);
        QVERIFY2(loaded.has_value(), qPrintable(error));
        QCOMPARE(loaded->layers.first().strokes.size(), 2);
        QCOMPARE(loaded->layers.first().strokes.first().points.size()
                     + loaded->layers.first().strokes.last().points.size(),
            DocumentLimits::maximumTotalPoints);
    }

    void rejectsUnsafeDocumentsBeforeSaving()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("unsafe.wagle"));

        Document document = Document::createDefault(QSize(100, 100));
        Stroke stroke;
        stroke.points = {
            {QPointF(
                 DocumentLimits::maximumStoredCoordinateMagnitude + 1.0, 50.0),
                0.5}};
        document.layers.first().strokes.append(stroke);

        QString error;
        QVERIFY(!DocumentSerializer::save(path, document, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!QFile::exists(path));

        document.layers.first().strokes.first().points.first().position =
            QPointF(50.0, 50.0);
        document.framesPerSecond = std::numeric_limits<qreal>::quiet_NaN();
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
            + pointArray(DocumentLimits::maximumTotalPoints
                         - DocumentLimits::maximumPointsPerStroke + 1)
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
        controller.newDocument(
            QSize(0, DocumentLimits::maximumCanvasEdge + 100));
        QCOMPARE(controller.document().size,
            QSize(DocumentLimits::minimumCanvasEdge,
                DocumentLimits::maximumCanvasEdge));

        controller.newDocument(QSize(100, 100));
        const QUuid layerId = controller.document().activeLayerId;
        Stroke stroke;
        stroke.points.fill({QPointF(50.0, 50.0), 0.5},
            DocumentLimits::maximumPointsPerStroke + 1);
        controller.addStroke(layerId, std::move(stroke));
        QCOMPARE(
            controller.document().layer(layerId)->strokes.first().points.size(),
            DocumentLimits::maximumPointsPerStroke);

        Stroke invalid;
        invalid.points = {{QPointF(150.0, 50.0), 0.5}};
        controller.addStroke(layerId, std::move(invalid));
        QCOMPARE(controller.document().layer(layerId)->strokes.size(), 1);

        const int undoCount = controller.undoStack()->count();
        controller.setFramesPerSecond(std::numeric_limits<qreal>::quiet_NaN());
        QCOMPARE(controller.undoStack()->count(), undoCount);

        Document full = Document::createDefault(QSize(100, 100));
        while (full.layers.size() < DocumentLimits::maximumLayers)
        {
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
        QTest::newRow("unsupported-version") << QByteArrayLiteral(
            R"({"schemaVersion":6,"canvas":{"width":10,"height":10},"layers":[{}]})");
        QTest::newRow("unsupported-algorithm") << QByteArrayLiteral(
            R"({"schemaVersion":2,"algorithmVersion":3,"canvas":{"width":10,"height":10},"layers":[{}]})");
        QTest::newRow("invalid-canvas") << QByteArrayLiteral(
            R"({"schemaVersion":1,"canvas":{"width":0,"height":10},"layers":[{}]})");
        QTest::newRow("missing-layers") << QByteArrayLiteral(
            R"({"schemaVersion":1,"canvas":{"width":10,"height":10},"layers":[]})");
        QTest::newRow("duplicate-layers") << QByteArrayLiteral(
            R"({"schemaVersion":1,"canvas":{"width":10,"height":10},"layers":[{"id":"11111111-1111-1111-1111-111111111111","strokes":[]},{"id":"11111111-1111-1111-1111-111111111111","strokes":[]}]})");
        QTest::newRow("empty-stroke") << QByteArrayLiteral(
            R"({"schemaVersion":1,"canvas":{"width":10,"height":10},"layers":[{"name":"Layer","strokes":[{"points":[]}]}]})");
        QTest::newRow("invalid-point") << QByteArrayLiteral(
            R"({"schemaVersion":1,"canvas":{"width":10,"height":10},"layers":[{"name":"Layer","strokes":[{"points":["bad"]}]}]})");
        QTest::newRow("fractional-canvas") << QByteArrayLiteral(
            R"({"schemaVersion":1,"algorithmVersion":1,"canvas":{"width":10.5,"height":10,"background":"#ffffffff"},"animation":{"frames":2,"fps":25,"wobble":1},"activeLayerId":"11111111-1111-1111-1111-111111111111","layers":[{"id":"11111111-1111-1111-1111-111111111111","name":"Layer","visible":true,"opacity":1,"strokes":[]}]})");
        QTest::newRow("invalid-active-layer") << QByteArrayLiteral(
            R"({"schemaVersion":1,"algorithmVersion":1,"canvas":{"width":10,"height":10,"background":"#ffffffff"},"animation":{"frames":2,"fps":25,"wobble":1},"activeLayerId":"22222222-2222-2222-2222-222222222222","layers":[{"id":"11111111-1111-1111-1111-111111111111","name":"Layer","visible":true,"opacity":1,"strokes":[]}]})");
        QTest::newRow("active-layer-without-layers") << QByteArrayLiteral(
            R"({"schemaVersion":4,"algorithmVersion":2,"canvas":{"width":10,"height":10,"background":"#ffffffff"},"animation":{"frames":2,"fps":25,"wobble":1},"activeLayerId":"11111111-1111-1111-1111-111111111111","layers":[],"clipMasks":[]})");
        QTest::newRow("malformed-active-layer-without-layers") << QByteArrayLiteral(
            R"({"schemaVersion":4,"algorithmVersion":2,"canvas":{"width":10,"height":10,"background":"#ffffffff"},"animation":{"frames":2,"fps":25,"wobble":1},"activeLayerId":"garbage","layers":[],"clipMasks":[]})");
        QTest::newRow("outside-point") << QByteArrayLiteral(
            R"({"schemaVersion":1,"algorithmVersion":1,"canvas":{"width":10,"height":10,"background":"#ffffffff"},"animation":{"frames":2,"fps":25,"wobble":1},"activeLayerId":"11111111-1111-1111-1111-111111111111","layers":[{"id":"11111111-1111-1111-1111-111111111111","name":"Layer","visible":true,"opacity":1,"strokes":[{"id":"22222222-2222-2222-2222-222222222222","seed":"1","mode":"paint","color":"#ff000000","width":1,"points":[[11,5,1]]}]}]})");
        QTest::newRow("invalid-pressure") << QByteArrayLiteral(
            R"({"schemaVersion":1,"algorithmVersion":1,"canvas":{"width":10,"height":10,"background":"#ffffffff"},"animation":{"frames":2,"fps":25,"wobble":1},"activeLayerId":"11111111-1111-1111-1111-111111111111","layers":[{"id":"11111111-1111-1111-1111-111111111111","name":"Layer","visible":true,"opacity":1,"strokes":[{"id":"22222222-2222-2222-2222-222222222222","seed":"1","mode":"paint","color":"#ff000000","width":1,"points":[[5,5,1.1]]}]}]})");
        QTest::newRow("missing-brush") << QByteArrayLiteral(
            R"({"schemaVersion":2,"algorithmVersion":2,"canvas":{"width":10,"height":10,"background":"#ffffffff"},"animation":{"frames":2,"fps":25,"wobble":1},"activeLayerId":"11111111-1111-1111-1111-111111111111","layers":[{"id":"11111111-1111-1111-1111-111111111111","name":"Layer","visible":true,"opacity":1,"strokes":[{"id":"22222222-2222-2222-2222-222222222222","seed":"1","mode":"paint","color":"#ff000000","width":1,"points":[[5,5,1]]}]}]})");
        QTest::newRow("wrong-field-type") << QByteArrayLiteral(
            R"({"schemaVersion":1,"algorithmVersion":1,"canvas":{"width":10,"height":10,"background":"#ffffffff"},"animation":{"frames":2,"fps":25,"wobble":1},"activeLayerId":"11111111-1111-1111-1111-111111111111","layers":[{"id":"11111111-1111-1111-1111-111111111111","name":"Layer","visible":1,"opacity":1,"strokes":[]}]})");
        QTest::newRow("fps-too-high") << QByteArrayLiteral(
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

int runDocumentTests(int argc, char **argv)
{
    DocumentTests tests;
    return QTest::qExec(&tests, argc, argv);
}

}

#include "DocumentTests.moc"

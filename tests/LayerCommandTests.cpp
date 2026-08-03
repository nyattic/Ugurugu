#include "support/DocumentTestHelpers.hpp"
#include "support/DocumentTestSuites.hpp"

namespace wobble
{

class LayerCommandTests final : public QObject
{
    Q_OBJECT

private slots:
    void changesLayerBlendModeUndoably()
    {
        DocumentController controller;
        controller.newDocument(QSize(96, 96));
        const QUuid layerId = controller.document().activeLayerId;

        controller.setLayerBlendMode(layerId, LayerBlendMode::Multiply);
        QCOMPARE(controller.document().layer(layerId)->blendMode,
            LayerBlendMode::Multiply);
        QCOMPARE(controller.undoStack()->count(), 1);

        controller.setLayerBlendMode(layerId, static_cast<LayerBlendMode>(100));
        QCOMPARE(controller.undoStack()->count(), 1);

        controller.undoStack()->undo();
        QCOMPARE(controller.document().layer(layerId)->blendMode,
            LayerBlendMode::Normal);
        controller.undoStack()->redo();
        QCOMPARE(controller.document().layer(layerId)->blendMode,
            LayerBlendMode::Multiply);
    }

    void changesReferenceLayerUndoably()
    {
        DocumentController controller;
        controller.newDocument(QSize(96, 96));
        const QUuid layerId = controller.document().activeLayerId;

        controller.setLayerReference(layerId, true);
        QVERIFY(controller.document().layer(layerId)->reference);
        QCOMPARE(controller.undoStack()->count(), 1);

        controller.undoStack()->undo();
        QVERIFY(!controller.document().layer(layerId)->reference);
        controller.undoStack()->redo();
        QVERIFY(controller.document().layer(layerId)->reference);

        controller.addLayerGroup(layerId);
        const QUuid groupId =
            controller.document().layer(layerId)->parentGroupId;
        const int count = controller.undoStack()->count();
        controller.setLayerReference(groupId, true);
        QCOMPARE(controller.undoStack()->count(), count);
        QVERIFY(!controller.document().layer(groupId)->reference);
    }

    void managesLayerGroupsAndClippingUndoably()
    {
        DocumentController controller;
        controller.newDocument(QSize(96, 96));
        const QUuid firstId = controller.document().activeLayerId;

        controller.addLayerGroup(firstId);
        QCOMPARE(controller.document().layers.size(), 2);
        const Layer *first = controller.document().layer(firstId);
        QVERIFY(first);
        QVERIFY(!first->parentGroupId.isNull());
        const QUuid groupId = first->parentGroupId;
        const Layer *group = controller.document().layer(groupId);
        QVERIFY(group);
        QCOMPARE(group->kind, LayerKind::Group);
        QVERIFY(group->strokes.isEmpty());
        QVERIFY(controller.document().isLayerDescendantOf(firstId, groupId));
        QCOMPARE(controller.document().layerDepth(firstId), 1);

        controller.addLayer();
        const QUuid secondId = controller.document().activeLayerId;
        QCOMPARE(controller.document().layer(secondId)->parentGroupId, groupId);
        controller.setLayerClipToBelow(secondId, true);
        QVERIFY(controller.document().layer(secondId)->clipToLayerBelow);
        controller.setLayerParentGroup(secondId, {});
        QVERIFY(controller.document().layer(secondId)->parentGroupId.isNull());

        controller.undoStack()->undo();
        QCOMPARE(controller.document().layer(secondId)->parentGroupId, groupId);
        controller.undoStack()->undo();
        QVERIFY(!controller.document().layer(secondId)->clipToLayerBelow);
        controller.undoStack()->redo();
        QVERIFY(controller.document().layer(secondId)->clipToLayerBelow);
    }

    void editsStrokePropertiesUndoably()
    {
        DocumentController controller;
        controller.newDocument(QSize(96, 96));
        const QUuid layerId = controller.document().activeLayerId;
        Stroke paint;
        paint.color = QColor(20, 40, 60);
        paint.width = 7.0;
        paint.brush.wobbleScale = 0.5;
        paint.points = {{QPointF(12.0, 14.0), 1.0}, {QPointF(50.0, 52.0), 1.0}};
        controller.addStroke(layerId, paint);
        controller.undoStack()->setClean();

        QVERIFY(controller.updateStrokeAttributes(
            layerId, {paint.id}, QColor(90, 110, 130), 15.0, 1.75));
        const Stroke &edited =
            controller.document().layer(layerId)->strokes.first();
        QCOMPARE(edited.color, QColor(90, 110, 130));
        QCOMPARE(edited.width, 15.0);
        QCOMPARE(edited.brush.wobbleScale, 1.75);

        controller.undoStack()->undo();
        const Stroke &restored =
            controller.document().layer(layerId)->strokes.first();
        QCOMPARE(restored.color, paint.color);
        QCOMPARE(restored.width, paint.width);
        QCOMPARE(restored.brush.wobbleScale, paint.brush.wobbleScale);
        controller.undoStack()->redo();
        QCOMPARE(controller.document()
                     .layer(layerId)
                     ->strokes.first()
                     .brush.wobbleScale,
            1.75);
    }

    void roundTripsLayerHierarchySchema()
    {
        Document document = Document::createDefault(QSize(64, 64));
        Layer group;
        group.name = QStringLiteral("Group");
        group.kind = LayerKind::Group;
        group.opacity = 0.7;
        group.initialCanvasSize = document.size;
        document.layers.first().parentGroupId = group.id;
        document.layers.first().clipToLayerBelow = true;
        document.layers.first().reference = true;
        document.layers.append(group);

        const QByteArray json = DocumentSerializer::toJson(document);
        QVERIFY(!json.isEmpty());
        const QJsonObject root = QJsonDocument::fromJson(json).object();
        QCOMPARE(root.value(QStringLiteral("schemaVersion")).toInt(), 9);
        QString error;
        const std::optional<Document> decoded =
            DocumentSerializer::fromJson(json, &error);
        QVERIFY2(decoded.has_value(), qPrintable(error));
        QCOMPARE(decoded->layers.last().kind, LayerKind::Group);
        QCOMPARE(decoded->layers.first().parentGroupId, group.id);
        QVERIFY(decoded->layers.first().clipToLayerBelow);
        QVERIFY(decoded->layers.first().reference);

        QJsonObject legacyRoot = root;
        legacyRoot.insert(QStringLiteral("schemaVersion"), 8);
        QJsonArray legacyLayers =
            legacyRoot.value(QStringLiteral("layers")).toArray();
        for (int index = 0; index < legacyLayers.size(); ++index)
        {
            QJsonObject legacyLayer = legacyLayers[index].toObject();
            legacyLayer.remove(QStringLiteral("reference"));
            legacyLayers[index] = legacyLayer;
        }
        legacyRoot.insert(QStringLiteral("layers"), legacyLayers);
        const std::optional<Document> legacy = DocumentSerializer::fromJson(
            QJsonDocument(legacyRoot).toJson(QJsonDocument::Compact), &error);
        QVERIFY2(legacy.has_value(), qPrintable(error));
        QVERIFY(!legacy->layers.first().reference);

        Document cyclic = *decoded;
        cyclic.layers.last().parentGroupId = cyclic.layers.last().id;
        QVERIFY(DocumentSerializer::toJson(cyclic).isEmpty());

        Document invalidReference = *decoded;
        invalidReference.layers.last().reference = true;
        QVERIFY(DocumentSerializer::toJson(invalidReference).isEmpty());
    }

    void analyzesLayerHierarchyMetricsAndFailures()
    {
        const Document document = documentWithNestedPaintAtDepth(3);
        const LayerHierarchyAnalysis hierarchy =
            analyzeLayerHierarchy(document);
        QVERIFY(hierarchy.isValid());
        QCOMPARE(hierarchy.issue(), LayerHierarchyIssue::None);
        QCOMPARE(hierarchy.maximumDepth(), 3);
        QCOMPARE(hierarchy.depths(), QVector<int>({3, 0, 1, 2}));
        QCOMPARE(hierarchy.subtreeHeights(), QVector<int>({0, 3, 2, 1}));
        QCOMPARE(hierarchy.depth(document.activeLayerId), 3);
        QCOMPARE(hierarchy.subtreeHeight(document.layers[1].id), 3);
        QVERIFY(hierarchy.isDescendantOf(
            document.activeLayerId, document.layers[1].id));
        QVERIFY(!hierarchy.isDescendantOf(
            document.layers[1].id, document.activeLayerId));

        Document missingParent = document;
        missingParent.layers.first().parentGroupId = QUuid::createUuid();
        QCOMPARE(analyzeLayerHierarchy(missingParent).issue(),
            LayerHierarchyIssue::MissingParent);

        Document nonGroupParent = document;
        nonGroupParent.layers[1].parentGroupId =
            nonGroupParent.layers.first().id;
        QCOMPARE(analyzeLayerHierarchy(nonGroupParent).issue(),
            LayerHierarchyIssue::ParentNotGroup);

        Document selfParent = document;
        selfParent.layers[1].parentGroupId = selfParent.layers[1].id;
        QCOMPARE(analyzeLayerHierarchy(selfParent).issue(),
            LayerHierarchyIssue::SelfParent);

        Document cyclic = document;
        cyclic.layers[1].parentGroupId = cyclic.layers.last().id;
        const LayerHierarchyAnalysis cyclicHierarchy =
            analyzeLayerHierarchy(cyclic);
        QCOMPARE(cyclicHierarchy.issue(), LayerHierarchyIssue::Cycle);
        QCOMPARE(cyclic.layers[cyclicHierarchy.issueLayerIndex()].kind,
            LayerKind::Group);

        Document duplicate = document;
        duplicate.layers.last().id = duplicate.layers[1].id;
        QCOMPARE(analyzeLayerHierarchy(duplicate).issue(),
            LayerHierarchyIssue::DuplicateLayerId);
    }

    void preservesLegacyOverDepthHierarchyThroughSerialization()
    {
        for (const int depth : {DocumentLimits::maximumLayerDepth,
                 DocumentLimits::maximumLayerDepth + 1})
        {
            const Document source = documentWithNestedPaintAtDepth(depth);
            const LayerHierarchyAnalysis sourceHierarchy =
                analyzeLayerHierarchy(source);
            QVERIFY(sourceHierarchy.isValid());
            QCOMPARE(sourceHierarchy.maximumDepth(), depth);

            const QByteArray json = DocumentSerializer::toJson(source);
            QVERIFY(!json.isEmpty());
            QString error;
            const std::optional<Document> decoded =
                DocumentSerializer::fromJson(json, &error);
            QVERIFY2(decoded.has_value(), qPrintable(error));
            const LayerHierarchyAnalysis decodedHierarchy =
                analyzeLayerHierarchy(*decoded);
            QVERIFY(decodedHierarchy.isValid());
            QCOMPARE(decodedHierarchy.maximumDepth(), depth);
            QCOMPARE(DocumentSerializer::toJson(*decoded), json);

            QTemporaryDir directory;
            QVERIFY(directory.isValid());
            const QString path =
                directory.filePath(QStringLiteral("depth-%1.wagle").arg(depth));
            QVERIFY2(DocumentSerializer::save(path, *decoded, &error),
                qPrintable(error));
            const std::optional<Document> loaded =
                DocumentSerializer::load(path, &error);
            QVERIFY2(loaded.has_value(), qPrintable(error));
            QCOMPARE(DocumentSerializer::toJson(*loaded), json);
        }

        const Document overDepth = documentWithNestedPaintAtDepth(
            DocumentLimits::maximumLayerDepth + 1);
        QJsonObject schemaEight =
            QJsonDocument::fromJson(DocumentSerializer::toJson(overDepth))
                .object();
        schemaEight.insert(QStringLiteral("schemaVersion"), 8);
        QJsonArray layers =
            schemaEight.value(QStringLiteral("layers")).toArray();
        for (int index = 0; index < layers.size(); ++index)
        {
            QJsonObject layer = layers[index].toObject();
            layer.remove(QStringLiteral("reference"));
            layers[index] = layer;
        }
        schemaEight.insert(QStringLiteral("layers"), layers);
        QString error;
        const std::optional<Document> decoded = DocumentSerializer::fromJson(
            QJsonDocument(schemaEight).toJson(QJsonDocument::Compact), &error);
        QVERIFY2(decoded.has_value(), qPrintable(error));
        QCOMPARE(analyzeLayerHierarchy(*decoded).maximumDepth(),
            DocumentLimits::maximumLayerDepth + 1);
        QVERIFY(!DocumentSerializer::toJson(*decoded).isEmpty());
    }

    void validatesHierarchyOnPreparedMetadataReuse()
    {
        DocumentSerializer::SerializationCache cache;
        QString error;
        std::optional<DocumentSerializer::PreparedDocument> base =
            DocumentSerializer::prepare(
                documentWithGroupAtDepth(2), cache, &error);
        QVERIFY2(base.has_value(), qPrintable(error));

        Document cyclic = base->document();
        const QUuid rootGroup = layerIdAtDepth(cyclic, 0, LayerKind::Group);
        const QUuid deepestGroup = layerIdAtDepth(cyclic, 2, LayerKind::Group);
        QVERIFY(!rootGroup.isNull());
        QVERIFY(!deepestGroup.isNull());
        cyclic.layer(rootGroup)->parentGroupId = deepestGroup;
        cache.resetStats();
        error.clear();
        const auto rejected = DocumentSerializer::prepare(std::move(cyclic),
            cache,
            &*base,
            DocumentLimits::maximumProjectBytes,
            &error);
        QVERIFY(!rejected.has_value());
        QVERIFY(!error.isEmpty());
        QCOMPARE(cache.stats().fullDocumentPreparations, 0ULL);

        base = DocumentSerializer::prepare(
            documentWithGroupAtDepth(DocumentLimits::maximumLayerDepth),
            cache,
            &error);
        QVERIFY2(base.has_value(), qPrintable(error));
        Document overDepth = base->document();
        const QUuid paintId = overDepth.activeLayerId;
        const QUuid depthLimitGroup = layerIdAtDepth(
            overDepth, DocumentLimits::maximumLayerDepth, LayerKind::Group);
        overDepth.layer(paintId)->parentGroupId = depthLimitGroup;
        cache.resetStats();
        error.clear();
        const auto preserved = DocumentSerializer::prepare(std::move(overDepth),
            cache,
            &*base,
            DocumentLimits::maximumProjectBytes,
            &error);
        QVERIFY2(preserved.has_value(), qPrintable(error));
        QCOMPARE(analyzeLayerHierarchy(preserved->document()).maximumDepth(),
            DocumentLimits::maximumLayerDepth + 1);
        QCOMPARE(cache.stats().fullDocumentPreparations, 0ULL);
    }

    void rejectsLayerDepthGrowthWithoutChangingControllerState()
    {
        DocumentController controller;
        QVERIFY(controller.loadDocument(
            documentWithGroupAtDepth(DocumentLimits::maximumLayerDepth)));
        const QUuid deepestGroup = layerIdAtDepth(controller.document(),
            DocumentLimits::maximumLayerDepth,
            LayerKind::Group);
        const QUuid rootPaint = controller.document().activeLayerId;
        QVERIFY(!deepestGroup.isNull());
        verifyRejectedHierarchyMutation(controller,
            [&controller, deepestGroup]
            {
                controller.addLayer(deepestGroup);
            });
        verifyRejectedHierarchyMutation(controller,
            [&controller, rootPaint, deepestGroup]
            {
                controller.setLayerParentGroup(rootPaint, deepestGroup);
            });

        QVERIFY(controller.loadDocument(
            documentWithNestedPaintAtDepth(DocumentLimits::maximumLayerDepth)));
        const QUuid deepestPaint = controller.document().activeLayerId;
        verifyRejectedHierarchyMutation(controller,
            [&controller, deepestPaint]
            {
                controller.addLayerGroup(deepestPaint);
            });

        Document subtree =
            documentWithNestedPaintAtDepth(DocumentLimits::maximumLayerDepth);
        const QUuid subtreeRoot = layerIdAtDepth(subtree, 0, LayerKind::Group);
        Layer targetGroup;
        targetGroup.name = QStringLiteral("Target group");
        targetGroup.kind = LayerKind::Group;
        targetGroup.initialCanvasSize = subtree.size;
        const QUuid targetGroupId = targetGroup.id;
        subtree.layers.append(std::move(targetGroup));
        QVERIFY(controller.loadDocument(std::move(subtree)));
        verifyRejectedHierarchyMutation(controller,
            [&controller, subtreeRoot]
            {
                controller.addLayerGroup(subtreeRoot);
            });
        verifyRejectedHierarchyMutation(controller,
            [&controller, subtreeRoot, targetGroupId]
            {
                controller.setLayerParentGroup(subtreeRoot, targetGroupId);
            });
    }

    void permitsOnlyNonGrowingLegacyHierarchyMutations()
    {
        DocumentController controller;
        QVERIFY(controller.loadDocument(documentWithNestedPaintAtDepth(
            DocumentLimits::maximumLayerDepth + 1)));
        const QUuid legacyPaint = controller.document().activeLayerId;
        QCOMPARE(analyzeLayerHierarchy(controller.document()).maximumDepth(),
            DocumentLimits::maximumLayerDepth + 1);

        verifyRejectedHierarchyMutation(controller,
            [&controller, legacyPaint]
            {
                controller.addLayerGroup(legacyPaint);
            });

        const int originalLayerCount =
            static_cast<int>(controller.document().layers.size());
        controller.addLayer();
        QCOMPARE(controller.document().layers.size(), originalLayerCount + 1);
        QCOMPARE(analyzeLayerHierarchy(controller.document()).maximumDepth(),
            DocumentLimits::maximumLayerDepth + 1);
        QVERIFY(controller.isModified());
        controller.undoStack()->undo();
        QCOMPARE(controller.document().layers.size(), originalLayerCount);

        controller.duplicateLayer(legacyPaint);
        QCOMPARE(controller.document().layers.size(), originalLayerCount + 1);
        QCOMPARE(analyzeLayerHierarchy(controller.document()).maximumDepth(),
            DocumentLimits::maximumLayerDepth + 1);
        controller.undoStack()->undo();

        controller.setLayerParentGroup(legacyPaint, {});
        QCOMPARE(analyzeLayerHierarchy(controller.document()).maximumDepth(),
            DocumentLimits::maximumLayerDepth);
        controller.undoStack()->undo();
        QCOMPARE(analyzeLayerHierarchy(controller.document()).maximumDepth(),
            DocumentLimits::maximumLayerDepth + 1);
    }

    void acceptsMaximumWidthShallowHierarchy()
    {
        Document wide = Document::createDefault(QSize(64, 64));
        while (wide.layers.size() < DocumentLimits::maximumLayers)
        {
            Layer layer;
            layer.name = QStringLiteral("Layer %1").arg(wide.layers.size() + 1);
            layer.initialCanvasSize = wide.size;
            wide.layers.append(std::move(layer));
        }
        const LayerHierarchyAnalysis hierarchy = analyzeLayerHierarchy(wide);
        QVERIFY(hierarchy.isValid());
        QCOMPARE(hierarchy.maximumDepth(), 0);
        QVERIFY(!DocumentSerializer::toJson(wide).isEmpty());

        DocumentController controller;
        QVERIFY(controller.loadDocument(std::move(wide)));
        QCOMPARE(
            controller.document().layers.size(), DocumentLimits::maximumLayers);
    }

    void undoActionsCannotBypassHistoryPreflight()
    {
        DocumentController controller;
        controller.newDocument(QSize(96, 96));
        const QUuid layerId = controller.document().activeLayerId;
        controller.renameLayer(layerId, QStringLiteral("Renamed"));

        QVERIFY(controller.undoStack()->canUndo());
        QVERIFY(!controller.undoStack()->canRedo());

        DocumentControllerTestAccess::failHistoryPrepareAfter(controller, 0);
        controller.undoStack()->undo();
        QCOMPARE(controller.undoStack()->index(), 1);
        QCOMPARE(controller.document().layer(layerId)->name,
            QStringLiteral("Renamed"));
        QCOMPARE(
            controller.undoStack()->storageStats().retainedPreparedDocuments,
            qsizetype(0));

        controller.undoStack()->undo();
        QCOMPARE(controller.undoStack()->index(), 0);
        QCOMPARE(controller.document().layer(layerId)->name,
            QStringLiteral("Layer 1"));
        QVERIFY(controller.undoStack()->canRedo());
        controller.undoStack()->redo();
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
};

int runLayerCommandTests(int argc, char **argv)
{
    LayerCommandTests tests;
    return QTest::qExec(&tests, argc, argv);
}

}

#include "LayerCommandTests.moc"

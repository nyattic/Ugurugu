#include "support/UiTestHelpers.hpp"
#include "support/UiTestSuites.hpp"

namespace wobble
{

class UiShellTests final : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        QSettings settings;
        settings.remove(QStringLiteral("drawingTools"));
        settings.remove(QStringLiteral("brush/recentColors"));
        settings.remove(QStringLiteral("canvas/strokeStabilization"));
        settings.sync();
    }

    void cleanup()
    {
        QSettings settings;
        settings.remove(QStringLiteral("drawingTools"));
        settings.remove(QStringLiteral("brush/recentColors"));
        settings.remove(QStringLiteral("canvas/strokeStabilization"));
        settings.sync();
    }

    void launchesAndEdits()
    {
        MainWindow window;
        window.resize(1100, 720);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        LayerDock *layerDock = window.findChild<LayerDock *>();
        QAction *undoAction =
            window.findChild<QAction *>(QStringLiteral("undoAction"));
        QVERIFY(canvas);
        QVERIFY(layerDock);
        QVERIFY(undoAction);
        QVERIFY(!undoAction->isEnabled());
        const QList<BrushPresetButton *> presetButtons =
            window.findChildren<BrushPresetButton *>();
        QSpinBox *brushSizeSpin =
            window.findChild<QSpinBox *>(QStringLiteral("brushSizeSpin"));
        QVERIFY(brushSizeSpin);
        QCOMPARE(presetButtons.size(), BrushPresetCatalog::builtIns().size());
        BrushPresetButton *softAirbrushButton = nullptr;
        for (BrushPresetButton *button : presetButtons)
        {
            if (button->presetId() == QStringLiteral("soft-airbrush"))
            {
                softAirbrushButton = button;
            }
        }
        QVERIFY(softAirbrushButton);
        softAirbrushButton->click();
        QCOMPARE(canvas->brushPresetId(), QStringLiteral("soft-airbrush"));
        QVERIFY(softAirbrushButton->isChecked());
        QCOMPARE(brushSizeSpin->value(),
            qRound(BrushPresetCatalog::find(QStringLiteral("soft-airbrush"))
                    ->defaultSize));

        QSpinBox *currentFrameSpin =
            window.findChild<QSpinBox *>(QStringLiteral("currentFrameSpin"));
        QVERIFY(currentFrameSpin);
        QCOMPARE(currentFrameSpin->maximum(), 30);
        currentFrameSpin->setFocus(Qt::OtherFocusReason);
        QTest::mouseClick(currentFrameSpin,
            Qt::LeftButton,
            Qt::NoModifier,
            currentFrameSpin->rect().center());
        QTRY_VERIFY(!canvas->isAnimating());
        const int targetFrameValue = currentFrameSpin->value() == 1 ? 2 : 1;
        currentFrameSpin->setValue(targetFrameValue);
        QCOMPARE(canvas->currentFrame(), targetFrameValue - 1);

        const QPoint start = canvas->rect().center() - QPoint(50, 20);
        const QPoint end = canvas->rect().center() + QPoint(50, 20);
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(canvas, end, 10);
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, end);
        QTRY_VERIFY(undoAction->isEnabled());

        const QString screenshotPath =
            qEnvironmentVariable("WOBBLEPAINT_TEST_SCREENSHOT");
        if (!screenshotPath.isEmpty())
        {
            QVERIFY(window.grab().save(screenshotPath, "PNG"));
            QVERIFY(QFileInfo(screenshotPath).size() > 0);
        }

        const QString brushPanelScreenshotPath =
            qEnvironmentVariable("WOBBLEPAINT_BRUSH_PANEL_SCREENSHOT");
        if (!brushPanelScreenshotPath.isEmpty())
        {
            BrushPopoverPanel *brushPanel =
                window.findChild<BrushPopoverPanel *>();
            QVERIFY(brushPanel);
            QWidget *popover = brushPanel->window();
            popover->adjustSize();
            QVERIFY(popover->grab().save(brushPanelScreenshotPath, "PNG"));
            QVERIFY(QFileInfo(brushPanelScreenshotPath).size() > 0);
        }

        undoAction->trigger();
        QTRY_VERIFY(!undoAction->isEnabled());

        QListWidget *layerList = layerDock->findChild<QListWidget *>();
        QVERIFY(layerList);
        QCOMPARE(layerList->count(), 1);

        QToolButton *addButton = layerDock->findChild<QToolButton *>(
            QStringLiteral("layerAddButton"));
        QVERIFY(addButton);
        QTest::mouseClick(addButton, Qt::LeftButton);
        QTRY_COMPARE(layerList->count(), 2);
        QTRY_VERIFY(undoAction->isEnabled());

        undoAction->trigger();
        QTRY_COMPARE(layerList->count(), 1);

        QAction *settingsAction =
            window.findChild<QAction *>(QStringLiteral("settingsAction"));
        QAction *checkForUpdatesAction = window.findChild<QAction *>(
            QStringLiteral("checkForUpdatesAction"));
        QToolButton *settingsButton =
            window.findChild<QToolButton *>(QStringLiteral("settingsButton"));
        QVERIFY(settingsAction);
        QVERIFY(checkForUpdatesAction);
        QVERIFY(settingsButton);
        QCOMPARE(settingsButton->defaultAction(), settingsAction);
        QVERIFY(
            window.windowTitle().contains(QStringLiteral("WagleWaglePaint")));
    }

    void exposesBrushPresetNameToAccessibility()
    {
        const BrushPreset &preset = BrushPresetCatalog::defaultPreset();
        BrushPresetButton button(preset);
        QCOMPARE(
            button.accessibleName(), BrushPresetCatalog::displayName(preset));
    }

    void exposesEraserPresetNameToAccessibility()
    {
        const EraserPreset &preset = EraserPresetCatalog::defaultPreset();
        EraserPresetButton button(preset);
        QCOMPARE(
            button.accessibleName(), EraserPresetCatalog::displayName(preset));
    }

    void switchesEraserPresetFromCards()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        CanvasWidget canvas(&controller);
        EraserPopoverPanel panel(&canvas);

        const QList<EraserPresetButton *> buttons =
            panel.findChildren<EraserPresetButton *>();
        QCOMPARE(buttons.size(), EraserPresetCatalog::builtIns().size());
        EraserPresetButton *softButton = nullptr;
        for (EraserPresetButton *button : buttons)
        {
            if (button->presetId() == QStringLiteral("soft-eraser"))
            {
                softButton = button;
                break;
            }
        }
        QVERIFY(softButton);
        softButton->click();
        QCOMPARE(canvas.eraserPresetId(), QStringLiteral("soft-eraser"));
        QVERIFY(softButton->isChecked());
        QCOMPARE(canvas.eraserWidth(),
            EraserPresetCatalog::find(QStringLiteral("soft-eraser"))
                ->defaultSize);
    }

    void switchesWandReferenceFromCards()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        CanvasWidget canvas(&controller);
        WandPopoverPanel panel(&canvas);

        WandReferenceButton *activeButton =
            panel.findChild<WandReferenceButton *>(
                QStringLiteral("wandReferenceActiveButton"));
        WandReferenceButton *markedButton =
            panel.findChild<WandReferenceButton *>(
                QStringLiteral("wandReferenceMarkedButton"));
        WandReferenceButton *visibleButton =
            panel.findChild<WandReferenceButton *>(
                QStringLiteral("wandReferenceVisibleButton"));
        QVERIFY(activeButton);
        QVERIFY(markedButton);
        QVERIFY(visibleButton);
        QCOMPARE(panel.findChildren<WandReferenceButton *>().size(), 3);
        QVERIFY(activeButton->isChecked());

        markedButton->click();
        QCOMPARE(canvas.wandReference(),
            CanvasWidget::WandReference::ReferenceLayers);
        QVERIFY(markedButton->isChecked());

        canvas.setWandReference(CanvasWidget::WandReference::AllVisibleLayers);
        QVERIFY(visibleButton->isChecked());
    }

    void switchesSelectionShapeFromCards()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        CanvasWidget canvas(&controller);
        LassoPopoverPanel panel(&canvas);

        SelectionShapeButton *freehandButton =
            panel.findChild<SelectionShapeButton *>(
                QStringLiteral("selectionShapeFreehandButton"));
        SelectionShapeButton *rectangleButton =
            panel.findChild<SelectionShapeButton *>(
                QStringLiteral("selectionShapeRectangleButton"));
        SelectionShapeButton *ellipseButton =
            panel.findChild<SelectionShapeButton *>(
                QStringLiteral("selectionShapeEllipseButton"));
        QVERIFY(freehandButton);
        QVERIFY(rectangleButton);
        QVERIFY(ellipseButton);
        QCOMPARE(panel.findChildren<SelectionShapeButton *>().size(), 3);
        QVERIFY(freehandButton->isChecked());

        rectangleButton->click();
        QCOMPARE(
            canvas.selectionShape(), CanvasWidget::SelectionShape::Rectangle);
        QVERIFY(rectangleButton->isChecked());

        canvas.setSelectionShape(CanvasWidget::SelectionShape::Ellipse);
        QVERIFY(ellipseButton->isChecked());
    }

    void tabsToFrameScrubberAndExposesCurrentFrame()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        CanvasWidget canvas(&controller);
        FrameScrubber scrubber(&controller, &canvas);

        QCOMPARE(scrubber.focusPolicy(), Qt::StrongFocus);
        QVERIFY(scrubber.focusPolicy() & Qt::TabFocus);
        QCOMPARE(
            scrubber.accessibleDescription(), QStringLiteral("Frame 1 of 30"));
        QTest::keyClick(&scrubber, Qt::Key_Right);
        QCOMPARE(canvas.currentFrame(), 1);
        QCOMPARE(
            scrubber.accessibleDescription(), QStringLiteral("Frame 2 of 30"));
    }

    void togglesLayerVisibilityFromKeyboard()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        const QUuid layerId = controller.document().activeLayerId;
        LayerDock dock(&controller);
        QListWidget *list = dock.findChild<QListWidget *>();
        QVERIFY(list);
        QCOMPARE(list->count(), 1);
        QCOMPARE(list->item(0)->data(Qt::AccessibleDescriptionRole).toString(),
            QStringLiteral("Layer is visible"));

        QTest::keyClick(list, Qt::Key_Space);
        QTRY_VERIFY(!controller.document().layer(layerId)->visible);
        QCOMPARE(list->item(0)->data(Qt::AccessibleDescriptionRole).toString(),
            QStringLiteral("Layer is hidden"));

        QTest::keyClick(list, Qt::Key_Space);
        QTRY_VERIFY(controller.document().layer(layerId)->visible);
    }

    void updatesLayerItemsIncrementallyAndRendersThumbnailsOffThread()
    {
        DocumentController controller;
        QVERIFY(controller.newDocument(QSize(4096, 4096)));
        const QUuid layerId = controller.document().activeLayerId;
        LayerDock dock(&controller);
        QListWidget *list = dock.findChild<QListWidget *>();
        QVERIFY(list);
        QCOMPARE(list->count(), 1);
        QListWidgetItem *originalItem = list->item(0);

        Stroke stroke;
        stroke.width = 20.0;
        stroke.points = {
            {QPointF(100.0, 100.0), 1.0}, {QPointF(1000.0, 1000.0), 1.0}};
        QCOMPARE(controller.addStroke(layerId, stroke),
            DocumentController::AddStrokeResult::Added);
        QCOMPARE(list->item(0), originalItem);
        QTRY_VERIFY_WITH_TIMEOUT(!list->item(0)
                                     ->data(LayerItemRoles::Thumbnail)
                                     .value<QPixmap>()
                                     .isNull(),
            5000);

        QCOMPARE(controller.renameLayer(layerId, QStringLiteral("Retained")),
            DocumentController::RenameLayerResult::Renamed);
        QCOMPARE(list->item(0), originalItem);
        QCOMPARE(list->item(0)->text(), QStringLiteral("Retained"));

        controller.addLayer();
        QListWidgetItem *retainedItem = nullptr;
        for (int row = 0; row < list->count(); ++row)
        {
            if (list->item(row)->data(LayerItemRoles::LayerId).toUuid()
                == layerId)
            {
                retainedItem = list->item(row);
                break;
            }
        }
        QCOMPARE(retainedItem, originalItem);
    }

    void animatesWobblePreviewOnlyAroundChanges()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        CanvasWidget canvas(&controller);
        TimelineBar timeline(&controller, &canvas);
        WobblePreview *preview = timeline.findChild<WobblePreview *>();
        QVERIFY(preview);
        QShowEvent showEvent;
        QApplication::sendEvent(preview, &showEvent);
        QVERIFY(!preview->isAnimationActive());

        controller.setWobbleAmount(controller.document().wobbleAmount + 1.0);
        QVERIFY(preview->isAnimationActive());

        timeline.setEnabled(false);
        QVERIFY(!preview->isAnimationActive());
        timeline.setEnabled(true);
        QVERIFY(preview->isAnimationActive());

        QTRY_VERIFY_WITH_TIMEOUT(!preview->isAnimationActive(), 3000);

        QHideEvent hideEvent;
        QApplication::sendEvent(preview, &hideEvent);
        QVERIFY(!preview->isAnimationActive());
    }

    void debouncesRecentColorPersistence()
    {
        const QString key = QStringLiteral("brush/recentColors");
        QSettings().remove(key);
        ColorSwatchRow row;
        const QColor first(20, 40, 60, 80);
        const QColor second(30, 50, 70, 90);
        const QColor finalColor(40, 60, 80, 100);

        row.setActiveColor(first);
        row.setActiveColor(second);
        QVERIFY(!QSettings().contains(key));
        QTest::qWait(100);
        row.setActiveColor(finalColor);
        QTest::qWait(75);
        QVERIFY(!QSettings().contains(key));
        QTRY_COMPARE(QSettings().value(key).toStringList().value(0),
            finalColor.name(QColor::HexArgb));
    }

    void changesLayerBlendModeFromDock()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        LayerDock dock(&controller);
        QComboBox *blendModeCombo =
            dock.findChild<QComboBox *>(QStringLiteral("layerBlendModeCombo"));
        QVERIFY(blendModeCombo);
        QCOMPARE(blendModeCombo->count(), 4);
        QCOMPARE(blendModeCombo->currentData().toInt(),
            static_cast<int>(LayerBlendMode::Normal));

        blendModeCombo->setCurrentIndex(blendModeCombo->findData(
            static_cast<int>(LayerBlendMode::Overlay)));
        QCOMPARE(controller.document().layers.first().blendMode,
            LayerBlendMode::Overlay);
        QCOMPARE(controller.undoStack()->count(), 1);

        controller.undoStack()->undo();
        QCOMPARE(controller.document().layers.first().blendMode,
            LayerBlendMode::Normal);
        QCOMPARE(blendModeCombo->currentData().toInt(),
            static_cast<int>(LayerBlendMode::Normal));
        controller.undoStack()->redo();
        QCOMPARE(blendModeCombo->currentData().toInt(),
            static_cast<int>(LayerBlendMode::Overlay));
    }

    void restoresRejectedLayerRenameInDock()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        const QUuid layerId = controller.document().activeLayerId;
        const QString originalName = controller.document().layer(layerId)->name;
        LayerDock dock(&controller);
        QListWidget *list = dock.findChild<QListWidget *>();
        QVERIFY(list);
        QCOMPARE(list->count(), 1);

        list->item(0)->setText(QString(
            DocumentLimits::maximumLayerNameLength + 1, QLatin1Char('x')));

        QCOMPARE(controller.document().layer(layerId)->name, originalName);
        QCOMPARE(list->item(0)->text(), originalName);
        QCOMPARE(controller.undoStack()->count(), 0);
    }

    void limitsLayerNameEditorLength()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        LayerDock dock(&controller);
        QListWidget *list = dock.findChild<QListWidget *>();
        QVERIFY(list);
        QCOMPARE(list->count(), 1);

        list->editItem(list->item(0));
        QLineEdit *editor = list->findChild<QLineEdit *>();
        QVERIFY(editor);
        QCOMPARE(editor->maxLength(), DocumentLimits::maximumLayerNameLength);
    }

    void reportsStrokeCommitFailureFromCanvas()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        const QUuid layerId = controller.document().activeLayerId;
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.fitToWindow();
        QSignalSpy messages(&canvas, &CanvasWidget::interactionMessage);
        const QPoint center = canvas.rect().center();

        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, center);
        controller.removeLayer(layerId);
        QTest::mouseRelease(
            &canvas, Qt::LeftButton, Qt::NoModifier, center + QPoint(20, 0));

        QCOMPARE(messages.size(), 1);
        QCOMPARE(messages.first().first().toString(),
            QStringLiteral("The stroke could not be added because its layer is "
                           "no longer available."));
        QVERIFY(controller.document().layers.isEmpty());
    }

    void managesLayerGroupsAndClippingFromDock()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        const QUuid layerId = controller.document().activeLayerId;
        LayerDock dock(&controller);
        QToolButton *groupButton = dock.findChild<QToolButton *>(
            QStringLiteral("layerAddGroupButton"));
        QComboBox *parentCombo = dock.findChild<QComboBox *>(
            QStringLiteral("layerParentGroupCombo"));
        QCheckBox *clipCheck =
            dock.findChild<QCheckBox *>(QStringLiteral("layerClipCheck"));
        QCheckBox *referenceCheck =
            dock.findChild<QCheckBox *>(QStringLiteral("layerReferenceCheck"));
        QListWidget *list = dock.findChild<QListWidget *>();
        QVERIFY(groupButton);
        QVERIFY(parentCombo);
        QVERIFY(clipCheck);
        QVERIFY(referenceCheck);
        QVERIFY(list);

        groupButton->click();
        QTRY_COMPARE(list->count(), 2);
        const Layer *layer = controller.document().layer(layerId);
        QVERIFY(layer);
        QVERIFY(!layer->parentGroupId.isNull());
        QCOMPARE(parentCombo->currentData().toUuid(), layer->parentGroupId);

        clipCheck->click();
        QVERIFY(controller.document().layer(layerId)->clipToLayerBelow);
        controller.undoStack()->undo();
        QVERIFY(!controller.document().layer(layerId)->clipToLayerBelow);

        referenceCheck->click();
        QVERIFY(controller.document().layer(layerId)->reference);
        controller.undoStack()->undo();
        QVERIFY(!controller.document().layer(layerId)->reference);
    }

    void filtersLayerHierarchyActionsAtDepthLimit()
    {
        DocumentController controller;
        QVERIFY(controller.newDocument(QSize(100, 100)));
        LayerDock dock(&controller);
        QToolButton *groupButton = dock.findChild<QToolButton *>(
            QStringLiteral("layerAddGroupButton"));
        QComboBox *parentCombo = dock.findChild<QComboBox *>(
            QStringLiteral("layerParentGroupCombo"));
        QListWidget *list = dock.findChild<QListWidget *>();
        QVERIFY(groupButton);
        QVERIFY(parentCombo);
        QVERIFY(list);

        for (int depth = 0; depth < DocumentLimits::maximumLayerDepth; ++depth)
        {
            QVERIFY(groupButton->isEnabled());
            groupButton->click();
        }
        QVERIFY(!groupButton->isEnabled());

        QUuid hierarchyRootId;
        for (const Layer &layer : controller.document().layers)
        {
            if (layer.kind == LayerKind::Group && layer.parentGroupId.isNull())
            {
                hierarchyRootId = layer.id;
                break;
            }
        }
        QVERIFY(!hierarchyRootId.isNull());

        controller.addLayerGroup();
        QUuid emptyRootId;
        for (const Layer &layer : controller.document().layers)
        {
            if (layer.kind == LayerKind::Group && layer.parentGroupId.isNull()
                && layer.id != hierarchyRootId)
            {
                emptyRootId = layer.id;
                break;
            }
        }
        QVERIFY(!emptyRootId.isNull());

        QListWidgetItem *hierarchyRootItem = nullptr;
        for (int row = 0; row < list->count(); ++row)
        {
            QListWidgetItem *item = list->item(row);
            if (item->data(LayerItemRoles::LayerId).toUuid() == hierarchyRootId)
            {
                hierarchyRootItem = item;
                break;
            }
        }
        QVERIFY(hierarchyRootItem);
        list->setCurrentItem(hierarchyRootItem);

        QVERIFY(!groupButton->isEnabled());
        QCOMPARE(parentCombo->findData(QVariant::fromValue(emptyRootId)), -1);
        QVERIFY(parentCombo->findData(QVariant::fromValue(QUuid())) >= 0);
    }

    void movesLayerAcrossGroupsByDrag()
    {
        DocumentController controller;
        QVERIFY(controller.newDocument(QSize(96, 96)));
        const QUuid firstPaintId = controller.document().activeLayerId;
        controller.addLayerGroup(firstPaintId);
        const QUuid groupId =
            controller.document().layer(firstPaintId)->parentGroupId;
        controller.addLayer();
        const QUuid secondPaintId = controller.document().activeLayerId;
        controller.addLayer();
        const QUuid rootPaintId = controller.document().activeLayerId;
        controller.setLayerParentGroup(rootPaintId, {});

        LayerDock dock(&controller);
        auto *list = dock.findChild<LayerListWidget *>();
        QVERIFY(list);
        const auto rowOf = [list](const QUuid &id)
        {
            for (int row = 0; row < list->count(); ++row)
            {
                if (list->item(row)->data(LayerItemRoles::LayerId).toUuid()
                    == id)
                {
                    return row;
                }
            }
            return -1;
        };
        QCOMPARE(rowOf(groupId), 0);
        QCOMPARE(rowOf(secondPaintId), 1);
        QCOMPARE(rowOf(firstPaintId), 2);
        QCOMPARE(rowOf(rootPaintId), 3);

        emit list->dropRequested(rowOf(firstPaintId),
            rowOf(rootPaintId),
            LayerListWidget::DropPlacement::AboveTarget);
        QVERIFY(
            controller.document().layer(firstPaintId)->parentGroupId.isNull());
        QCOMPARE(rowOf(groupId), 0);
        QCOMPARE(rowOf(secondPaintId), 1);
        QCOMPARE(rowOf(firstPaintId), 2);
        QCOMPARE(rowOf(rootPaintId), 3);

        controller.undoStack()->undo();
        QCOMPARE(
            controller.document().layer(firstPaintId)->parentGroupId, groupId);
        QCOMPARE(rowOf(firstPaintId), 2);
        controller.undoStack()->redo();
        QVERIFY(
            controller.document().layer(firstPaintId)->parentGroupId.isNull());
        controller.undoStack()->undo();

        emit list->dropRequested(rowOf(firstPaintId),
            rowOf(secondPaintId),
            LayerListWidget::DropPlacement::AboveTarget);
        QCOMPARE(
            controller.document().layer(firstPaintId)->parentGroupId, groupId);
        QCOMPARE(rowOf(firstPaintId), 1);
        QCOMPARE(rowOf(secondPaintId), 2);

        emit list->dropRequested(rowOf(rootPaintId),
            rowOf(groupId),
            LayerListWidget::DropPlacement::BelowTarget);
        QCOMPARE(
            controller.document().layer(rootPaintId)->parentGroupId, groupId);
        QCOMPARE(rowOf(rootPaintId), 1);

        emit list->dropRequested(
            rowOf(rootPaintId), -1, LayerListWidget::DropPlacement::OnViewport);
        QVERIFY(
            controller.document().layer(rootPaintId)->parentGroupId.isNull());
        QCOMPARE(rowOf(rootPaintId), list->count() - 1);
    }

    void blocksDrawingWhileGroupSelected()
    {
        MainWindow window;
        window.resize(1100, 720);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        LayerDock *layerDock = window.findChild<LayerDock *>();
        QVERIFY(canvas);
        QVERIFY(layerDock);
        auto *list = layerDock->findChild<LayerListWidget *>();
        QVERIFY(list);

        DocumentController &controller =
            MainWindowTestAccess::controller(window);
        const QUuid paintId = controller.document().activeLayerId;
        controller.addLayerGroup(paintId);
        const QUuid groupId =
            controller.document().layer(paintId)->parentGroupId;

        const auto selectRowOf = [list](const QUuid &id)
        {
            for (int row = 0; row < list->count(); ++row)
            {
                QListWidgetItem *item = list->item(row);
                if (item->data(LayerItemRoles::LayerId).toUuid() == id)
                {
                    list->setCurrentItem(item);
                    return true;
                }
            }
            return false;
        };
        const auto strokeCount = [&controller, &paintId]()
        {
            const Layer *layer = controller.document().layer(paintId);
            return layer ? layer->strokes.size() : qsizetype(-1);
        };
        const auto drag = [canvas]()
        {
            const QPoint start = canvas->rect().center() - QPoint(40, 10);
            const QPoint end = canvas->rect().center() + QPoint(40, 10);
            QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, start);
            QTest::mouseMove(canvas, end, 10);
            QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, end);
        };

        QVERIFY(selectRowOf(groupId));
        QCOMPARE(canvas->cursor().shape(), Qt::ForbiddenCursor);
        QSignalSpy messages(canvas, &CanvasWidget::interactionMessage);
        drag();
        QCOMPARE(strokeCount(), 0);
        QVERIFY(!messages.isEmpty());

        canvas->setTool(CanvasWidget::Tool::Bucket);
        QTest::mouseClick(
            canvas, Qt::LeftButton, Qt::NoModifier, canvas->rect().center());
        QCOMPARE(strokeCount(), 0);
        canvas->setTool(CanvasWidget::Tool::Brush);

        QVERIFY(selectRowOf(paintId));
        QVERIFY(canvas->cursor().shape() != Qt::ForbiddenCursor);
        drag();
        QCOMPARE(strokeCount(), 1);
    }

    void rejectsInvalidLayerDrops()
    {
        DocumentController controller;
        QVERIFY(controller.newDocument(QSize(96, 96)));
        const QUuid paintId = controller.document().activeLayerId;
        for (int depth = 0; depth < DocumentLimits::maximumLayerDepth; ++depth)
        {
            controller.addLayerGroup(paintId);
        }
        controller.addLayerGroup(QUuid());
        const QUuid emptyGroupId = controller.document().layers.last().id;
        QUuid outerGroupId;
        for (const Layer &layer : controller.document().layers)
        {
            if (layer.kind == LayerKind::Group && layer.parentGroupId.isNull()
                && layer.id != emptyGroupId)
            {
                outerGroupId = layer.id;
                break;
            }
        }
        QVERIFY(!outerGroupId.isNull());
        QUuid secondGroupId;
        for (const Layer &layer : controller.document().layers)
        {
            if (layer.parentGroupId == outerGroupId)
            {
                secondGroupId = layer.id;
                break;
            }
        }
        QVERIFY(!secondGroupId.isNull());

        LayerDock dock(&controller);
        auto *list = dock.findChild<LayerListWidget *>();
        QVERIFY(list);
        const auto rowOf = [list](const QUuid &id)
        {
            for (int row = 0; row < list->count(); ++row)
            {
                if (list->item(row)->data(LayerItemRoles::LayerId).toUuid()
                    == id)
                {
                    return row;
                }
            }
            return -1;
        };
        const int historyCount = controller.undoStack()->count();

        emit list->dropRequested(rowOf(outerGroupId),
            rowOf(emptyGroupId),
            LayerListWidget::DropPlacement::BelowTarget);
        QVERIFY(
            controller.document().layer(outerGroupId)->parentGroupId.isNull());

        emit list->dropRequested(rowOf(outerGroupId),
            rowOf(secondGroupId),
            LayerListWidget::DropPlacement::BelowTarget);
        QVERIFY(
            controller.document().layer(outerGroupId)->parentGroupId.isNull());
        QCOMPARE(controller.document().layer(secondGroupId)->parentGroupId,
            outerGroupId);
        QCOMPARE(controller.undoStack()->count(), historyCount);
    }

    void warnsWhenOpeningLegacyLayerHierarchy()
    {
        EnvironmentVariableGuard environmentGuard(
            QByteArrayLiteral("WAGLEWAGLEPAINT_RECOVERY_PATH"));
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        qputenv("WAGLEWAGLEPAINT_RECOVERY_PATH",
            directory.filePath(QStringLiteral("recovery.wagle")).toUtf8());
        const QString projectPath =
            directory.filePath(QStringLiteral("legacy-depth.wagle"));
        QString error;
        QVERIFY2(DocumentSerializer::save(projectPath,
                     nestedLayerDocument(DocumentLimits::maximumLayerDepth + 1),
                     &error),
            qPrintable(error));

        MainWindow window;
        bool warningAccepted = false;
        QTimer::singleShot(0,
            &window,
            [&warningAccepted]()
            {
                QDialog *dialog =
                    qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (!dialog
                    || dialog->objectName()
                           != QStringLiteral("legacyLayerDepthWarning"))
                {
                    return;
                }
                warningAccepted = true;
                dialog->accept();
            });

        QVERIFY(window.openFile(projectPath));
        QVERIFY(warningAccepted);
        QCOMPARE(
            window.windowFilePath(), QFileInfo(projectPath).absoluteFilePath());
    }

    void warnsWhenRecoveringLegacyLayerHierarchy()
    {
        EnvironmentVariableGuard environmentGuard(
            QByteArrayLiteral("WAGLEWAGLEPAINT_RECOVERY_PATH"));
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString recoveryPath =
            directory.filePath(QStringLiteral("recovery.wagle"));
        qputenv("WAGLEWAGLEPAINT_RECOVERY_PATH", recoveryPath.toUtf8());
        QString error;
        QVERIFY2(DocumentSerializer::save(recoveryPath,
                     nestedLayerDocument(DocumentLimits::maximumLayerDepth + 1),
                     &error),
            qPrintable(error));

        bool recoverClicked = false;
        bool warningAccepted = false;
        MainWindow window;
        scheduleDialogButtonClickAndAcceptNext(&window,
            QStringLiteral("startupRecoverButton"),
            &recoverClicked,
            &warningAccepted);

        QCOMPARE(
            window.initializeSession(), MainWindow::StartupResult::Recovered);
        QVERIFY(recoverClicked);
        QVERIFY(warningAccepted);
        QVERIFY(window.windowFilePath().isEmpty());
    }

    void explainsStaticExportHierarchyMemoryLimit()
    {
        MainWindow window;
        QVERIFY(MainWindowTestAccess::loadDocument(window,
            nestedLayerDocument(
                DocumentLimits::maximumLayerDepth, QSize(4096, 4096))));
        bool warningAccepted = false;
        QTimer::singleShot(0,
            &window,
            [&warningAccepted]()
            {
                QDialog *dialog =
                    qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (!dialog
                    || dialog->objectName()
                           != QStringLiteral("staticExportMemoryWarning"))
                {
                    return;
                }
                warningAccepted = true;
                dialog->accept();
            });

        MainWindowTestAccess::exportImage(window);

        QVERIFY(warningAccepted);
    }

    void exportsSnapshotsOffThreadAndCancelsWithoutPartialFiles()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        Document document = Document::createDefault(QSize(512, 512));
        document.background = Qt::transparent;
        Stroke stroke;
        stroke.width = 16.0;
        stroke.points = {
            {QPointF(32.0, 32.0), 1.0}, {QPointF(480.0, 480.0), 1.0}};
        document.layers.first().strokes.append(stroke);

        ExportWorker worker;
        QSignalSpy finished(&worker, &ExportWorker::finished);
        bool eventLoopAdvanced = false;
        QTimer::singleShot(0,
            &worker,
            [&eventLoopAdvanced]()
            {
                eventLoopAdvanced = true;
            });
        const QString imagePath =
            directory.filePath(QStringLiteral("snapshot.png"));
        QVERIFY(worker.startImage(document, 0, imagePath, false));
        QVERIFY(worker.isBusy());
        QTRY_VERIFY(eventLoopAdvanced);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QVERIFY(QFileInfo::exists(imagePath));
        QVERIFY(!QImage(imagePath).isNull());

        document.animationFrames = 60;
        const QString gifPath =
            directory.filePath(QStringLiteral("canceled.gif"));
        QVERIFY(worker.startGif(document, gifPath));
        worker.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 5000);
        const QList<QVariant> canceledResult = finished.at(1);
        QVERIFY(!canceledResult.at(1).toBool());
        QVERIFY(canceledResult.at(2).toBool());
        QVERIFY(!QFileInfo::exists(gifPath));
    }

    void editsStrokePropertyDialogValues()
    {
        StrokePropertiesDialog::Values values;
        values.colorSupported = true;
        values.widthSupported = true;
        values.roughnessSupported = true;
        values.color = QColor(10, 20, 30);
        values.width = 8.0;
        values.roughness = 0.75;
        StrokePropertiesDialog dialog(values);

        QDoubleSpinBox *width = dialog.findChild<QDoubleSpinBox *>(
            QStringLiteral("strokeWidthSpin"));
        QDoubleSpinBox *roughness = dialog.findChild<QDoubleSpinBox *>(
            QStringLiteral("strokeRoughnessSpin"));
        QVERIFY(width);
        QVERIFY(roughness);
        width->setValue(18.0);
        roughness->setValue(140.0);
        QVERIFY(dialog.width().has_value());
        QVERIFY(dialog.roughness().has_value());
        QVERIFY(dialog.color().has_value());
        QCOMPARE(*dialog.width(), 18.0);
        QCOMPARE(*dialog.roughness(), 1.4);
        QCOMPARE(*dialog.color(), *values.color);
    }

    void editsAndRestoresShortcuts()
    {
        const QString brushKey = QStringLiteral("shortcuts/brushAction");
        const QString eraserKey = QStringLiteral("shortcuts/eraserAction");
        const QString folderKey = QStringLiteral("files/defaultSaveFolder");
        const QString languageKey = QStringLiteral("appearance/language");
        SettingValueGuard brushGuard(brushKey);
        SettingValueGuard eraserGuard(eraserKey);
        SettingValueGuard folderGuard(folderKey);
        SettingValueGuard languageGuard(languageKey);
        QTemporaryDir saveFolder;
        QVERIFY(saveFolder.isValid());
        QSettings settings;
        settings.remove(brushKey);
        settings.remove(eraserKey);
        settings.setValue(folderKey, saveFolder.path());
        settings.remove(languageKey);
        settings.sync();

        QAction brushAction(QStringLiteral("Brush"));
        brushAction.setObjectName(QStringLiteral("brushAction"));
        ShortcutBinding::initialize(
            &brushAction, QKeySequence(QStringLiteral("B")));

        QAction eraserAction(QStringLiteral("Eraser"));
        eraserAction.setObjectName(QStringLiteral("eraserAction"));
        ShortcutBinding::initialize(
            &eraserAction, QKeySequence(QStringLiteral("E")));

        SettingsDialog dialog(nullptr, {&brushAction, &eraserAction});
        QLineEdit *folderEdit = dialog.findChild<QLineEdit *>(
            QStringLiteral("defaultSaveFolderEdit"));
        QKeySequenceEdit *brushEditor = dialog.findChild<QKeySequenceEdit *>(
            QStringLiteral("brushActionShortcutEdit"));
        QComboBox *languageCombo =
            dialog.findChild<QComboBox *>(QStringLiteral("languageCombo"));
        QVERIFY(folderEdit);
        QVERIFY(brushEditor);
        QVERIFY(languageCombo);
        QCOMPARE(folderEdit->text(), saveFolder.path());
        QCOMPARE(SettingsDialog::defaultSaveFolder(), saveFolder.path());
        QCOMPARE(SettingsDialog::uiLanguage(), QStringLiteral("system"));

        languageCombo->setCurrentIndex(
            languageCombo->findData(QStringLiteral("ko")));
        QCOMPARE(SettingsDialog::uiLanguage(), QStringLiteral("ko"));

        brushEditor->setKeySequence(QKeySequence(QStringLiteral("V")));
        QTRY_COMPARE(brushAction.shortcut(), QKeySequence(QStringLiteral("V")));
        QCOMPARE(settings.value(brushKey).toString(), QStringLiteral("V"));

        brushEditor->setKeySequence(QKeySequence(QStringLiteral("E")));
        QTRY_COMPARE(
            brushEditor->keySequence(), QKeySequence(QStringLiteral("V")));
        QCOMPARE(brushAction.shortcut(), QKeySequence(QStringLiteral("V")));

        QDialogButtonBox *buttons = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(buttons);
        QPushButton *restoreButton =
            buttons->button(QDialogButtonBox::RestoreDefaults);
        QVERIFY(restoreButton);
        QTest::mouseClick(restoreButton, Qt::LeftButton);
        QCOMPARE(brushAction.shortcut(), QKeySequence(QStringLiteral("B")));
        QVERIFY(!settings.contains(brushKey));
        QVERIFY(!settings.contains(folderKey));
        QVERIFY(!settings.contains(languageKey));
        QCOMPARE(folderEdit->text(), SettingsDialog::defaultSaveFolder());
        QCOMPARE(SettingsDialog::uiLanguage(), QStringLiteral("system"));
    }

    void preservesShortcutAliasesAcrossEditingAndRestore()
    {
        const QString zoomKey = QStringLiteral("shortcuts/zoomAction");
        const QString otherKey = QStringLiteral("shortcuts/otherAction");
        SettingValueGuard zoomGuard(zoomKey);
        SettingValueGuard otherGuard(otherKey);
        QSettings settings;
        settings.remove(zoomKey);
        settings.remove(otherKey);

        QAction zoomAction(QStringLiteral("Zoom in"));
        zoomAction.setObjectName(QStringLiteral("zoomAction"));
        const QKeySequence zoomDefault(QStringLiteral("Ctrl++"));
        const QKeySequence zoomAlias(QStringLiteral("Ctrl+="));
        ShortcutBinding::initialize(&zoomAction, zoomDefault, {zoomAlias});

        QAction otherAction(QStringLiteral("Other"));
        otherAction.setObjectName(QStringLiteral("otherAction"));
        const QKeySequence otherDefault(QStringLiteral("O"));
        ShortcutBinding::initialize(&otherAction, otherDefault);

        SettingsDialog dialog(nullptr, {&zoomAction, &otherAction});
        QKeySequenceEdit *zoomEditor = dialog.findChild<QKeySequenceEdit *>(
            QStringLiteral("zoomActionShortcutEdit"));
        QKeySequenceEdit *otherEditor = dialog.findChild<QKeySequenceEdit *>(
            QStringLiteral("otherActionShortcutEdit"));
        QVERIFY(zoomEditor);
        QVERIFY(otherEditor);
        QCOMPARE(zoomAction.shortcuts(),
            QList<QKeySequence>({zoomDefault, zoomAlias}));

        otherEditor->setKeySequence(zoomAlias);
        QTRY_COMPARE(otherEditor->keySequence(), otherDefault);
        QCOMPARE(otherAction.shortcuts(), QList<QKeySequence>({otherDefault}));

        const QKeySequence customPrimary(QStringLiteral("Ctrl+I"));
        zoomEditor->setKeySequence(customPrimary);
        QTRY_COMPARE(ShortcutBinding::primary(&zoomAction), customPrimary);
        QCOMPARE(zoomAction.shortcuts(),
            QList<QKeySequence>({customPrimary, zoomAlias}));
        QCOMPARE(settings.value(zoomKey).toString(),
            customPrimary.toString(QKeySequence::PortableText));

        QDialogButtonBox *buttons = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(buttons);
        QPushButton *restoreButton =
            buttons->button(QDialogButtonBox::RestoreDefaults);
        QVERIFY(restoreButton);
        QTest::mouseClick(restoreButton, Qt::LeftButton);
        QCOMPARE(ShortcutBinding::primary(&zoomAction), zoomDefault);
        QCOMPARE(zoomAction.shortcuts(),
            QList<QKeySequence>({zoomDefault, zoomAlias}));
        QVERIFY(!settings.contains(zoomKey));
    }

    void preservesPersistedPrimaryShortcutsOverNewAliases()
    {
        const QString zoomKey = QStringLiteral("shortcuts/zoomInAction");
        const QString otherKey = QStringLiteral("shortcuts/actualSizeAction");
        SettingValueGuard zoomGuard(zoomKey);
        SettingValueGuard otherGuard(otherKey);
        const QKeySequence zoomAlias(QStringLiteral("Ctrl+="));
        const QKeySequence zoomPrimary(QStringLiteral("Ctrl+I"));
        QSettings settings;
        settings.setValue(
            zoomKey, zoomPrimary.toString(QKeySequence::PortableText));
        settings.setValue(
            otherKey, zoomAlias.toString(QKeySequence::PortableText));
        settings.sync();

        MainWindow window;
        QAction *zoomAction =
            window.findChild<QAction *>(QStringLiteral("zoomInAction"));
        QAction *otherAction =
            window.findChild<QAction *>(QStringLiteral("actualSizeAction"));
        QVERIFY(zoomAction);
        QVERIFY(otherAction);
        QCOMPARE(ShortcutBinding::primary(zoomAction), zoomPrimary);
        QCOMPARE(ShortcutBinding::primary(otherAction), zoomAlias);
        QCOMPARE(zoomAction->shortcuts(), QList<QKeySequence>({zoomPrimary}));
        QCOMPARE(otherAction->shortcuts(), QList<QKeySequence>({zoomAlias}));

        SettingsDialog dialog(nullptr, {zoomAction, otherAction});

        QKeySequenceEdit *otherEditor = dialog.findChild<QKeySequenceEdit *>(
            QStringLiteral("actualSizeActionShortcutEdit"));
        QVERIFY(otherEditor);
        const QKeySequence replacement(QStringLiteral("Ctrl+K"));
        otherEditor->setKeySequence(replacement);
        QTRY_COMPARE(ShortcutBinding::primary(otherAction), replacement);
        QCOMPARE(zoomAction->shortcuts(),
            QList<QKeySequence>({zoomPrimary, zoomAlias}));
    }

    void showsApplicationVersionInAboutTab()
    {
        const ApplicationVersionGuard versionGuard;
        QApplication::setApplicationVersion(QStringLiteral("9.8.7-test"));

        SettingsDialog dialog;
        QTabWidget *tabs = dialog.findChild<QTabWidget *>();
        QLabel *versionLabel = dialog.findChild<QLabel *>(
            QStringLiteral("applicationVersionLabel"));
        QWidget *aboutTab =
            dialog.findChild<QWidget *>(QStringLiteral("aboutTab"));
        QVERIFY(tabs);
        QVERIFY(versionLabel);
        QVERIFY(aboutTab);
        QCOMPARE(versionLabel->text(), QStringLiteral("Version 9.8.7-test"));
        QCOMPARE(
            tabs->tabText(tabs->indexOf(aboutTab)), QStringLiteral("About"));
    }

    void migratesGlobalStrokeStabilizationToDrawingTools()
    {
        const QString legacyKey = QStringLiteral("canvas/strokeStabilization");
        QSettings settings;
        settings.setValue(legacyKey, 0.65);
        settings.sync();

        MainWindow window;
        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QVERIFY(canvas);
        for (const BrushPreset &preset : BrushPresetCatalog::builtIns())
        {
            QCOMPARE(canvas->brushPresetStabilization(preset.id), 0.65);
            QVERIFY(settings.contains(
                QStringLiteral("drawingTools/brush/presetStabilizations/%1")
                    .arg(preset.id)));
        }
        for (const EraserPreset &preset : EraserPresetCatalog::builtIns())
        {
            QCOMPARE(canvas->eraserPresetStabilization(preset.id), 0.65);
            QVERIFY(settings.contains(
                QStringLiteral("drawingTools/eraser/presetStabilizations/%1")
                    .arg(preset.id)));
        }
        QVERIFY(!settings.contains(
            QStringLiteral("drawingTools/eraser/stabilization")));
        QVERIFY(!settings.contains(legacyKey));
    }

    void configuresCanvasSizeDialog()
    {
        CanvasSizeDialog dialog(QSize(640, 480));
        QCheckBox *relativeCheck = dialog.findChild<QCheckBox *>(
            QStringLiteral("canvasRelativeSizeCheck"));
        QSpinBox *widthSpin =
            dialog.findChild<QSpinBox *>(QStringLiteral("canvasWidthSpin"));
        QSpinBox *heightSpin =
            dialog.findChild<QSpinBox *>(QStringLiteral("canvasHeightSpin"));
        QSpinBox *offsetXSpin =
            dialog.findChild<QSpinBox *>(QStringLiteral("canvasOffsetXSpin"));
        QSpinBox *offsetYSpin =
            dialog.findChild<QSpinBox *>(QStringLiteral("canvasOffsetYSpin"));
        QToolButton *topLeft = dialog.findChild<QToolButton *>(
            QStringLiteral("canvasAnchorTopLeft"));
        QToolButton *center = dialog.findChild<QToolButton *>(
            QStringLiteral("canvasAnchorCenter"));
        QToolButton *bottomRight = dialog.findChild<QToolButton *>(
            QStringLiteral("canvasAnchorBottomRight"));
        QVERIFY(relativeCheck);
        QVERIFY(widthSpin);
        QVERIFY(heightSpin);
        QVERIFY(offsetXSpin);
        QVERIFY(offsetYSpin);
        QVERIFY(topLeft);
        QVERIFY(center);
        QVERIFY(bottomRight);

        QCOMPARE(dialog.canvasSize(), QSize(640, 480));
        QCOMPARE(dialog.contentOffset(), QPoint());
        QVERIFY(center->isChecked());

        widthSpin->setValue(800);
        heightSpin->setValue(600);
        QCOMPARE(dialog.canvasSize(), QSize(800, 600));
        QCOMPARE(dialog.contentOffset(), QPoint(80, 60));

        topLeft->click();
        QCOMPARE(dialog.contentOffset(), QPoint());
        bottomRight->click();
        QCOMPARE(dialog.contentOffset(), QPoint(160, 120));

        relativeCheck->setChecked(true);
        QCOMPARE(dialog.canvasSize(), QSize(800, 600));
        QCOMPARE(widthSpin->value(), 160);
        QCOMPARE(heightSpin->value(), 120);
        widthSpin->setValue(-100);
        heightSpin->setValue(20);
        QCOMPARE(dialog.canvasSize(), QSize(540, 500));
        QCOMPARE(dialog.contentOffset(), QPoint(-100, 20));

        center->click();
        QCOMPARE(dialog.contentOffset(), QPoint(-50, 10));
        offsetXSpin->setValue(37);
        offsetYSpin->setValue(-12);
        QCOMPARE(dialog.contentOffset(), QPoint(37, -12));
        QVERIFY(!center->isChecked());

        relativeCheck->setChecked(false);
        QCOMPARE(widthSpin->value(), 540);
        QCOMPARE(heightSpin->value(), 500);
        const CanvasSizeDialog::Result result = dialog.result();
        QCOMPARE(result.size, QSize(540, 500));
        QCOMPARE(result.contentOffset, QPoint(37, -12));
    }

    void configuresImageSizeDialog()
    {
        ImageSizeDialog dialog(QSize(640, 480));
        QSpinBox *widthSpin =
            dialog.findChild<QSpinBox *>(QStringLiteral("imageWidthSpin"));
        QSpinBox *heightSpin =
            dialog.findChild<QSpinBox *>(QStringLiteral("imageHeightSpin"));
        QDoubleSpinBox *percentageSpin = dialog.findChild<QDoubleSpinBox *>(
            QStringLiteral("imageScalePercentSpin"));
        QCheckBox *keepAspectCheck = dialog.findChild<QCheckBox *>(
            QStringLiteral("imageKeepAspectCheck"));
        QLabel *warningLabel = dialog.findChild<QLabel *>(
            QStringLiteral("imageDistortionWarningLabel"));
        QVERIFY(widthSpin);
        QVERIFY(heightSpin);
        QVERIFY(percentageSpin);
        QVERIFY(keepAspectCheck);
        QVERIFY(warningLabel);
        QVERIFY(keepAspectCheck->isChecked());

        widthSpin->setValue(1280);
        QCOMPARE(dialog.imageSize(), QSize(1280, 960));
        QVERIFY(qAbs(dialog.horizontalScale() - 2.0) < 0.0001);
        QVERIFY(qAbs(dialog.verticalScale() - 2.0) < 0.0001);

        percentageSpin->setValue(150.0);
        QCOMPARE(dialog.imageSize(), QSize(960, 720));
        keepAspectCheck->setChecked(false);
        widthSpin->setValue(800);
        heightSpin->setValue(900);
        const ImageSizeDialog::Result distorted = dialog.result();
        QCOMPARE(distorted.size, QSize(800, 900));
        QVERIFY(qAbs(distorted.horizontalScale - 1.25) < 0.0001);
        QVERIFY(qAbs(distorted.verticalScale - 1.875) < 0.0001);
        QVERIFY(warningLabel->text().contains(QStringLiteral("distorted")));

        keepAspectCheck->setChecked(true);
        QCOMPARE(dialog.imageSize(), QSize(800, 600));
        percentageSpin->setValue(200.0);
        QCOMPARE(dialog.imageSize(), QSize(1280, 960));
        const ImageSizeDialog::Result uniform = dialog.result();
        QVERIFY(qAbs(uniform.horizontalScale - 2.0) < 0.0001);
        QVERIFY(qAbs(uniform.verticalScale - 2.0) < 0.0001);
    }

    void unlocksImageSizeDialogForExtremeAspectRatios()
    {
        QCOMPARE(
            LayerThumbnailRenderer::renderSize(QSize(1, 4096)), QSize(1, 64));
        QCOMPARE(
            LayerThumbnailRenderer::renderSize(QSize(4096, 1)), QSize(96, 1));
        QCOMPARE(PreviewRenderPolicy::renderSize(QSize(4096, 4096), 16.0),
            QSize(4096, 4096));

        ImageSizeDialog dialog(QSize(4096, 1));
        QSpinBox *widthSpin =
            dialog.findChild<QSpinBox *>(QStringLiteral("imageWidthSpin"));
        QSpinBox *heightSpin =
            dialog.findChild<QSpinBox *>(QStringLiteral("imageHeightSpin"));
        QCheckBox *keepAspectCheck = dialog.findChild<QCheckBox *>(
            QStringLiteral("imageKeepAspectCheck"));
        QDialogButtonBox *buttons = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(widthSpin);
        QVERIFY(heightSpin);
        QVERIFY(keepAspectCheck);
        QVERIFY(buttons);
        QVERIFY(!keepAspectCheck->isChecked());
        QVERIFY(!keepAspectCheck->isEnabled());
        QCOMPARE(widthSpin->minimum(), 1);
        QCOMPARE(widthSpin->maximum(), 4096);
        QCOMPARE(heightSpin->minimum(), 1);
        QCOMPARE(heightSpin->maximum(), 4096);

        widthSpin->setValue(2048);
        QCOMPARE(dialog.imageSize(), QSize(2048, 1));
        QVERIFY(buttons->button(QDialogButtonBox::Ok)->isEnabled());

        ImageSizeDialog tallDialog(QSize(1, 4096));
        QCheckBox *tallKeepAspect = tallDialog.findChild<QCheckBox *>(
            QStringLiteral("imageKeepAspectCheck"));
        QVERIFY(tallKeepAspect);
        QVERIFY(!tallKeepAspect->isChecked());
        QVERIFY(!tallKeepAspect->isEnabled());

        ImageSizeDialog narrowDialog(QSize(100, 1));
        QCheckBox *narrowKeepAspect = narrowDialog.findChild<QCheckBox *>(
            QStringLiteral("imageKeepAspectCheck"));
        QSpinBox *narrowWidthSpin = narrowDialog.findChild<QSpinBox *>(
            QStringLiteral("imageWidthSpin"));
        QVERIFY(narrowKeepAspect);
        QVERIFY(narrowWidthSpin);
        QVERIFY(narrowKeepAspect->isChecked());
        QVERIFY(narrowKeepAspect->isEnabled());
        narrowWidthSpin->setValue(400);
        QCOMPARE(narrowDialog.imageSize(), QSize(400, 4));
    }

    void handlesUnsavedChangesDialogShortcuts_data()
    {
        QTest::addColumn<int>("key");
        QTest::addColumn<bool>("closesWindow");
        QTest::addColumn<bool>("savesDocument");

        QTest::newRow("save") << int(Qt::Key_S) << true << true;
        QTest::newRow("discard") << int(Qt::Key_N) << true << false;
        QTest::newRow("cancel") << int(Qt::Key_Escape) << false << false;
    }

    void handlesUnsavedChangesDialogShortcuts()
    {
        QFETCH(int, key);
        QFETCH(bool, closesWindow);
        QFETCH(bool, savesDocument);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        EnvironmentVariableGuard recoveryGuard(
            QByteArrayLiteral("WAGLEWAGLEPAINT_RECOVERY_PATH"));
        qputenv("WAGLEWAGLEPAINT_RECOVERY_PATH",
            directory.filePath(QStringLiteral("recovery.wagle")).toUtf8());
        const QString filePath =
            directory.filePath(QStringLiteral("shortcuts.wagle"));
        QString error;
        QVERIFY2(
            DocumentSerializer::save(
                filePath, Document::createDefault(QSize(100, 100)), &error),
            qPrintable(error));

        MainWindow window;
        window.resize(1000, 680);
        QVERIFY(window.openFile(filePath));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        QToolButton *addLayerButton =
            window.findChild<QToolButton *>(QStringLiteral("layerAddButton"));
        QVERIFY(addLayerButton);
        addLayerButton->click();
        QTRY_VERIFY(window.isWindowModified());

        bool dialogInspected = false;
        QString saveText;
        QString discardText;
        QString cancelText;
        QTimer::singleShot(0,
            &window,
            [&]()
            {
                QDialog *dialog =
                    qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (!dialog)
                {
                    return;
                }
                QPushButton *saveButton = dialog->findChild<QPushButton *>(
                    QStringLiteral("unsavedChangesSaveButton"));
                QPushButton *discardButton = dialog->findChild<QPushButton *>(
                    QStringLiteral("unsavedChangesDiscardButton"));
                QPushButton *cancelButton = dialog->findChild<QPushButton *>(
                    QStringLiteral("unsavedChangesCancelButton"));
                if (!saveButton || !discardButton || !cancelButton)
                {
                    QTest::keyClick(dialog, Qt::Key_Escape);
                    return;
                }
                saveText = saveButton->text();
                discardText = discardButton->text();
                cancelText = cancelButton->text();
                dialogInspected = true;
                QWidget *keyTarget = QApplication::focusWidget();
                QTest::keyClick(
                    keyTarget ? keyTarget : dialog, static_cast<Qt::Key>(key));
            });
        QTimer::singleShot(1000,
            &window,
            []()
            {
                QDialog *dialog =
                    qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (dialog)
                {
                    dialog->reject();
                }
            });

        QCOMPARE(window.close(), closesWindow);
        QVERIFY(dialogInspected);
        QCOMPARE(saveText, QStringLiteral("Save (S)"));
        QCOMPARE(discardText, QStringLiteral("Don't Save (N)"));
        QCOMPARE(cancelText, QStringLiteral("Cancel (ESC)"));

        const std::optional<Document> savedDocument =
            DocumentSerializer::load(filePath, &error);
        QVERIFY2(savedDocument.has_value(), qPrintable(error));
        QCOMPARE(savedDocument->layers.size(), savesDocument ? 2 : 1);
        if (!closesWindow)
        {
            QVERIFY(window.isVisible());
            QVERIFY(window.isWindowModified());
        }
    }
};

int runUiShellTests(int argc, char **argv)
{
    UiShellTests tests;
    return QTest::qExec(&tests, argc, argv);
}

}

#include "UiShellTests.moc"

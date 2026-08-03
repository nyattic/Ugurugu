#include "brush/BrushPreset.hpp"
#include "brush/EraserPreset.hpp"
#include "ui/BrushPopoverPanel.hpp"
#include "ui/CanvasWidget.hpp"
#include "ui/ColorSwatchRow.hpp"
#include "ui/EraserPopoverPanel.hpp"
#include "ui/Icons.hpp"
#include "ui/LassoPopoverPanel.hpp"
#include "ui/LayerDock.hpp"
#include "ui/MainWindow.hpp"
#include "ui/PopoverToolButton.hpp"
#include "ui/SelectionActionBar.hpp"
#include "ui/SettingsDialog.hpp"
#include "ui/ShortcutBinding.hpp"
#include "ui/Theme.hpp"
#include "ui/TimelineBar.hpp"
#include "ui/ToolPopover.hpp"
#include "ui/WandPopoverPanel.hpp"

#include <QActionGroup>
#include <QColorDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>

#include <algorithm>
#include <cmath>

namespace wobble
{

namespace
{

constexpr int minimumZoomPercent = 1;
constexpr int maximumZoomPercent = 1600;
constexpr int zoomSliderSteps = 1000;

int zoomPercentFromSlider(int value)
{
    const qreal progress = std::clamp(value, 0, zoomSliderSteps)
                           / static_cast<qreal>(zoomSliderSteps);
    const qreal minimum = std::log(minimumZoomPercent);
    const qreal maximum = std::log(maximumZoomPercent);
    return qRound(std::exp(minimum + (maximum - minimum) * progress));
}

int sliderFromZoomPercent(int percent)
{
    const qreal clamped =
        std::clamp(percent, minimumZoomPercent, maximumZoomPercent);
    const qreal minimum = std::log(minimumZoomPercent);
    const qreal maximum = std::log(maximumZoomPercent);
    return qRound(
        (std::log(clamped) - minimum) / (maximum - minimum) * zoomSliderSteps);
}
}

void MainWindow::createActions()
{
    const auto registerShortcut =
        [this](QAction *action,
            const QKeySequence &defaultShortcut,
            const QList<QKeySequence> &aliases = QList<QKeySequence>())
    {
        action->setProperty("shortcutLabel", action->text());
        ShortcutBinding::initialize(action, defaultShortcut, aliases);
        m_shortcutActions.append(action);
    };

    auto *newAction = new QAction(tr("&New"), this);
    newAction->setObjectName(QStringLiteral("newAction"));
    registerShortcut(newAction, QKeySequence(QKeySequence::New));
    connect(newAction, &QAction::triggered, this, &MainWindow::newDocument);

    auto *openAction = new QAction(tr("&Open…"), this);
    openAction->setObjectName(QStringLiteral("openAction"));
    registerShortcut(openAction, QKeySequence(QKeySequence::Open));
    connect(openAction, &QAction::triggered, this, &MainWindow::chooseOpenFile);

    m_saveAction = new QAction(tr("&Save"), this);
    m_saveAction->setObjectName(QStringLiteral("saveAction"));
    registerShortcut(m_saveAction, QKeySequence(QKeySequence::Save));
    connect(m_saveAction,
        &QAction::triggered,
        this,
        [this]()
        {
            save();
        });

    auto *saveAsAction = new QAction(tr("Save &As…"), this);
    saveAsAction->setObjectName(QStringLiteral("saveAsAction"));
    registerShortcut(saveAsAction, QKeySequence(QKeySequence::SaveAs));
    connect(saveAsAction,
        &QAction::triggered,
        this,
        [this]()
        {
            saveAs();
        });

    auto *exportGifAction = new QAction(tr("Export animated &GIF…"), this);
    exportGifAction->setObjectName(QStringLiteral("exportGifAction"));
    registerShortcut(exportGifAction, QKeySequence(QStringLiteral("Ctrl+E")));
    connect(exportGifAction, &QAction::triggered, this, &MainWindow::exportGif);

    auto *exportPngAction =
        new QAction(tr("Export current frame as &image…"), this);
    exportPngAction->setObjectName(QStringLiteral("exportPngAction"));
    registerShortcut(exportPngAction, {});
    connect(
        exportPngAction, &QAction::triggered, this, &MainWindow::exportImage);

    auto *quitAction = new QAction(tr("&Quit"), this);
    quitAction->setObjectName(QStringLiteral("quitAction"));
    registerShortcut(quitAction, QKeySequence(QKeySequence::Quit));
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    auto *settingsAction = new QAction(tr("&Settings…"), this);
    settingsAction->setObjectName(QStringLiteral("settingsAction"));
    settingsAction->setIcon(Icons::icon(IconGlyph::Settings));
    settingsAction->setToolTip(tr("Settings"));
    registerShortcut(settingsAction, QKeySequence(QKeySequence::Preferences));
    settingsAction->setMenuRole(QAction::PreferencesRole);
    connect(settingsAction,
        &QAction::triggered,
        this,
        [this]()
        {
            SettingsDialog dialog(this, m_shortcutActions);
            connect(&dialog,
                &SettingsDialog::animateWhileDrawingChanged,
                m_canvas,
                &CanvasWidget::setAnimateWhileDrawing);
            connect(&dialog,
                &SettingsDialog::wobbleAnimationEnabledChanged,
                this,
                &MainWindow::applyWobbleAnimationEnabled);
            dialog.exec();
        });

    auto *checkForUpdatesAction = new QAction(tr("Check for &Updates…"), this);
    checkForUpdatesAction->setObjectName(
        QStringLiteral("checkForUpdatesAction"));

    QAction *undoAction = new QAction(tr("&Undo"), this);
    undoAction->setObjectName(QStringLiteral("undoAction"));
    undoAction->setIcon(Icons::icon(IconGlyph::Undo));
    registerShortcut(undoAction, QKeySequence(QKeySequence::Undo));

    QAction *redoAction = new QAction(tr("&Redo"), this);
    redoAction->setObjectName(QStringLiteral("redoAction"));
    redoAction->setIcon(Icons::icon(IconGlyph::Redo));
    registerShortcut(redoAction, QKeySequence(QKeySequence::Redo));

    // A pending selection transform is undone before any history entry, so the
    // visible undo action reflects that first and the stack's own state only
    // when nothing is pending.
    const auto syncHistoryActions = [this, undoAction, redoAction]()
    {
        DocumentUndoStack *stack = m_controller.undoStack();
        const bool pending = m_canvas->hasPendingSelectionTransform();
        undoAction->setEnabled(pending || stack->canUndo());
        undoAction->setText(
            pending ? tr("Undo Selection Transform") : stack->undoText());
        redoAction->setEnabled(!pending && stack->canRedo());
        redoAction->setText(stack->redoText());
    };
    for (const auto signal : {&DocumentUndoStack::canUndoChanged,
             &DocumentUndoStack::canRedoChanged})
    {
        connect(m_controller.undoStack(), signal, this, syncHistoryActions);
    }
    for (const auto signal : {&DocumentUndoStack::undoTextChanged,
             &DocumentUndoStack::redoTextChanged})
    {
        connect(m_controller.undoStack(), signal, this, syncHistoryActions);
    }
    connect(m_canvas,
        &CanvasWidget::selectionTransformSessionChanged,
        this,
        syncHistoryActions);
    syncHistoryActions();
    connect(undoAction,
        &QAction::triggered,
        this,
        [this]()
        {
            if (m_canvas->hasPendingSelectionTransform())
            {
                m_canvas->cancelSelectionTransform();
                return;
            }
            m_controller.undoStack()->undo();
        });
    connect(redoAction,
        &QAction::triggered,
        this,
        [this]()
        {
            if (m_canvas->hasPendingSelectionTransform())
            {
                return;
            }
            m_controller.undoStack()->redo();
        });
    syncHistoryActions();

    auto *resizeCanvasAction = new QAction(tr("Change canvas size…"), this);
    resizeCanvasAction->setObjectName(QStringLiteral("resizeCanvasAction"));
    registerShortcut(resizeCanvasAction, {});
    connect(resizeCanvasAction,
        &QAction::triggered,
        this,
        &MainWindow::resizeCanvas);

    auto *resizeImageAction = new QAction(tr("Change image size…"), this);
    resizeImageAction->setObjectName(QStringLiteral("resizeImageAction"));
    registerShortcut(resizeImageAction, {});
    connect(
        resizeImageAction, &QAction::triggered, this, &MainWindow::resizeImage);

    auto *backgroundAction = new QAction(tr("Canvas background…"), this);
    backgroundAction->setObjectName(QStringLiteral("backgroundAction"));
    registerShortcut(backgroundAction, {});
    connect(backgroundAction,
        &QAction::triggered,
        this,
        &MainWindow::chooseBackgroundColor);

    m_moveSelectionAction = new QAction(tr("Move selection"), this);
    m_moveSelectionAction->setObjectName(QStringLiteral("moveSelectionAction"));
    m_moveSelectionAction->setCheckable(true);
    m_moveSelectionAction->setIcon(Icons::toggleIcon(IconGlyph::Move));
    m_moveSelectionAction->setToolTip(tr("Move selected content by dragging"));
    registerShortcut(m_moveSelectionAction, {});
    connect(m_moveSelectionAction,
        &QAction::toggled,
        m_canvas,
        &CanvasWidget::setSelectionMoveMode);
    connect(m_canvas,
        &CanvasWidget::selectionMoveModeChanged,
        m_moveSelectionAction,
        &QAction::setChecked);

    m_scaleSelectionAction = new QAction(tr("Scale selection…"), this);
    m_scaleSelectionAction->setObjectName(
        QStringLiteral("scaleSelectionAction"));
    m_scaleSelectionAction->setIcon(Icons::icon(IconGlyph::Scale));
    m_scaleSelectionAction->setToolTip(tr("Scale selected content"));
    m_scaleSelectionAction->setEnabled(false);
    registerShortcut(m_scaleSelectionAction, {});
    connect(m_scaleSelectionAction,
        &QAction::triggered,
        this,
        &MainWindow::scaleSelection);

    m_rotateSelectionAction = new QAction(tr("Rotate selection…"), this);
    m_rotateSelectionAction->setObjectName(
        QStringLiteral("rotateSelectionAction"));
    m_rotateSelectionAction->setIcon(Icons::icon(IconGlyph::Rotate));
    m_rotateSelectionAction->setToolTip(tr("Rotate selected content"));
    m_rotateSelectionAction->setEnabled(false);
    registerShortcut(m_rotateSelectionAction, {});
    connect(m_rotateSelectionAction,
        &QAction::triggered,
        this,
        &MainWindow::rotateSelection);

    m_selectAllAction = new QAction(tr("Select &all"), this);
    m_selectAllAction->setObjectName(QStringLiteral("selectAllAction"));
    m_selectAllAction->setToolTip(tr("Select the whole canvas"));
    registerShortcut(m_selectAllAction, QKeySequence(QKeySequence::SelectAll));
    connect(m_selectAllAction,
        &QAction::triggered,
        m_canvas,
        &CanvasWidget::selectAll);

    m_invertSelectionAction = new QAction(tr("&Invert selection"), this);
    m_invertSelectionAction->setObjectName(
        QStringLiteral("invertSelectionAction"));
    m_invertSelectionAction->setToolTip(tr("Invert the selected area"));
    m_invertSelectionAction->setEnabled(false);
    registerShortcut(m_invertSelectionAction,
        QKeySequence(QStringLiteral("Ctrl+Shift+I")));
    connect(m_invertSelectionAction,
        &QAction::triggered,
        m_canvas,
        &CanvasWidget::invertSelection);

    m_cutSelectionAction = new QAction(tr("Cu&t"), this);
    m_cutSelectionAction->setObjectName(QStringLiteral("cutSelectionAction"));
    m_cutSelectionAction->setToolTip(
        tr("Copy the selection to the clipboard and delete it"));
    m_cutSelectionAction->setEnabled(false);
    registerShortcut(m_cutSelectionAction, QKeySequence(QKeySequence::Cut));
    connect(m_cutSelectionAction,
        &QAction::triggered,
        m_canvas,
        &CanvasWidget::cutSelection);

    m_copySelectionAction = new QAction(tr("&Copy"), this);
    m_copySelectionAction->setObjectName(QStringLiteral("copySelectionAction"));
    m_copySelectionAction->setIcon(Icons::icon(IconGlyph::Copy));
    m_copySelectionAction->setToolTip(
        tr("Copy the selection to a new layer and the clipboard"));
    m_copySelectionAction->setEnabled(false);
    registerShortcut(m_copySelectionAction, QKeySequence(QKeySequence::Copy));
    connect(m_copySelectionAction,
        &QAction::triggered,
        m_canvas,
        &CanvasWidget::copySelection);

    m_pasteAction = new QAction(tr("&Paste"), this);
    m_pasteAction->setObjectName(QStringLiteral("pasteAction"));
    m_pasteAction->setToolTip(tr("Paste the clipboard as a new layer"));
    registerShortcut(m_pasteAction, QKeySequence(QKeySequence::Paste));
    connect(m_pasteAction,
        &QAction::triggered,
        this,
        &MainWindow::pasteFromClipboard);

    m_editStrokePropertiesAction =
        new QAction(tr("Edit selected stroke properties…"), this);
    m_editStrokePropertiesAction->setObjectName(
        QStringLiteral("editStrokePropertiesAction"));
    m_editStrokePropertiesAction->setIcon(Icons::icon(IconGlyph::Brush));
    m_editStrokePropertiesAction->setToolTip(
        tr("Change the color, width, or roughness of selected strokes"));
    m_editStrokePropertiesAction->setEnabled(false);
    registerShortcut(m_editStrokePropertiesAction, {});
    connect(m_editStrokePropertiesAction,
        &QAction::triggered,
        this,
        &MainWindow::editSelectedStrokeProperties);

    m_flipSelectionHorizontalAction =
        new QAction(tr("Flip selection horizontally"), this);
    m_flipSelectionHorizontalAction->setObjectName(
        QStringLiteral("flipSelectionHorizontalAction"));
    m_flipSelectionHorizontalAction->setIcon(
        Icons::icon(IconGlyph::MirrorHorizontal));
    m_flipSelectionHorizontalAction->setToolTip(
        tr("Flip selected content horizontally"));
    registerShortcut(m_flipSelectionHorizontalAction, {});
    connect(m_flipSelectionHorizontalAction,
        &QAction::triggered,
        m_canvas,
        &CanvasWidget::flipSelectionHorizontally);

    m_flipSelectionVerticalAction =
        new QAction(tr("Flip selection vertically"), this);
    m_flipSelectionVerticalAction->setObjectName(
        QStringLiteral("flipSelectionVerticalAction"));
    m_flipSelectionVerticalAction->setIcon(
        Icons::icon(IconGlyph::MirrorVertical));
    m_flipSelectionVerticalAction->setToolTip(
        tr("Flip selected content vertically"));
    registerShortcut(m_flipSelectionVerticalAction, {});
    connect(m_flipSelectionVerticalAction,
        &QAction::triggered,
        m_canvas,
        &CanvasWidget::flipSelectionVertically);

    m_applySelectionTransformAction = new QAction(tr("Apply transform"), this);
    m_applySelectionTransformAction->setObjectName(
        QStringLiteral("applySelectionTransformAction"));
    m_applySelectionTransformAction->setIcon(Icons::icon(IconGlyph::Confirm));
    m_applySelectionTransformAction->setToolTip(
        tr("Apply selection transform (Enter)"));
    m_applySelectionTransformAction->setEnabled(false);
    registerShortcut(m_applySelectionTransformAction,
        QKeySequence(QStringLiteral("Return")));
    connect(m_applySelectionTransformAction,
        &QAction::triggered,
        m_canvas,
        &CanvasWidget::applySelectionTransform);

    m_cancelSelectionTransformAction =
        new QAction(tr("Cancel transform"), this);
    m_cancelSelectionTransformAction->setObjectName(
        QStringLiteral("cancelSelectionTransformAction"));
    m_cancelSelectionTransformAction->setIcon(Icons::icon(IconGlyph::Cancel));
    m_cancelSelectionTransformAction->setToolTip(
        tr("Cancel selection transform (Esc)"));
    m_cancelSelectionTransformAction->setEnabled(false);
    registerShortcut(m_cancelSelectionTransformAction, {});
    connect(m_cancelSelectionTransformAction,
        &QAction::triggered,
        m_canvas,
        &CanvasWidget::cancelSelectionTransform);

    m_deleteSelectionAction = new QAction(tr("Delete selected content"), this);
    m_deleteSelectionAction->setObjectName(
        QStringLiteral("deleteSelectionAction"));
    m_deleteSelectionAction->setIcon(Icons::icon(IconGlyph::Delete));
    m_deleteSelectionAction->setToolTip(tr("Delete selected content"));
    registerShortcut(m_deleteSelectionAction, QKeySequence(Qt::Key_Delete));
    connect(m_deleteSelectionAction,
        &QAction::triggered,
        m_canvas,
        &CanvasWidget::deleteSelection);

    m_deselectSelectionAction = new QAction(tr("Deselect"), this);
    m_deselectSelectionAction->setObjectName(
        QStringLiteral("deselectSelectionAction"));
    m_deselectSelectionAction->setIcon(Icons::icon(IconGlyph::Deselect));
    m_deselectSelectionAction->setToolTip(tr("Deselect (Ctrl+D)"));
    registerShortcut(
        m_deselectSelectionAction, QKeySequence(QStringLiteral("Ctrl+D")));
    connect(m_deselectSelectionAction,
        &QAction::triggered,
        m_canvas,
        &CanvasWidget::deselectSelection);

    auto *escapeCanvasAction =
        new QAction(tr("Cancel current canvas action"), this);
    escapeCanvasAction->setObjectName(QStringLiteral("escapeCanvasAction"));
    escapeCanvasAction->setShortcutContext(Qt::WindowShortcut);
    registerShortcut(escapeCanvasAction, QKeySequence(Qt::Key_Escape));
    connect(escapeCanvasAction,
        &QAction::triggered,
        m_canvas,
        &CanvasWidget::handleEscape);

    const auto syncSelectionActions = [this](bool hasArea, bool hasContent)
    {
        m_moveSelectionAction->setEnabled(hasContent);
        m_scaleSelectionAction->setEnabled(hasContent);
        m_rotateSelectionAction->setEnabled(hasContent);
        m_cutSelectionAction->setEnabled(hasContent);
        m_copySelectionAction->setEnabled(hasContent);
        m_invertSelectionAction->setEnabled(hasArea);
        m_editStrokePropertiesAction->setEnabled(
            hasContent && m_canvas->hasEditableStrokeSelection()
            && !m_canvas->hasSelectionTransformSession());
        m_flipSelectionHorizontalAction->setEnabled(hasContent);
        m_flipSelectionVerticalAction->setEnabled(hasContent);
        m_deleteSelectionAction->setEnabled(hasContent);
        m_deselectSelectionAction->setEnabled(hasArea);
        if (!hasContent)
        {
            m_moveSelectionAction->setChecked(false);
        }
    };
    connect(m_canvas,
        &CanvasWidget::selectionAvailabilityChanged,
        this,
        syncSelectionActions);
    syncSelectionActions(
        m_canvas->hasSelection(), m_canvas->hasTransformableSelection());
    const auto syncSelectionTransformSession = [this](bool active, bool dirty)
    {
        m_applySelectionTransformAction->setEnabled(active && dirty);
        m_cancelSelectionTransformAction->setEnabled(active);
        m_editStrokePropertiesAction->setEnabled(
            !active && m_canvas->hasEditableStrokeSelection());
    };
    connect(m_canvas,
        &CanvasWidget::selectionTransformSessionChanged,
        this,
        syncSelectionTransformSession);
    syncSelectionTransformSession(m_canvas->hasSelectionTransformSession(),
        m_canvas->hasPendingSelectionTransform());

    auto *selectionBar = new SelectionActionBar(m_canvas);
    selectionBar->addAction(m_moveSelectionAction);
    selectionBar->addAction(m_scaleSelectionAction);
    selectionBar->addAction(m_rotateSelectionAction);
    selectionBar->addSeparator();
    selectionBar->addAction(m_flipSelectionHorizontalAction);
    selectionBar->addAction(m_flipSelectionVerticalAction);
    selectionBar->addSeparator();
    selectionBar->addAction(m_applySelectionTransformAction);
    selectionBar->addAction(m_cancelSelectionTransformAction);
    selectionBar->addSeparator();
    selectionBar->addAction(m_copySelectionAction);
    selectionBar->addAction(m_editStrokePropertiesAction);
    selectionBar->addAction(m_deleteSelectionAction);
    selectionBar->addSeparator();
    selectionBar->addAction(m_deselectSelectionAction);
    m_canvas->setSelectionActionBar(selectionBar);

    auto *clearLayerAction = new QAction(tr("Clear active layer"), this);
    clearLayerAction->setObjectName(QStringLiteral("clearLayerAction"));
    clearLayerAction->setEnabled(
        !m_controller.document().activeLayerId.isNull());
    registerShortcut(clearLayerAction, {});
    connect(clearLayerAction,
        &QAction::triggered,
        this,
        [this]()
        {
            m_controller.clearLayer(m_controller.document().activeLayerId);
        });
    connect(&m_controller,
        &DocumentController::activeLayerChanged,
        clearLayerAction,
        [clearLayerAction](const QUuid &id)
        {
            clearLayerAction->setEnabled(!id.isNull());
        });

    auto *zoomInAction = new QAction(tr("Zoom &in"), this);
    zoomInAction->setObjectName(QStringLiteral("zoomInAction"));
    registerShortcut(zoomInAction,
        QKeySequence(QKeySequence::ZoomIn),
        {QKeySequence(QStringLiteral("Ctrl+="))});
    connect(zoomInAction, &QAction::triggered, m_canvas, &CanvasWidget::zoomIn);

    auto *zoomOutAction = new QAction(tr("Zoom &out"), this);
    zoomOutAction->setObjectName(QStringLiteral("zoomOutAction"));
    registerShortcut(zoomOutAction, QKeySequence(QKeySequence::ZoomOut));
    connect(
        zoomOutAction, &QAction::triggered, m_canvas, &CanvasWidget::zoomOut);

    auto *actualSizeAction = new QAction(tr("Actual &pixels"), this);
    actualSizeAction->setObjectName(QStringLiteral("actualSizeAction"));
    actualSizeAction->setToolTip(tr("Show the canvas at 100%"));
    registerShortcut(actualSizeAction, QKeySequence(QStringLiteral("Ctrl+1")));
    connect(actualSizeAction,
        &QAction::triggered,
        m_canvas,
        &CanvasWidget::resetZoom);

    auto *fitAction = new QAction(tr("&Fit canvas"), this);
    fitAction->setObjectName(QStringLiteral("fitAction"));
    fitAction->setIcon(Icons::icon(IconGlyph::FitView));
    registerShortcut(fitAction, QKeySequence(QStringLiteral("Ctrl+0")));
    connect(
        fitAction, &QAction::triggered, m_canvas, &CanvasWidget::fitToWindow);

    m_mirrorCanvasAction = new QAction(tr("Flip canvas horizontally"), this);
    m_mirrorCanvasAction->setObjectName(QStringLiteral("mirrorCanvasAction"));
    m_mirrorCanvasAction->setCheckable(true);
    m_mirrorCanvasAction->setIcon(
        Icons::toggleIcon(IconGlyph::MirrorHorizontal));
    registerShortcut(m_mirrorCanvasAction, QKeySequence(QStringLiteral("M")));
    connect(m_mirrorCanvasAction,
        &QAction::toggled,
        m_canvas,
        &CanvasWidget::setCanvasMirrored);
    connect(m_canvas,
        &CanvasWidget::canvasMirroredChanged,
        m_mirrorCanvasAction,
        &QAction::setChecked);

    m_playAction = new QAction(tr("&Animate preview"), this);
    m_playAction->setCheckable(true);
    m_playAction->setChecked(true);
    m_playAction->setIcon(Icons::toggleIcon(IconGlyph::Play));
    m_playAction->setObjectName(QStringLiteral("playAction"));
    registerShortcut(m_playAction, QKeySequence(QStringLiteral("P")));
    connect(
        m_playAction, &QAction::toggled, m_canvas, &CanvasWidget::setAnimating);
    connect(m_canvas,
        &CanvasWidget::animatingChanged,
        m_playAction,
        &QAction::setChecked);

    applyWobbleAnimationEnabled(SettingsDialog::wobbleAnimationEnabled());

    m_brushAction = new QAction(tr("&Brush"), this);
    m_brushAction->setCheckable(true);
    m_brushAction->setIcon(Icons::toggleIcon(IconGlyph::Brush));
    m_brushAction->setObjectName(QStringLiteral("brushAction"));
    registerShortcut(m_brushAction, QKeySequence(QStringLiteral("B")));

    m_eraserAction = new QAction(tr("&Eraser"), this);
    m_eraserAction->setCheckable(true);
    m_eraserAction->setIcon(Icons::toggleIcon(IconGlyph::Eraser));
    m_eraserAction->setObjectName(QStringLiteral("eraserAction"));
    registerShortcut(m_eraserAction, QKeySequence(QStringLiteral("E")));

    m_lassoAction = new QAction(tr("&Area select"), this);
    m_lassoAction->setCheckable(true);
    m_lassoAction->setIcon(Icons::toggleIcon(IconGlyph::Lasso));
    m_lassoAction->setObjectName(QStringLiteral("lassoAction"));
    registerShortcut(m_lassoAction, QKeySequence(QStringLiteral("L")));

    m_wandAction = new QAction(tr("Auto se&lect"), this);
    m_wandAction->setCheckable(true);
    m_wandAction->setIcon(Icons::toggleIcon(IconGlyph::Wand));
    m_wandAction->setObjectName(QStringLiteral("wandAction"));
    registerShortcut(m_wandAction, QKeySequence(QStringLiteral("W")));

    m_bucketAction = new QAction(tr("Paint &bucket"), this);
    m_bucketAction->setCheckable(true);
    m_bucketAction->setIcon(Icons::toggleIcon(IconGlyph::Bucket));
    m_bucketAction->setObjectName(QStringLiteral("bucketAction"));
    registerShortcut(m_bucketAction, QKeySequence(QStringLiteral("G")));

    auto *toolGroup = new QActionGroup(this);
    toolGroup->setExclusive(true);
    toolGroup->addAction(m_brushAction);
    toolGroup->addAction(m_eraserAction);
    toolGroup->addAction(m_lassoAction);
    toolGroup->addAction(m_wandAction);
    toolGroup->addAction(m_bucketAction);
    connect(m_brushAction,
        &QAction::triggered,
        this,
        [this]()
        {
            m_canvas->setTool(CanvasWidget::Tool::Brush);
        });
    connect(m_eraserAction,
        &QAction::triggered,
        this,
        [this]()
        {
            m_canvas->setTool(CanvasWidget::Tool::Eraser);
        });
    connect(m_lassoAction,
        &QAction::triggered,
        this,
        [this]()
        {
            m_canvas->setTool(CanvasWidget::Tool::Lasso);
        });
    connect(m_wandAction,
        &QAction::triggered,
        this,
        [this]()
        {
            m_canvas->setTool(CanvasWidget::Tool::Wand);
        });
    connect(m_bucketAction,
        &QAction::triggered,
        this,
        [this]()
        {
            m_canvas->setTool(CanvasWidget::Tool::Bucket);
        });
    const auto syncToolAction = [this](CanvasWidget::Tool tool)
    {
        switch (tool)
        {
        case CanvasWidget::Tool::Brush:
            m_brushAction->setChecked(true);
            break;
        case CanvasWidget::Tool::Eraser:
            m_eraserAction->setChecked(true);
            break;
        case CanvasWidget::Tool::Lasso:
            m_lassoAction->setChecked(true);
            break;
        case CanvasWidget::Tool::Wand:
            m_wandAction->setChecked(true);
            break;
        case CanvasWidget::Tool::Bucket:
            m_bucketAction->setChecked(true);
            break;
        }
    };
    syncToolAction(m_canvas->tool());
    connect(m_canvas, &CanvasWidget::toolChanged, this, syncToolAction);

    ShortcutBinding::resolveAliasConflicts(m_shortcutActions);

    addAction(newAction);
    addAction(openAction);
    addAction(m_saveAction);
    addAction(saveAsAction);
    addAction(exportGifAction);
    addAction(exportPngAction);
    addAction(quitAction);
    addAction(undoAction);
    addAction(redoAction);
    addAction(resizeImageAction);
    addAction(resizeCanvasAction);
    addAction(m_selectAllAction);
    addAction(m_invertSelectionAction);
    addAction(m_cutSelectionAction);
    addAction(m_copySelectionAction);
    addAction(m_pasteAction);
    addAction(m_moveSelectionAction);
    addAction(m_scaleSelectionAction);
    addAction(m_rotateSelectionAction);
    addAction(m_flipSelectionHorizontalAction);
    addAction(m_flipSelectionVerticalAction);
    addAction(m_applySelectionTransformAction);
    addAction(m_cancelSelectionTransformAction);
    addAction(m_editStrokePropertiesAction);
    addAction(m_deleteSelectionAction);
    addAction(m_deselectSelectionAction);
    addAction(escapeCanvasAction);
    addAction(clearLayerAction);
    addAction(zoomInAction);
    addAction(zoomOutAction);
    addAction(actualSizeAction);
    addAction(fitAction);
    addAction(m_mirrorCanvasAction);
    addAction(m_playAction);
    addAction(checkForUpdatesAction);
}

void MainWindow::createMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(findChild<QAction *>(QStringLiteral("newAction")));
    fileMenu->addAction(findChild<QAction *>(QStringLiteral("openAction")));
    fileMenu->addSeparator();
    fileMenu->addAction(m_saveAction);
    fileMenu->addAction(findChild<QAction *>(QStringLiteral("saveAsAction")));
    fileMenu->addSeparator();
    fileMenu->addAction(
        findChild<QAction *>(QStringLiteral("exportGifAction")));
    fileMenu->addAction(
        findChild<QAction *>(QStringLiteral("exportPngAction")));
    fileMenu->addSeparator();
    fileMenu->addAction(findChild<QAction *>(QStringLiteral("quitAction")));

    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(findChild<QAction *>(QStringLiteral("undoAction")));
    editMenu->addAction(findChild<QAction *>(QStringLiteral("redoAction")));
    editMenu->addSeparator();
    editMenu->addAction(m_cutSelectionAction);
    editMenu->addAction(m_copySelectionAction);
    editMenu->addAction(m_pasteAction);
    editMenu->addSeparator();
    editMenu->addAction(
        findChild<QAction *>(QStringLiteral("resizeImageAction")));
    editMenu->addAction(
        findChild<QAction *>(QStringLiteral("resizeCanvasAction")));
    editMenu->addAction(
        findChild<QAction *>(QStringLiteral("backgroundAction")));
    editMenu->addSeparator();
    QMenu *selectionMenu = editMenu->addMenu(tr("&Selection"));
    selectionMenu->addAction(m_selectAllAction);
    selectionMenu->addAction(m_invertSelectionAction);
    selectionMenu->addAction(m_deselectSelectionAction);
    selectionMenu->addSeparator();
    selectionMenu->addAction(m_moveSelectionAction);
    selectionMenu->addAction(m_scaleSelectionAction);
    selectionMenu->addAction(m_rotateSelectionAction);
    selectionMenu->addSeparator();
    selectionMenu->addAction(m_flipSelectionHorizontalAction);
    selectionMenu->addAction(m_flipSelectionVerticalAction);
    selectionMenu->addSeparator();
    selectionMenu->addAction(m_applySelectionTransformAction);
    selectionMenu->addAction(m_cancelSelectionTransformAction);
    selectionMenu->addSeparator();
    selectionMenu->addAction(m_editStrokePropertiesAction);
    selectionMenu->addAction(m_deleteSelectionAction);
    editMenu->addSeparator();
    editMenu->addAction(
        findChild<QAction *>(QStringLiteral("clearLayerAction")));
    editMenu->addSeparator();
    editMenu->addAction(findChild<QAction *>(QStringLiteral("settingsAction")));

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(findChild<QAction *>(QStringLiteral("zoomInAction")));
    viewMenu->addAction(findChild<QAction *>(QStringLiteral("zoomOutAction")));
    viewMenu->addAction(
        findChild<QAction *>(QStringLiteral("actualSizeAction")));
    viewMenu->addAction(findChild<QAction *>(QStringLiteral("fitAction")));
    viewMenu->addAction(m_mirrorCanvasAction);
    viewMenu->addAction(m_playAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_layerDock->toggleViewAction());

    QMenu *toolMenu = menuBar()->addMenu(tr("&Tools"));
    toolMenu->addAction(m_brushAction);
    toolMenu->addAction(m_eraserAction);
    toolMenu->addAction(m_lassoAction);
    toolMenu->addAction(m_wandAction);
    toolMenu->addAction(m_bucketAction);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(
        findChild<QAction *>(QStringLiteral("checkForUpdatesAction")));
}

void MainWindow::createToolBars()
{
    auto *rail = new QToolBar(tr("Tools"), this);
    rail->setObjectName(QStringLiteral("ToolRail"));
    rail->setMovable(false);
    rail->setIconSize(QSize(24, 24));
    addToolBar(Qt::LeftToolBarArea, rail);

    const auto addRailButton = [rail](QAction *action, IconGlyph glyph)
    {
        auto *button = new PopoverToolButton(rail);
        button->setDefaultAction(action);
        button->setIconSize(rail->iconSize());
        button->setHoverGlyph(glyph);
        rail->addWidget(button);
        return button;
    };

    PopoverToolButton *brushButton =
        addRailButton(m_brushAction, IconGlyph::Brush);
    PopoverToolButton *eraserButton =
        addRailButton(m_eraserAction, IconGlyph::Eraser);
    PopoverToolButton *lassoButton =
        addRailButton(m_lassoAction, IconGlyph::Lasso);
    PopoverToolButton *wandButton =
        addRailButton(m_wandAction, IconGlyph::Wand);
    addRailButton(m_bucketAction, IconGlyph::Bucket);

    auto *brushPopover = new ToolPopover(this);
    auto *brushPanel = new BrushPopoverPanel(m_canvas);
    brushPopover->setContentWidget(brushPanel);
    connect(brushPopover,
        &ToolPopover::popoverShown,
        brushPanel,
        [brushPanel]()
        {
            brushPanel->setAnimationActive(true);
        });
    connect(brushPopover,
        &ToolPopover::popoverHidden,
        brushPanel,
        [brushPanel]()
        {
            brushPanel->setAnimationActive(false);
        });
    brushButton->setPopover(brushPopover);

    auto *eraserPopover = new ToolPopover(this);
    eraserPopover->setContentWidget(new EraserPopoverPanel(m_canvas));
    eraserButton->setPopover(eraserPopover);

    auto *lassoPopover = new ToolPopover(this);
    lassoPopover->setContentWidget(new LassoPopoverPanel(m_canvas));
    lassoButton->setPopover(lassoPopover);

    auto *wandPopover = new ToolPopover(this);
    wandPopover->setContentWidget(new WandPopoverPanel(m_canvas));
    wandButton->setPopover(wandPopover);

    connect(m_canvas,
        &CanvasWidget::brushPresetChanged,
        this,
        [this](const QString &)
        {
            if (m_canvas->tool() != CanvasWidget::Tool::Brush)
            {
                m_brushAction->trigger();
            }
        });
    connect(m_canvas,
        &CanvasWidget::eraserPresetChanged,
        this,
        [this](const QString &)
        {
            if (m_canvas->tool() != CanvasWidget::Tool::Eraser)
            {
                m_eraserAction->trigger();
            }
        });

    QToolBar *quick = addToolBar(tr("Quick access"));
    quick->setObjectName(QStringLiteral("PaintTools"));
    quick->setMovable(false);
    quick->setIconSize(QSize(22, 22));

    m_colorButton = new QPushButton(quick);
    m_colorButton->setFixedSize(28, 28);
    m_colorButton->setToolTip(tr("Choose brush color"));
    m_colorButton->setAccessibleName(tr("Brush color"));
    m_colorButton->setCursor(Qt::PointingHandCursor);
    connect(m_colorButton,
        &QPushButton::clicked,
        this,
        [this]()
        {
            const QColor color = QColorDialog::getColor(m_canvas->brushColor(),
                this,
                tr("Brush color"),
                QColorDialog::ShowAlphaChannel);
            if (color.isValid())
            {
                m_canvas->setBrushColor(color);
            }
        });
    auto *colorHolder = new QWidget(quick);
    auto *colorLayout = new QHBoxLayout(colorHolder);
    colorLayout->setContentsMargins(4, 0, 6, 0);
    colorLayout->addWidget(m_colorButton, 0, Qt::AlignVCenter);
    quick->addWidget(colorHolder);
    quick->addSeparator();

    m_swatchRow = new ColorSwatchRow(quick);
    connect(m_swatchRow,
        &ColorSwatchRow::colorSelected,
        m_canvas,
        &CanvasWidget::setBrushColor);
    quick->addWidget(m_swatchRow);

    auto *quickSpacer = new QWidget(quick);
    quickSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    quick->addWidget(quickSpacer);

    quick->addAction(findChild<QAction *>(QStringLiteral("undoAction")));
    quick->addAction(findChild<QAction *>(QStringLiteral("redoAction")));
    quick->addSeparator();
    QAction *settingsAction =
        findChild<QAction *>(QStringLiteral("settingsAction"));
    quick->addAction(settingsAction);
    if (QWidget *settingsButton = quick->widgetForAction(settingsAction))
    {
        settingsButton->setObjectName(QStringLiteral("settingsButton"));
    }

    connect(m_canvas,
        &CanvasWidget::brushColorChanged,
        this,
        [this](const QColor &color)
        {
            updateColorButton();
            m_swatchRow->setActiveColor(color);
        });
    updateColorButton();
    m_swatchRow->setActiveColor(m_canvas->brushColor());
}

void MainWindow::createStatusBar()
{
    m_pointerLabel = new QLabel(this);
    m_pointerLabel->setMinimumWidth(150);
    statusBar()->addPermanentWidget(m_pointerLabel);

    m_zoomSlider = new QSlider(Qt::Horizontal, this);
    m_zoomSlider->setObjectName(QStringLiteral("zoomSlider"));
    m_zoomSlider->setRange(0, zoomSliderSteps);
    m_zoomSlider->setFixedWidth(96);
    m_zoomSlider->setToolTip(tr("Canvas zoom"));
    m_zoomSlider->setAccessibleName(tr("Canvas zoom"));
    statusBar()->addPermanentWidget(m_zoomSlider);

    m_zoomSpin = new QSpinBox(this);
    m_zoomSpin->setObjectName(QStringLiteral("zoomPercentSpin"));
    m_zoomSpin->setRange(minimumZoomPercent, maximumZoomPercent);
    m_zoomSpin->setSuffix(tr("%"));
    m_zoomSpin->setWrapping(false);
    m_zoomSpin->setFixedWidth(72);
    m_zoomSpin->setToolTip(tr("Canvas zoom percentage"));
    m_zoomSpin->setAccessibleName(tr("Canvas zoom percentage"));
    statusBar()->addPermanentWidget(m_zoomSpin);

    auto *mirrorButton = new QToolButton(this);
    mirrorButton->setObjectName(QStringLiteral("mirrorCanvasButton"));
    mirrorButton->setDefaultAction(m_mirrorCanvasAction);
    mirrorButton->setIconSize(QSize(16, 16));
    statusBar()->addPermanentWidget(mirrorButton);

    auto *fitButton = new QToolButton(this);
    fitButton->setDefaultAction(
        findChild<QAction *>(QStringLiteral("fitAction")));
    fitButton->setIconSize(QSize(16, 16));
    statusBar()->addPermanentWidget(fitButton);

    connect(m_canvas,
        &CanvasWidget::pointerPositionChanged,
        this,
        [this](const QPointF &position, bool inside)
        {
            m_pointerLabel->setText(inside ? tr("x %1  y %2")
                                                 .arg(qRound(position.x()))
                                                 .arg(qRound(position.y()))
                                           : QString());
        });
    connect(m_zoomSlider,
        &QSlider::valueChanged,
        this,
        [this](int value)
        {
            m_canvas->setZoomPercent(zoomPercentFromSlider(value));
        });
    connect(m_zoomSpin,
        &QSpinBox::valueChanged,
        m_canvas,
        &CanvasWidget::setZoomPercent);
    connect(m_canvas,
        &CanvasWidget::zoomChanged,
        this,
        [this](int percent)
        {
            const QSignalBlocker sliderBlocker(m_zoomSlider);
            const QSignalBlocker spinBlocker(m_zoomSpin);
            m_zoomSlider->setValue(sliderFromZoomPercent(percent));
            m_zoomSpin->setValue(percent);
        });
    connect(m_canvas,
        &CanvasWidget::interactionMessage,
        this,
        [this](const QString &message)
        {
            statusBar()->showMessage(message, 4000);
        });
    const int initialZoom = qRound(m_canvas->zoom() * 100.0);
    m_zoomSlider->setValue(sliderFromZoomPercent(initialZoom));
    m_zoomSpin->setValue(initialZoom);
    statusBar()->showMessage(tr("Ready"), 2000);
}
}

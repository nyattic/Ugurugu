#include "ui/MainWindow.hpp"

#include "app/RecoveryStore.hpp"
#include "brush/BrushPreset.hpp"
#include "brush/EraserPreset.hpp"
#include "document/DocumentLimits.hpp"
#include "io/AnimationExportPolicy.hpp"
#include "io/DocumentSerializer.hpp"
#include "io/GifWriter.hpp"
#include "render/RenderEngine.hpp"
#include "ui/BrushPopoverPanel.hpp"
#include "ui/CanvasSizeDialog.hpp"
#include "ui/CanvasWidget.hpp"
#include "ui/ColorSwatchRow.hpp"
#include "ui/EraserPopoverPanel.hpp"
#include "ui/Icons.hpp"
#include "ui/ImageSizeDialog.hpp"
#include "ui/LassoPopoverPanel.hpp"
#include "ui/LayerDock.hpp"
#include "ui/PopoverToolButton.hpp"
#include "ui/SelectionActionBar.hpp"
#include "ui/SettingsDialog.hpp"
#include "ui/ShortcutBinding.hpp"
#include "ui/StrokePropertiesDialog.hpp"
#include "ui/Theme.hpp"
#include "ui/TimelineBar.hpp"
#include "ui/ToolPopover.hpp"
#include "ui/WandPopoverPanel.hpp"

#ifdef Q_OS_MACOS
#include "ui/MacWindowChrome.hpp"
#endif

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QImageWriter>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaType>
#include <QProgressDialog>
#include <QPushButton>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace wobble
{

namespace
{

constexpr auto activeToolKey = "drawingTools/activeTool";
constexpr auto activePresetKey = "drawingTools/brush/presetId";
constexpr auto activeEraserPresetKey = "drawingTools/eraser/presetId";
constexpr auto activeColorKey = "drawingTools/brush/color";
constexpr auto recentColorsKey = "brush/recentColors";
constexpr auto roughnessKey = "drawingTools/brush/roughness";
constexpr auto antialiasingKey = "drawingTools/brush/antialiasing";
constexpr auto eraserWidthKey = "drawingTools/eraser/width";
constexpr auto eraserStabilizationKey = "drawingTools/eraser/stabilization";
constexpr auto selectionShapeKey = "drawingTools/selection/shape";
constexpr auto wandReferenceKey = "drawingTools/wand/reference";
constexpr auto legacyStabilizationKey = "canvas/strokeStabilization";
constexpr qreal minimumRememberedStrokeWidth = 1.0;
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

QString presetWidthKey(const QString &presetId)
{
    return QStringLiteral("drawingTools/brush/presetWidths/%1").arg(presetId);
}

QString presetStabilizationKey(const QString &presetId)
{
    return QStringLiteral("drawingTools/brush/presetStabilizations/%1")
        .arg(presetId);
}

QString eraserPresetWidthKey(const QString &presetId)
{
    return QStringLiteral("drawingTools/eraser/presetWidths/%1").arg(presetId);
}

QString eraserPresetStabilizationKey(const QString &presetId)
{
    return QStringLiteral("drawingTools/eraser/presetStabilizations/%1")
        .arg(presetId);
}

qreal realSetting(const QSettings &settings,
    const QString &key,
    qreal fallback,
    qreal minimum,
    qreal maximum)
{
    bool converted = false;
    const qreal value = settings.value(key).toDouble(&converted);
    if (!converted || !std::isfinite(value))
    {
        return fallback;
    }
    return std::clamp(value, minimum, maximum);
}

bool boolSetting(const QSettings &settings, const QString &key, bool fallback)
{
    if (!settings.contains(key))
    {
        return fallback;
    }
    const QVariant value = settings.value(key);
    if (value.metaType().id() == QMetaType::Bool)
    {
        return value.toBool();
    }
    const QString text = value.toString().trimmed().toLower();
    if (text == QStringLiteral("true") || text == QStringLiteral("1"))
    {
        return true;
    }
    if (text == QStringLiteral("false") || text == QStringLiteral("0"))
    {
        return false;
    }
    return fallback;
}

QString toolSettingsId(CanvasWidget::Tool tool)
{
    switch (tool)
    {
    case CanvasWidget::Tool::Brush:
        return QStringLiteral("brush");
    case CanvasWidget::Tool::Eraser:
        return QStringLiteral("eraser");
    case CanvasWidget::Tool::Lasso:
        return QStringLiteral("lasso");
    case CanvasWidget::Tool::Wand:
        return QStringLiteral("wand");
    case CanvasWidget::Tool::Bucket:
        return QStringLiteral("bucket");
    }
    return QStringLiteral("brush");
}

std::optional<CanvasWidget::Tool> toolFromSettingsId(const QString &id)
{
    if (id == QStringLiteral("brush"))
    {
        return CanvasWidget::Tool::Brush;
    }
    if (id == QStringLiteral("eraser"))
    {
        return CanvasWidget::Tool::Eraser;
    }
    if (id == QStringLiteral("lasso"))
    {
        return CanvasWidget::Tool::Lasso;
    }
    if (id == QStringLiteral("wand"))
    {
        return CanvasWidget::Tool::Wand;
    }
    if (id == QStringLiteral("bucket"))
    {
        return CanvasWidget::Tool::Bucket;
    }
    return std::nullopt;
}

QString selectionShapeSettingsId(CanvasWidget::SelectionShape shape)
{
    switch (shape)
    {
    case CanvasWidget::SelectionShape::Freehand:
        return QStringLiteral("freehand");
    case CanvasWidget::SelectionShape::Rectangle:
        return QStringLiteral("rectangle");
    case CanvasWidget::SelectionShape::Ellipse:
        return QStringLiteral("ellipse");
    }
    return QStringLiteral("freehand");
}

std::optional<CanvasWidget::SelectionShape> selectionShapeFromSettingsId(
    const QString &id)
{
    if (id == QStringLiteral("freehand"))
    {
        return CanvasWidget::SelectionShape::Freehand;
    }
    if (id == QStringLiteral("rectangle"))
    {
        return CanvasWidget::SelectionShape::Rectangle;
    }
    if (id == QStringLiteral("ellipse"))
    {
        return CanvasWidget::SelectionShape::Ellipse;
    }
    return std::nullopt;
}

QString wandReferenceSettingsId(CanvasWidget::WandReference reference)
{
    switch (reference)
    {
    case CanvasWidget::WandReference::ActiveLayer:
        return QStringLiteral("active");
    case CanvasWidget::WandReference::ReferenceLayers:
        return QStringLiteral("reference");
    case CanvasWidget::WandReference::AllVisibleLayers:
        return QStringLiteral("visible");
    }
    return QStringLiteral("active");
}

std::optional<CanvasWidget::WandReference> wandReferenceFromSettingsId(
    const QString &id)
{
    if (id == QStringLiteral("active"))
    {
        return CanvasWidget::WandReference::ActiveLayer;
    }
    if (id == QStringLiteral("reference"))
    {
        return CanvasWidget::WandReference::ReferenceLayers;
    }
    if (id == QStringLiteral("visible"))
    {
        return CanvasWidget::WandReference::AllVisibleLayers;
    }
    return std::nullopt;
}

QSize requestCanvasSize(QWidget *parent,
    const QSize &current,
    const QString &title,
    const QString &description = {})
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);

    auto *layout = new QFormLayout(&dialog);
    if (!description.isEmpty())
    {
        auto *label = new QLabel(description, &dialog);
        label->setWordWrap(true);
        layout->addRow(label);
    }
    auto *widthSpin = new QSpinBox(&dialog);
    auto *heightSpin = new QSpinBox(&dialog);
    widthSpin->setRange(64, DocumentLimits::maximumCanvasEdge);
    heightSpin->setRange(64, DocumentLimits::maximumCanvasEdge);
    widthSpin->setValue(current.width());
    heightSpin->setValue(current.height());
    widthSpin->setSuffix(QObject::tr(" px"));
    heightSpin->setSuffix(QObject::tr(" px"));
    layout->addRow(QObject::tr("Width"), widthSpin);
    layout->addRow(QObject::tr("Height"), heightSpin);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(
        buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(
        buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted)
    {
        return {};
    }
    return QSize(widthSpin->value(), heightSpin->value());
}

QVector<int> gifFrameDelays(int frameCount, qreal framesPerSecond)
{
    const qreal fps = std::clamp(framesPerSecond,
        DocumentLimits::minimumFramesPerSecond,
        DocumentLimits::maximumFramesPerSecond);
    QVector<int> delays;
    delays.reserve(frameCount);

    qint64 emittedCentiseconds = 0;
    for (int frame = 1; frame <= frameCount; ++frame)
    {
        const qint64 targetCentiseconds =
            qRound64(static_cast<qreal>(frame) * 100.0 / fps);
        const int delay = static_cast<int>(
            std::max<qint64>(1, targetCentiseconds - emittedCentiseconds));
        delays.append(delay);
        emittedCentiseconds += delay;
    }
    return delays;
}

}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setObjectName(QStringLiteral("MainWindow"));
    setAcceptDrops(false);
    setMinimumSize(900, 640);

    auto *central = new QWidget(this);
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    m_canvas = new CanvasWidget(&m_controller, central);
    restoreDrawingToolSettings();
    centralLayout->addWidget(m_canvas, 1);
    m_timeline = new TimelineBar(&m_controller, m_canvas, central);
    centralLayout->addWidget(m_timeline);
    setCentralWidget(central);

    m_layerDock = new LayerDock(&m_controller, this);
    addDockWidget(Qt::RightDockWidgetArea, m_layerDock);

    createActions();
    createMenus();
    createToolBars();
    createStatusBar();
    connectDocument();
    m_drawingToolSettingsSaveTimer.setSingleShot(true);
    m_drawingToolSettingsSaveTimer.setInterval(200);
    connect(&m_drawingToolSettingsSaveTimer,
        &QTimer::timeout,
        this,
        &MainWindow::saveDrawingToolSettings);
    connectDrawingToolSettings();
    m_autosaveTimer.setInterval(30000);
    connect(
        &m_autosaveTimer, &QTimer::timeout, this, &MainWindow::writeAutosave);
    m_autosaveTimer.start();

    const QSettings settings;
    const bool geometryRestored = restoreGeometry(
        settings.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("window/state")).toByteArray());
    if (!geometryRestored)
    {
        resize(1280, 820);
    }

    m_canvas->setAnimateWhileDrawing(SettingsDialog::animateWhileDrawing());
    updateWindowTitle();
    m_canvas->setFocus(Qt::OtherFocusReason);
    QTimer::singleShot(0, m_canvas, &CanvasWidget::fitToWindow);
    qApp->installEventFilter(this);

#ifdef Q_OS_MACOS
    applySeamlessTitleBar(this);
#endif
}

bool MainWindow::openFile(const QString &filePath)
{
    if (filePath.isEmpty())
    {
        return false;
    }
    if (!maybeSave())
    {
        return false;
    }

    QString error;
    const std::optional<Document> document =
        DocumentSerializer::load(filePath, &error);
    if (!document)
    {
        spdlog::error("Failed to open project {}: {}",
            filePath.toUtf8().constData(),
            error.toUtf8().constData());
        QMessageBox::critical(this,
            tr("Open failed"),
            tr("Could not open the project.\n\n%1").arg(error));
        return false;
    }

    m_controller.loadDocument(*document);
    m_currentFilePath = QFileInfo(filePath).absoluteFilePath();
    clearAutosave();
    m_canvas->fitToWindow();
    updateWindowTitle();
    statusBar()->showMessage(tr("Opened %1").arg(m_currentFilePath), 4000);
    spdlog::info("Opened project {}", m_currentFilePath.toUtf8().constData());
    return true;
}

bool MainWindow::offerRecovery()
{
    const QString recoveryPath = RecoveryStore::filePath();
    if (!QFileInfo::exists(recoveryPath))
    {
        return false;
    }

    const QMessageBox::StandardButton choice = QMessageBox::question(this,
        tr("Recover unsaved work"),
        tr("WagleWaglePaint found work from a previous session. "
           "Would you like to recover it?"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::Yes);
    if (choice != QMessageBox::Yes)
    {
        clearAutosave();
        return false;
    }

    QString error;
    const std::optional<Document> recovered =
        DocumentSerializer::load(recoveryPath, &error);
    if (!recovered)
    {
        spdlog::error("Failed to load recovery file {}: {}",
            recoveryPath.toUtf8().constData(),
            error.toUtf8().constData());
        QString preservationError;
        const QString quarantinedPath =
            RecoveryStore::quarantine(&preservationError);
        const QString preservedPath =
            quarantinedPath.isEmpty() ? recoveryPath : quarantinedPath;
        if (!preservationError.isEmpty())
        {
            spdlog::warn("Could not quarantine recovery file {}: {}",
                recoveryPath.toUtf8().constData(),
                preservationError.toUtf8().constData());
        }
        m_autosavePending = false;
        QSettings settings;
        settings.remove(QStringLiteral("recovery/sourcePath"));
        QMessageBox::warning(this,
            tr("Recovery failed"),
            tr("The recovery file could not be opened.\n\n%1"
               "\n\nThe recovery file was not deleted. "
               "You can find it at:\n%2")
                .arg(error, preservedPath));
        return false;
    }

    const QSettings settings;
    m_currentFilePath =
        settings.value(QStringLiteral("recovery/sourcePath")).toString();
    m_controller.loadRecoveredDocument(*recovered);
    m_canvas->fitToWindow();
    updateWindowTitle();
    statusBar()->showMessage(tr("Recovered unsaved work."), 5000);
    return true;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!maybeSave())
    {
        event->ignore();
        return;
    }
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState());
    saveDrawingToolSettings();
    clearAutosave();
    event->accept();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_canvas
        && (event->type() == QEvent::KeyPress
            || event->type() == QEvent::KeyRelease))
    {
        const auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Space && !keyEvent->isAutoRepeat())
        {
            if (event->type() == QEvent::KeyRelease)
            {
                m_canvas->setPanModifierActive(false);
                return true;
            }
            m_canvas->setPanModifierActive(true);
            return true;
        }
    }

    if (event->type() == QEvent::ApplicationDeactivate
        || (event->type() == QEvent::WindowDeactivate && watched == this))
    {
        m_canvas->cancelActiveInteraction();
        saveDrawingToolSettings();
        writeAutosave();
    }
    else if (event->type() == QEvent::TabletLeaveProximity)
    {
        m_canvas->cancelActiveInteraction();
    }

    return QMainWindow::eventFilter(watched, event);
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

    QAction *stackUndoAction = m_controller.undoStack()->createUndoAction(this);
    stackUndoAction->setObjectName(QStringLiteral("undoStackAction"));
    QAction *stackRedoAction = m_controller.undoStack()->createRedoAction(this);
    stackRedoAction->setObjectName(QStringLiteral("redoStackAction"));

    QAction *undoAction = new QAction(tr("&Undo"), this);
    undoAction->setObjectName(QStringLiteral("undoAction"));
    undoAction->setIcon(Icons::icon(IconGlyph::Undo));
    registerShortcut(undoAction, QKeySequence(QKeySequence::Undo));

    QAction *redoAction = new QAction(tr("&Redo"), this);
    redoAction->setObjectName(QStringLiteral("redoAction"));
    redoAction->setIcon(Icons::icon(IconGlyph::Redo));
    registerShortcut(redoAction, QKeySequence(QKeySequence::Redo));

    const auto syncHistoryActions =
        [this, undoAction, redoAction, stackUndoAction, stackRedoAction]()
    {
        const bool pending = m_canvas->hasPendingSelectionTransform();
        undoAction->setEnabled(pending || stackUndoAction->isEnabled());
        undoAction->setText(
            pending ? tr("Undo Selection Transform") : stackUndoAction->text());
        redoAction->setEnabled(!pending && stackRedoAction->isEnabled());
        redoAction->setText(stackRedoAction->text());
    };
    connect(stackUndoAction, &QAction::changed, this, syncHistoryActions);
    connect(stackRedoAction, &QAction::changed, this, syncHistoryActions);
    connect(m_canvas,
        &CanvasWidget::selectionTransformSessionChanged,
        this,
        syncHistoryActions);
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

    m_duplicateSelectionAction = new QAction(tr("Duplicate selection"), this);
    m_duplicateSelectionAction->setObjectName(
        QStringLiteral("duplicateSelectionAction"));
    m_duplicateSelectionAction->setIcon(Icons::icon(IconGlyph::Duplicate));
    m_duplicateSelectionAction->setToolTip(tr("Duplicate selected content"));
    m_duplicateSelectionAction->setEnabled(false);
    registerShortcut(
        m_duplicateSelectionAction, QKeySequence(QStringLiteral("Ctrl+D")));
    connect(m_duplicateSelectionAction,
        &QAction::triggered,
        m_canvas,
        &CanvasWidget::duplicateSelection);

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
    m_deselectSelectionAction->setToolTip(tr("Deselect (Esc)"));
    registerShortcut(m_deselectSelectionAction, {});
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
        m_duplicateSelectionAction->setEnabled(hasContent);
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
    selectionBar->addAction(m_duplicateSelectionAction);
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
    addAction(m_moveSelectionAction);
    addAction(m_scaleSelectionAction);
    addAction(m_rotateSelectionAction);
    addAction(m_flipSelectionHorizontalAction);
    addAction(m_flipSelectionVerticalAction);
    addAction(m_applySelectionTransformAction);
    addAction(m_cancelSelectionTransformAction);
    addAction(m_duplicateSelectionAction);
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
    editMenu->addAction(
        findChild<QAction *>(QStringLiteral("resizeImageAction")));
    editMenu->addAction(
        findChild<QAction *>(QStringLiteral("resizeCanvasAction")));
    editMenu->addSeparator();
    QMenu *selectionMenu = editMenu->addMenu(tr("&Selection"));
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
    selectionMenu->addAction(m_duplicateSelectionAction);
    selectionMenu->addAction(m_editStrokePropertiesAction);
    selectionMenu->addAction(m_deleteSelectionAction);
    selectionMenu->addSeparator();
    selectionMenu->addAction(m_deselectSelectionAction);
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

void MainWindow::restoreDrawingToolSettings()
{
    QSettings settings;
    const bool hasLegacyStabilization =
        settings.contains(legacyStabilizationKey);
    const qreal legacyStabilization = realSetting(
        settings, QString::fromLatin1(legacyStabilizationKey), 0.0, 0.0, 1.0);
    for (const BrushPreset &preset : BrushPresetCatalog::builtIns())
    {
        m_canvas->setBrushPresetWidth(preset.id,
            realSetting(settings,
                presetWidthKey(preset.id),
                preset.defaultSize,
                minimumRememberedStrokeWidth,
                DocumentLimits::maximumStrokeWidth));
        const QString stabilizationKey = presetStabilizationKey(preset.id);
        m_canvas->setBrushPresetStabilization(preset.id,
            realSetting(
                settings, stabilizationKey, legacyStabilization, 0.0, 1.0));
        if (hasLegacyStabilization && !settings.contains(stabilizationKey))
        {
            settings.setValue(stabilizationKey, legacyStabilization);
        }
    }

    const BrushPreset &defaultPreset = BrushPresetCatalog::defaultPreset();
    const QString storedPresetId =
        settings.value(activePresetKey, defaultPreset.id).toString();
    m_canvas->setBrushPreset(BrushPresetCatalog::find(storedPresetId)
                                 ? storedPresetId
                                 : defaultPreset.id);

    const EraserPreset &defaultEraser = EraserPresetCatalog::defaultPreset();
    const bool hasLegacyEraserWidth = settings.contains(eraserWidthKey);
    const bool hasLegacyEraserStabilization =
        settings.contains(eraserStabilizationKey);
    const qreal legacyEraserWidth = realSetting(settings,
        QString::fromLatin1(eraserWidthKey),
        defaultEraser.defaultSize,
        minimumRememberedStrokeWidth,
        DocumentLimits::maximumStrokeWidth);
    const qreal legacyEraserStabilization = realSetting(settings,
        QString::fromLatin1(eraserStabilizationKey),
        legacyStabilization,
        0.0,
        1.0);
    for (const EraserPreset &preset : EraserPresetCatalog::builtIns())
    {
        const qreal widthFallback =
            preset.id == defaultEraser.id && hasLegacyEraserWidth
                ? legacyEraserWidth
                : preset.defaultSize;
        const QString widthKey = eraserPresetWidthKey(preset.id);
        m_canvas->setEraserPresetWidth(preset.id,
            realSetting(settings,
                widthKey,
                widthFallback,
                minimumRememberedStrokeWidth,
                DocumentLimits::maximumStrokeWidth));
        const QString stabilizationKey =
            eraserPresetStabilizationKey(preset.id);
        m_canvas->setEraserPresetStabilization(preset.id,
            realSetting(settings,
                stabilizationKey,
                legacyEraserStabilization,
                0.0,
                1.0));
        if (hasLegacyEraserWidth && preset.id == defaultEraser.id
            && !settings.contains(widthKey))
        {
            settings.setValue(widthKey, legacyEraserWidth);
        }
        if ((hasLegacyEraserStabilization || hasLegacyStabilization)
            && !settings.contains(stabilizationKey))
        {
            settings.setValue(stabilizationKey, legacyEraserStabilization);
        }
    }
    const QString storedEraserPresetId =
        settings.value(activeEraserPresetKey, defaultEraser.id).toString();
    m_canvas->setEraserPreset(EraserPresetCatalog::find(storedEraserPresetId)
                                  ? storedEraserPresetId
                                  : defaultEraser.id);
    m_canvas->setBrushRoughness(realSetting(settings,
        QString::fromLatin1(roughnessKey),
        1.0,
        DocumentLimits::minimumBrushWobbleScale,
        DocumentLimits::maximumBrushWobbleScale));
    m_canvas->setBrushAntialiasing(
        boolSetting(settings, QString::fromLatin1(antialiasingKey), false));

    QColor storedColor(settings.value(activeColorKey).toString());
    if (!storedColor.isValid())
    {
        const QStringList recentColors =
            settings.value(recentColorsKey).toStringList();
        for (const QString &name : recentColors)
        {
            const QColor recentColor(name);
            if (recentColor.isValid())
            {
                storedColor = recentColor;
                break;
            }
        }
    }
    m_canvas->setBrushColor(
        storedColor.isValid() ? storedColor : QColor(Qt::black));

    const std::optional<CanvasWidget::WandReference> storedReference =
        wandReferenceFromSettingsId(
            settings.value(wandReferenceKey).toString());
    m_canvas->setWandReference(
        storedReference.value_or(CanvasWidget::WandReference::ActiveLayer));

    const std::optional<CanvasWidget::SelectionShape> storedSelectionShape =
        selectionShapeFromSettingsId(
            settings.value(selectionShapeKey).toString());
    m_canvas->setSelectionShape(
        storedSelectionShape.value_or(CanvasWidget::SelectionShape::Freehand));

    const std::optional<CanvasWidget::Tool> storedTool =
        toolFromSettingsId(settings.value(activeToolKey).toString());
    m_canvas->setTool(storedTool.value_or(CanvasWidget::Tool::Brush));

    if (hasLegacyStabilization || hasLegacyEraserWidth
        || hasLegacyEraserStabilization)
    {
        settings.remove(legacyStabilizationKey);
        settings.remove(eraserWidthKey);
        settings.remove(eraserStabilizationKey);
        settings.sync();
    }
}

void MainWindow::connectDrawingToolSettings()
{
    const auto schedule = [this]()
    {
        scheduleDrawingToolSettingsSave();
    };
    connect(m_canvas,
        &CanvasWidget::toolChanged,
        this,
        [schedule](CanvasWidget::Tool)
        {
            schedule();
        });
    connect(m_canvas,
        &CanvasWidget::brushColorChanged,
        this,
        [schedule](const QColor &)
        {
            schedule();
        });
    connect(m_canvas,
        &CanvasWidget::brushWidthChanged,
        this,
        [schedule](qreal)
        {
            schedule();
        });
    connect(m_canvas,
        &CanvasWidget::eraserWidthChanged,
        this,
        [schedule](qreal)
        {
            schedule();
        });
    connect(m_canvas,
        &CanvasWidget::brushStabilizationChanged,
        this,
        [schedule](qreal)
        {
            schedule();
        });
    connect(m_canvas,
        &CanvasWidget::eraserStabilizationChanged,
        this,
        [schedule](qreal)
        {
            schedule();
        });
    connect(m_canvas,
        &CanvasWidget::brushRoughnessChanged,
        this,
        [schedule](qreal)
        {
            schedule();
        });
    connect(m_canvas,
        &CanvasWidget::brushAntialiasingChanged,
        this,
        [schedule](bool)
        {
            schedule();
        });
    connect(m_canvas,
        &CanvasWidget::brushPresetChanged,
        this,
        [schedule](const QString &)
        {
            schedule();
        });
    connect(m_canvas,
        &CanvasWidget::eraserPresetChanged,
        this,
        [schedule](const QString &)
        {
            schedule();
        });
    connect(m_canvas,
        &CanvasWidget::wandReferenceChanged,
        this,
        [schedule](CanvasWidget::WandReference)
        {
            schedule();
        });
    connect(m_canvas,
        &CanvasWidget::selectionShapeChanged,
        this,
        [schedule](CanvasWidget::SelectionShape)
        {
            schedule();
        });
}

void MainWindow::scheduleDrawingToolSettingsSave()
{
    m_drawingToolSettingsSaveTimer.start();
}

void MainWindow::saveDrawingToolSettings()
{
    m_drawingToolSettingsSaveTimer.stop();
    QSettings settings;
    settings.setValue(activeToolKey, toolSettingsId(m_canvas->tool()));
    settings.setValue(activePresetKey, m_canvas->brushPresetId());
    settings.setValue(activeEraserPresetKey, m_canvas->eraserPresetId());
    settings.setValue(
        activeColorKey, m_canvas->brushColor().name(QColor::HexArgb));
    settings.setValue(roughnessKey, m_canvas->brushRoughness());
    settings.setValue(antialiasingKey, m_canvas->brushAntialiasing());
    settings.setValue(
        wandReferenceKey, wandReferenceSettingsId(m_canvas->wandReference()));
    settings.setValue(selectionShapeKey,
        selectionShapeSettingsId(m_canvas->selectionShape()));
    for (const BrushPreset &preset : BrushPresetCatalog::builtIns())
    {
        settings.setValue(
            presetWidthKey(preset.id), m_canvas->brushPresetWidth(preset.id));
        settings.setValue(presetStabilizationKey(preset.id),
            m_canvas->brushPresetStabilization(preset.id));
    }
    for (const EraserPreset &preset : EraserPresetCatalog::builtIns())
    {
        settings.setValue(eraserPresetWidthKey(preset.id),
            m_canvas->eraserPresetWidth(preset.id));
        settings.setValue(eraserPresetStabilizationKey(preset.id),
            m_canvas->eraserPresetStabilization(preset.id));
    }
    settings.sync();
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

void MainWindow::connectDocument()
{
    connect(&m_controller,
        &DocumentController::documentChanged,
        this,
        [this]()
        {
            m_autosavePending = true;
        });
    connect(&m_controller,
        &DocumentController::modifiedChanged,
        this,
        [this](bool modified)
        {
            refreshUnsavedState();
            updateWindowTitle();
            if (!modified && !m_canvas->hasPendingSelectionTransform())
            {
                clearAutosave();
            }
        });
    connect(m_canvas,
        &CanvasWidget::selectionTransformSessionChanged,
        this,
        [this](bool, bool dirty)
        {
            refreshUnsavedState();
            if (dirty || m_controller.isModified())
            {
                m_autosavePending = true;
            }
            else
            {
                clearAutosave();
            }
        });
}

void MainWindow::updateWindowTitle()
{
    const QString name = m_currentFilePath.isEmpty()
                             ? tr("Untitled")
                             : QFileInfo(m_currentFilePath).fileName();
    setWindowTitle(tr("%1[*] — WagleWaglePaint").arg(name));
    setWindowFilePath(m_currentFilePath);
}

void MainWindow::updateColorButton()
{
    if (!m_colorButton)
    {
        return;
    }
    const QColor color = m_canvas->brushColor();
    m_colorButton->setStyleSheet(
        QStringLiteral("QPushButton { background: %1; border: 2px solid "
                       "rgba(255, 255, 255, 70); border-radius: 14px; }"
                       "QPushButton:hover { border-color: %2; }"
                       "QPushButton:focus { border-color: %2; }")
            .arg(color.name(QColor::HexArgb), Theme::accent().name()));
}

bool MainWindow::hasUnsavedWork() const
{
    return m_controller.isModified()
           || m_canvas->hasPendingSelectionTransform();
}

void MainWindow::refreshUnsavedState()
{
    setWindowModified(hasUnsavedWork());
}

bool MainWindow::maybeSave()
{
    if (!hasUnsavedWork())
    {
        return true;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Unsaved changes"));
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(14);
    layout->addWidget(
        new QLabel(tr("The document has unsaved changes."), &dialog));

    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->setSpacing(8);
    buttonLayout->addStretch(1);
    auto *saveButton = new QPushButton(tr("Save (S)"), &dialog);
    saveButton->setObjectName(QStringLiteral("unsavedChangesSaveButton"));
    saveButton->setDefault(true);
    buttonLayout->addWidget(saveButton);
    auto *discardButton = new QPushButton(tr("Don't Save (N)"), &dialog);
    discardButton->setObjectName(QStringLiteral("unsavedChangesDiscardButton"));
    buttonLayout->addWidget(discardButton);
    auto *cancelButton = new QPushButton(tr("Cancel (ESC)"), &dialog);
    cancelButton->setObjectName(QStringLiteral("unsavedChangesCancelButton"));
    buttonLayout->addWidget(cancelButton);
    layout->addLayout(buttonLayout);

    connect(saveButton,
        &QPushButton::clicked,
        &dialog,
        [&dialog]()
        {
            dialog.done(1);
        });
    connect(discardButton,
        &QPushButton::clicked,
        &dialog,
        [&dialog]()
        {
            dialog.done(2);
        });
    connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    auto *saveShortcut =
        new QShortcut(QKeySequence(QStringLiteral("S")), &dialog);
    auto *discardShortcut =
        new QShortcut(QKeySequence(QStringLiteral("N")), &dialog);
    auto *cancelShortcut =
        new QShortcut(QKeySequence(QStringLiteral("Esc")), &dialog);
    saveShortcut->setContext(Qt::ApplicationShortcut);
    discardShortcut->setContext(Qt::ApplicationShortcut);
    cancelShortcut->setContext(Qt::ApplicationShortcut);
    connect(
        saveShortcut, &QShortcut::activated, saveButton, &QPushButton::click);
    connect(discardShortcut,
        &QShortcut::activated,
        discardButton,
        &QPushButton::click);
    connect(cancelShortcut,
        &QShortcut::activated,
        cancelButton,
        &QPushButton::click);

    const int choice = dialog.exec();
    if (choice == 1)
    {
        return save();
    }
    return choice == 2;
}

bool MainWindow::save()
{
    return m_currentFilePath.isEmpty() ? saveAs()
                                       : saveToFile(m_currentFilePath);
}

bool MainWindow::saveAs()
{
    const QString selected = QFileDialog::getSaveFileName(this,
        tr("Save project"),
        saveDialogStartPath(QStringLiteral("wagle")),
        tr("WagleWaglePaint projects (*.wagle)"));
    if (selected.isEmpty())
    {
        return false;
    }
    return saveToFile(normalizedPath(selected, QStringLiteral("wagle")));
}

bool MainWindow::saveToFile(const QString &filePath)
{
    if (m_canvas->hasPendingSelectionTransform()
        && !m_canvas->applySelectionTransform())
    {
        spdlog::error("Aborted saving {}: the pending selection transform "
                      "could not be applied",
            filePath.toUtf8().constData());
        QMessageBox::critical(this,
            tr("Save failed"),
            tr("The pending selection transform could not be "
               "applied. Adjust or cancel the transform, then save "
               "again."));
        return false;
    }
    QString error;
    if (!m_controller.saveDocument(filePath, &error))
    {
        spdlog::error("Failed to save project {}: {}",
            filePath.toUtf8().constData(),
            error.toUtf8().constData());
        QMessageBox::critical(this,
            tr("Save failed"),
            tr("Could not save the project.\n\n%1").arg(error));
        return false;
    }
    m_currentFilePath = QFileInfo(filePath).absoluteFilePath();
    m_controller.markSaved();
    clearAutosave();
    updateWindowTitle();
    statusBar()->showMessage(tr("Saved %1").arg(m_currentFilePath), 4000);
    spdlog::info("Saved project {}", m_currentFilePath.toUtf8().constData());
    return true;
}

void MainWindow::newDocument()
{
    if (!maybeSave())
    {
        return;
    }
    const QSize size = requestCanvasSize(
        this, m_controller.document().size, tr("New document"));
    if (!size.isValid())
    {
        return;
    }
    clearAutosave();
    m_controller.newDocument(size);
    m_currentFilePath.clear();
    m_canvas->fitToWindow();
    updateWindowTitle();
    spdlog::info("Created {}x{} document", size.width(), size.height());
}

void MainWindow::resizeCanvas()
{
    CanvasSizeDialog dialog(m_controller.document().size, this);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }
    const CanvasSizeDialog::Result requested = dialog.result();
    if (requested.size == m_controller.document().size
        && requested.contentOffset.isNull())
    {
        return;
    }
    if (m_canvas->hasPendingSelectionTransform()
        && !m_canvas->applySelectionTransform())
    {
        QMessageBox::critical(this,
            tr("Canvas size"),
            tr("The pending selection transform could not be "
               "applied. Adjust or cancel the transform, then "
               "change the size again."));
        return;
    }
    m_canvas->setSelectionMoveMode(false);
    m_canvas->cancelActiveInteraction();
    const bool hadSelection = m_canvas->hasSelection();
    if (hadSelection)
    {
        m_controller.undoStack()->beginMacro(tr("Resize canvas"));
        m_canvas->deselectSelection();
    }
    const bool resized =
        m_controller.resizeCanvas(requested.size, requested.contentOffset);
    if (hadSelection)
    {
        m_controller.undoStack()->endMacro();
    }
    if (!resized)
    {
        QMessageBox::warning(this,
            tr("Canvas size"),
            tr("The canvas size could not be changed. "
               "Try a smaller size or offset."));
    }
}

void MainWindow::resizeImage()
{
    ImageSizeDialog dialog(m_controller.document().size, this);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }
    const QSize requested = dialog.imageSize();
    if (requested == m_controller.document().size)
    {
        return;
    }
    if (m_canvas->hasPendingSelectionTransform()
        && !m_canvas->applySelectionTransform())
    {
        QMessageBox::critical(this,
            tr("Image size"),
            tr("The pending selection transform could not be "
               "applied. Adjust or cancel the transform, then "
               "change the size again."));
        return;
    }
    m_canvas->setSelectionMoveMode(false);
    m_canvas->cancelActiveInteraction();
    const bool hadSelection = m_canvas->hasSelection();
    if (hadSelection)
    {
        m_controller.undoStack()->beginMacro(tr("Resize image"));
        m_canvas->deselectSelection();
    }
    const bool resized = m_controller.resizeImage(requested);
    if (hadSelection)
    {
        m_controller.undoStack()->endMacro();
    }
    if (!resized)
    {
        QMessageBox::warning(this,
            tr("Image size"),
            tr("The image size could not be changed. "
               "Try smaller dimensions."));
    }
}

void MainWindow::scaleSelection()
{
    bool accepted = false;
    const double percent = QInputDialog::getDouble(this,
        tr("Scale selection"),
        tr("Scale (%)"),
        125.0,
        10.0,
        400.0,
        0,
        &accepted);
    if (accepted)
    {
        m_canvas->scaleSelection(percent / 100.0);
    }
}

void MainWindow::rotateSelection()
{
    bool accepted = false;
    const double degrees = QInputDialog::getDouble(this,
        tr("Rotate selection"),
        tr("Angle (degrees)"),
        90.0,
        -360.0,
        360.0,
        1,
        &accepted);
    if (accepted && !qFuzzyIsNull(degrees))
    {
        if (m_canvas->rotateSelection(degrees))
        {
            m_canvas->applySelectionTransform();
        }
    }
}

void MainWindow::editSelectedStrokeProperties()
{
    if (m_canvas->hasSelectionTransformSession())
    {
        return;
    }
    const QUuid layerId = m_canvas->selectionLayerId();
    const QVector<QUuid> strokeIds = m_canvas->selectedStrokeIds();
    const Layer *layer = m_controller.document().layer(layerId);
    if (!layer || strokeIds.isEmpty())
    {
        return;
    }

    const QSet<QUuid> selected(strokeIds.cbegin(), strokeIds.cend());
    StrokePropertiesDialog::Values values;
    bool colorMixed = false;
    bool widthMixed = false;
    bool roughnessMixed = false;
    for (const Stroke &stroke : layer->strokes)
    {
        if (!selected.contains(stroke.id))
        {
            continue;
        }
        if (stroke.mode == StrokeMode::Paint || stroke.mode == StrokeMode::Fill)
        {
            if (!values.colorSupported)
            {
                values.color = stroke.color;
            }
            else if (values.color && *values.color != stroke.color)
            {
                colorMixed = true;
            }
            values.colorSupported = true;
        }
        if (stroke.mode == StrokeMode::Paint
            || stroke.mode == StrokeMode::Erase)
        {
            if (!values.widthSupported)
            {
                values.width = stroke.width;
                values.roughness = stroke.brush.wobbleScale;
            }
            else
            {
                widthMixed =
                    widthMixed || !qFuzzyCompare(*values.width, stroke.width);
                roughnessMixed = roughnessMixed
                                 || !qFuzzyCompare(*values.roughness,
                                     stroke.brush.wobbleScale);
            }
            values.widthSupported = true;
            values.roughnessSupported = true;
        }
    }
    if (!values.colorSupported && !values.widthSupported
        && !values.roughnessSupported)
    {
        return;
    }
    if (colorMixed)
    {
        values.color.reset();
    }
    if (widthMixed)
    {
        values.width.reset();
    }
    if (roughnessMixed)
    {
        values.roughness.reset();
    }

    StrokePropertiesDialog dialog(values, this);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }
    m_controller.updateStrokeAttributes(
        layerId, strokeIds, dialog.color(), dialog.width(), dialog.roughness());
}

void MainWindow::writeAutosave()
{
    if (!hasUnsavedWork() || !m_autosavePending)
    {
        return;
    }
    const QString filePath = RecoveryStore::filePath();
    QString directoryError;
    if (!RecoveryStore::ensureParentDirectory(&directoryError))
    {
        spdlog::warn("{}", directoryError.toUtf8().constData());
        return;
    }

    QString error;
    const bool saved =
        m_canvas->hasPendingSelectionTransform()
            ? DocumentSerializer::save(filePath,
                  m_canvas->documentWithPendingSelectionTransform(),
                  &error)
            : m_controller.saveDocument(filePath, &error);
    if (!saved)
    {
        spdlog::warn("Failed to write recovery file {}: {}",
            filePath.toUtf8().constData(),
            error.toUtf8().constData());
        return;
    }
    QSettings settings;
    settings.setValue(QStringLiteral("recovery/sourcePath"), m_currentFilePath);
    m_autosavePending = false;
}

void MainWindow::clearAutosave()
{
    QString error;
    if (!RecoveryStore::discard(&error))
    {
        spdlog::warn("{}", error.toUtf8().constData());
    }
    m_autosavePending = false;
    QSettings settings;
    settings.remove(QStringLiteral("recovery/sourcePath"));
}

void MainWindow::chooseOpenFile()
{
    const QString filePath = QFileDialog::getOpenFileName(this,
        tr("Open project"),
        QString(),
        tr("WagleWaglePaint projects (*.wagle *.wobble);;All files (*)"));
    if (!filePath.isEmpty())
    {
        openFile(filePath);
    }
}

void MainWindow::exportGif()
{
    const Document document = m_canvas->documentWithPendingSelectionTransform();
    const long double workingBytes =
        AnimationExportPolicy::estimatedWorkingBytes(
            document.size, document.animationFrames);
    if (document.size.width() <= 0 || document.size.height() <= 0
        || document.animationFrames <= 0
        || !AnimationExportPolicy::fitsMemoryBudget(
            document.size, document.animationFrames))
    {
        const long double mebibytes = workingBytes / (1024.0L * 1024.0L);
        QMessageBox::warning(this,
            tr("Animation is too large"),
            tr("This GIF would need about %1 MiB of working memory. "
               "Reduce the canvas size or frame count before exporting.")
                .arg(static_cast<double>(mebibytes), 0, 'f', 0));
        return;
    }

    const QString selected = QFileDialog::getSaveFileName(this,
        tr("Export animated GIF"),
        saveDialogStartPath(QStringLiteral("gif")),
        tr("GIF images (*.gif)"));
    if (selected.isEmpty())
    {
        return;
    }
    const QString filePath = normalizedPath(selected, QStringLiteral("gif"));
    m_canvas->releaseTransientRenderCaches();
    m_controller.releaseTransientCaches();
    QVector<QImage> frames;
    frames.reserve(document.animationFrames);

    QProgressDialog progress(tr("Rendering animation…"),
        tr("Cancel"),
        0,
        document.animationFrames,
        this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);

    for (int frame = 0; frame < document.animationFrames; ++frame)
    {
        progress.setValue(frame);
        QApplication::processEvents();
        if (progress.wasCanceled())
        {
            spdlog::info("GIF export canceled");
            return;
        }
        QImage image = RenderEngine::render(document, frame);
        if (image.isNull())
        {
            spdlog::error("Failed to render frame {} for GIF export", frame);
            QMessageBox::critical(this,
                tr("Export failed"),
                tr("A frame could not be rendered. Free some memory or "
                   "reduce the canvas size and frame count."));
            return;
        }
        frames.append(std::move(image));
    }
    progress.setValue(document.animationFrames);

    QString error;
    const QVector<int> delaysCentiseconds =
        gifFrameDelays(document.animationFrames, document.framesPerSecond);
    if (!GifWriter::write(filePath, frames, delaysCentiseconds, &error))
    {
        spdlog::error("Failed to export GIF {}: {}",
            filePath.toUtf8().constData(),
            error.toUtf8().constData());
        QMessageBox::critical(this,
            tr("Export failed"),
            tr("Could not export the GIF.\n\n%1").arg(error));
        return;
    }
    statusBar()->showMessage(tr("Exported %1").arg(filePath), 5000);
    spdlog::info("Exported GIF {} with {} frames",
        filePath.toUtf8().constData(),
        frames.size());
}

void MainWindow::exportImage()
{
    const int frame = m_canvas->currentFrame();
    const QString pngFilter = tr("PNG images (*.png)");
    const QString jpegFilter = tr("JPEG images (*.jpg *.jpeg)");
    QString selectedFilter = pngFilter;
    const QString selected = QFileDialog::getSaveFileName(this,
        tr("Export current frame"),
        saveDialogStartPath(QStringLiteral("png")),
        pngFilter + QStringLiteral(";;") + jpegFilter,
        &selectedFilter);
    if (selected.isEmpty())
    {
        return;
    }
    const QString suffix = QFileInfo(selected).suffix().toLower();
    const bool jpeg = suffix == QStringLiteral("jpg")
                      || suffix == QStringLiteral("jpeg")
                      || (suffix.isEmpty() && selectedFilter == jpegFilter);
    const QString filePath =
        suffix == QStringLiteral("jpeg")
            ? selected
            : normalizedPath(selected,
                  jpeg ? QStringLiteral("jpg") : QStringLiteral("png"));
    Document exportDocument = m_canvas->documentWithPendingSelectionTransform();
    if (!m_canvas->isWobbleAnimationEnabled())
    {
        exportDocument.wobbleAmount = 0.0;
    }
    m_canvas->releaseTransientRenderCaches();
    m_controller.releaseTransientCaches();
    const QImage image = RenderEngine::render(exportDocument, frame);
    QSaveFile file(filePath);
    QString error;
    bool saved = !image.isNull() && file.open(QIODevice::WriteOnly);
    if (saved)
    {
        QImageWriter writer(&file, jpeg ? "JPEG" : "PNG");
        if (jpeg)
        {
            writer.setQuality(92);
        }
        saved = writer.write(image);
        if (!saved)
        {
            error = writer.errorString();
        }
    }
    else if (!image.isNull())
    {
        error = file.errorString();
    }
    if (saved)
    {
        saved = file.commit();
        if (!saved)
        {
            error = file.errorString();
        }
    }
    if (!saved)
    {
        spdlog::error("Failed to export image {}: {}",
            filePath.toUtf8().constData(),
            error.toUtf8().constData());
        QMessageBox::critical(this,
            tr("Export failed"),
            error.isEmpty()
                ? tr("Could not export the image.")
                : tr("Could not export the image.\n\n%1").arg(error));
        return;
    }
    statusBar()->showMessage(tr("Exported %1").arg(filePath), 5000);
    spdlog::info("Exported image {}", filePath.toUtf8().constData());
}

void MainWindow::applyWobbleAnimationEnabled(bool enabled)
{
    m_canvas->setWobbleAnimationEnabled(enabled);
    m_timeline->setEnabled(enabled);
    m_playAction->setEnabled(enabled);
    if (auto *exportGifAction =
            findChild<QAction *>(QStringLiteral("exportGifAction")))
    {
        exportGifAction->setEnabled(enabled);
    }
    if (auto *exportImageAction =
            findChild<QAction *>(QStringLiteral("exportPngAction")))
    {
        const QString label = enabled ? tr("Export current frame as &image…")
                                      : tr("Export &image…");
        exportImageAction->setText(label);
        exportImageAction->setProperty("shortcutLabel", label);
    }
}

QString MainWindow::normalizedPath(
    const QString &filePath, const QString &extension) const
{
    if (QFileInfo(filePath).suffix().compare(extension, Qt::CaseInsensitive)
        == 0)
    {
        return filePath;
    }
    return filePath + QStringLiteral(".") + extension;
}

QString MainWindow::saveDialogStartPath(const QString &extension) const
{
    if (!m_currentFilePath.isEmpty())
    {
        const QFileInfo currentFile(m_currentFilePath);
        if (extension.compare(QStringLiteral("wagle"), Qt::CaseInsensitive)
            == 0)
        {
            return currentFile.absoluteFilePath();
        }
        return QDir(currentFile.absolutePath())
            .filePath(currentFile.completeBaseName() + QStringLiteral(".")
                      + extension);
    }
    return QDir(SettingsDialog::defaultSaveFolder())
        .filePath(QStringLiteral("Untitled.") + extension);
}

}

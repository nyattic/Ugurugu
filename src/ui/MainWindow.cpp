#include "ui/MainWindow.hpp"

#include "document/DocumentLimits.hpp"
#include "io/DocumentSerializer.hpp"
#include "io/GifWriter.hpp"
#include "render/RenderEngine.hpp"
#include "ui/BrushPopoverPanel.hpp"
#include "ui/BrushSizeRow.hpp"
#include "ui/CanvasWidget.hpp"
#include "ui/ColorSwatchRow.hpp"
#include "ui/Icons.hpp"
#include "ui/LayerDock.hpp"
#include "ui/PopoverToolButton.hpp"
#include "ui/SettingsDialog.hpp"
#include "ui/Theme.hpp"
#include "ui/TimelineBar.hpp"
#include "ui/ToolPopover.hpp"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QImageWriter>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QTextEdit>
#include <QVBoxLayout>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace wobble {

namespace {

QSize requestCanvasSize(
    QWidget *parent,
    const QSize &current,
    const QString &title,
    const QString &description = {})
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);

    auto *layout = new QFormLayout(&dialog);
    if (!description.isEmpty()) {
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
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        &dialog);
    QObject::connect(
        buttons,
        &QDialogButtonBox::accepted,
        &dialog,
        &QDialog::accept);
    QObject::connect(
        buttons,
        &QDialogButtonBox::rejected,
        &dialog,
        &QDialog::reject);
    layout->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return {};
    }
    return QSize(widthSpin->value(), heightSpin->value());
}

QVector<int> gifFrameDelays(int frameCount, qreal framesPerSecond)
{
    const qreal fps = std::clamp(
        framesPerSecond,
        DocumentLimits::minimumFramesPerSecond,
        DocumentLimits::maximumFramesPerSecond);
    QVector<int> delays;
    delays.reserve(frameCount);

    qint64 emittedCentiseconds = 0;
    for (int frame = 1; frame <= frameCount; ++frame) {
        const qint64 targetCentiseconds =
            qRound64(static_cast<qreal>(frame) * 100.0 / fps);
        const int delay = static_cast<int>(
            std::max<qint64>(
                1,
                targetCentiseconds - emittedCentiseconds));
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
    m_autosaveTimer.setInterval(30000);
    connect(
        &m_autosaveTimer,
        &QTimer::timeout,
        this,
        &MainWindow::writeAutosave);
    m_autosaveTimer.start();

    const QSettings settings;
    const bool geometryRestored = restoreGeometry(
        settings.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("window/state")).toByteArray());
    if (!geometryRestored) {
        resize(1280, 820);
    }

    m_canvas->setAnimateWhileDrawing(SettingsDialog::animateWhileDrawing());
    updateWindowTitle();
    m_canvas->setFocus(Qt::OtherFocusReason);
    qApp->installEventFilter(this);
}

bool MainWindow::openFile(const QString &filePath)
{
    if (filePath.isEmpty()) {
        return false;
    }
    if (!maybeSave()) {
        return false;
    }

    QString error;
    const std::optional<Document> document =
        DocumentSerializer::load(filePath, &error);
    if (!document) {
        spdlog::error(
            "Failed to open project {}: {}",
            filePath.toUtf8().constData(),
            error.toUtf8().constData());
        QMessageBox::critical(
            this,
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
    const QString recoveryPath = autosavePath();
    if (!QFileInfo::exists(recoveryPath)) {
        return false;
    }

    const QMessageBox::StandardButton choice = QMessageBox::question(
        this,
        tr("Recover unsaved work"),
        tr("WagleWaglePaint found work from a previous session. "
           "Would you like to recover it?"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::Yes);
    if (choice != QMessageBox::Yes) {
        clearAutosave();
        return false;
    }

    QString error;
    const std::optional<Document> recovered =
        DocumentSerializer::load(recoveryPath, &error);
    if (!recovered) {
        spdlog::error(
            "Failed to load recovery file {}: {}",
            recoveryPath.toUtf8().constData(),
            error.toUtf8().constData());
        clearAutosave();
        QMessageBox::warning(
            this,
            tr("Recovery failed"),
            tr("The recovery file could not be opened.\n\n%1").arg(error));
        return false;
    }

    const QSettings settings;
    m_currentFilePath = settings
        .value(QStringLiteral("recovery/sourcePath"))
        .toString();
    m_controller.loadRecoveredDocument(*recovered);
    m_canvas->fitToWindow();
    updateWindowTitle();
    statusBar()->showMessage(tr("Recovered unsaved work."), 5000);
    return true;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!maybeSave()) {
        event->ignore();
        return;
    }
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState());
    clearAutosave();
    event->accept();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    const auto *widget = qobject_cast<QWidget *>(watched);
    const bool belongsToWindow = widget && widget->window() == this;
    const bool acceptsText =
        qobject_cast<QLineEdit *>(watched)
        || qobject_cast<QPlainTextEdit *>(watched)
        || qobject_cast<QTextEdit *>(watched);
    if (belongsToWindow
        && (event->type() == QEvent::KeyPress
            || event->type() == QEvent::KeyRelease)) {
        const auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Space
            && !keyEvent->isAutoRepeat()) {
            if (event->type() == QEvent::KeyRelease) {
                m_canvas->setPanModifierActive(false);
                return !acceptsText;
            }
            if (!acceptsText) {
                m_canvas->setPanModifierActive(true);
                return true;
            }
            return false;
        }
    }

    if (event->type() == QEvent::ApplicationDeactivate
        || (event->type() == QEvent::WindowDeactivate
            && watched == this)) {
        m_canvas->setPanModifierActive(false);
        writeAutosave();
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::createActions()
{
    const auto registerShortcut =
        [this](QAction *action, const QKeySequence &defaultShortcut) {
            action->setProperty("shortcutLabel", action->text());
            action->setProperty(
                "defaultShortcut",
                defaultShortcut.toString(QKeySequence::PortableText));
            action->setShortcut(SettingsDialog::shortcutForAction(
                action->objectName(),
                defaultShortcut));
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
    connect(m_saveAction, &QAction::triggered, this, [this]() {
        save();
    });

    auto *saveAsAction = new QAction(tr("Save &As…"), this);
    saveAsAction->setObjectName(QStringLiteral("saveAsAction"));
    registerShortcut(saveAsAction, QKeySequence(QKeySequence::SaveAs));
    connect(saveAsAction, &QAction::triggered, this, [this]() {
        saveAs();
    });

    auto *exportGifAction = new QAction(tr("Export animated &GIF…"), this);
    exportGifAction->setObjectName(QStringLiteral("exportGifAction"));
    registerShortcut(
        exportGifAction,
        QKeySequence(QStringLiteral("Ctrl+E")));
    connect(exportGifAction, &QAction::triggered, this, &MainWindow::exportGif);

    auto *exportPngAction = new QAction(tr("Export current frame as &PNG…"), this);
    exportPngAction->setObjectName(QStringLiteral("exportPngAction"));
    registerShortcut(exportPngAction, {});
    connect(exportPngAction, &QAction::triggered, this, &MainWindow::exportPng);

    auto *quitAction = new QAction(tr("&Quit"), this);
    quitAction->setObjectName(QStringLiteral("quitAction"));
    registerShortcut(quitAction, QKeySequence(QKeySequence::Quit));
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    auto *settingsAction = new QAction(tr("&Settings…"), this);
    settingsAction->setObjectName(QStringLiteral("settingsAction"));
    settingsAction->setIcon(Icons::icon(IconGlyph::Settings));
    settingsAction->setToolTip(tr("Settings"));
    registerShortcut(
        settingsAction,
        QKeySequence(QKeySequence::Preferences));
    settingsAction->setMenuRole(QAction::PreferencesRole);
    connect(settingsAction, &QAction::triggered, this, [this]() {
        SettingsDialog dialog(this, m_shortcutActions);
        connect(
            &dialog,
            &SettingsDialog::animateWhileDrawingChanged,
            m_canvas,
            &CanvasWidget::setAnimateWhileDrawing);
        dialog.exec();
    });

    auto *checkForUpdatesAction =
        new QAction(tr("Check for &Updates…"), this);
    checkForUpdatesAction->setObjectName(
        QStringLiteral("checkForUpdatesAction"));

    QAction *undoAction = m_controller.undoStack()->createUndoAction(this);
    undoAction->setText(tr("&Undo"));
    undoAction->setObjectName(QStringLiteral("undoAction"));
    undoAction->setIcon(Icons::icon(IconGlyph::Undo));
    registerShortcut(undoAction, QKeySequence(QKeySequence::Undo));

    QAction *redoAction = m_controller.undoStack()->createRedoAction(this);
    redoAction->setText(tr("&Redo"));
    redoAction->setObjectName(QStringLiteral("redoAction"));
    redoAction->setIcon(Icons::icon(IconGlyph::Redo));
    registerShortcut(redoAction, QKeySequence(QKeySequence::Redo));

    auto *resizeCanvasAction = new QAction(tr("Resize canvas…"), this);
    resizeCanvasAction->setObjectName(QStringLiteral("resizeCanvasAction"));
    registerShortcut(resizeCanvasAction, {});
    connect(
        resizeCanvasAction,
        &QAction::triggered,
        this,
        &MainWindow::resizeCanvas);

    m_scaleSelectionAction = new QAction(tr("Scale selection…"), this);
    m_scaleSelectionAction->setObjectName(
        QStringLiteral("scaleSelectionAction"));
    m_scaleSelectionAction->setEnabled(false);
    registerShortcut(m_scaleSelectionAction, {});
    connect(
        m_scaleSelectionAction,
        &QAction::triggered,
        this,
        &MainWindow::scaleSelection);

    m_rotateSelectionAction = new QAction(tr("Rotate selection…"), this);
    m_rotateSelectionAction->setObjectName(
        QStringLiteral("rotateSelectionAction"));
    m_rotateSelectionAction->setEnabled(false);
    registerShortcut(m_rotateSelectionAction, {});
    connect(
        m_rotateSelectionAction,
        &QAction::triggered,
        this,
        &MainWindow::rotateSelection);

    m_duplicateSelectionAction =
        new QAction(tr("Duplicate selection"), this);
    m_duplicateSelectionAction->setObjectName(
        QStringLiteral("duplicateSelectionAction"));
    m_duplicateSelectionAction->setEnabled(false);
    registerShortcut(
        m_duplicateSelectionAction,
        QKeySequence(QStringLiteral("Ctrl+D")));
    connect(
        m_duplicateSelectionAction,
        &QAction::triggered,
        m_canvas,
        &CanvasWidget::duplicateSelection);
    connect(
        m_canvas,
        &CanvasWidget::selectionTransformAvailabilityChanged,
        this,
        [this](bool available) {
            m_scaleSelectionAction->setEnabled(available);
            m_rotateSelectionAction->setEnabled(available);
            m_duplicateSelectionAction->setEnabled(available);
        });

    auto *clearLayerAction = new QAction(tr("Clear active layer"), this);
    clearLayerAction->setObjectName(QStringLiteral("clearLayerAction"));
    registerShortcut(clearLayerAction, {});
    connect(clearLayerAction, &QAction::triggered, this, [this]() {
        m_controller.clearLayer(m_controller.document().activeLayerId);
    });

    auto *fitAction = new QAction(tr("&Fit canvas"), this);
    fitAction->setObjectName(QStringLiteral("fitAction"));
    fitAction->setIcon(Icons::icon(IconGlyph::FitView));
    registerShortcut(fitAction, QKeySequence(QStringLiteral("Ctrl+0")));
    connect(fitAction, &QAction::triggered, m_canvas, &CanvasWidget::fitToWindow);

    m_playAction = new QAction(tr("&Animate preview"), this);
    m_playAction->setCheckable(true);
    m_playAction->setChecked(true);
    m_playAction->setIcon(Icons::toggleIcon(IconGlyph::Play));
    m_playAction->setObjectName(QStringLiteral("playAction"));
    registerShortcut(m_playAction, QKeySequence(QStringLiteral("P")));
    connect(
        m_playAction,
        &QAction::toggled,
        m_canvas,
        &CanvasWidget::setAnimating);
    connect(
        m_canvas,
        &CanvasWidget::animatingChanged,
        m_playAction,
        &QAction::setChecked);

    m_brushAction = new QAction(tr("&Brush"), this);
    m_brushAction->setCheckable(true);
    m_brushAction->setChecked(true);
    m_brushAction->setIcon(Icons::toggleIcon(IconGlyph::Brush));
    m_brushAction->setObjectName(QStringLiteral("brushAction"));
    registerShortcut(m_brushAction, QKeySequence(QStringLiteral("B")));

    m_eraserAction = new QAction(tr("&Eraser"), this);
    m_eraserAction->setCheckable(true);
    m_eraserAction->setIcon(Icons::toggleIcon(IconGlyph::Eraser));
    m_eraserAction->setObjectName(QStringLiteral("eraserAction"));
    registerShortcut(m_eraserAction, QKeySequence(QStringLiteral("E")));

    m_lassoAction = new QAction(tr("&Lasso select"), this);
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
    connect(m_brushAction, &QAction::triggered, this, [this]() {
        m_canvas->setTool(CanvasWidget::Tool::Brush);
    });
    connect(m_eraserAction, &QAction::triggered, this, [this]() {
        m_canvas->setTool(CanvasWidget::Tool::Eraser);
    });
    connect(m_lassoAction, &QAction::triggered, this, [this]() {
        m_canvas->setTool(CanvasWidget::Tool::Lasso);
    });
    connect(m_wandAction, &QAction::triggered, this, [this]() {
        m_canvas->setTool(CanvasWidget::Tool::Wand);
    });
    connect(m_bucketAction, &QAction::triggered, this, [this]() {
        m_canvas->setTool(CanvasWidget::Tool::Bucket);
    });

    addAction(newAction);
    addAction(openAction);
    addAction(m_saveAction);
    addAction(saveAsAction);
    addAction(exportGifAction);
    addAction(exportPngAction);
    addAction(quitAction);
    addAction(undoAction);
    addAction(redoAction);
    addAction(resizeCanvasAction);
    addAction(m_scaleSelectionAction);
    addAction(m_rotateSelectionAction);
    addAction(m_duplicateSelectionAction);
    addAction(clearLayerAction);
    addAction(fitAction);
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
    fileMenu->addAction(findChild<QAction *>(QStringLiteral("exportGifAction")));
    fileMenu->addAction(findChild<QAction *>(QStringLiteral("exportPngAction")));
    fileMenu->addSeparator();
    fileMenu->addAction(findChild<QAction *>(QStringLiteral("quitAction")));

    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(findChild<QAction *>(QStringLiteral("undoAction")));
    editMenu->addAction(findChild<QAction *>(QStringLiteral("redoAction")));
    editMenu->addSeparator();
    editMenu->addAction(
        findChild<QAction *>(QStringLiteral("resizeCanvasAction")));
    editMenu->addSeparator();
    editMenu->addAction(m_scaleSelectionAction);
    editMenu->addAction(m_rotateSelectionAction);
    editMenu->addAction(m_duplicateSelectionAction);
    editMenu->addSeparator();
    editMenu->addAction(findChild<QAction *>(QStringLiteral("clearLayerAction")));
    editMenu->addSeparator();
    editMenu->addAction(findChild<QAction *>(QStringLiteral("settingsAction")));

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(findChild<QAction *>(QStringLiteral("fitAction")));
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
        findChild<QAction *>(
            QStringLiteral("checkForUpdatesAction")));
}

void MainWindow::createToolBars()
{
    auto *rail = new QToolBar(tr("Tools"), this);
    rail->setObjectName(QStringLiteral("ToolRail"));
    rail->setMovable(false);
    rail->setIconSize(QSize(24, 24));
    addToolBar(Qt::LeftToolBarArea, rail);

    const auto addRailButton = [rail](QAction *action) {
        auto *button = new PopoverToolButton(rail);
        button->setDefaultAction(action);
        button->setIconSize(rail->iconSize());
        rail->addWidget(button);
        return button;
    };

    PopoverToolButton *brushButton = addRailButton(m_brushAction);
    PopoverToolButton *eraserButton = addRailButton(m_eraserAction);
    addRailButton(m_lassoAction);
    addRailButton(m_wandAction);
    addRailButton(m_bucketAction);

    auto *brushPopover = new ToolPopover(this);
    auto *brushPanel = new BrushPopoverPanel(m_canvas);
    brushPopover->setContentWidget(brushPanel);
    connect(
        brushPopover,
        &ToolPopover::popoverShown,
        brushPanel,
        [brushPanel]() {
            brushPanel->setAnimationActive(true);
        });
    connect(
        brushPopover,
        &ToolPopover::popoverHidden,
        brushPanel,
        [brushPanel]() {
            brushPanel->setAnimationActive(false);
        });
    brushButton->setPopover(brushPopover);

    auto *eraserPopover = new ToolPopover(this);
    eraserPopover->setContentWidget(
        new BrushSizeRow(m_canvas, QStringLiteral("eraserSize")));
    eraserButton->setPopover(eraserPopover);

    connect(
        m_canvas,
        &CanvasWidget::brushPresetChanged,
        this,
        [this](const QString &) {
            if (m_canvas->tool() != CanvasWidget::Tool::Brush) {
                m_brushAction->trigger();
            }
        });

    auto *railSpacer = new QWidget(rail);
    railSpacer->setSizePolicy(
        QSizePolicy::Preferred,
        QSizePolicy::Expanding);
    rail->addWidget(railSpacer);

    m_colorButton = new QPushButton(rail);
    m_colorButton->setFixedSize(28, 28);
    m_colorButton->setToolTip(tr("Choose brush color"));
    m_colorButton->setAccessibleName(tr("Brush color"));
    m_colorButton->setCursor(Qt::PointingHandCursor);
    connect(m_colorButton, &QPushButton::clicked, this, [this]() {
        const QColor color = QColorDialog::getColor(
            m_canvas->brushColor(),
            this,
            tr("Brush color"),
            QColorDialog::ShowAlphaChannel);
        if (color.isValid()) {
            m_canvas->setBrushColor(color);
        }
    });
    auto *colorHolder = new QWidget(rail);
    auto *colorLayout = new QHBoxLayout(colorHolder);
    colorLayout->setContentsMargins(0, 2, 0, 8);
    colorLayout->addWidget(m_colorButton, 0, Qt::AlignHCenter);
    rail->addWidget(colorHolder);

    QToolBar *quick = addToolBar(tr("Quick access"));
    quick->setObjectName(QStringLiteral("PaintTools"));
    quick->setMovable(false);
    quick->setIconSize(QSize(22, 22));

    m_swatchRow = new ColorSwatchRow(quick);
    connect(
        m_swatchRow,
        &ColorSwatchRow::colorSelected,
        m_canvas,
        &CanvasWidget::setBrushColor);
    quick->addWidget(m_swatchRow);

    auto *quickSpacer = new QWidget(quick);
    quickSpacer->setSizePolicy(
        QSizePolicy::Expanding,
        QSizePolicy::Preferred);
    quick->addWidget(quickSpacer);

    quick->addAction(
        findChild<QAction *>(QStringLiteral("undoAction")));
    quick->addAction(
        findChild<QAction *>(QStringLiteral("redoAction")));
    quick->addSeparator();
    QAction *settingsAction =
        findChild<QAction *>(QStringLiteral("settingsAction"));
    quick->addAction(settingsAction);
    if (QWidget *settingsButton = quick->widgetForAction(settingsAction)) {
        settingsButton->setObjectName(QStringLiteral("settingsButton"));
    }

    connect(
        m_canvas,
        &CanvasWidget::brushColorChanged,
        this,
        [this](const QColor &color) {
            updateColorButton();
            m_swatchRow->setActiveColor(color);
        });
    updateColorButton();
}

void MainWindow::createStatusBar()
{
    m_pointerLabel = new QLabel(this);
    m_pointerLabel->setMinimumWidth(150);
    statusBar()->addPermanentWidget(m_pointerLabel);

    m_zoomLabel = new QLabel(tr("100%"), this);
    m_zoomLabel->setMinimumWidth(56);
    m_zoomLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    statusBar()->addPermanentWidget(m_zoomLabel);

    auto *fitButton = new QToolButton(this);
    fitButton->setDefaultAction(
        findChild<QAction *>(QStringLiteral("fitAction")));
    fitButton->setIconSize(QSize(16, 16));
    statusBar()->addPermanentWidget(fitButton);

    connect(
        m_canvas,
        &CanvasWidget::pointerPositionChanged,
        this,
        [this](const QPointF &position, bool inside) {
            m_pointerLabel->setText(
                inside
                    ? tr("x %1  y %2")
                          .arg(qRound(position.x()))
                          .arg(qRound(position.y()))
                    : QString());
        });
    connect(
        m_canvas,
        &CanvasWidget::zoomChanged,
        this,
        [this](int percent) {
            m_zoomLabel->setText(tr("%1%").arg(percent));
        });
    connect(
        m_canvas,
        &CanvasWidget::interactionMessage,
        this,
        [this](const QString &message) {
            statusBar()->showMessage(message, 4000);
        });
    statusBar()->showMessage(tr("Ready"), 2000);
}

void MainWindow::connectDocument()
{
    connect(
        &m_controller,
        &DocumentController::documentChanged,
        this,
        [this]() {
            m_autosavePending = true;
        });
    connect(
        &m_controller,
        &DocumentController::modifiedChanged,
        this,
        [this](bool modified) {
            setWindowModified(modified);
            updateWindowTitle();
            if (!modified) {
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
    if (!m_colorButton) {
        return;
    }
    const QColor color = m_canvas->brushColor();
    m_colorButton->setStyleSheet(
        QStringLiteral(
            "QPushButton { background: %1; border: 2px solid "
            "rgba(255, 255, 255, 70); border-radius: 14px; }"
            "QPushButton:hover { border-color: %2; }")
            .arg(color.name(QColor::HexArgb), Theme::accent().name()));
}

bool MainWindow::maybeSave()
{
    if (!m_controller.isModified()) {
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
    auto *saveButton = new QPushButton(tr("Save"), &dialog);
    saveButton->setDefault(true);
    buttonLayout->addWidget(saveButton);
    auto *discardButton = new QPushButton(tr("Don't Save"), &dialog);
    buttonLayout->addWidget(discardButton);
    auto *cancelButton = new QPushButton(tr("Cancel"), &dialog);
    buttonLayout->addWidget(cancelButton);
    layout->addLayout(buttonLayout);

    connect(saveButton, &QPushButton::clicked, &dialog, [&dialog]() {
        dialog.done(1);
    });
    connect(discardButton, &QPushButton::clicked, &dialog, [&dialog]() {
        dialog.done(2);
    });
    connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);

    const int choice = dialog.exec();
    if (choice == 1) {
        return save();
    }
    return choice == 2;
}

bool MainWindow::save()
{
    return m_currentFilePath.isEmpty()
        ? saveAs()
        : saveToFile(m_currentFilePath);
}

bool MainWindow::saveAs()
{
    const QString selected = QFileDialog::getSaveFileName(
        this,
        tr("Save project"),
        saveDialogStartPath(QStringLiteral("wagle")),
        tr("WagleWaglePaint projects (*.wagle)"));
    if (selected.isEmpty()) {
        return false;
    }
    return saveToFile(normalizedPath(selected, QStringLiteral("wagle")));
}

bool MainWindow::saveToFile(const QString &filePath)
{
    QString error;
    if (!DocumentSerializer::save(filePath, m_controller.document(), &error)) {
        spdlog::error(
            "Failed to save project {}: {}",
            filePath.toUtf8().constData(),
            error.toUtf8().constData());
        QMessageBox::critical(
            this,
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
    if (!maybeSave()) {
        return;
    }
    const QSize size = requestCanvasSize(
        this,
        m_controller.document().size,
        tr("New document"));
    if (!size.isValid()) {
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
    const QSize previous = m_controller.document().size;
    const QSize size = requestCanvasSize(
        this,
        previous,
        tr("Resize canvas"),
        tr("Artwork and brush sizes will be scaled to the new canvas."));
    if (!size.isValid() || size == previous) {
        return;
    }
    m_controller.resizeCanvas(size);
}

void MainWindow::scaleSelection()
{
    bool accepted = false;
    const double percent = QInputDialog::getDouble(
        this,
        tr("Scale selection"),
        tr("Scale (%)"),
        125.0,
        10.0,
        400.0,
        0,
        &accepted);
    if (accepted) {
        m_canvas->scaleSelection(percent / 100.0);
    }
}

void MainWindow::rotateSelection()
{
    bool accepted = false;
    const double degrees = QInputDialog::getDouble(
        this,
        tr("Rotate selection"),
        tr("Angle (degrees)"),
        90.0,
        -360.0,
        360.0,
        1,
        &accepted);
    if (accepted && !qFuzzyIsNull(degrees)) {
        m_canvas->rotateSelection(degrees);
    }
}

void MainWindow::writeAutosave()
{
    if (!m_controller.isModified() || !m_autosavePending) {
        return;
    }
    const QString filePath = autosavePath();
    if (!QDir().mkpath(QFileInfo(filePath).absolutePath())) {
        spdlog::warn(
            "Could not create the recovery directory {}",
            QFileInfo(filePath).absolutePath().toUtf8().constData());
        return;
    }

    QString error;
    if (!DocumentSerializer::save(
            filePath,
            m_controller.document(),
            &error)) {
        spdlog::warn(
            "Failed to write recovery file {}: {}",
            filePath.toUtf8().constData(),
            error.toUtf8().constData());
        return;
    }
    QSettings settings;
    settings.setValue(
        QStringLiteral("recovery/sourcePath"),
        m_currentFilePath);
    m_autosavePending = false;
}

void MainWindow::clearAutosave()
{
    QFile::remove(autosavePath());
    m_autosavePending = false;
    QSettings settings;
    settings.remove(QStringLiteral("recovery/sourcePath"));
}

QString MainWindow::autosavePath() const
{
    const QString configured =
        qEnvironmentVariable("WAGLEWAGLEPAINT_RECOVERY_PATH");
    if (!configured.isEmpty()) {
        return QFileInfo(configured).absoluteFilePath();
    }
    return QDir(QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("recovery.wagle"));
}

void MainWindow::chooseOpenFile()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("Open project"),
        QString(),
        tr("WagleWaglePaint projects (*.wagle *.wobble);;All files (*)"));
    if (!filePath.isEmpty()) {
        openFile(filePath);
    }
}

void MainWindow::exportGif()
{
    const Document &document = m_controller.document();
    const long double workingBytes =
        static_cast<long double>(document.size.width())
        * static_cast<long double>(document.size.height())
        * static_cast<long double>(document.animationFrames)
        * 12.0L;
    if (document.size.width() <= 0
        || document.size.height() <= 0
        || document.animationFrames <= 0
        || workingBytes
            > static_cast<long double>(
                DocumentLimits::maximumGifWorkingBytes)) {
        const long double mebibytes =
            workingBytes / (1024.0L * 1024.0L);
        QMessageBox::warning(
            this,
            tr("Animation is too large"),
            tr(
                "This GIF would need about %1 MiB of working memory. "
                "Reduce the canvas size or frame count before exporting.")
                .arg(static_cast<double>(mebibytes), 0, 'f', 0));
        return;
    }

    const QString selected = QFileDialog::getSaveFileName(
        this,
        tr("Export animated GIF"),
        saveDialogStartPath(QStringLiteral("gif")),
        tr("GIF images (*.gif)"));
    if (selected.isEmpty()) {
        return;
    }
    const QString filePath = normalizedPath(selected, QStringLiteral("gif"));
    QVector<QImage> frames;
    frames.reserve(document.animationFrames);

    QProgressDialog progress(
        tr("Rendering animation…"),
        tr("Cancel"),
        0,
        document.animationFrames,
        this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);

    for (int frame = 0; frame < document.animationFrames; ++frame) {
        progress.setValue(frame);
        QApplication::processEvents();
        if (progress.wasCanceled()) {
            spdlog::info("GIF export canceled");
            return;
        }
        QImage image = RenderEngine::render(document, frame);
        if (image.isNull()) {
            spdlog::error(
                "Failed to render frame {} for GIF export",
                frame);
            QMessageBox::critical(
                this,
                tr("Export failed"),
                tr(
                    "A frame could not be rendered. Free some memory or "
                    "reduce the canvas size and frame count."));
            return;
        }
        frames.append(std::move(image));
    }
    progress.setValue(document.animationFrames);

    QString error;
    const QVector<int> delaysCentiseconds = gifFrameDelays(
        document.animationFrames,
        document.framesPerSecond);
    if (!GifWriter::write(
            filePath,
            frames,
            delaysCentiseconds,
            &error)) {
        spdlog::error(
            "Failed to export GIF {}: {}",
            filePath.toUtf8().constData(),
            error.toUtf8().constData());
        QMessageBox::critical(
            this,
            tr("Export failed"),
            tr("Could not export the GIF.\n\n%1").arg(error));
        return;
    }
    statusBar()->showMessage(tr("Exported %1").arg(filePath), 5000);
    spdlog::info(
        "Exported GIF {} with {} frames",
        filePath.toUtf8().constData(),
        frames.size());
}

void MainWindow::exportPng()
{
    const int frame = m_canvas->currentFrame();
    const QString selected = QFileDialog::getSaveFileName(
        this,
        tr("Export current frame"),
        saveDialogStartPath(QStringLiteral("png")),
        tr("PNG images (*.png)"));
    if (selected.isEmpty()) {
        return;
    }
    const QString filePath = normalizedPath(selected, QStringLiteral("png"));
    const QImage image =
        RenderEngine::render(m_controller.document(), frame);
    QSaveFile file(filePath);
    QString error;
    bool saved = !image.isNull() && file.open(QIODevice::WriteOnly);
    if (saved) {
        QImageWriter writer(&file, "PNG");
        saved = writer.write(image);
        if (!saved) {
            error = writer.errorString();
        }
    } else if (!image.isNull()) {
        error = file.errorString();
    }
    if (saved) {
        saved = file.commit();
        if (!saved) {
            error = file.errorString();
        }
    }
    if (!saved) {
        spdlog::error(
            "Failed to export PNG {}: {}",
            filePath.toUtf8().constData(),
            error.toUtf8().constData());
        QMessageBox::critical(
            this,
            tr("Export failed"),
            error.isEmpty()
                ? tr("Could not export the PNG.")
                : tr("Could not export the PNG.\n\n%1").arg(error));
        return;
    }
    statusBar()->showMessage(tr("Exported %1").arg(filePath), 5000);
    spdlog::info("Exported PNG {}", filePath.toUtf8().constData());
}

QString MainWindow::normalizedPath(
    const QString &filePath,
    const QString &extension) const
{
    if (QFileInfo(filePath).suffix().compare(
            extension,
            Qt::CaseInsensitive) == 0) {
        return filePath;
    }
    return filePath + QStringLiteral(".") + extension;
}

QString MainWindow::saveDialogStartPath(const QString &extension) const
{
    if (!m_currentFilePath.isEmpty()) {
        const QFileInfo currentFile(m_currentFilePath);
        if (extension.compare(
                QStringLiteral("wagle"),
                Qt::CaseInsensitive) == 0) {
            return currentFile.absoluteFilePath();
        }
        return QDir(currentFile.absolutePath()).filePath(
            currentFile.completeBaseName()
            + QStringLiteral(".")
            + extension);
    }
    return QDir(SettingsDialog::defaultSaveFolder()).filePath(
        QStringLiteral("Untitled.")
        + extension);
}

}

#include "brush/BrushPreset.hpp"
#include "ui/BrushPopoverPanel.hpp"
#include "ui/BrushPresetButton.hpp"
#include "ui/CanvasWidget.hpp"
#include "ui/LayerDock.hpp"
#include "ui/MainWindow.hpp"
#include "ui/SettingsDialog.hpp"
#include "io/DocumentSerializer.hpp"
#include "render/RenderEngine.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QToolButton>
#include <QVariant>
#include <QtTest>

#include <utility>

namespace wobble {

class SettingValueGuard final
{
public:
    explicit SettingValueGuard(QString key)
        : m_key(std::move(key))
        , m_existed(m_settings.contains(m_key))
        , m_value(m_settings.value(m_key))
    {
    }

    ~SettingValueGuard()
    {
        if (m_existed) {
            m_settings.setValue(m_key, m_value);
        } else {
            m_settings.remove(m_key);
        }
    }

private:
    QSettings m_settings;
    QString m_key;
    bool m_existed = false;
    QVariant m_value;
};

class EnvironmentVariableGuard final
{
public:
    explicit EnvironmentVariableGuard(QByteArray name)
        : m_name(std::move(name))
        , m_existed(qEnvironmentVariableIsSet(m_name.constData()))
        , m_value(qgetenv(m_name.constData()))
    {
    }

    ~EnvironmentVariableGuard()
    {
        if (m_existed) {
            qputenv(m_name.constData(), m_value);
        } else {
            qunsetenv(m_name.constData());
        }
    }

private:
    QByteArray m_name;
    bool m_existed = false;
    QByteArray m_value;
};

class UiTests final : public QObject
{
    Q_OBJECT

private slots:
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
            window.findChild<QSpinBox *>(
                QStringLiteral("brushSizeSpin"));
        QVERIFY(brushSizeSpin);
        QCOMPARE(
            presetButtons.size(),
            BrushPresetCatalog::builtIns().size());
        BrushPresetButton *softAirbrushButton = nullptr;
        for (BrushPresetButton *button : presetButtons) {
            if (button->presetId() == QStringLiteral("soft-airbrush")) {
                softAirbrushButton = button;
            }
        }
        QVERIFY(softAirbrushButton);
        softAirbrushButton->click();
        QCOMPARE(canvas->brushPresetId(), QStringLiteral("soft-airbrush"));
        QVERIFY(softAirbrushButton->isChecked());
        QCOMPARE(
            brushSizeSpin->value(),
            qRound(
                BrushPresetCatalog::find(
                    QStringLiteral("soft-airbrush"))->defaultSize));

        QSpinBox *currentFrameSpin =
            window.findChild<QSpinBox *>(QStringLiteral("currentFrameSpin"));
        QVERIFY(currentFrameSpin);
        QCOMPARE(currentFrameSpin->maximum(), 30);
        currentFrameSpin->setFocus(Qt::OtherFocusReason);
        QTest::mouseClick(
            currentFrameSpin,
            Qt::LeftButton,
            Qt::NoModifier,
            currentFrameSpin->rect().center());
        QTRY_VERIFY(!canvas->isAnimating());
        const int targetFrameValue =
            currentFrameSpin->value() == 1 ? 2 : 1;
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
        if (!screenshotPath.isEmpty()) {
            QVERIFY(window.grab().save(screenshotPath, "PNG"));
            QVERIFY(QFileInfo(screenshotPath).size() > 0);
        }

        const QString brushPanelScreenshotPath =
            qEnvironmentVariable("WOBBLEPAINT_BRUSH_PANEL_SCREENSHOT");
        if (!brushPanelScreenshotPath.isEmpty()) {
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
        QAction *checkForUpdatesAction =
            window.findChild<QAction *>(
                QStringLiteral("checkForUpdatesAction"));
        QToolButton *settingsButton =
            window.findChild<QToolButton *>(QStringLiteral("settingsButton"));
        QVERIFY(settingsAction);
        QVERIFY(checkForUpdatesAction);
        QVERIFY(settingsButton);
        QCOMPARE(settingsButton->defaultAction(), settingsAction);
        QVERIFY(window.windowTitle().contains(
            QStringLiteral("WagleWaglePaint")));
    }

    void editsAndRestoresShortcuts()
    {
        const QString brushKey = QStringLiteral("shortcuts/brushAction");
        const QString eraserKey = QStringLiteral("shortcuts/eraserAction");
        const QString folderKey = QStringLiteral("files/defaultSaveFolder");
        const QString languageKey =
            QStringLiteral("appearance/language");
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

        QAction brushAction(QStringLiteral("Brush"));
        brushAction.setObjectName(QStringLiteral("brushAction"));
        brushAction.setProperty("defaultShortcut", QStringLiteral("B"));
        brushAction.setShortcut(QKeySequence(QStringLiteral("B")));

        QAction eraserAction(QStringLiteral("Eraser"));
        eraserAction.setObjectName(QStringLiteral("eraserAction"));
        eraserAction.setProperty("defaultShortcut", QStringLiteral("E"));
        eraserAction.setShortcut(QKeySequence(QStringLiteral("E")));

        SettingsDialog dialog(
            nullptr,
            {&brushAction, &eraserAction});
        QLineEdit *folderEdit =
            dialog.findChild<QLineEdit *>(
                QStringLiteral("defaultSaveFolderEdit"));
        QKeySequenceEdit *brushEditor =
            dialog.findChild<QKeySequenceEdit *>(
                QStringLiteral("brushActionShortcutEdit"));
        QComboBox *languageCombo =
            dialog.findChild<QComboBox *>(
                QStringLiteral("languageCombo"));
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
        QTRY_COMPARE(
            brushAction.shortcut(),
            QKeySequence(QStringLiteral("V")));
        QCOMPARE(
            settings.value(brushKey).toString(),
            QStringLiteral("V"));

        brushEditor->setKeySequence(QKeySequence(QStringLiteral("E")));
        QTRY_COMPARE(
            brushEditor->keySequence(),
            QKeySequence(QStringLiteral("V")));
        QCOMPARE(
            brushAction.shortcut(),
            QKeySequence(QStringLiteral("V")));

        QDialogButtonBox *buttons =
            dialog.findChild<QDialogButtonBox *>();
        QVERIFY(buttons);
        QPushButton *restoreButton =
            buttons->button(QDialogButtonBox::RestoreDefaults);
        QVERIFY(restoreButton);
        QTest::mouseClick(restoreButton, Qt::LeftButton);
        QCOMPARE(
            brushAction.shortcut(),
            QKeySequence(QStringLiteral("B")));
        QVERIFY(!settings.contains(brushKey));
        QVERIFY(!settings.contains(folderKey));
        QVERIFY(!settings.contains(languageKey));
        QCOMPARE(
            folderEdit->text(),
            SettingsDialog::defaultSaveFolder());
        QCOMPARE(SettingsDialog::uiLanguage(), QStringLiteral("system"));
    }

    void selectionParticipatesInUndoHistory()
    {
        MainWindow window;
        window.resize(1000, 680);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QAction *lassoAction =
            window.findChild<QAction *>(QStringLiteral("lassoAction"));
        QAction *undoAction =
            window.findChild<QAction *>(QStringLiteral("undoAction"));
        QAction *redoAction =
            window.findChild<QAction *>(QStringLiteral("redoAction"));
        QVERIFY(canvas);
        QVERIFY(lassoAction);
        QVERIFY(undoAction);
        QVERIFY(redoAction);
        QVERIFY(!window.isWindowModified());

        lassoAction->trigger();
        const QPoint center = canvas->rect().center();
        const QPoint topLeft = center - QPoint(80, 60);
        const QPoint topRight = center + QPoint(80, -60);
        const QPoint bottomRight = center + QPoint(80, 60);
        const QPoint bottomLeft = center + QPoint(-80, 60);
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
        QTest::mouseMove(canvas, topRight, 5);
        QTest::mouseMove(canvas, bottomRight, 5);
        QTest::mouseMove(canvas, bottomLeft, 5);
        QTest::mouseRelease(
            canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            topLeft);

        QTRY_VERIFY(canvas->hasSelection());
        QVERIFY(undoAction->isEnabled());
        QVERIFY(!window.isWindowModified());

        const QString screenshotPath =
            qEnvironmentVariable("WOBBLEPAINT_SELECTION_SCREENSHOT");
        if (!screenshotPath.isEmpty()) {
            QVERIFY(window.grab().save(screenshotPath, "PNG"));
            QVERIFY(QFileInfo(screenshotPath).size() > 0);
        }

        undoAction->trigger();
        QTRY_VERIFY(!canvas->hasSelection());
        QVERIFY(redoAction->isEnabled());
        QVERIFY(!window.isWindowModified());

        redoAction->trigger();
        QTRY_VERIFY(canvas->hasSelection());
        QVERIFY(!window.isWindowModified());
    }

    void selectsAStrokeCrossingTheLasso()
    {
        MainWindow window;
        window.resize(1000, 680);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QAction *lassoAction =
            window.findChild<QAction *>(QStringLiteral("lassoAction"));
        QAction *undoAction =
            window.findChild<QAction *>(QStringLiteral("undoAction"));
        QVERIFY(canvas);
        QVERIFY(lassoAction);
        QVERIFY(undoAction);

        const QPoint center = canvas->rect().center();
        const QPoint strokeStart = center - QPoint(150, 0);
        const QPoint strokeEnd = center + QPoint(150, 0);
        QTest::mousePress(
            canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            strokeStart);
        QTest::mouseMove(canvas, strokeEnd, 5);
        QTest::mouseRelease(
            canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            strokeEnd);
        QTRY_VERIFY(window.isWindowModified());

        lassoAction->trigger();
        const QPoint topLeft = center - QPoint(30, 30);
        const QPoint topRight = center + QPoint(30, -30);
        const QPoint bottomRight = center + QPoint(30, 30);
        const QPoint bottomLeft = center + QPoint(-30, 30);
        QTest::mousePress(
            canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            topLeft);
        QTest::mouseMove(canvas, topRight, 5);
        QTest::mouseMove(canvas, bottomRight, 5);
        QTest::mouseMove(canvas, bottomLeft, 5);
        QTest::mouseRelease(
            canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            topLeft);
        QTest::keyClick(canvas, Qt::Key_Delete);

        undoAction->trigger();
        undoAction->trigger();
        QVERIFY(window.isWindowModified());
        undoAction->trigger();
        QVERIFY(!window.isWindowModified());
    }

    void keepsSelectionAcrossToolsAndTransformsIt()
    {
        MainWindow window;
        window.resize(1000, 680);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QAction *brushAction =
            window.findChild<QAction *>(QStringLiteral("brushAction"));
        QAction *lassoAction =
            window.findChild<QAction *>(QStringLiteral("lassoAction"));
        QAction *bucketAction =
            window.findChild<QAction *>(QStringLiteral("bucketAction"));
        QAction *scaleAction = window.findChild<QAction *>(
            QStringLiteral("scaleSelectionAction"));
        QAction *rotateAction = window.findChild<QAction *>(
            QStringLiteral("rotateSelectionAction"));
        QAction *duplicateAction = window.findChild<QAction *>(
            QStringLiteral("duplicateSelectionAction"));
        QVERIFY(canvas);
        QVERIFY(brushAction);
        QVERIFY(lassoAction);
        QVERIFY(bucketAction);
        QVERIFY(scaleAction);
        QVERIFY(rotateAction);
        QVERIFY(duplicateAction);
        QVERIFY(!scaleAction->isEnabled());
        QVERIFY(!rotateAction->isEnabled());
        QVERIFY(!duplicateAction->isEnabled());

        const QPoint center = canvas->rect().center();
        QTest::mousePress(
            canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            center - QPoint(60, 0));
        QTest::mouseMove(canvas, center + QPoint(60, 0), 5);
        QTest::mouseRelease(
            canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            center + QPoint(60, 0));

        lassoAction->trigger();
        const QPoint topLeft = center - QPoint(90, 50);
        const QPoint topRight = center + QPoint(90, -50);
        const QPoint bottomRight = center + QPoint(90, 50);
        const QPoint bottomLeft = center + QPoint(-90, 50);
        QTest::mousePress(
            canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            topLeft);
        QTest::mouseMove(canvas, topRight, 5);
        QTest::mouseMove(canvas, bottomRight, 5);
        QTest::mouseMove(canvas, bottomLeft, 5);
        QTest::mouseRelease(
            canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            topLeft);

        QTRY_VERIFY(canvas->hasSelection());
        QTRY_VERIFY(canvas->hasTransformableSelection());
        QTRY_VERIFY(scaleAction->isEnabled());
        QTRY_VERIFY(rotateAction->isEnabled());
        QTRY_VERIFY(duplicateAction->isEnabled());

        bucketAction->trigger();
        QVERIFY(canvas->hasSelection());
        brushAction->trigger();
        QVERIFY(canvas->hasSelection());
        QVERIFY(canvas->scaleSelection(0.75));
        QVERIFY(canvas->rotateSelection(90.0));
        duplicateAction->trigger();
        QVERIFY(canvas->hasTransformableSelection());
    }

    void mirrorsTheCanvasAsAViewOnlyToggle()
    {
        MainWindow window;
        window.resize(1000, 680);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QAction *mirrorAction = window.findChild<QAction *>(
            QStringLiteral("mirrorCanvasAction"));
        QToolButton *mirrorButton = window.findChild<QToolButton *>(
            QStringLiteral("mirrorCanvasButton"));
        QVERIFY(canvas);
        QVERIFY(mirrorAction);
        QVERIFY(mirrorButton);
        QVERIFY(mirrorAction->isCheckable());
        QCOMPARE(
            mirrorAction->shortcut(),
            QKeySequence(QStringLiteral("M")));
        QVERIFY(!canvas->isCanvasMirrored());
        QVERIFY(!window.isWindowModified());

        mirrorAction->trigger();
        QVERIFY(canvas->isCanvasMirrored());
        QVERIFY(mirrorAction->isChecked());
        QVERIFY(canvas->zoom() > 0.0);
        QVERIFY(!window.isWindowModified());

        mirrorAction->trigger();
        QVERIFY(!canvas->isCanvasMirrored());
        QVERIFY(!mirrorAction->isChecked());
        QVERIFY(!window.isWindowModified());
    }

    void zoomsWithKeyboardActions()
    {
        MainWindow window;
        window.resize(1000, 680);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QAction *zoomInAction = window.findChild<QAction *>(
            QStringLiteral("zoomInAction"));
        QAction *zoomOutAction = window.findChild<QAction *>(
            QStringLiteral("zoomOutAction"));
        QVERIFY(canvas);
        QVERIFY(zoomInAction);
        QVERIFY(zoomOutAction);

        const qreal initialZoom = canvas->zoom();
        zoomInAction->trigger();
        QVERIFY(canvas->zoom() > initialZoom);
        zoomOutAction->trigger();
        zoomOutAction->trigger();
        QVERIFY(canvas->zoom() < initialZoom);
        QVERIFY(!window.isWindowModified());
    }

    void zoomsWithPanModifierCtrlDrag()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));

        const qreal initialZoom = canvas.zoom();
        canvas.setPanModifierActive(true);
        const QPoint start(200, 200);
        QTest::mousePress(
            &canvas,
            Qt::LeftButton,
            Qt::ControlModifier,
            start);
        QTest::mouseMove(&canvas, start + QPoint(120, 0), 5);
        QTest::mouseRelease(
            &canvas,
            Qt::LeftButton,
            Qt::ControlModifier,
            start + QPoint(120, 0));
        canvas.setPanModifierActive(false);
        QVERIFY(canvas.zoom() > initialZoom);

        const Layer &layer = controller.document().layers.first();
        QVERIFY(layer.strokes.isEmpty());
    }

    void mapsDrawingInputThroughTheMirroredCanvas()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.setAnimating(false);
        canvas.setCanvasMirrored(true);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));

        const QPoint leftOfCenter(100, 200);
        QTest::mousePress(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            leftOfCenter);
        QTest::mouseRelease(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            leftOfCenter);

        const Layer &layer = controller.document().layers.first();
        QCOMPARE(layer.strokes.size(), 1);
        QVERIFY(layer.strokes.first().points.first().position.x() > 70.0);
    }

    void clipsDrawingToolsToPersistentLasso()
    {
        Document document = Document::createDefault(QSize(100, 100));
        document.wobbleAmount = 0.0;
        DocumentController controller;
        controller.loadDocument(document);
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));

        const QPoint center = canvas.rect().center();
        canvas.setTool(CanvasWidget::Tool::Lasso);
        const QPoint topLeft = center - QPoint(50, 50);
        const QPoint topRight = center + QPoint(50, -50);
        const QPoint bottomRight = center + QPoint(50, 50);
        const QPoint bottomLeft = center + QPoint(-50, 50);
        QTest::mousePress(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            topLeft);
        QTest::mouseMove(&canvas, topRight, 5);
        QTest::mouseMove(&canvas, bottomRight, 5);
        QTest::mouseMove(&canvas, bottomLeft, 5);
        QTest::mouseRelease(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            topLeft);
        QVERIFY(canvas.hasSelection());

        canvas.setTool(CanvasWidget::Tool::Brush);
        QVERIFY(canvas.hasSelection());
        QTest::mousePress(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            center - QPoint(120, 0));
        QTest::mouseMove(&canvas, center + QPoint(120, 0), 5);
        QTest::mouseRelease(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            center + QPoint(120, 0));
        QCOMPARE(controller.document().layers.first().strokes.size(), 1);
        QVERIFY(!controller.document()
                     .layers.first()
                     .strokes.first()
                     .clipMask.isNull());
        QImage rendered = RenderEngine::render(
            controller.document(),
            0);
        QCOMPARE(rendered.pixelColor(20, 50), QColor(Qt::white));
        QCOMPARE(rendered.pixelColor(50, 50), QColor(Qt::black));

        canvas.setBrushColor(QColor(220, 30, 40));
        canvas.setTool(CanvasWidget::Tool::Bucket);
        QVERIFY(canvas.hasSelection());
        QTest::mouseClick(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            center - QPoint(0, 25));
        QCOMPARE(controller.document().layers.first().strokes.size(), 2);
        QVERIFY(!controller.document()
                     .layers.first()
                     .strokes.last()
                     .clipMask.isNull());
        rendered = RenderEngine::render(controller.document(), 0);
        QCOMPARE(
            rendered.pixelColor(50, 42),
            canvas.brushColor());
        QCOMPARE(rendered.pixelColor(20, 42), QColor(Qt::white));
    }

    void autosavesModifiedWork()
    {
        const QString recoveryKey =
            QStringLiteral("recovery/sourcePath");
        SettingValueGuard recoveryGuard(recoveryKey);
        EnvironmentVariableGuard environmentGuard(
            QByteArrayLiteral("WAGLEWAGLEPAINT_RECOVERY_PATH"));
        QTemporaryDir recoveryDirectory;
        QVERIFY(recoveryDirectory.isValid());
        const QString recoveryPath = recoveryDirectory.filePath(
            QStringLiteral("recovery.wagle"));
        qputenv(
            "WAGLEWAGLEPAINT_RECOVERY_PATH",
            recoveryPath.toUtf8());

        MainWindow window;
        window.resize(1000, 680);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QVERIFY(canvas);

        const QPoint center = canvas->rect().center();
        QTest::mousePress(
            canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            center - QPoint(40, 0));
        QTest::mouseMove(canvas, center + QPoint(40, 0), 5);
        QTest::mouseRelease(
            canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            center + QPoint(40, 0));
        QTRY_VERIFY(window.isWindowModified());

        QEvent deactivate(QEvent::ApplicationDeactivate);
        QApplication::sendEvent(qApp, &deactivate);
        QVERIFY(QFileInfo::exists(recoveryPath));

        QString error;
        const std::optional<Document> recovered =
            DocumentSerializer::load(recoveryPath, &error);
        QVERIFY2(recovered.has_value(), qPrintable(error));
        QCOMPARE(recovered->layers.first().strokes.size(), 1);
        QFile::remove(recoveryPath);
    }

    void globalPanModifierPreservesTextInput()
    {
        MainWindow window;
        window.resize(1000, 680);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QListWidget *layerList = window.findChild<QListWidget *>();
        QVERIFY(canvas);
        QVERIFY(layerList);

        layerList->setFocus(Qt::OtherFocusReason);
        QTest::keyPress(layerList, Qt::Key_Space);
        QCOMPARE(canvas->cursor().shape(), Qt::OpenHandCursor);
        QTest::keyRelease(layerList, Qt::Key_Space);
        QCOMPARE(canvas->cursor().shape(), Qt::BlankCursor);

        QLineEdit editor(&window);
        editor.show();
        editor.setFocus(Qt::OtherFocusReason);
        QTest::keyClicks(&editor, QStringLiteral("Layer name"));
        QCOMPARE(editor.text(), QStringLiteral("Layer name"));
        QCOMPARE(canvas->cursor().shape(), Qt::BlankCursor);
    }
};

int runUiTests(int argc, char **argv)
{
    UiTests tests;
    return QTest::qExec(&tests, argc, argv);
}

}

#include "UiTests.moc"

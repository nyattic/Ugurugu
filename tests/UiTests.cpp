#include "brush/BrushPreset.hpp"
#include "document/DocumentLimits.hpp"
#include "document/SelectionOperation.hpp"
#include "ui/BrushPopoverPanel.hpp"
#include "ui/BrushPresetButton.hpp"
#include "ui/BrushSizeRow.hpp"
#include "ui/CanvasSizeDialog.hpp"
#include "ui/CanvasWidget.hpp"
#include "ui/ImageSizeDialog.hpp"
#include "ui/LayerDock.hpp"
#include "ui/MainWindow.hpp"
#include "ui/SelectionActionBar.hpp"
#include "ui/SettingsDialog.hpp"
#include "io/DocumentSerializer.hpp"
#include "render/RenderEngine.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFocusEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointingDevice>
#include <QPushButton>
#include <QPixmap>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTabletEvent>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QVariant>
#include <QtTest>

#include <algorithm>
#include <limits>
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

class ApplicationVersionGuard final
{
public:
    ApplicationVersionGuard()
        : m_version(QApplication::applicationVersion())
    {
    }

    ~ApplicationVersionGuard()
    {
        QApplication::setApplicationVersion(m_version);
    }

private:
    QString m_version;
};

class UiTests final : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        QSettings settings;
        settings.remove(QStringLiteral("drawingTools"));
        settings.remove(QStringLiteral("brush/recentColors"));
        settings.sync();
    }

    void cleanup()
    {
        QSettings settings;
        settings.remove(QStringLiteral("drawingTools"));
        settings.remove(QStringLiteral("brush/recentColors"));
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
        settings.sync();

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

    void showsApplicationVersionInAboutTab()
    {
        const ApplicationVersionGuard versionGuard;
        QApplication::setApplicationVersion(
            QStringLiteral("9.8.7-test"));

        SettingsDialog dialog;
        QTabWidget *tabs = dialog.findChild<QTabWidget *>();
        QLabel *versionLabel = dialog.findChild<QLabel *>(
            QStringLiteral("applicationVersionLabel"));
        QWidget *aboutTab = dialog.findChild<QWidget *>(
            QStringLiteral("aboutTab"));
        QVERIFY(tabs);
        QVERIFY(versionLabel);
        QVERIFY(aboutTab);
        QCOMPARE(
            versionLabel->text(),
            QStringLiteral("Version 9.8.7-test"));
        QCOMPARE(
            tabs->tabText(tabs->indexOf(aboutTab)),
            QStringLiteral("About"));
    }

    void configuresCanvasSizeDialog()
    {
        CanvasSizeDialog dialog(QSize(640, 480));
        QCheckBox *relativeCheck = dialog.findChild<QCheckBox *>(
            QStringLiteral("canvasRelativeSizeCheck"));
        QSpinBox *widthSpin = dialog.findChild<QSpinBox *>(
            QStringLiteral("canvasWidthSpin"));
        QSpinBox *heightSpin = dialog.findChild<QSpinBox *>(
            QStringLiteral("canvasHeightSpin"));
        QSpinBox *offsetXSpin = dialog.findChild<QSpinBox *>(
            QStringLiteral("canvasOffsetXSpin"));
        QSpinBox *offsetYSpin = dialog.findChild<QSpinBox *>(
            QStringLiteral("canvasOffsetYSpin"));
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
        QSpinBox *widthSpin = dialog.findChild<QSpinBox *>(
            QStringLiteral("imageWidthSpin"));
        QSpinBox *heightSpin = dialog.findChild<QSpinBox *>(
            QStringLiteral("imageHeightSpin"));
        QDoubleSpinBox *percentageSpin =
            dialog.findChild<QDoubleSpinBox *>(
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
        QVERIFY(warningLabel->text().contains(
            QStringLiteral("distorted")));

        keepAspectCheck->setChecked(true);
        QCOMPARE(dialog.imageSize(), QSize(800, 600));
        percentageSpin->setValue(200.0);
        QCOMPARE(dialog.imageSize(), QSize(1280, 960));
        const ImageSizeDialog::Result uniform = dialog.result();
        QVERIFY(qAbs(uniform.horizontalScale - 2.0) < 0.0001);
        QVERIFY(qAbs(uniform.verticalScale - 2.0) < 0.0001);
    }

    void handlesUnsavedChangesDialogShortcuts_data()
    {
        QTest::addColumn<int>("key");
        QTest::addColumn<bool>("closesWindow");
        QTest::addColumn<bool>("savesDocument");

        QTest::newRow("save")
            << int(Qt::Key_S)
            << true
            << true;
        QTest::newRow("discard")
            << int(Qt::Key_N)
            << true
            << false;
        QTest::newRow("cancel")
            << int(Qt::Key_Escape)
            << false
            << false;
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
        qputenv(
            "WAGLEWAGLEPAINT_RECOVERY_PATH",
            directory.filePath(QStringLiteral("recovery.wagle")).toUtf8());
        const QString filePath =
            directory.filePath(QStringLiteral("shortcuts.wagle"));
        QString error;
        QVERIFY2(
            DocumentSerializer::save(
                filePath,
                Document::createDefault(QSize(100, 100)),
                &error),
            qPrintable(error));

        MainWindow window;
        window.resize(1000, 680);
        QVERIFY(window.openFile(filePath));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        QToolButton *addLayerButton =
            window.findChild<QToolButton *>(
                QStringLiteral("layerAddButton"));
        QVERIFY(addLayerButton);
        addLayerButton->click();
        QTRY_VERIFY(window.isWindowModified());

        bool dialogInspected = false;
        QString saveText;
        QString discardText;
        QString cancelText;
        QTimer::singleShot(0, &window, [&]() {
            QDialog *dialog =
                qobject_cast<QDialog *>(
                    QApplication::activeModalWidget());
            if (!dialog) {
                return;
            }
            QPushButton *saveButton =
                dialog->findChild<QPushButton *>(
                    QStringLiteral("unsavedChangesSaveButton"));
            QPushButton *discardButton =
                dialog->findChild<QPushButton *>(
                    QStringLiteral("unsavedChangesDiscardButton"));
            QPushButton *cancelButton =
                dialog->findChild<QPushButton *>(
                    QStringLiteral("unsavedChangesCancelButton"));
            if (!saveButton || !discardButton || !cancelButton) {
                QTest::keyClick(dialog, Qt::Key_Escape);
                return;
            }
            saveText = saveButton->text();
            discardText = discardButton->text();
            cancelText = cancelButton->text();
            dialogInspected = true;
            QWidget *keyTarget = QApplication::focusWidget();
            QTest::keyClick(
                keyTarget ? keyTarget : dialog,
                static_cast<Qt::Key>(key));
        });
        QTimer::singleShot(1000, &window, []() {
            QDialog *dialog =
                qobject_cast<QDialog *>(
                    QApplication::activeModalWidget());
            if (dialog) {
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
        QCOMPARE(
            savedDocument->layers.size(),
            savesDocument ? 2 : 1);
        if (!closesWindow) {
            QVERIFY(window.isVisible());
            QVERIFY(window.isWindowModified());
        }
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
        QAction *moveAction = window.findChild<QAction *>(
            QStringLiteral("moveSelectionAction"));
        QAction *applyTransformAction = window.findChild<QAction *>(
            QStringLiteral("applySelectionTransformAction"));
        QAction *cancelTransformAction = window.findChild<QAction *>(
            QStringLiteral("cancelSelectionTransformAction"));
        SelectionActionBar *actionBar =
            window.findChild<SelectionActionBar *>();
        QToolButton *moveButton = window.findChild<QToolButton *>(
            QStringLiteral("moveSelectionButton"));
        QToolButton *applyTransformButton =
            window.findChild<QToolButton *>(
                QStringLiteral("applySelectionTransformButton"));
        QToolButton *cancelTransformButton =
            window.findChild<QToolButton *>(
                QStringLiteral("cancelSelectionTransformButton"));
        QVERIFY(canvas);
        QVERIFY(brushAction);
        QVERIFY(lassoAction);
        QVERIFY(bucketAction);
        QVERIFY(scaleAction);
        QVERIFY(rotateAction);
        QVERIFY(duplicateAction);
        QVERIFY(moveAction);
        QVERIFY(applyTransformAction);
        QVERIFY(cancelTransformAction);
        QVERIFY(actionBar);
        QVERIFY(moveButton);
        QVERIFY(applyTransformButton);
        QVERIFY(cancelTransformButton);
        QVERIFY(!scaleAction->isEnabled());
        QVERIFY(!rotateAction->isEnabled());
        QVERIFY(!duplicateAction->isEnabled());
        QVERIFY(!moveAction->isEnabled());
        QVERIFY(!applyTransformAction->isEnabled());
        QVERIFY(!cancelTransformAction->isEnabled());
        QVERIFY(!actionBar->isVisible());

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
        QTRY_VERIFY(moveAction->isEnabled());
        QTRY_VERIFY(actionBar->isVisible());

        bucketAction->trigger();
        QVERIFY(canvas->hasSelection());
        brushAction->trigger();
        QVERIFY(canvas->hasSelection());
        QVERIFY(canvas->scaleSelection(0.75));
        QVERIFY(canvas->hasPendingSelectionTransform());
        QVERIFY(applyTransformAction->isEnabled());
        QVERIFY(cancelTransformAction->isEnabled());
        QVERIFY(canvas->rotateSelection(90.0));
        QVERIFY(canvas->hasPendingSelectionTransform());
        QTest::mouseClick(applyTransformButton, Qt::LeftButton);
        QTRY_VERIFY(!canvas->hasSelectionTransformSession());
        QVERIFY(!applyTransformAction->isEnabled());
        QVERIFY(!cancelTransformAction->isEnabled());
        duplicateAction->trigger();
        QVERIFY(canvas->hasTransformableSelection());

        QTest::mouseClick(moveButton, Qt::LeftButton);
        QVERIFY(canvas->selectionMoveMode());
        moveButton->setFocus();
        QVERIFY(moveButton->hasFocus());
        QTest::keyClick(moveButton, Qt::Key_Escape);
        QTRY_VERIFY(!canvas->selectionMoveMode());
        QVERIFY(canvas->hasSelection());
        QTest::keyClick(moveButton, Qt::Key_Escape);
        QTRY_VERIFY(!canvas->hasSelection());
    }

    void routesTransformApplyAndCancelThroughMainWindowShortcuts()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString filePath =
            directory.filePath(QStringLiteral("transform-shortcuts.wagle"));
        Document document = Document::createDefault(QSize(100, 100));
        document.wobbleAmount = 0.0;
        Stroke source;
        source.width = 10.0;
        source.points = {
            {QPointF(30.0, 50.0), 1.0},
            {QPointF(70.0, 50.0), 1.0}
        };
        source.brush.antialiasing = false;
        document.layers.first().strokes.append(source);
        QString error;
        QVERIFY2(
            DocumentSerializer::save(filePath, document, &error),
            qPrintable(error));

        MainWindow window;
        window.resize(1000, 680);
        QVERIFY(window.openFile(filePath));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QAction *lassoAction = window.findChild<QAction *>(
            QStringLiteral("lassoAction"));
        QAction *applyAction = window.findChild<QAction *>(
            QStringLiteral("applySelectionTransformAction"));
        QAction *cancelAction = window.findChild<QAction *>(
            QStringLiteral("cancelSelectionTransformAction"));
        QAction *escapeAction = window.findChild<QAction *>(
            QStringLiteral("escapeCanvasAction"));
        QAction *undoAction = window.findChild<QAction *>(
            QStringLiteral("undoAction"));
        QToolButton *applyButton = window.findChild<QToolButton *>(
            QStringLiteral("applySelectionTransformButton"));
        QVERIFY(canvas);
        QVERIFY(lassoAction);
        QVERIFY(applyAction);
        QVERIFY(cancelAction);
        QVERIFY(escapeAction);
        QVERIFY(undoAction);
        QVERIFY(applyButton);
        QCOMPARE(
            applyAction->shortcut(),
            QKeySequence(QStringLiteral("Return")));
        QCOMPARE(
            escapeAction->shortcut(),
            QKeySequence(Qt::Key_Escape));

        lassoAction->trigger();
        const QPoint center = canvas->rect().center();
        const QPoint topLeft = center - QPoint(100, 70);
        const QPoint topRight = center + QPoint(100, -70);
        const QPoint bottomRight = center + QPoint(100, 70);
        const QPoint bottomLeft = center + QPoint(-100, 70);
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
        QTest::mouseMove(canvas, topRight, 5);
        QTest::mouseMove(canvas, bottomRight, 5);
        QTest::mouseMove(canvas, bottomLeft, 5);
        QTest::mouseRelease(
            canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            topLeft);
        QTRY_VERIFY(canvas->hasTransformableSelection());
        QVERIFY(!window.isWindowModified());
        const QString undoTextBeforeTransform = undoAction->text();

        QVERIFY(canvas->rotateSelection(15.0));
        QVERIFY(applyAction->isEnabled());
        QVERIFY(cancelAction->isEnabled());
        QVERIFY(canvas->hasPendingSelectionTransform());
        QSignalSpy escapeTriggered(escapeAction, &QAction::triggered);
        applyButton->setFocus();
        QVERIFY(applyButton->hasFocus());
        QTest::keyClick(applyButton, Qt::Key_Escape);
        QTRY_COMPARE(escapeTriggered.size(), 1);
        QVERIFY(!canvas->hasSelectionTransformSession());
        QVERIFY(canvas->hasTransformableSelection());
        QVERIFY(!window.isWindowModified());
        QCOMPARE(undoAction->text(), undoTextBeforeTransform);

        QVERIFY(canvas->scaleSelection(0.8));
        QVERIFY(canvas->hasPendingSelectionTransform());
        QSignalSpy applyTriggered(applyAction, &QAction::triggered);
        canvas->setFocus();
        QTest::keyClick(canvas, Qt::Key_Return);
        QTRY_COMPARE(applyTriggered.size(), 1);
        QTRY_VERIFY(!canvas->hasSelectionTransformSession());
        QTRY_VERIFY(window.isWindowModified());
        QVERIFY(undoAction->text() != undoTextBeforeTransform);

        undoAction->trigger();
        QTRY_VERIFY(!window.isWindowModified());
        QTRY_VERIFY(canvas->hasTransformableSelection());
        QCOMPARE(undoAction->text(), undoTextBeforeTransform);
    }

    void floatingSelectionTransformCommitsOnceAndCancelsLosslessly()
    {
        Document document = Document::createDefault(QSize(120, 100));
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;
        Stroke source;
        source.color = QColor(35, 95, 225);
        source.width = 12.0;
        source.points = {
            {QPointF(30.0, 50.0), 1.0},
            {QPointF(70.0, 50.0), 1.0}
        };
        source.brush.antialiasing = false;
        document.layers.first().strokes.append(source);

        DocumentController controller;
        controller.loadDocument(document);
        CanvasWidget canvas(&controller);
        canvas.resize(480, 400);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.fitToWindow();

        const auto widgetPoint = [&canvas](const QPointF &documentPoint) {
            const QPointF center(canvas.rect().center());
            return (center
                    + (documentPoint - QPointF(60.0, 50.0))
                        * canvas.zoom())
                .toPoint();
        };
        canvas.setTool(CanvasWidget::Tool::Lasso);
        const QPoint topLeft = widgetPoint(QPointF(20.0, 35.0));
        const QPoint topRight = widgetPoint(QPointF(80.0, 35.0));
        const QPoint bottomRight = widgetPoint(QPointF(80.0, 65.0));
        const QPoint bottomLeft = widgetPoint(QPointF(20.0, 65.0));
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
        QTest::mouseMove(&canvas, topRight, 5);
        QTest::mouseMove(&canvas, bottomRight, 5);
        QTest::mouseMove(&canvas, bottomLeft, 5);
        QTest::mouseRelease(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            topLeft);
        QTRY_VERIFY(canvas.hasTransformableSelection());

        const QByteArray originalDocument =
            DocumentSerializer::toJson(controller.document());
        const QImage originalFrame =
            RenderEngine::render(controller.document(), 0);
        const int originalUndoCount = controller.undoStack()->count();
        const int originalUndoIndex = controller.undoStack()->index();

        QVERIFY(canvas.scaleSelection(1.2));
        const QTransform afterScale = canvas.pendingSelectionTransform();
        QVERIFY(canvas.rotateSelection(18.0));
        QVERIFY(
            canvas.pendingSelectionTransform()
            != afterScale);
        QVERIFY(canvas.flipSelectionHorizontally());
        QVERIFY(canvas.hasPendingSelectionTransform());
        QCOMPARE(
            DocumentSerializer::toJson(controller.document()),
            originalDocument);
        QCOMPARE(controller.document().layers.first().strokes.size(), 1);
        QCOMPARE(controller.undoStack()->count(), originalUndoCount);
        QCOMPARE(controller.undoStack()->index(), originalUndoIndex);

        canvas.handleEscape();
        QVERIFY(!canvas.hasSelectionTransformSession());
        QVERIFY(canvas.hasTransformableSelection());
        QCOMPARE(
            DocumentSerializer::toJson(controller.document()),
            originalDocument);
        QCOMPARE(controller.undoStack()->count(), originalUndoCount);
        QCOMPARE(controller.undoStack()->index(), originalUndoIndex);

        QVERIFY(canvas.scaleSelection(1.2));
        QVERIFY(canvas.rotateSelection(18.0));
        QVERIFY(canvas.flipSelectionHorizontally());
        const QTransform accumulated =
            canvas.pendingSelectionTransform();
        QCOMPARE(
            DocumentSerializer::toJson(controller.document()),
            originalDocument);
        QVERIFY(canvas.applySelectionTransform());
        QVERIFY(!canvas.hasSelectionTransformSession());
        QCOMPARE(controller.undoStack()->count(), originalUndoCount + 1);
        QCOMPARE(controller.undoStack()->index(), originalUndoIndex + 1);
        QCOMPARE(controller.document().layers.first().strokes.size(), 2);
        const Stroke &operation =
            controller.document().layers.first().strokes.last();
        QCOMPARE(operation.mode, StrokeMode::PixelSelection);
        QVERIFY(operation.pixelSelectionOp.has_value());
        QCOMPARE(operation.pixelSelectionOp->transform, accumulated);

        controller.undoStack()->undo();
        QCOMPARE(
            DocumentSerializer::toJson(controller.document()),
            originalDocument);
        QCOMPARE(
            RenderEngine::render(controller.document(), 0),
            originalFrame);

        QVERIFY(canvas.scaleSelection(1.1));
        QVERIFY(canvas.hasPendingSelectionTransform());
        canvas.setTool(CanvasWidget::Tool::Brush);
        QVERIFY(!canvas.hasSelectionTransformSession());
        QVERIFY(canvas.hasTransformableSelection());
        QCOMPARE(
            DocumentSerializer::toJson(controller.document()),
            originalDocument);

        QVERIFY(canvas.scaleSelection(1.1));
        const int beforeDuplicateIndex =
            controller.undoStack()->index();
        QVERIFY(canvas.duplicateSelection());
        QVERIFY(!canvas.hasSelectionTransformSession());
        QCOMPARE(
            controller.undoStack()->index(),
            beforeDuplicateIndex + 1);
        const Stroke &duplicateOperation =
            controller.document().layers.first().strokes.last();
        QCOMPARE(duplicateOperation.mode, StrokeMode::PixelSelection);
        QVERIFY(duplicateOperation.pixelSelectionOp.has_value());
        QVERIFY(qFuzzyCompare(
            duplicateOperation.pixelSelectionOp->transform.dx() + 1.0,
            13.0));
        controller.undoStack()->undo();
        QCOMPARE(
            DocumentSerializer::toJson(controller.document()),
            originalDocument);

        QVERIFY(canvas.scaleSelection(1.1));
        QVERIFY(controller.resizeCanvas(QSize(130, 100), QPoint(5, 0)));
        QVERIFY(!canvas.hasSelectionTransformSession());
        QVERIFY(canvas.hasTransformableSelection());

        QVERIFY(canvas.rotateSelection(5.0));
        controller.loadDocument(document);
        QVERIFY(!canvas.hasSelectionTransformSession());
        QVERIFY(!canvas.hasSelection());
    }

    void clipsPartialFloatingTransformAndRetainsFailedSession()
    {
        Document document = Document::createDefault(QSize(100, 100));
        document.background = Qt::white;
        document.wobbleAmount = 0.0;
        Stroke source;
        source.color = QColor(35, 95, 225);
        source.width = 10.0;
        source.points = {
            {QPointF(5.0, 50.0), 1.0},
            {QPointF(30.0, 50.0), 1.0}
        };
        source.brush.antialiasing = false;
        document.layers.first().strokes.append(source);

        DocumentController controller;
        controller.loadDocument(document);
        CanvasWidget canvas(&controller);
        canvas.resize(420, 420);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.fitToWindow();

        const auto widgetPoint = [&canvas](const QPointF &documentPoint) {
            const QPointF center(canvas.rect().center());
            return (center
                    + (documentPoint - QPointF(50.0, 50.0))
                        * canvas.zoom())
                .toPoint();
        };
        const auto sample = [&canvas, &widgetPoint](
                                const QPixmap &pixmap,
                                const QPointF &documentPoint) {
            const QImage image = pixmap.toImage();
            const QPoint widgetPosition = widgetPoint(documentPoint);
            const qreal xScale =
                static_cast<qreal>(image.width()) / canvas.width();
            const qreal yScale =
                static_cast<qreal>(image.height()) / canvas.height();
            return image.pixelColor(
                std::clamp(
                    qRound(widgetPosition.x() * xScale),
                    0,
                    image.width() - 1),
                std::clamp(
                    qRound(widgetPosition.y() * yScale),
                    0,
                    image.height() - 1));
        };
        canvas.setTool(CanvasWidget::Tool::Lasso);
        const QPoint topLeft = widgetPoint(QPointF(0.0, 38.0));
        const QPoint topRight = widgetPoint(QPointF(36.0, 38.0));
        const QPoint bottomRight = widgetPoint(QPointF(36.0, 62.0));
        const QPoint bottomLeft = widgetPoint(QPointF(0.0, 62.0));
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
        QTest::mouseMove(&canvas, topRight, 5);
        QTest::mouseMove(&canvas, bottomRight, 5);
        QTest::mouseMove(&canvas, bottomLeft, 5);
        QTest::mouseRelease(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            topLeft);
        QTRY_VERIFY(canvas.hasTransformableSelection());

        const QByteArray beforeTransform =
            DocumentSerializer::toJson(controller.document());
        const QImage beforeFrame =
            RenderEngine::render(controller.document(), 0);
        const int undoCount = controller.undoStack()->count();
        const int undoIndex = controller.undoStack()->index();

        canvas.setSelectionMoveMode(true);
        const QPoint partialStart = widgetPoint(QPointF(10.0, 50.0));
        const QPoint partialEnd = widgetPoint(QPointF(-10.0, 50.0));
        QTest::mousePress(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            partialStart);
        QTest::mouseMove(&canvas, partialEnd, 5);
        QTest::mouseRelease(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            partialEnd);
        QVERIFY(canvas.hasPendingSelectionTransform());
        QVERIFY(canvas.pendingSelectionTransform().dx() < -19.0);
        QCOMPARE(
            DocumentSerializer::toJson(controller.document()),
            beforeTransform);
        const QPixmap partialPreview = canvas.grab();
        QVERIFY(canvas.applySelectionTransform());
        QApplication::processEvents();
        const QPixmap partialCommitted = canvas.grab();
        QCOMPARE(
            sample(partialPreview, QPointF(3.0, 50.0)),
            sample(partialCommitted, QPointF(3.0, 50.0)));
        QCOMPARE(controller.undoStack()->count(), undoCount + 1);
        QCOMPARE(controller.undoStack()->index(), undoIndex + 1);

        controller.undoStack()->undo();
        QCOMPARE(
            DocumentSerializer::toJson(controller.document()),
            beforeTransform);
        QCOMPARE(
            RenderEngine::render(controller.document(), 0),
            beforeFrame);

        canvas.setSelectionMoveMode(true);
        const QPoint outsideStart = widgetPoint(QPointF(10.0, 50.0));
        const QPoint outsideEnd = widgetPoint(QPointF(-80.0, 50.0));
        QTest::mousePress(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            outsideStart);
        QTest::mouseMove(&canvas, outsideEnd, 5);
        QTest::mouseRelease(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            outsideEnd);
        QVERIFY(canvas.hasPendingSelectionTransform());
        const QTransform rejected = canvas.pendingSelectionTransform();
        QVERIFY(rejected.dx() < -89.0);
        QVERIFY(!canvas.applySelectionTransform());
        QVERIFY(canvas.hasPendingSelectionTransform());
        QCOMPARE(canvas.pendingSelectionTransform(), rejected);
        QCOMPARE(
            DocumentSerializer::toJson(controller.document()),
            beforeTransform);
        QCOMPARE(controller.undoStack()->index(), undoIndex);
        canvas.cancelSelectionTransform();
        QVERIFY(!canvas.hasSelectionTransformSession());
        QVERIFY(canvas.hasTransformableSelection());
        QCOMPARE(
            DocumentSerializer::toJson(controller.document()),
            beforeTransform);
    }

    void requiresExplicitSelectionMoveMode()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.setAnimating(false);

        QAction moveAction(&canvas);
        moveAction.setObjectName(QStringLiteral("moveSelectionAction"));
        moveAction.setCheckable(true);
        connect(
            &moveAction,
            &QAction::toggled,
            &canvas,
            &CanvasWidget::setSelectionMoveMode);
        connect(
            &canvas,
            &CanvasWidget::selectionMoveModeChanged,
            &moveAction,
            &QAction::setChecked);

        auto *actionBar = new SelectionActionBar(&canvas);
        QToolButton *moveButton = actionBar->addAction(&moveAction);
        canvas.setSelectionActionBar(actionBar);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.fitToWindow();
        QVERIFY(moveButton);
        QVERIFY(!actionBar->isVisible());

        const QPoint center = canvas.rect().center();
        QTest::mousePress(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            center - QPoint(55, 0));
        QTest::mouseMove(&canvas, center + QPoint(55, 0), 5);
        QTest::mouseRelease(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            center + QPoint(55, 0));

        canvas.setTool(CanvasWidget::Tool::Lasso);
        const QPoint topLeft = center - QPoint(90, 55);
        const QPoint topRight = center + QPoint(90, -55);
        const QPoint bottomRight = center + QPoint(90, 55);
        const QPoint bottomLeft = center + QPoint(-90, 55);
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

        QTRY_VERIFY(canvas.hasTransformableSelection());
        QTRY_VERIFY(actionBar->isVisible());
        QVERIFY(!moveAction.isChecked());
        QVERIFY(!canvas.selectionMoveMode());

        const QByteArray beforeInactiveDrag =
            DocumentSerializer::toJson(controller.document());
        QTest::mousePress(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            center);
        QTest::mouseMove(&canvas, center + QPoint(30, 10), 5);
        QTest::mouseRelease(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            center + QPoint(30, 10));
        QCOMPARE(
            DocumentSerializer::toJson(controller.document()),
            beforeInactiveDrag);

        controller.undoStack()->undo();
        QTRY_VERIFY(canvas.hasTransformableSelection());
        QTRY_VERIFY(actionBar->isVisible());
        QTest::mouseClick(moveButton, Qt::LeftButton);
        QVERIFY(moveAction.isChecked());
        QVERIFY(canvas.selectionMoveMode());

        const QByteArray beforeActiveDrag =
            DocumentSerializer::toJson(controller.document());
        const int undoCountBeforeActiveDrag =
            controller.undoStack()->count();
        const int undoIndexBeforeActiveDrag =
            controller.undoStack()->index();
        QTest::mousePress(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            center);
        QTest::mouseMove(&canvas, center + QPoint(35, 15), 5);
        QTest::mouseRelease(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            center + QPoint(35, 15));
        QCOMPARE(
            DocumentSerializer::toJson(controller.document()),
            beforeActiveDrag);
        QVERIFY(canvas.hasPendingSelectionTransform());
        QCOMPARE(
            controller.undoStack()->count(),
            undoCountBeforeActiveDrag);
        QCOMPARE(
            controller.undoStack()->index(),
            undoIndexBeforeActiveDrag);

        QVERIFY(canvas.applySelectionTransform());
        QVERIFY(!canvas.hasSelectionTransformSession());
        QVERIFY(
            DocumentSerializer::toJson(controller.document())
            != beforeActiveDrag);
        QCOMPARE(
            controller.undoStack()->count(),
            undoIndexBeforeActiveDrag + 1);
        QCOMPARE(
            controller.undoStack()->index(),
            undoIndexBeforeActiveDrag + 1);
    }

    void rejectsSelectionMoveJustOutsideTheCanvas()
    {
        Document document = Document::createDefault(QSize(100, 100));
        document.wobbleAmount = 0.0;
        Stroke stroke;
        stroke.width = 4.0;
        stroke.points = {
            {QPointF(1.0, 30.0), 1.0},
            {QPointF(1.0, 70.0), 1.0}
        };
        document.layers.first().strokes.append(stroke);

        DocumentController controller;
        controller.loadDocument(document);
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.fitToWindow();

        const auto widgetPoint = [&canvas](const QPointF &documentPoint) {
            const QPointF center(canvas.rect().center());
            return (center
                    + (documentPoint - QPointF(50.0, 50.0))
                        * canvas.zoom())
                .toPoint();
        };
        canvas.setTool(CanvasWidget::Tool::Lasso);
        const QPoint topLeft = widgetPoint(QPointF(0.0, 20.0));
        const QPoint topRight = widgetPoint(QPointF(15.0, 20.0));
        const QPoint bottomRight = widgetPoint(QPointF(15.0, 80.0));
        const QPoint bottomLeft = widgetPoint(QPointF(0.0, 80.0));
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
        QTRY_VERIFY(canvas.hasTransformableSelection());

        canvas.setSelectionMoveMode(true);
        QVERIFY(canvas.selectionMoveMode());
        const QByteArray before =
            DocumentSerializer::toJson(controller.document());
        QSignalSpy messages(
            &canvas,
            &CanvasWidget::interactionMessage);
        const QPoint justOutside =
            widgetPoint(QPointF(-0.25, 50.0));
        QTest::mousePress(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            justOutside);
        QTest::mouseMove(
            &canvas,
            justOutside + QPoint(20, 0),
            5);
        QTest::mouseRelease(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            justOutside + QPoint(20, 0));

        QCOMPARE(
            DocumentSerializer::toJson(controller.document()),
            before);
        QCOMPARE(messages.size(), 1);
        QCOMPARE(
            messages.first().first().toString(),
            QStringLiteral("Drag inside the selection to move it."));
    }

    void selectionHitTestingRespectsClipAndLayerVisibility()
    {
        Document document = Document::createDefault(QSize(100, 100));
        document.wobbleAmount = 0.0;
        Stroke stroke;
        stroke.width = 5.0;
        stroke.points = {
            {QPointF(10.0, 50.0), 1.0},
            {QPointF(90.0, 50.0), 1.0}
        };
        stroke.clipMask =
            QImage(document.size, QImage::Format_Grayscale8);
        stroke.clipMask.fill(0);
        for (int y = 0; y < stroke.clipMask.height(); ++y) {
            std::fill_n(stroke.clipMask.scanLine(y), 40, 255);
        }
        document.layers.first().strokes.append(stroke);
        const QUuid layerId = document.activeLayerId;

        DocumentController controller;
        controller.loadDocument(document);
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.fitToWindow();

        const auto widgetPoint = [&canvas](const QPointF &documentPoint) {
            const QPointF center(canvas.rect().center());
            return (center
                    + (documentPoint - QPointF(50.0, 50.0))
                        * canvas.zoom())
                .toPoint();
        };
        const auto lasso = [&canvas, &widgetPoint](
                               const QRectF &documentRect) {
            const QPoint topLeft =
                widgetPoint(documentRect.topLeft());
            const QPoint topRight =
                widgetPoint(documentRect.topRight());
            const QPoint bottomRight =
                widgetPoint(documentRect.bottomRight());
            const QPoint bottomLeft =
                widgetPoint(documentRect.bottomLeft());
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
        };

        canvas.setTool(CanvasWidget::Tool::Lasso);
        lasso(QRectF(60.0, 40.0, 25.0, 20.0));
        QTRY_VERIFY(canvas.hasSelection());
        QVERIFY(!canvas.hasTransformableSelection());

        canvas.deselectSelection();
        controller.setLayerVisible(layerId, false);
        lasso(QRectF(10.0, 40.0, 25.0, 20.0));
        QTRY_VERIFY(canvas.hasSelection());
        QVERIFY(!canvas.hasTransformableSelection());
    }

    void selectionHitTestingUsesProceduralFillCoverage()
    {
        Document document = Document::createDefault(QSize(100, 100));
        document.wobbleAmount = 0.0;

        Stroke boundary;
        boundary.color = Qt::black;
        boundary.width = 6.0;
        boundary.points = {
            {QPointF(50.0, 0.0), 1.0},
            {QPointF(50.0, 99.0), 1.0}
        };
        boundary.brush.antialiasing = false;

        Stroke fill;
        fill.mode = StrokeMode::Fill;
        fill.color = QColor(30, 80, 220);
        fill.points = {{QPointF(70.0, 50.0), 1.0}};
        document.layers.first().strokes = {boundary, fill};

        DocumentController controller;
        controller.loadDocument(document);
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.fitToWindow();

        const auto widgetPoint = [&canvas](const QPointF &documentPoint) {
            const QPointF center(canvas.rect().center());
            return (center
                    + (documentPoint - QPointF(50.0, 50.0))
                        * canvas.zoom())
                .toPoint();
        };
        const auto lassoFillCoverage = [&]() {
            const QPoint topLeft =
                widgetPoint(QPointF(58.0, 38.0));
            const QPoint topRight =
                widgetPoint(QPointF(83.0, 38.0));
            const QPoint bottomRight =
                widgetPoint(QPointF(83.0, 63.0));
            const QPoint bottomLeft =
                widgetPoint(QPointF(58.0, 63.0));
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
        };

        canvas.setTool(CanvasWidget::Tool::Lasso);
        lassoFillCoverage();
        QTRY_VERIFY(canvas.hasTransformableSelection());

        canvas.deselectSelection();
        document.layers.first().strokes.last().points = {
            {QPointF(20.0, 50.0), 1.0}
        };
        controller.loadDocument(document);
        lassoFillCoverage();
        QTRY_VERIFY(canvas.hasSelection());
        QVERIFY(!canvas.hasTransformableSelection());
    }

    void selectionHitTestingUsesFinalLayerPixelsIncludingOperations()
    {
        Document document = Document::createDefault(QSize(100, 100));
        document.wobbleAmount = 0.0;

        Stroke paint;
        paint.color = QColor(30, 90, 220);
        paint.width = 12.0;
        paint.points = {
            {QPointF(10.0, 50.0), 1.0},
            {QPointF(35.0, 50.0), 1.0}
        };
        paint.brush.antialiasing = false;
        document.layers.first().strokes.append(paint);

        QImage sourceMask(
            document.size,
            QImage::Format_Grayscale8);
        sourceMask.fill(0);
        for (int y = 40; y < 61; ++y) {
            std::fill(
                sourceMask.scanLine(y) + 5,
                sourceMask.scanLine(y) + 41,
                255);
        }
        QTransform shift;
        shift.translate(45.0, 0.0);
        const std::optional<PixelSelectionOp> operation =
            makePixelSelectionOp(
                sourceMask,
                shift,
                true,
                true);
        QVERIFY(operation.has_value());
        Stroke move;
        move.mode = StrokeMode::PixelSelection;
        move.pixelSelectionOp = *operation;
        document.layers.first().strokes.append(move);

        DocumentController controller;
        controller.loadDocument(document);
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.fitToWindow();

        const auto widgetPoint = [&canvas](const QPointF &documentPoint) {
            const QPointF center(canvas.rect().center());
            return (center
                    + (documentPoint - QPointF(50.0, 50.0))
                        * canvas.zoom())
                .toPoint();
        };
        const auto lasso = [&canvas, &widgetPoint](
                               const QRectF &documentRect) {
            const QPoint topLeft =
                widgetPoint(documentRect.topLeft());
            const QPoint topRight =
                widgetPoint(documentRect.topRight());
            const QPoint bottomRight =
                widgetPoint(documentRect.bottomRight());
            const QPoint bottomLeft =
                widgetPoint(documentRect.bottomLeft());
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
        };

        canvas.setTool(CanvasWidget::Tool::Lasso);
        QSignalSpy messages(
            &canvas,
            &CanvasWidget::interactionMessage);
        lasso(QRectF(10.0, 45.0, 20.0, 10.0));
        QTRY_VERIFY(canvas.hasSelection());
        QVERIFY(!canvas.hasTransformableSelection());
        QVERIFY(!messages.isEmpty());
        QCOMPARE(
            messages.last().first().toString(),
            QStringLiteral("No content in the selected area."));

        canvas.deselectSelection();
        lasso(QRectF(55.0, 45.0, 25.0, 10.0));
        QTRY_VERIFY(canvas.hasTransformableSelection());
        QCOMPARE(
            messages.last().first().toString(),
            QStringLiteral(
                "Selected content. Use the action bar to transform "
                "or remove it."));
    }

    void selectionAvailabilityChecksEveryAnimationFrame()
    {
        Document document = Document::createDefault(QSize(100, 100));
        document.background = Qt::transparent;
        document.animationFrames = 12;
        document.wobbleAmount =
            DocumentLimits::maximumWobbleAmount;
        Stroke animated;
        animated.seed = 0x7d1a2b3c4d5e6f70ULL;
        animated.color = QColor(35, 100, 225);
        animated.width = 2.0;
        animated.points = {
            {QPointF(35.0, 50.0), 1.0},
            {QPointF(65.0, 50.0), 1.0}
        };
        animated.brush.antialiasing = false;
        document.layers.first().strokes.append(animated);

        QVector<QImage> frames;
        frames.reserve(document.animationFrames);
        for (int frame = 0;
             frame < document.animationFrames;
             ++frame) {
            QImage layerImage;
            QVERIFY(RenderEngine::renderStrokesOnLayer(
                layerImage,
                document,
                document.layers.first().strokes,
                frame,
                document.size));
            frames.append(std::move(layerImage));
        }

        QPoint laterFrameOnlyPixel(-1, -1);
        const QImage &first = frames.first();
        for (int y = 4;
             y < first.height() - 4
             && laterFrameOnlyPixel.x() < 0;
             ++y) {
            for (int x = 4; x < first.width() - 4; ++x) {
                bool firstNeighborhoodIsTransparent = true;
                for (int offsetY = -3;
                     offsetY <= 3
                     && firstNeighborhoodIsTransparent;
                     ++offsetY) {
                    for (int offsetX = -3;
                         offsetX <= 3;
                         ++offsetX) {
                        if (first.pixelColor(
                                x + offsetX,
                                y + offsetY).alpha()
                            != 0) {
                            firstNeighborhoodIsTransparent = false;
                            break;
                        }
                    }
                }
                if (!firstNeighborhoodIsTransparent) {
                    continue;
                }
                const bool paintedLater = std::any_of(
                    frames.cbegin() + 1,
                    frames.cend(),
                    [x, y](const QImage &frame) {
                        return frame.pixelColor(x, y).alpha() != 0;
                    });
                if (paintedLater) {
                    laterFrameOnlyPixel = QPoint(x, y);
                    break;
                }
            }
        }
        QVERIFY(laterFrameOnlyPixel.x() >= 0);

        DocumentController controller;
        controller.loadDocument(document);
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.fitToWindow();
        QCOMPARE(canvas.currentFrame(), 0);

        const auto widgetPoint = [&canvas](const QPointF &documentPoint) {
            const QPointF center(canvas.rect().center());
            return (center
                    + (documentPoint - QPointF(50.0, 50.0))
                        * canvas.zoom())
                .toPoint();
        };
        const QPoint seed = widgetPoint(QPointF(
            laterFrameOnlyPixel.x() + 0.5,
            laterFrameOnlyPixel.y() + 0.5));
        canvas.setTool(CanvasWidget::Tool::Wand);
        QTest::mouseClick(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            seed);

        QTRY_VERIFY(canvas.hasTransformableSelection());
    }

    void packedSelectionSnapshotRoundTripsWithin4kUndoBudget()
    {
        constexpr int edge = 4096;
        QImage mask(
            QSize(edge, edge),
            QImage::Format_Grayscale8);
        QVERIFY(!mask.isNull());
        quint32 random = 0x6d2b79f5U;
        for (int y = 0; y < edge; ++y) {
            uchar *line = mask.scanLine(y);
            for (int x = 0; x < edge; ++x) {
                random ^= random << 13U;
                random ^= random >> 17U;
                random ^= random << 5U;
                line[x] = (random & 1U) != 0U ? 255 : 0;
            }
        }
        mask.scanLine(0)[0] = 255;
        mask.scanLine(edge - 1)[edge - 1] = 255;

        const std::optional<PackedMaskRegion> snapshot =
            packBinaryMask(mask);
        QVERIFY(snapshot.has_value());
        QCOMPARE(snapshot->bounds, QRect(QPoint(), mask.size()));
        QVERIFY(
            snapshot->packedMask.size()
            <= qsizetype(2 * 1024 * 1024));
        QCOMPARE(unpackBinaryMask(*snapshot), mask);

        DocumentController controller;
        controller.newDocument(mask.size());
        const QUuid layerId = controller.document().activeLayerId;
        QImage restored;
        QObject::connect(
            &controller,
            &DocumentController::selectionHistoryStateRequested,
            &controller,
            [&restored](const QUuid &, const QImage &state) {
                restored = state;
            });
        controller.pushSelectionStateCommand(
            QStringLiteral("4K selection snapshot"),
            {},
            {},
            layerId,
            mask);
        QCOMPARE(restored, mask);
        controller.undoStack()->undo();
        QVERIFY(restored.isNull());
        controller.undoStack()->redo();
        QCOMPARE(restored, mask);

        mask.fill(0);
        QVERIFY(!packBinaryMask(mask).has_value());
    }

    void failedCanvasResizeMacroPreservesSelectionAndHistory()
    {
        Document document = Document::createDefault(QSize(100, 100));
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;
        Stroke stroke;
        stroke.color = QColor(35, 95, 225);
        stroke.width = 12.0;
        stroke.points = {
            {QPointF(30.0, 50.0), 1.0},
            {QPointF(70.0, 50.0), 1.0}
        };
        stroke.brush.antialiasing = false;
        document.layers.first().strokes.append(stroke);

        DocumentController controller;
        controller.loadDocument(document);
        CanvasWidget canvas(&controller);
        canvas.setAnimating(false);

        QImage selection(document.size, QImage::Format_Grayscale8);
        selection.fill(0);
        for (int y = 35; y <= 65; ++y) {
            std::fill(
                selection.scanLine(y) + 20,
                selection.scanLine(y) + 81,
                255);
        }
        const QUuid layerId = controller.document().activeLayerId;
        controller.pushSelectionStateCommand(
            QStringLiteral("Select"),
            {},
            {},
            layerId,
            selection);
        QVERIFY(canvas.hasSelection());
        QVERIFY(canvas.hasTransformableSelection());

        const QByteArray beforeDocument =
            DocumentSerializer::toJson(controller.document());
        const int beforeCount = controller.undoStack()->count();
        const int beforeIndex = controller.undoStack()->index();

        controller.undoStack()->beginMacro(
            QStringLiteral("Resize canvas"));
        canvas.deselectSelection();
        // Selection changes are journaled while a macro is open; the UI is
        // updated only if the whole document transaction commits.
        QVERIFY(canvas.hasSelection());
        QVERIFY(!controller.resizeCanvas(
            controller.document().size,
            QPoint(
                static_cast<int>(
                    DocumentLimits::maximumStoredCoordinateMagnitude)
                    + 1,
                0)));
        controller.undoStack()->endMacro();

        QCOMPARE(
            DocumentSerializer::toJson(controller.document()),
            beforeDocument);
        QVERIFY(canvas.hasSelection());
        QVERIFY(canvas.hasTransformableSelection());
        QCOMPARE(controller.undoStack()->count(), beforeCount);
        QCOMPARE(controller.undoStack()->index(), beforeIndex);
    }

    void emptyLassoDoesNotCreateSelectionHistory()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.fitToWindow();

        const int undoCount = controller.undoStack()->count();
        const QPoint center = canvas.rect().center();
        canvas.setTool(CanvasWidget::Tool::Lasso);
        QTest::mousePress(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            center - QPoint(60, 0));
        QTest::mouseMove(&canvas, center, 5);
        QTest::mouseMove(
            &canvas,
            center + QPoint(60, 0),
            5);
        QTest::mouseRelease(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            center - QPoint(60, 0));

        QVERIFY(!canvas.hasSelection());
        QCOMPARE(controller.undoStack()->count(), undoCount);
    }

    void selectionMovePreviewMatchesCommittedPixelOperation()
    {
        Document document = Document::createDefault(QSize(100, 100));
        document.background = Qt::white;
        document.wobbleAmount = 0.0;

        Stroke destination;
        destination.color = QColor(220, 35, 35);
        destination.width = 20.0;
        destination.points = {
            {QPointF(60.0, 50.0), 1.0},
            {QPointF(90.0, 50.0), 1.0}
        };
        destination.brush.antialiasing = false;

        Stroke source;
        source.color = QColor(25, 90, 220);
        source.width = 20.0;
        source.points = {
            {QPointF(10.0, 50.0), 1.0},
            {QPointF(40.0, 50.0), 1.0}
        };
        source.brush.antialiasing = false;

        Stroke hole;
        hole.mode = StrokeMode::Erase;
        hole.width = 8.0;
        hole.points = {{QPointF(25.0, 50.0), 1.0}};
        hole.brush.antialiasing = false;
        document.layers.first().strokes = {
            destination,
            source,
            hole
        };

        DocumentController controller;
        controller.loadDocument(document);
        CanvasWidget canvas(&controller);
        canvas.resize(500, 500);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.fitToWindow();

        const auto widgetPoint = [&canvas](const QPointF &documentPoint) {
            const QPointF center(canvas.rect().center());
            return (center
                    + (documentPoint - QPointF(50.0, 50.0))
                        * canvas.zoom())
                .toPoint();
        };
        const auto lasso = [&canvas, &widgetPoint](
                               const QRectF &documentRect) {
            const QPoint topLeft =
                widgetPoint(documentRect.topLeft());
            const QPoint topRight =
                widgetPoint(documentRect.topRight());
            const QPoint bottomRight =
                widgetPoint(documentRect.bottomRight());
            const QPoint bottomLeft =
                widgetPoint(documentRect.bottomLeft());
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
        };
        const auto sample = [&canvas](
                                const QPixmap &pixmap,
                                const QPoint &widgetPosition) {
            const QImage image = pixmap.toImage();
            const qreal xScale =
                static_cast<qreal>(image.width()) / canvas.width();
            const qreal yScale =
                static_cast<qreal>(image.height()) / canvas.height();
            const int x = std::clamp(
                qRound(widgetPosition.x() * xScale),
                0,
                image.width() - 1);
            const int y = std::clamp(
                qRound(widgetPosition.y() * yScale),
                0,
                image.height() - 1);
            return image.pixelColor(x, y);
        };

        canvas.setTool(CanvasWidget::Tool::Lasso);
        lasso(QRectF(5.0, 38.0, 40.0, 24.0));
        QTRY_VERIFY(canvas.hasTransformableSelection());
        canvas.setSelectionMoveMode(true);
        const QByteArray beforeTransform =
            DocumentSerializer::toJson(controller.document());
        const int undoIndexBeforeTransform =
            controller.undoStack()->index();

        const QPoint dragStart = widgetPoint(QPointF(15.0, 50.0));
        const QPoint dragEnd = widgetPoint(QPointF(65.0, 50.0));
        QTest::mousePress(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            dragStart);
        QTest::mouseMove(&canvas, dragEnd, 5);
        QApplication::processEvents();
        const QPixmap preview = canvas.grab();

        QTest::mouseRelease(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            dragEnd);
        QApplication::processEvents();
        QVERIFY(canvas.hasPendingSelectionTransform());
        QCOMPARE(
            DocumentSerializer::toJson(controller.document()),
            beforeTransform);
        QCOMPARE(
            controller.undoStack()->index(),
            undoIndexBeforeTransform);
        QVERIFY(canvas.applySelectionTransform());
        QApplication::processEvents();
        const QPixmap committed = canvas.grab();
        QCOMPARE(
            controller.undoStack()->index(),
            undoIndexBeforeTransform + 1);

        const QPoint blueSample =
            widgetPoint(QPointF(65.0, 50.0));
        const QPoint holeSample =
            widgetPoint(QPointF(75.0, 50.0));
        QCOMPARE(
            sample(preview, blueSample),
            sample(committed, blueSample));
        QCOMPARE(
            sample(preview, holeSample),
            sample(committed, holeSample));
        QCOMPARE(
            sample(committed, holeSample),
            destination.color);

        controller.undoStack()->undo();
        QCOMPARE(
            DocumentSerializer::toJson(controller.document()),
            beforeTransform);
    }

    void sequentialSmoothSelectionTransformLeavesNoSourceFringe()
    {
        Document document = Document::createDefault(QSize(160, 100));
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;
        Stroke source;
        source.color = QColor(35, 95, 225);
        source.width = 20.0;
        source.points = {
            {QPointF(20.0, 50.0), 1.0},
            {QPointF(60.0, 50.0), 1.0}
        };
        source.brush.antialiasing = false;
        document.layers.first().strokes.append(source);

        DocumentController controller;
        controller.loadDocument(document);
        CanvasWidget canvas(&controller);
        canvas.resize(600, 400);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.fitToWindow();

        const auto widgetPoint = [&canvas](const QPointF &documentPoint) {
            const QPointF center(canvas.rect().center());
            return (center
                    + (documentPoint - QPointF(80.0, 50.0))
                        * canvas.zoom())
                .toPoint();
        };
        const QPoint topLeft =
            widgetPoint(QPointF(8.0, 35.0));
        const QPoint topRight =
            widgetPoint(QPointF(72.0, 35.0));
        const QPoint bottomRight =
            widgetPoint(QPointF(72.0, 65.0));
        const QPoint bottomLeft =
            widgetPoint(QPointF(8.0, 65.0));
        canvas.setTool(CanvasWidget::Tool::Lasso);
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
        QTRY_VERIFY(canvas.hasTransformableSelection());

        const QByteArray beforeTransform =
            DocumentSerializer::toJson(controller.document());
        const int undoCountBeforeTransform =
            controller.undoStack()->count();
        const int undoIndexBeforeTransform =
            controller.undoStack()->index();
        QVERIFY(canvas.rotateSelection(27.0));
        QTRY_VERIFY(canvas.hasTransformableSelection());
        QCOMPARE(
            DocumentSerializer::toJson(controller.document()),
            beforeTransform);
        QCOMPARE(
            controller.undoStack()->count(),
            undoCountBeforeTransform);

        canvas.setSelectionMoveMode(true);
        const QPoint dragStart =
            widgetPoint(QPointF(40.0, 50.0));
        const QPoint dragEnd =
            widgetPoint(QPointF(120.0, 50.0));
        QTest::mousePress(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            dragStart);
        QTest::mouseMove(&canvas, dragEnd, 5);
        QTest::mouseRelease(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            dragEnd);

        QVERIFY(canvas.hasPendingSelectionTransform());
        QCOMPARE(
            DocumentSerializer::toJson(controller.document()),
            beforeTransform);
        QCOMPARE(
            controller.undoStack()->index(),
            undoIndexBeforeTransform);
        QVERIFY(canvas.applySelectionTransform());
        QCOMPARE(
            controller.undoStack()->count(),
            undoCountBeforeTransform + 1);
        QCOMPARE(
            controller.undoStack()->index(),
            undoIndexBeforeTransform + 1);
        QCOMPARE(
            controller.document().layers.first().strokes.size(),
            2);
        QCOMPARE(
            controller.document().layers.first().strokes.last().mode,
            StrokeMode::PixelSelection);

        const QImage rendered =
            RenderEngine::render(controller.document(), 0);
        QVERIFY(!rendered.isNull());
        bool sourceFringeRemains = false;
        for (int y = 0;
             y < rendered.height() && !sourceFringeRemains;
             ++y) {
            const auto *line = reinterpret_cast<const QRgb *>(
                rendered.constScanLine(y));
            for (int x = 0; x < 80; ++x) {
                if (qAlpha(line[x]) != 0) {
                    sourceFringeRemains = true;
                    break;
                }
            }
        }
        QVERIFY(!sourceFringeRemains);
    }

    void clearsSelectionSafelyAcrossCanvasAndImageResizeUndo()
    {
        MainWindow window;
        window.resize(1000, 680);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QAction *lassoAction =
            window.findChild<QAction *>(QStringLiteral("lassoAction"));
        QAction *resizeCanvasAction = window.findChild<QAction *>(
            QStringLiteral("resizeCanvasAction"));
        QAction *resizeImageAction = window.findChild<QAction *>(
            QStringLiteral("resizeImageAction"));
        QAction *undoAction =
            window.findChild<QAction *>(QStringLiteral("undoAction"));
        QVERIFY(canvas);
        QVERIFY(lassoAction);
        QVERIFY(resizeCanvasAction);
        QVERIFY(resizeImageAction);
        QVERIFY(undoAction);

        const auto expectedFitZoom = [canvas](const QSize &size) {
            return std::clamp(
                std::min(
                    (canvas->width() - 64.0) / size.width(),
                    (canvas->height() - 64.0) / size.height()),
                0.01,
                16.0);
        };
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
        QTRY_VERIFY(canvas->hasTransformableSelection());

        bool canvasDialogHandled = false;
        QSize originalCanvasSize;
        QSize resizedCanvasSize;
        canvas->setZoomPercent(200);
        QTimer::singleShot(0, &window, [&]() {
            CanvasSizeDialog *dialog =
                window.findChild<CanvasSizeDialog *>();
            if (!dialog) {
                return;
            }
            QSpinBox *width = dialog->findChild<QSpinBox *>(
                QStringLiteral("canvasWidthSpin"));
            QSpinBox *height = dialog->findChild<QSpinBox *>(
                QStringLiteral("canvasHeightSpin"));
            if (!width || !height) {
                return;
            }
            originalCanvasSize =
                QSize(width->value(), height->value());
            width->setValue(width->value() + 24);
            resizedCanvasSize =
                QSize(width->value(), height->value());
            canvasDialogHandled = true;
            dialog->accept();
        });
        resizeCanvasAction->trigger();
        QVERIFY(canvasDialogHandled);
        QVERIFY(!canvas->hasSelection());
        QVERIFY(
            qAbs(canvas->zoom()
                 - expectedFitZoom(resizedCanvasSize))
            < 0.000001);
        undoAction->trigger();
        QTRY_VERIFY(canvas->hasTransformableSelection());
        QVERIFY(
            qAbs(canvas->zoom()
                 - expectedFitZoom(originalCanvasSize))
            < 0.000001);

        bool imageDialogHandled = false;
        QSize resizedImageSize;
        canvas->setZoomPercent(175);
        QTimer::singleShot(0, &window, [&]() {
            ImageSizeDialog *dialog =
                window.findChild<ImageSizeDialog *>();
            if (!dialog) {
                return;
            }
            QDoubleSpinBox *percentage =
                dialog->findChild<QDoubleSpinBox *>(
                    QStringLiteral("imageScalePercentSpin"));
            QSpinBox *width = dialog->findChild<QSpinBox *>(
                QStringLiteral("imageWidthSpin"));
            QSpinBox *height = dialog->findChild<QSpinBox *>(
                QStringLiteral("imageHeightSpin"));
            if (!percentage || !width || !height) {
                return;
            }
            percentage->setValue(110.0);
            resizedImageSize =
                QSize(width->value(), height->value());
            imageDialogHandled = true;
            dialog->accept();
        });
        resizeImageAction->trigger();
        QVERIFY(imageDialogHandled);
        QVERIFY(!canvas->hasSelection());
        QVERIFY(
            qAbs(canvas->zoom()
                 - expectedFitZoom(resizedImageSize))
            < 0.000001);
        undoAction->trigger();
        QTRY_VERIFY(canvas->hasTransformableSelection());
        QVERIFY(
            qAbs(canvas->zoom()
                 - expectedFitZoom(originalCanvasSize))
            < 0.000001);

        canvas->deselectSelection();
        QVERIFY(!canvas->hasSelection());
        lassoAction->trigger();
        QTest::mousePress(
            canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            topLeft);
        QTest::mouseMove(canvas, topRight, 5);
        QTest::mouseMove(canvas, bottomRight, 5);

        bool activeLassoDialogHandled = false;
        QSize lassoResizeSize;
        canvas->setZoomPercent(150);
        QTimer::singleShot(0, &window, [&]() {
            CanvasSizeDialog *dialog =
                window.findChild<CanvasSizeDialog *>();
            if (!dialog) {
                return;
            }
            QSpinBox *width = dialog->findChild<QSpinBox *>(
                QStringLiteral("canvasWidthSpin"));
            QSpinBox *height = dialog->findChild<QSpinBox *>(
                QStringLiteral("canvasHeightSpin"));
            if (!width || !height) {
                return;
            }
            width->setValue(width->value() + 1);
            lassoResizeSize =
                QSize(width->value(), height->value());
            activeLassoDialogHandled = true;
            dialog->accept();
        });
        resizeCanvasAction->trigger();
        QVERIFY(activeLassoDialogHandled);
        QTest::mouseRelease(
            canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            bottomRight);
        QVERIFY(!canvas->hasSelection());
        QVERIFY(
            qAbs(canvas->zoom()
                 - expectedFitZoom(lassoResizeSize))
            < 0.000001);
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

    void distinguishesActualZoomFromFit()
    {
        DocumentController controller;
        controller.newDocument(QSize(1000, 800));
        CanvasWidget canvas(&controller);
        canvas.resize(500, 400);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));

        canvas.setZoomPercent(250);
        QVERIFY(qAbs(canvas.zoom() - 2.5) < 0.0001);
        canvas.resetZoom();
        QVERIFY(qAbs(canvas.zoom() - 1.0) < 0.0001);

        canvas.fitToWindow();
        const qreal fittedZoom = canvas.zoom();
        QVERIFY(qAbs(fittedZoom - 1.0) > 0.0001);
        QVERIFY(qAbs(fittedZoom - 2.5) > 0.0001);

        canvas.resetZoom();
        QVERIFY(qAbs(canvas.zoom() - 1.0) < 0.0001);
    }

    void syncsMainWindowZoomControls()
    {
        MainWindow window;
        window.resize(1000, 680);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QSlider *zoomSlider = window.findChild<QSlider *>(
            QStringLiteral("zoomSlider"));
        QSpinBox *zoomSpin = window.findChild<QSpinBox *>(
            QStringLiteral("zoomPercentSpin"));
        QAction *actualSizeAction = window.findChild<QAction *>(
            QStringLiteral("actualSizeAction"));
        QVERIFY(canvas);
        QVERIFY(zoomSlider);
        QVERIFY(zoomSpin);
        QVERIFY(actualSizeAction);

        zoomSpin->setValue(250);
        QVERIFY(qAbs(canvas->zoom() - 2.5) < 0.0001);
        QCOMPARE(zoomSpin->value(), 250);
        const int sliderAt250 = zoomSlider->value();

        canvas->setZoomPercent(175);
        QCOMPARE(zoomSpin->value(), 175);
        QVERIFY(zoomSlider->value() != sliderAt250);

        int sliderTarget = zoomSlider->maximum() * 3 / 4;
        if (sliderTarget == zoomSlider->value()) {
            sliderTarget = zoomSlider->maximum() / 2;
        }
        zoomSlider->setValue(sliderTarget);
        QCOMPARE(zoomSpin->value(), qRound(canvas->zoom() * 100.0));
        QVERIFY(qAbs(canvas->zoom() - 1.75) > 0.0001);

        actualSizeAction->trigger();
        QCOMPARE(zoomSpin->value(), 100);
        QVERIFY(qAbs(canvas->zoom() - 1.0) < 0.0001);
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

    void picksBrushColorWithAltClick()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));

        canvas.setBrushColor(Qt::red);
        const QPoint center(200, 200);
        QTest::mousePress(
            &canvas,
            Qt::LeftButton,
            Qt::AltModifier,
            center);
        QTest::mouseRelease(
            &canvas,
            Qt::LeftButton,
            Qt::AltModifier,
            center);

        QCOMPARE(canvas.brushColor(), QColor(Qt::white));
        const Layer &layer = controller.document().layers.first();
        QVERIFY(layer.strokes.isEmpty());
    }

    void appliesBrushRoughnessToNewStrokes()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));

        canvas.setBrushRoughness(0.4);
        QCOMPARE(canvas.brushRoughness(), 0.4);

        const QPoint center(200, 200);
        QTest::mousePress(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            center);
        QTest::mouseMove(&canvas, center + QPoint(40, 0), 5);
        QTest::mouseRelease(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            center + QPoint(40, 0));

        const Layer &layer = controller.document().layers.first();
        QCOMPARE(layer.strokes.size(), 1);
        QCOMPARE(layer.strokes.first().brush.wobbleScale, 0.4);
    }

    void keepsBrushAndEraserSizesIndependent()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));

        BrushSizeRow brushSizeRow(
            &canvas,
            BrushSizeRow::Target::Brush,
            QStringLiteral("testBrush"));
        BrushSizeRow eraserSizeRow(
            &canvas,
            BrushSizeRow::Target::Eraser,
            QStringLiteral("testEraser"));
        QSpinBox *brushSizeSpin =
            brushSizeRow.findChild<QSpinBox *>(
                QStringLiteral("testBrushSpin"));
        QSpinBox *eraserSizeSpin =
            eraserSizeRow.findChild<QSpinBox *>(
                QStringLiteral("testEraserSpin"));
        QVERIFY(brushSizeSpin);
        QVERIFY(eraserSizeSpin);

        brushSizeSpin->setValue(17);
        QCOMPARE(canvas.brushWidth(), 17.0);
        QCOMPARE(eraserSizeSpin->value(), qRound(canvas.eraserWidth()));

        eraserSizeSpin->setValue(41);
        QCOMPARE(canvas.eraserWidth(), 41.0);
        QCOMPARE(brushSizeSpin->value(), 17);

        canvas.setBrushWidth(23.0);
        QCOMPARE(brushSizeSpin->value(), 23);
        QCOMPARE(eraserSizeSpin->value(), 41);
        canvas.setEraserWidth(57.0);
        QCOMPARE(eraserSizeSpin->value(), 57);
        QCOMPARE(brushSizeSpin->value(), 23);

        const QString originalPreset = canvas.brushPresetId();
        canvas.setBrushPreset(QStringLiteral("soft-airbrush"));
        QCOMPARE(canvas.eraserWidth(), 57.0);
        canvas.setBrushPreset(originalPreset);
        QCOMPARE(canvas.brushWidth(), 23.0);
        QCOMPARE(canvas.eraserWidth(), 57.0);

        const QPoint center = canvas.rect().center();
        canvas.setTool(CanvasWidget::Tool::Brush);
        QTest::mouseClick(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            center - QPoint(40, 0));
        canvas.setTool(CanvasWidget::Tool::Eraser);
        QTest::mouseClick(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            center + QPoint(40, 0));

        canvas.setTool(CanvasWidget::Tool::Lasso);
        QPointingDevice eraserStylus(
            QStringLiteral("Test eraser stylus"),
            2,
            QInputDevice::DeviceType::Stylus,
            QPointingDevice::PointerType::Eraser,
            QInputDevice::Capability::Position
                | QInputDevice::Capability::Pressure,
            1,
            1);
        const QPointF tabletPosition =
            QPointF(center) + QPointF(0.0, 40.0);
        const QPointF globalTabletPosition =
            canvas.mapToGlobal(tabletPosition.toPoint());
        QTabletEvent tabletHover(
            QEvent::TabletMove,
            &eraserStylus,
            tabletPosition,
            globalTabletPosition,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            Qt::NoModifier,
            Qt::NoButton,
            Qt::NoButton);
        QApplication::sendEvent(&canvas, &tabletHover);
        QCOMPARE(canvas.cursor().shape(), Qt::BlankCursor);
        QTest::mouseMove(&canvas, center + QPoint(10, 40));
        QCOMPARE(canvas.cursor().shape(), Qt::CrossCursor);
        QTabletEvent tabletPress(
            QEvent::TabletPress,
            &eraserStylus,
            tabletPosition,
            globalTabletPosition,
            0.8,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            Qt::NoModifier,
            Qt::LeftButton,
            Qt::LeftButton);
        QApplication::sendEvent(&canvas, &tabletPress);
        QTabletEvent tabletRelease(
            QEvent::TabletRelease,
            &eraserStylus,
            tabletPosition,
            globalTabletPosition,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            Qt::NoModifier,
            Qt::LeftButton,
            Qt::NoButton);
        QApplication::sendEvent(&canvas, &tabletRelease);

        const QVector<Stroke> &strokes =
            controller.document().layers.first().strokes;
        QCOMPARE(strokes.size(), 3);
        QVERIFY(strokes.at(0).mode == StrokeMode::Paint);
        QCOMPARE(strokes.at(0).width, 23.0);
        QVERIFY(strokes.at(1).mode == StrokeMode::Erase);
        QCOMPARE(strokes.at(1).width, 57.0);
        QVERIFY(strokes.at(2).mode == StrokeMode::Erase);
        QCOMPARE(strokes.at(2).width, 57.0);
    }

    void persistsDrawingToolSettings()
    {
        const QColor rememberedColor(18, 52, 86, 120);
        {
            MainWindow window;
            CanvasWidget *canvas = window.findChild<CanvasWidget *>();
            QVERIFY(canvas);

            canvas->setBrushPreset(QStringLiteral("monoline"));
            canvas->setBrushWidth(23.0);
            canvas->setBrushPreset(QStringLiteral("soft-airbrush"));
            canvas->setBrushWidth(47.0);
            canvas->setEraserWidth(57.0);
            canvas->setBrushRoughness(0.37);
            canvas->setBrushAntialiasing(true);
            canvas->setBrushColor(rememberedColor);
            canvas->setTool(CanvasWidget::Tool::Eraser);

            QVERIFY(window.close());
            QCOMPARE(
                QSettings()
                    .value(QStringLiteral(
                        "drawingTools/brush/presetId"))
                    .toString(),
                QStringLiteral("soft-airbrush"));
        }

        QSettings persistedSettings;
        persistedSettings.sync();
        QCOMPARE(
            persistedSettings.status(),
            QSettings::NoError);
        MainWindow restoredWindow;
        CanvasWidget *restored =
            restoredWindow.findChild<CanvasWidget *>();
        QVERIFY(restored);
        QCOMPARE(
            restored->brushPresetId(),
            QStringLiteral("soft-airbrush"));
        QCOMPARE(restored->brushWidth(), 47.0);
        QCOMPARE(restored->eraserWidth(), 57.0);
        QCOMPARE(restored->brushRoughness(), 0.37);
        QVERIFY(restored->brushAntialiasing());
        QCOMPARE(restored->brushColor(), rememberedColor);
        QCOMPARE(restored->tool(), CanvasWidget::Tool::Eraser);

        QAction *eraserAction = restoredWindow.findChild<QAction *>(
            QStringLiteral("eraserAction"));
        QSpinBox *brushSizeSpin =
            restoredWindow.findChild<QSpinBox *>(
                QStringLiteral("brushSizeSpin"));
        QSpinBox *eraserSizeSpin =
            restoredWindow.findChild<QSpinBox *>(
                QStringLiteral("eraserSizeSpin"));
        QSpinBox *roughnessSpin =
            restoredWindow.findChild<QSpinBox *>(
                QStringLiteral("brushRoughnessSpin"));
        QCheckBox *antialiasingToggle =
            restoredWindow.findChild<QCheckBox *>(
                QStringLiteral("brushAntialiasingToggle"));
        QVERIFY(eraserAction);
        QVERIFY(brushSizeSpin);
        QVERIFY(eraserSizeSpin);
        QVERIFY(roughnessSpin);
        QVERIFY(antialiasingToggle);
        QVERIFY(eraserAction->isChecked());
        QCOMPARE(brushSizeSpin->value(), 47);
        QCOMPARE(eraserSizeSpin->value(), 57);
        QCOMPARE(roughnessSpin->value(), 37);
        QVERIFY(antialiasingToggle->isChecked());

        restored->setBrushPreset(QStringLiteral("monoline"));
        QCOMPARE(restored->brushWidth(), 23.0);
        QCOMPARE(brushSizeSpin->value(), 23);
        restored->setBrushPreset(QStringLiteral("soft-airbrush"));
        QCOMPARE(restored->brushWidth(), 47.0);
        QCOMPARE(brushSizeSpin->value(), 47);

        BrushPresetButton *selectedPresetButton = nullptr;
        for (BrushPresetButton *button :
             restoredWindow.findChildren<BrushPresetButton *>()) {
            if (button->presetId() == QStringLiteral("soft-airbrush")) {
                selectedPresetButton = button;
                break;
            }
        }
        QVERIFY(selectedPresetButton);
        QVERIFY(selectedPresetButton->isChecked());
    }

    void migratesActiveColorFromRecentColors()
    {
        const QColor recentColor(42, 91, 137, 173);
        QSettings settings;
        settings.setValue(
            QStringLiteral("brush/recentColors"),
            QStringList {
                QStringLiteral("not-a-color"),
                recentColor.name(QColor::HexArgb)
            });
        settings.sync();

        MainWindow window;
        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QVERIFY(canvas);
        QCOMPARE(canvas->brushColor(), recentColor);
        QVERIFY(window.close());
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);
        QCOMPARE(
            settings
                .value(QStringLiteral("drawingTools/brush/color"))
                .toString(),
            recentColor.name(QColor::HexArgb));
    }

    void sanitizesInvalidDrawingToolSettings()
    {
        QSettings settings;
        settings.setValue(
            QStringLiteral("drawingTools/activeTool"),
            QStringLiteral("transform"));
        settings.setValue(
            QStringLiteral("drawingTools/brush/presetId"),
            QStringLiteral("missing-brush"));
        settings.setValue(
            QStringLiteral("drawingTools/brush/color"),
            QStringLiteral("not-a-color"));
        settings.setValue(
            QStringLiteral("drawingTools/brush/roughness"),
            std::numeric_limits<double>::infinity());
        settings.setValue(
            QStringLiteral("drawingTools/brush/antialiasing"),
            QStringLiteral("sometimes"));
        settings.setValue(
            QStringLiteral(
                "drawingTools/brush/presetWidths/ink-pen"),
            9999.0);
        settings.setValue(
            QStringLiteral(
                "drawingTools/brush/presetWidths/g-pen"),
            std::numeric_limits<double>::quiet_NaN());
        settings.setValue(
            QStringLiteral("drawingTools/eraser/width"),
            -100.0);
        settings.sync();

        MainWindow window;
        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QVERIFY(canvas);
        QCOMPARE(canvas->tool(), CanvasWidget::Tool::Brush);
        QCOMPARE(
            canvas->brushPresetId(),
            BrushPresetCatalog::defaultPreset().id);
        QCOMPARE(
            canvas->brushWidth(),
            DocumentLimits::maximumStrokeWidth);
        QCOMPARE(canvas->eraserWidth(), 1.0);
        QCOMPARE(canvas->brushRoughness(), 1.0);
        QVERIFY(!canvas->brushAntialiasing());
        QCOMPARE(canvas->brushColor(), QColor(Qt::black));

        canvas->setBrushPreset(QStringLiteral("g-pen"));
        QCOMPARE(
            canvas->brushWidth(),
            BrushPresetCatalog::find(
                QStringLiteral("g-pen"))->defaultSize);

        const qreal brushWidth = canvas->brushWidth();
        const qreal eraserWidth = canvas->eraserWidth();
        const qreal roughness = canvas->brushRoughness();
        canvas->setBrushWidth(
            std::numeric_limits<qreal>::quiet_NaN());
        canvas->setEraserWidth(
            std::numeric_limits<qreal>::infinity());
        canvas->setBrushRoughness(
            std::numeric_limits<qreal>::quiet_NaN());
        QCOMPARE(canvas->brushWidth(), brushWidth);
        QCOMPARE(canvas->eraserWidth(), eraserWidth);
        QCOMPARE(canvas->brushRoughness(), roughness);
    }

    void deletesTheLastLayerAndAddsOneBack()
    {
        MainWindow window;
        window.resize(1000, 680);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QListWidget *layerList = window.findChild<QListWidget *>();
        QToolButton *addButton = window.findChild<QToolButton *>(
            QStringLiteral("layerAddButton"));
        QToolButton *deleteButton = window.findChild<QToolButton *>(
            QStringLiteral("layerDeleteButton"));
        QAction *undoAction = window.findChild<QAction *>(
            QStringLiteral("undoAction"));
        QAction *redoAction = window.findChild<QAction *>(
            QStringLiteral("redoAction"));
        QAction *clearLayerAction = window.findChild<QAction *>(
            QStringLiteral("clearLayerAction"));
        QVERIFY(canvas);
        QVERIFY(layerList);
        QVERIFY(addButton);
        QVERIFY(deleteButton);
        QVERIFY(undoAction);
        QVERIFY(redoAction);
        QVERIFY(clearLayerAction);
        QCOMPARE(layerList->count(), 1);
        QVERIFY(deleteButton->isEnabled());
        QVERIFY(clearLayerAction->isEnabled());

        QTest::mouseClick(deleteButton, Qt::LeftButton);
        QTRY_COMPARE(layerList->count(), 0);
        QVERIFY(!deleteButton->isEnabled());
        QVERIFY(addButton->isEnabled());
        QVERIFY(!clearLayerAction->isEnabled());
        QVERIFY(window.isWindowModified());

        QSignalSpy interactionMessages(
            canvas,
            &CanvasWidget::interactionMessage);
        QTest::mouseClick(
            canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            canvas->rect().center());
        QCOMPARE(interactionMessages.size(), 1);
        QCOMPARE(
            interactionMessages.first().first().toString(),
            QStringLiteral("Add a layer before using this tool."));

        undoAction->trigger();
        QTRY_COMPARE(layerList->count(), 1);
        QVERIFY(deleteButton->isEnabled());
        QVERIFY(clearLayerAction->isEnabled());

        redoAction->trigger();
        QTRY_COMPARE(layerList->count(), 0);
        QVERIFY(addButton->isEnabled());
        QVERIFY(!clearLayerAction->isEnabled());

        QTest::mouseClick(addButton, Qt::LeftButton);
        QTRY_COMPARE(layerList->count(), 1);
        QVERIFY(deleteButton->isEnabled());
        QVERIFY(clearLayerAction->isEnabled());

        undoAction->trigger();
        QTRY_COMPARE(layerList->count(), 0);
        QVERIFY(addButton->isEnabled());
        QVERIFY(!clearLayerAction->isEnabled());
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
        canvas.fitToWindow();

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
        canvas.fitToWindow();

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

    void deactivationCancelsMouseAndTabletInput()
    {
        MainWindow window;
        window.resize(1000, 680);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *windowCanvas =
            window.findChild<CanvasWidget *>();
        QAction *undoAction =
            window.findChild<QAction *>(QStringLiteral("undoAction"));
        QVERIFY(windowCanvas);
        QVERIFY(undoAction);

        const QPoint windowCenter = windowCanvas->rect().center();
        QTest::mousePress(
            windowCanvas,
            Qt::LeftButton,
            Qt::NoModifier,
            windowCenter - QPoint(30, 0));
        QTest::mouseMove(
            windowCanvas,
            windowCenter + QPoint(30, 0),
            5);
        QEvent deactivate(QEvent::ApplicationDeactivate);
        QApplication::sendEvent(qApp, &deactivate);
        QTest::mouseRelease(
            windowCanvas,
            Qt::LeftButton,
            Qt::NoModifier,
            windowCenter + QPoint(30, 0));
        QVERIFY(!undoAction->isEnabled());
        QVERIFY(!window.isWindowModified());

        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));

        QPointingDevice stylus(
            QStringLiteral("Test stylus"),
            1,
            QInputDevice::DeviceType::Stylus,
            QPointingDevice::PointerType::Pen,
            QInputDevice::Capability::Position
                | QInputDevice::Capability::Pressure,
            1,
            1);
        const QPointF center = canvas.rect().center();
        const QPointF globalCenter =
            canvas.mapToGlobal(center.toPoint());
        QTabletEvent tabletPress(
            QEvent::TabletPress,
            &stylus,
            center,
            globalCenter,
            0.7,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            Qt::NoModifier,
            Qt::LeftButton,
            Qt::LeftButton);
        QApplication::sendEvent(&canvas, &tabletPress);

        QFocusEvent focusOut(
            QEvent::FocusOut,
            Qt::ActiveWindowFocusReason);
        QApplication::sendEvent(&canvas, &focusOut);

        QTabletEvent tabletRelease(
            QEvent::TabletRelease,
            &stylus,
            center,
            globalCenter,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            Qt::NoModifier,
            Qt::LeftButton,
            Qt::NoButton);
        QApplication::sendEvent(&canvas, &tabletRelease);
        QVERIFY(controller.document().layers.first().strokes.isEmpty());

        QTest::mouseClick(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            center.toPoint());
        QCOMPARE(
            controller.document().layers.first().strokes.size(),
            1);

        canvas.setPanModifierActive(true);
        QTest::mousePress(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            center.toPoint());
        QCOMPARE(canvas.cursor().shape(), Qt::ClosedHandCursor);
        QEvent ungrabMouse(QEvent::UngrabMouse);
        QApplication::sendEvent(&canvas, &ungrabMouse);
        QCOMPARE(canvas.cursor().shape(), Qt::BlankCursor);
        QTest::mouseRelease(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            center.toPoint());
        QCOMPARE(
            controller.document().layers.first().strokes.size(),
            1);
    }

    void deactivationRestoresLassoAndCancelsSelectionMove()
    {
        Document document = Document::createDefault(QSize(100, 100));
        document.wobbleAmount = 0.0;
        Stroke stroke;
        stroke.width = 8.0;
        stroke.points = {
            {QPointF(20.0, 50.0), 1.0},
            {QPointF(80.0, 50.0), 1.0}
        };
        document.layers.first().strokes = {stroke};

        DocumentController controller;
        controller.loadDocument(document);
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.setAnimating(false);
        canvas.setTool(CanvasWidget::Tool::Lasso);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));

        const QPoint center = canvas.rect().center();
        const QPoint topLeft = center - QPoint(120, 80);
        const QPoint topRight = center + QPoint(120, -80);
        const QPoint bottomRight = center + QPoint(120, 80);
        const QPoint bottomLeft = center + QPoint(-120, 80);
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
        QVERIFY(canvas.hasTransformableSelection());

        const QByteArray documentBeforeMove =
            DocumentSerializer::toJson(controller.document());
        QTest::mousePress(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            center);
        QTest::mouseMove(&canvas, center + QPoint(50, 20), 5);
        QFocusEvent moveFocusOut(
            QEvent::FocusOut,
            Qt::ActiveWindowFocusReason);
        QApplication::sendEvent(&canvas, &moveFocusOut);
        QTest::mouseRelease(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            center + QPoint(50, 20));
        QCOMPARE(
            DocumentSerializer::toJson(controller.document()),
            documentBeforeMove);
        QVERIFY(canvas.hasSelection());
        QVERIFY(canvas.hasTransformableSelection());

        const QPoint outsideSelection = center + QPoint(140, 140);
        QTest::mousePress(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            outsideSelection);
        QTest::mouseMove(
            &canvas,
            outsideSelection - QPoint(30, 0),
            5);
        QFocusEvent lassoFocusOut(
            QEvent::FocusOut,
            Qt::ActiveWindowFocusReason);
        QApplication::sendEvent(&canvas, &lassoFocusOut);
        QTest::mouseRelease(
            &canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            outsideSelection - QPoint(30, 0));
        QCOMPARE(
            DocumentSerializer::toJson(controller.document()),
            documentBeforeMove);
        QVERIFY(canvas.hasSelection());
        QVERIFY(canvas.hasTransformableSelection());
    }

    void spacePanIsLimitedToCanvas()
    {
        MainWindow window;
        window.resize(1000, 680);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QListWidget *layerList = window.findChild<QListWidget *>();
        QToolButton *addButton = window.findChild<QToolButton *>(
            QStringLiteral("layerAddButton"));
        QVERIFY(canvas);
        QVERIFY(layerList);
        QVERIFY(addButton);

        canvas->setFocus(Qt::OtherFocusReason);
        QTest::keyPress(canvas, Qt::Key_Space);
        QCOMPARE(canvas->cursor().shape(), Qt::OpenHandCursor);
        QTest::keyRelease(canvas, Qt::Key_Space);
        QCOMPARE(canvas->cursor().shape(), Qt::BlankCursor);

        const int layerCount = layerList->count();
        addButton->setFocus(Qt::OtherFocusReason);
        QTest::keyClick(addButton, Qt::Key_Space);
        QTRY_COMPARE(layerList->count(), layerCount + 1);
        QCOMPARE(canvas->cursor().shape(), Qt::BlankCursor);

        layerList->setCurrentRow(0);
        layerList->clearSelection();
        QVERIFY(layerList->selectedItems().isEmpty());
        layerList->setFocus(Qt::OtherFocusReason);
        QTest::keyClick(layerList, Qt::Key_Space);
        QCOMPARE(layerList->selectedItems().size(), 1);
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

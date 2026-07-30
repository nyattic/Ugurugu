#include "brush/BrushPreset.hpp"
#include "ui/CanvasWidget.hpp"
#include "ui/LayerDock.hpp"
#include "ui/MainWindow.hpp"
#include "ui/SettingsDialog.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
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
        QComboBox *brushPresetCombo =
            window.findChild<QComboBox *>(
                QStringLiteral("brushPresetCombo"));
        QSpinBox *brushSizeSpin =
            window.findChild<QSpinBox *>(
                QStringLiteral("brushSizeSpin"));
        QVERIFY(brushPresetCombo);
        QVERIFY(brushSizeSpin);
        QCOMPARE(
            brushPresetCombo->count(),
            BrushPresetCatalog::builtIns().size());
        const int softAirbrushIndex =
            brushPresetCombo->findData(QStringLiteral("soft-airbrush"));
        QVERIFY(softAirbrushIndex >= 0);
        brushPresetCombo->setCurrentIndex(softAirbrushIndex);
        QCOMPARE(canvas->brushPresetId(), QStringLiteral("soft-airbrush"));
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

#include "ui/CanvasWidget.hpp"
#include "ui/LayerDock.hpp"
#include "ui/MainWindow.hpp"

#include <QAction>
#include <QApplication>
#include <QFileInfo>
#include <QLineEdit>
#include <QListWidget>
#include <QSpinBox>
#include <QToolButton>
#include <QtTest>

namespace wobble {

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

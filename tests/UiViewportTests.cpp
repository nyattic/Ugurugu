#include "support/UiTestHelpers.hpp"
#include "support/UiTestSuites.hpp"

namespace wobble
{

class UiViewportTests final : public QObject
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

    void mirrorsTheCanvasAsAViewOnlyToggle()
    {
        MainWindow window;
        window.resize(1000, 680);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QAction *mirrorAction =
            window.findChild<QAction *>(QStringLiteral("mirrorCanvasAction"));
        QToolButton *mirrorButton = window.findChild<QToolButton *>(
            QStringLiteral("mirrorCanvasButton"));
        QVERIFY(canvas);
        QVERIFY(mirrorAction);
        QVERIFY(mirrorButton);
        QVERIFY(mirrorAction->isCheckable());
        QCOMPARE(mirrorAction->shortcut(), QKeySequence(QStringLiteral("M")));
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
        QAction *zoomInAction =
            window.findChild<QAction *>(QStringLiteral("zoomInAction"));
        QAction *zoomOutAction =
            window.findChild<QAction *>(QStringLiteral("zoomOutAction"));
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

    void defersPreviewRerenderUntilZoomInputIsIdle()
    {
        DocumentController controller;
        QVERIFY(controller.newDocument(QSize(4096, 4096)));
        CanvasWidget canvas(&controller);
        canvas.resize(800, 600);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));

        canvas.setZoomPercent(25);
        QTRY_VERIFY(!CanvasWidgetTestAccess::zoomRenderPending(canvas));
        canvas.grab();
        const QSize initialRenderSize =
            CanvasWidgetTestAccess::cachedRenderSize(canvas);
        QVERIFY(initialRenderSize.isValid());

        canvas.setZoomPercent(50);
        canvas.setZoomPercent(100);
        QVERIFY(CanvasWidgetTestAccess::zoomRenderPending(canvas));
        QCOMPARE(CanvasWidgetTestAccess::previewRenderSize(canvas),
            initialRenderSize);

        QTRY_VERIFY(!CanvasWidgetTestAccess::zoomRenderPending(canvas));
        QTRY_VERIFY(CanvasWidgetTestAccess::previewRenderSize(canvas)
                    != initialRenderSize);
    }

    void keepsRegionalStrokePreviewFreeOfSeams()
    {
        DocumentController controller;
        QVERIFY(controller.newDocument(QSize(256, 256)));
        const QUuid layerId = controller.document().activeLayerId;

        Stroke background;
        background.color = QColor(255, 0, 0);
        background.width = 512.0;
        background.points = {{QPointF(0.0, 0.0), 1.0},
            {QPointF(128.0, 128.0), 1.0},
            {QPointF(256.0, 256.0), 1.0}};
        QCOMPARE(controller.addStroke(layerId, background),
            DocumentController::AddStrokeResult::Added);

        CanvasWidget canvas(&controller);
        canvas.resize(300, 300);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.setAnimating(false);
        canvas.fitToWindow();
        canvas.setBrushColor(QColor(0, 0, 255));
        canvas.setBrushWidth(8.0);

        const QPoint center = canvas.rect().center();
        QTest::mousePress(
            &canvas, Qt::LeftButton, Qt::NoModifier, center - QPoint(30, 0));
        for (int step = -20; step <= 30; step += 10)
        {
            QTest::mouseMove(&canvas, center + QPoint(step, step / 3), 5);
        }
        const QPoint pointerPosition = center + QPoint(30, 10);

        int seamPixels = 0;
        for (const int zoomPercent : {87, 93, 101, 113})
        {
            canvas.setZoomPercent(zoomPercent);
            const QImage frame = canvas.grab().toImage();

            QRect paintedBounds;
            for (int y = 0; y < frame.height(); ++y)
            {
                for (int x = 0; x < frame.width(); ++x)
                {
                    const QColor color = frame.pixelColor(x, y);
                    if (color.red() > 150 && color.green() < 80
                        && color.blue() < 80)
                    {
                        paintedBounds |= QRect(x, y, 1, 1);
                    }
                }
            }
            QVERIFY(paintedBounds.width() > 100);
            QVERIFY(paintedBounds.height() > 100);

            const QRect interior = paintedBounds.adjusted(4, 4, -4, -4);
            for (int y = interior.top(); y <= interior.bottom(); ++y)
            {
                for (int x = interior.left(); x <= interior.right(); ++x)
                {
                    if ((QPoint(x, y) - pointerPosition).manhattanLength() < 40)
                    {
                        continue;
                    }
                    const QColor color = frame.pixelColor(x, y);
                    if (color.red() > 90 && color.green() > 90
                        && color.blue() > 90)
                    {
                        ++seamPixels;
                    }
                }
            }
        }
        QTest::mouseRelease(
            &canvas, Qt::LeftButton, Qt::NoModifier, pointerPosition);
        QCOMPARE(seamPixels, 0);
    }

    void promotesCompletedStrokePreviewIntoFrameCache()
    {
        DocumentController controller;
        QVERIFY(controller.newDocument(QSize(4096, 4096)));
        CanvasWidget canvas(&controller);
        canvas.resize(800, 600);
        canvas.setAnimating(false);
        canvas.setZoomPercent(25);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        QTRY_VERIFY(!CanvasWidgetTestAccess::zoomRenderPending(canvas));

        const QPoint center = canvas.rect().center();
        QTest::mousePress(
            &canvas, Qt::LeftButton, Qt::NoModifier, center - QPoint(80, 0));
        for (int step = -60; step <= 80; step += 20)
        {
            QTest::mouseMove(&canvas, center + QPoint(step, step / 4), 1);
        }

        QElapsedTimer timer;
        timer.start();
        QTest::mouseRelease(
            &canvas, Qt::LeftButton, Qt::NoModifier, center + QPoint(80, 20));
        QApplication::processEvents();
        const qreal penUpMilliseconds = timer.nsecsElapsed() / 1000000.0;

        QCOMPARE(controller.document().layers.first().strokes.size(), 1);
        QVERIFY(CanvasWidgetTestAccess::hasCachedFrame(
            canvas, canvas.currentFrame()));
        qInfo().nospace() << "4K pen-up cache promotion and next paint took "
                          << penUpMilliseconds << " ms";
    }

    void fitsCanvasToViewportOnFirstShow()
    {
        MainWindow window;
        QApplication::processEvents();

        window.resize(1280, 820);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QVERIFY(canvas);
        QVERIFY(canvas->width() > 200);
        QVERIFY(canvas->height() > 200);
        const QSize documentSize =
            MainWindowTestAccess::controller(window).document().size;
        const qreal expectedZoom =
            std::clamp(std::min((canvas->width() - 64.0) / documentSize.width(),
                           (canvas->height() - 64.0) / documentSize.height()),
                0.01,
                16.0);
        QTRY_VERIFY(qAbs(canvas->zoom() - expectedZoom) < 0.0001);
    }

    void syncsMainWindowZoomControls()
    {
        MainWindow window;
        window.resize(1000, 680);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QSlider *zoomSlider =
            window.findChild<QSlider *>(QStringLiteral("zoomSlider"));
        QSpinBox *zoomSpin =
            window.findChild<QSpinBox *>(QStringLiteral("zoomPercentSpin"));
        QAction *actualSizeAction =
            window.findChild<QAction *>(QStringLiteral("actualSizeAction"));
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
        if (sliderTarget == zoomSlider->value())
        {
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

    void pausesAnimationWhilePanning()
    {
        Document document = Document::createDefault(QSize(128, 128));
        document.framesPerSecond = 50.0;
        document.animationFrames = 30;
        DocumentController controller;
        controller.loadDocument(document);
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));

        const QPoint center = canvas.rect().center();
        QTest::mousePress(&canvas, Qt::MiddleButton, Qt::NoModifier, center);
        const int heldFrame = canvas.currentFrame();
        QTest::qWait(100);
        QCOMPARE(canvas.currentFrame(), heldFrame);

        QSignalSpy frameChanges(&canvas, &CanvasWidget::currentFrameChanged);
        QTest::mouseRelease(&canvas, Qt::MiddleButton, Qt::NoModifier, center);
        QTRY_VERIFY_WITH_TIMEOUT(!frameChanges.isEmpty(), 1000);
    }

    void repaintsOnlyExposedStripsWhilePanning()
    {
        DocumentController controller;
        QVERIFY(controller.newDocument(QSize(4096, 4096)));
        CanvasWidget canvas(&controller);
        canvas.resize(800, 600);
        canvas.setAnimating(false);
        canvas.setZoomPercent(100);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        QTRY_VERIFY(!CanvasWidgetTestAccess::zoomRenderPending(canvas));

        PaintRegionTracker tracker;
        canvas.installEventFilter(&tracker);
        const QPoint center = canvas.rect().center();
        QTest::mousePress(&canvas, Qt::MiddleButton, Qt::NoModifier, center);
        QApplication::processEvents();
        tracker.reset();

        QTest::mouseMove(&canvas, center + QPoint(12, 7), 5);
        QTRY_VERIFY(tracker.eventCount() > 0);
        QVERIFY(tracker.largestArea()
                < static_cast<qint64>(canvas.width()) * canvas.height() / 3);

        QTest::mouseRelease(
            &canvas, Qt::MiddleButton, Qt::NoModifier, center + QPoint(12, 7));
        canvas.removeEventFilter(&tracker);
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
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::ControlModifier, start);
        QTest::mouseMove(&canvas, start + QPoint(120, 0), 5);
        QTest::mouseRelease(&canvas,
            Qt::LeftButton,
            Qt::ControlModifier,
            start + QPoint(120, 0));
        canvas.setPanModifierActive(false);
        QVERIFY(canvas.zoom() > initialZoom);

        const Layer &layer = controller.document().layers.first();
        QVERIFY(layer.strokes.isEmpty());
    }
};

int runUiViewportTests(int argc, char **argv)
{
    UiViewportTests tests;
    return QTest::qExec(&tests, argc, argv);
}

}

#include "UiViewportTests.moc"

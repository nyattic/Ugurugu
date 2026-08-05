#include "support/UiTestHelpers.hpp"
#include "support/UiTestSuites.hpp"

#include <cmath>

namespace ugurugu
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
        settings.remove(QStringLiteral("brush/colorHistory"));
        settings.remove(QStringLiteral("canvas/strokeStabilization"));
        settings.sync();
    }

    void cleanup()
    {
        QSettings settings;
        settings.remove(QStringLiteral("drawingTools"));
        settings.remove(QStringLiteral("brush/recentColors"));
        settings.remove(QStringLiteral("brush/colorHistory"));
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

    void disablesDocumentAndLayerWobbleForTheCanvasView()
    {
        DocumentController controller;
        controller.newDocument(QSize(96, 96));
        const QUuid layerId = controller.document().activeLayerId;
        MotionSettings motion = controller.document().motion;
        controller.setLayerWobbleOverride(layerId, 5.0, motion);
        CanvasWidget canvas(&controller);

        canvas.setWobbleAnimationEnabled(false);
        const Document display =
            CanvasWidgetTestAccess::displayDocument(canvas);
        const Layer *displayLayer = display.layer(layerId);
        QVERIFY(displayLayer);
        QCOMPARE(display.wobbleAmount, 0.0);
        QCOMPARE(effectiveWobbleAmount(display, *displayLayer), 0.0);
    }

    void keepsAnimatingWhileThePenIsDownWhenEnabled()
    {
        DocumentController controller;
        QVERIFY(controller.newDocument(QSize(128, 128)));
        CanvasWidget canvas(&controller);
        canvas.resize(320, 320);
        canvas.setAnimateWhileDrawing(true);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.fitToWindow();
        QTRY_VERIFY_WITH_TIMEOUT(
            !CanvasWidgetTestAccess::frameCacheWarmupActive(canvas), 5000);

        const QPoint center = canvas.rect().center();
        QTest::mousePress(
            &canvas, Qt::LeftButton, Qt::NoModifier, center - QPoint(30, 0));
        const int frameWhilePressed = canvas.currentFrame();
        QTRY_VERIFY_WITH_TIMEOUT(
            canvas.currentFrame() != frameWhilePressed, 2000);
        QTest::mouseRelease(
            &canvas, Qt::LeftButton, Qt::NoModifier, center + QPoint(30, 0));
    }

    void resumesPlaybackAfterChangingMotionLinkage()
    {
        DocumentController controller;
        QVERIFY(controller.newDocument(QSize(128, 128)));
        const QUuid layerId = controller.document().activeLayerId;
        Stroke stroke;
        stroke.points = {
            {QPointF(16.0, 64.0), 1.0}, {QPointF(112.0, 64.0), 1.0}};
        QCOMPARE(controller.addStroke(layerId, stroke),
            DocumentController::AddStrokeResult::Added);

        CanvasWidget canvas(&controller);
        canvas.resize(320, 320);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.fitToWindow();
        QTRY_VERIFY_WITH_TIMEOUT(
            !CanvasWidgetTestAccess::frameCacheWarmupActive(canvas), 5000);

        controller.setMotionLinked(0.35);
        const int frameAfterEdit = canvas.currentFrame();
        QTRY_VERIFY_WITH_TIMEOUT(canvas.currentFrame() != frameAfterEdit, 3000);
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
        const qreal penUpMilliseconds =
            static_cast<qreal>(timer.nsecsElapsed()) / 1000000.0;

        QCOMPARE(controller.document().layers.first().strokes.size(), 1);
        QVERIFY(CanvasWidgetTestAccess::hasCachedFrame(
            canvas, canvas.currentFrame()));
        qInfo().nospace() << "4K pen-up cache promotion and next paint took "
                          << penUpMilliseconds << " ms";
    }

    void warmsFramesWhilePausedSoResumingDoesNotRenderThemOnTheUiThread()
    {
        Document document = Document::createDefault(QSize(256, 256));
        document.animationFrames = 30;
        document.wobbleAmount = 1.6;
        Stroke background;
        background.color = QColor(220, 70, 50);
        background.width = 24.0;
        background.points = {{QPointF(40.0, 80.0), 1.0},
            {QPointF(120.0, 190.0), 1.0},
            {QPointF(210.0, 70.0), 1.0}};
        document.layers.first().strokes.append(background);

        DocumentController controller;
        QVERIFY(controller.loadDocument(document));
        CanvasWidget canvas(&controller);
        canvas.resize(320, 320);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.fitToWindow();
        canvas.setAnimating(false);
        QVERIFY(!canvas.isAnimating());
        QTRY_VERIFY_WITH_TIMEOUT(
            !CanvasWidgetTestAccess::frameCacheWarmupActive(canvas), 10000);

        Stroke edit;
        edit.color = QColor(20, 90, 210);
        edit.width = 18.0;
        edit.points = {
            {QPointF(60.0, 200.0), 1.0}, {QPointF(200.0, 60.0), 1.0}};
        QCOMPARE(
            controller.addStroke(controller.document().activeLayerId, edit),
            DocumentController::AddStrokeResult::Added);
        QVERIFY(!canvas.isAnimating());

        // Editing clears the cache; the warmup must refill it while paused so
        // that resuming plays from cache instead of rendering on the GUI
        // thread.
        QTRY_COMPARE_WITH_TIMEOUT(
            CanvasWidgetTestAccess::cachedFrameCount(canvas),
            qsizetype{30},
            10000);
        QVERIFY(!CanvasWidgetTestAccess::frameCacheWarmupActive(canvas));

        canvas.setAnimating(true);
        QCOMPARE(
            CanvasWidgetTestAccess::cachedFrameCount(canvas), qsizetype{30});
        for (int frame = 0; frame < 30; ++frame)
        {
            QVERIFY(CanvasWidgetTestAccess::hasCachedFrame(canvas, frame));
        }
    }

    void warmsAnimatedFourKFramesOffTheUiThreadAfterErasing()
    {
        Document document = Document::createDefault(QSize(4096, 4096));
        document.animationFrames = 30;
        document.wobbleAmount = 1.6;
        Stroke background;
        background.color = QColor(220, 70, 50);
        background.width = 320.0;
        background.points = {{QPointF(300.0, 1200.0), 1.0},
            {QPointF(1100.0, 2800.0), 1.0},
            {QPointF(2100.0, 1300.0), 1.0},
            {QPointF(3100.0, 2900.0), 1.0},
            {QPointF(3800.0, 1500.0), 1.0}};
        document.layers.first().strokes.append(background);

        DocumentController controller;
        QVERIFY(controller.loadDocument(document));
        CanvasWidget canvas(&controller);
        canvas.resize(800, 600);
        canvas.setZoomPercent(25);
        canvas.setTool(CanvasWidget::Tool::Eraser);
        canvas.setEraserWidth(100.0);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        QTRY_VERIFY_WITH_TIMEOUT(
            !CanvasWidgetTestAccess::frameCacheWarmupActive(canvas), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(
            CanvasWidgetTestAccess::cachedFrameCount(canvas),
            qsizetype{30},
            10000);

        const QPoint center = canvas.rect().center();
        QTest::mousePress(
            &canvas, Qt::LeftButton, Qt::NoModifier, center - QPoint(70, 0));
        for (int step = 0; step < 160; ++step)
        {
            const qreal angle = step * 0.31;
            const QPoint offset(
                qRound(std::cos(angle) * 70.0), qRound(std::sin(angle) * 45.0));
            QTest::mouseMove(&canvas, center + offset);
        }

        QElapsedTimer timer;
        timer.start();
        QTest::mouseRelease(
            &canvas, Qt::LeftButton, Qt::NoModifier, center + QPoint(70, 0));
        QApplication::processEvents(QEventLoop::AllEvents, 5);
        const qreal penUpMilliseconds =
            static_cast<qreal>(timer.nsecsElapsed()) / 1000000.0;

        QCOMPARE(controller.document().layers.first().strokes.size(), 2);
        QVERIFY(CanvasWidgetTestAccess::hasCachedFrame(
            canvas, canvas.currentFrame()));
        QVERIFY(CanvasWidgetTestAccess::frameCacheWarmupActive(canvas)
                || CanvasWidgetTestAccess::cachedFrameCount(canvas)
                       == qsizetype{30});
#ifdef NDEBUG
        QVERIFY2(penUpMilliseconds < 500.0,
            qPrintable(QStringLiteral("4K animated pen-up blocked for %1 ms")
                    .arg(penUpMilliseconds)));
#endif
        QTRY_VERIFY_WITH_TIMEOUT(
            !CanvasWidgetTestAccess::frameCacheWarmupActive(canvas), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(
            CanvasWidgetTestAccess::cachedFrameCount(canvas),
            qsizetype{30},
            10000);
        qInfo().nospace() << "4K 100px animated eraser pen-up returned in "
                          << penUpMilliseconds
                          << " ms; remaining frames were warmed asynchronously";
    }

    void keepsEveryPreviewSurfaceInsideTheDeclaredBudget()
    {
        const QSize canvasSize(4096, 4096);
        Document document = Document::createDefault(canvasSize);
        document.animationFrames = 3;
        document.wobbleAmount = 3.0;
        for (int index = 0; index < 3; ++index)
        {
            Layer layer;
            layer.name = QStringLiteral("Paint %1").arg(index + 1);
            layer.kind = LayerKind::Paint;
            layer.initialCanvasSize = canvasSize;
            Stroke stroke;
            stroke.width = 48.0;
            stroke.points = {{QPointF(200.0 + index * 300.0, 200.0), 1.0},
                {QPointF(3800.0, 3800.0 - index * 300.0), 1.0}};
            layer.strokes.append(stroke);
            document.layers.append(std::move(layer));
        }
        DocumentController controller;
        QVERIFY(controller.loadDocument(document));

        CanvasWidget canvas(&controller);
        canvas.resize(800, 600);
        canvas.setAnimating(false);
        canvas.setZoomPercent(100);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        QTRY_VERIFY(!CanvasWidgetTestAccess::zoomRenderPending(canvas));

        PreviewSurfaceUsage peak;
        const auto sample = [&canvas, &peak]()
        {
            const PreviewSurfaceUsage usage =
                CanvasWidgetTestAccess::previewSurfaceUsage(canvas);
            if (usage.totalBytes() > peak.totalBytes())
            {
                peak = usage;
            }
        };

        for (int frame = 0; frame < controller.document().animationFrames;
            ++frame)
        {
            canvas.setCurrentFrame(frame);
            canvas.repaint();
            sample();
        }

        const QPoint center = canvas.rect().center();
        QTest::mousePress(
            &canvas, Qt::LeftButton, Qt::NoModifier, center - QPoint(80, 0));
        for (int step = -60; step <= 80; step += 20)
        {
            QTest::mouseMove(&canvas, center + QPoint(step, step / 4), 1);
            canvas.repaint();
            sample();
        }
        QTest::mouseRelease(
            &canvas, Qt::LeftButton, Qt::NoModifier, center + QPoint(80, 20));
        canvas.repaint();
        sample();

        // Sampled while the pick is still held, because releasing drops the
        // native-size frame the picker renders.
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::AltModifier, center);
        canvas.repaint();
        sample();
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::AltModifier, center);

        const qint64 budgetBytes =
            static_cast<qint64>(MemoryBudget::previewCacheKiB()) * 1024;
        constexpr qreal mib = 1024.0 * 1024.0;
        qInfo().nospace() << "4K preview peak "
                          << static_cast<qreal>(peak.totalBytes()) / mib
                          << " MiB of " << static_cast<qreal>(budgetBytes) / mib
                          << " MiB budget (frames "
                          << static_cast<qreal>(peak.frameCacheBytes) / mib
                          << ", split "
                          << static_cast<qreal>(peak.layerSplitBytes) / mib
                          << ", rasters "
                          << static_cast<qreal>(peak.layerRasterBytes) / mib
                          << ", composed "
                          << static_cast<qreal>(peak.composedPreviewBytes) / mib
                          << ", colour pick "
                          << static_cast<qreal>(peak.colorPickBytes) / mib
                          << ", tiles "
                          << static_cast<qreal>(peak.strokeTileBytes) / mib
                          << ")";
        QVERIFY(peak.totalBytes() <= budgetBytes);
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
        QTRY_VERIFY_WITH_TIMEOUT(
            !CanvasWidgetTestAccess::frameCacheWarmupActive(canvas), 10000);

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

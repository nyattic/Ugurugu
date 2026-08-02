#include "support/RenderTestHelpers.hpp"
#include "support/RenderTestSuites.hpp"

namespace wobble
{

class WobbleAnimationTests final : public QObject
{
    Q_OBJECT

private slots:
    void loopsAtFrameCount()
    {
        const Document document = animatedDocument();
        const QImage first = RenderEngine::render(document, 0);
        const QImage looped =
            RenderEngine::render(document, document.animationFrames);

        QVERIFY(first == looped);
    }

    void changesAcrossWobbleFrames()
    {
        const Document document = animatedDocument();
        const QImage first = RenderEngine::render(document, 0);
        const QImage second = RenderEngine::render(document, 1);

        QVERIFY(first != second);
    }

    void staysStillWhenWobbleIsZero()
    {
        Document document = animatedDocument();
        document.wobbleAmount = 0.0;
        document.layers.first().strokes.first().width = 40.0;

        const QImage first = RenderEngine::render(document, 0);
        QVERIFY(!first.isNull());
        for (int frame = 1; frame < document.animationFrames; ++frame)
        {
            QCOMPARE(RenderEngine::render(document, frame), first);
        }
    }

    void staysStillWhenBrushWobbleScaleIsZero()
    {
        Document document = animatedDocument();
        document.layers.first().strokes.first().brush.wobbleScale = 0.0;

        const QImage first = RenderEngine::render(document, 0);
        QVERIFY(!first.isNull());
        for (int frame = 1; frame < document.animationFrames; ++frame)
        {
            QCOMPARE(RenderEngine::render(document, frame), first);
        }
    }

    void scalesWobbleByBrushWobbleScale()
    {
        Document document = animatedDocument();
        const QImage normal = RenderEngine::render(document, 0);
        document.layers.first().strokes.first().brush.wobbleScale = 2.0;
        const QImage rough = RenderEngine::render(document, 0);

        QVERIFY(!normal.isNull());
        QVERIFY(normal != rough);
    }
};

int runWobbleAnimationTests(int argc, char **argv)
{
    WobbleAnimationTests tests;
    return QTest::qExec(&tests, argc, argv);
}

}

#include "WobbleAnimationTests.moc"

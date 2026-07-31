#include "document/Document.hpp"
#include "document/DocumentController.hpp"
#include "io/DocumentSerializer.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include <algorithm>
#include <cstring>

namespace wobble {

namespace {

Stroke testStroke(const QPointF &start, const QPointF &end)
{
    Stroke stroke;
    stroke.points = {
        {start, 1.0},
        {end, 1.0}
    };
    return stroke;
}

void setRowPadding(QImage &image, uchar value)
{
    for (int y = 0; y < image.height(); ++y) {
        std::fill(
            image.scanLine(y) + image.width(),
            image.scanLine(y) + image.bytesPerLine(),
            value);
    }
}

}

class MaskRegressionTests final : public QObject
{
    Q_OBJECT

private slots:
    void schemaFourDeduplicatesMasksByContent()
    {
        Document document = Document::createDefault(QSize(65, 17));
        QImage mask(document.size, QImage::Format_Grayscale8);
        mask.fill(0);
        for (int y = 3; y < 12; ++y) {
            std::fill(mask.scanLine(y) + 5, mask.scanLine(y) + 45, 255);
        }

        Stroke first =
            testStroke(QPointF(5.0, 5.0), QPointF(40.0, 5.0));
        first.clipMask = mask;
        Stroke second =
            testStroke(QPointF(5.0, 8.0), QPointF(40.0, 8.0));
        second.clipMask = mask.copy();
        QVERIFY(first.clipMask.cacheKey() != second.clipMask.cacheKey());
        document.layers.first().strokes = {first, second};

        const QByteArray json = DocumentSerializer::toJson(document);
        QVERIFY(!json.isEmpty());
        const QJsonObject root = QJsonDocument::fromJson(json).object();
        QCOMPARE(root.value(QStringLiteral("schemaVersion")).toInt(), 4);
        const QJsonArray masks =
            root.value(QStringLiteral("clipMasks")).toArray();
        QCOMPARE(masks.size(), 1);

        const QJsonArray strokes =
            root.value(QStringLiteral("layers"))
                .toArray()
                .first()
                .toObject()
                .value(QStringLiteral("strokes"))
                .toArray();
        QCOMPARE(
            strokes[0].toObject().value(QStringLiteral("clipMaskId")),
            strokes[1].toObject().value(QStringLiteral("clipMaskId")));
        QVERIFY(!strokes[0].toObject().contains(
            QStringLiteral("clipMask")));

        QString error;
        const std::optional<Document> loaded =
            DocumentSerializer::fromJson(json, &error);
        QVERIFY2(loaded.has_value(), qPrintable(error));
        const QVector<Stroke> &loadedStrokes =
            loaded->layers.first().strokes;
        QCOMPARE(loadedStrokes[0].clipMask, mask);
        QCOMPARE(loadedStrokes[1].clipMask, mask);
        QCOMPARE(
            loadedStrokes[0].clipMask.cacheKey(),
            loadedStrokes[1].clipMask.cacheKey());
    }

    void schemaFourIgnoresImageRowPadding()
    {
        Document first = Document::createDefault(QSize(65, 9));
        Stroke stroke =
            testStroke(QPointF(2.0, 2.0), QPointF(60.0, 6.0));
        stroke.clipMask =
            QImage(first.size, QImage::Format_Grayscale8);
        stroke.clipMask.fill(127);
        setRowPadding(stroke.clipMask, 0x11);
        first.layers.first().strokes = {stroke};

        Document second = first;
        second.layers.first().strokes[0].clipMask =
            first.layers.first().strokes[0].clipMask.copy();
        setRowPadding(
            second.layers.first().strokes[0].clipMask,
            0xEE);

        QCOMPARE(
            DocumentSerializer::toJson(first),
            DocumentSerializer::toJson(second));
    }

    void loadsLegacySchemaThreeInlineMask()
    {
        Document source = Document::createDefault(QSize(13, 7));
        Stroke stroke =
            testStroke(QPointF(1.0, 1.0), QPointF(10.0, 5.0));
        stroke.clipMask =
            QImage(source.size, QImage::Format_Grayscale8);
        stroke.clipMask.fill(0);
        for (int y = 2; y < 5; ++y) {
            std::fill(
                stroke.clipMask.scanLine(y) + 3,
                stroke.clipMask.scanLine(y) + 9,
                255);
        }
        source.layers.first().strokes = {stroke};

        QJsonObject root =
            QJsonDocument::fromJson(
                DocumentSerializer::toJson(source)).object();
        root.insert(QStringLiteral("schemaVersion"), 3);
        root.remove(QStringLiteral("clipMasks"));
        QJsonArray layers =
            root.value(QStringLiteral("layers")).toArray();
        QJsonObject layer = layers.first().toObject();
        QJsonArray strokes =
            layer.value(QStringLiteral("strokes")).toArray();
        QJsonObject legacyStroke = strokes.first().toObject();
        legacyStroke.remove(QStringLiteral("clipMaskId"));
        const QByteArray bytes(
            reinterpret_cast<const char *>(stroke.clipMask.constBits()),
            stroke.clipMask.sizeInBytes());
        QJsonObject legacyMask;
        legacyMask.insert(
            QStringLiteral("width"),
            stroke.clipMask.width());
        legacyMask.insert(
            QStringLiteral("height"),
            stroke.clipMask.height());
        legacyMask.insert(
            QStringLiteral("data"),
            QString::fromLatin1(qCompress(bytes, 6).toBase64()));
        legacyStroke.insert(QStringLiteral("clipMask"), legacyMask);
        strokes[0] = legacyStroke;
        layer.insert(QStringLiteral("strokes"), strokes);
        layers[0] = layer;
        root.insert(QStringLiteral("layers"), layers);

        QString error;
        const std::optional<Document> loaded =
            DocumentSerializer::fromJson(
                QJsonDocument(root).toJson(QJsonDocument::Compact),
                &error);
        QVERIFY2(loaded.has_value(), qPrintable(error));
        QCOMPARE(
            loaded->layers.first().strokes.first().clipMask,
            stroke.clipMask);
    }

    void rejectsTamperedContentAddress()
    {
        Document source = Document::createDefault(QSize(16, 16));
        Stroke stroke =
            testStroke(QPointF(1.0, 1.0), QPointF(14.0, 14.0));
        stroke.clipMask =
            QImage(source.size, QImage::Format_Grayscale8);
        stroke.clipMask.fill(255);
        source.layers.first().strokes = {stroke};

        QJsonObject root =
            QJsonDocument::fromJson(
                DocumentSerializer::toJson(source)).object();
        QJsonArray masks =
            root.value(QStringLiteral("clipMasks")).toArray();
        QJsonObject mask = masks.first().toObject();
        const QString tamperedId(64, QLatin1Char('0'));
        mask.insert(QStringLiteral("id"), tamperedId);
        masks[0] = mask;
        root.insert(QStringLiteral("clipMasks"), masks);
        QJsonArray layers =
            root.value(QStringLiteral("layers")).toArray();
        QJsonObject layer = layers.first().toObject();
        QJsonArray strokes =
            layer.value(QStringLiteral("strokes")).toArray();
        QJsonObject tamperedStroke = strokes.first().toObject();
        tamperedStroke.insert(
            QStringLiteral("clipMaskId"),
            tamperedId);
        strokes[0] = tamperedStroke;
        layer.insert(QStringLiteral("strokes"), strokes);
        layers[0] = layer;
        root.insert(QStringLiteral("layers"), layers);

        QString error;
        QVERIFY(!DocumentSerializer::fromJson(
            QJsonDocument(root).toJson(QJsonDocument::Compact),
            &error));
        QVERIFY(!error.isEmpty());
    }

    void partialSelectionReusesDerivedMasks()
    {
        Document document = Document::createDefault(QSize(128, 128));
        Stroke first =
            testStroke(QPointF(10.0, 50.0), QPointF(110.0, 50.0));
        Stroke second =
            testStroke(QPointF(10.0, 70.0), QPointF(110.0, 70.0));
        const QUuid firstId = first.id;
        const QUuid secondId = second.id;
        document.layers.first().strokes = {first, second};

        QImage selection(document.size, QImage::Format_Grayscale8);
        selection.fill(0);
        for (int y = 30; y < 90; ++y) {
            std::fill(
                selection.scanLine(y) + 40,
                selection.scanLine(y) + 90,
                255);
        }

        DocumentController controller;
        controller.loadDocument(document);
        QVERIFY(controller.moveStrokes(
            document.activeLayerId,
            {firstId, secondId},
            QPointF(5.0, 0.0),
            selection));

        const QVector<Stroke> &strokes =
            controller.document().layers.first().strokes;
        QCOMPARE(strokes.size(), 4);
        QVector<qint64> movedKeys;
        QVector<qint64> remainderKeys;
        for (const Stroke &candidate : strokes) {
            if (candidate.id == firstId || candidate.id == secondId) {
                movedKeys.append(candidate.clipMask.cacheKey());
            } else {
                remainderKeys.append(candidate.clipMask.cacheKey());
            }
        }
        QCOMPARE(movedKeys.size(), 2);
        QCOMPARE(remainderKeys.size(), 2);
        QCOMPARE(movedKeys[0], movedKeys[1]);
        QCOMPARE(remainderKeys[0], remainderKeys[1]);
    }
};

int runMaskRegressionTests(int argc, char **argv)
{
    MaskRegressionTests tests;
    return QTest::qExec(&tests, argc, argv);
}

}

#include "MaskRegressionTests.moc"

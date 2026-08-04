#include "io/DocumentSerializer.hpp"
#include "io/WawaV10Importer.hpp"
#include "render/RenderEngine.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (application.arguments().size() != 2)
    {
        return 2;
    }
    QFile file(application.arguments()[1]);
    if (!file.open(QIODevice::ReadOnly))
    {
        return 3;
    }
    QString error;
    const std::optional<wobble::WawaImportResult> imported =
        wobble::WawaV10Importer::import(file.readAll(), &error);
    QTextStream output(stdout);
    if (!imported)
    {
        output << error << '\n';
        return 1;
    }
    const QByteArray serialized =
        wobble::DocumentSerializer::toJson(imported->document);
    const std::optional<wobble::Document> reloaded =
        wobble::DocumentSerializer::fromJson(serialized, &error);
    if (serialized.isEmpty() || !reloaded)
    {
        output << (error.isEmpty() ? QStringLiteral("Serialization failed")
                                   : error)
               << '\n';
        return 1;
    }
    for (int frame = 0; frame < imported->document.animationFrames; ++frame)
    {
        const QImage importedFrame =
            wobble::RenderEngine::render(imported->document, frame);
        const QImage reloadedFrame =
            wobble::RenderEngine::render(*reloaded, frame);
        if (importedFrame.isNull() || importedFrame != reloadedFrame)
        {
            output << "Render verification failed at frame " << frame << '\n';
            return 1;
        }
    }
    const wobble::WawaImportSummary &summary = imported->summary;
    output << "canvas=" << imported->document.size.width() << 'x'
           << imported->document.size.height() << '\n';
    output << "layers=" << summary.layers << '\n';
    output << "baseImages=" << summary.baseImages << '\n';
    output << "paintStrokes=" << summary.paintStrokes << '\n';
    output << "eraserStrokes=" << summary.eraserStrokes << '\n';
    output << "polygonFills=" << summary.polygonFills << '\n';
    output << "skippedOperations=" << summary.skippedOperations << '\n';
    output << "serializedBytes=" << serialized.size() << '\n';
    output << "verifiedFrames=" << imported->document.animationFrames << '\n';
    return 0;
}

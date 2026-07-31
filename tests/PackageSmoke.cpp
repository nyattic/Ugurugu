#include <QCoreApplication>
#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QTemporaryDir>

#include <cstdio>

namespace {

int fail(const QString &message)
{
    std::fprintf(stderr, "%s\n", message.toLocal8Bit().constData());
    return 1;
}

}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (application.arguments().size() != 2) {
        return fail(QStringLiteral(
            "Usage: wobblepaint_package_smoke <WagleWaglePaint.app>"));
    }

    const QDir bundle(application.arguments().at(1));
    const QString pluginRoot =
        bundle.filePath(QStringLiteral("Contents/PlugIns"));
    const QString jpegPlugin = QDir(pluginRoot).filePath(
        QStringLiteral("imageformats/libqjpeg.dylib"));
    const QString offscreenPlugin = QDir(pluginRoot).filePath(
        QStringLiteral("platforms/libqoffscreen.dylib"));
    const QString resourceRoot = bundle.filePath(
        QStringLiteral("Contents/Resources"));
    const QString licenseFile = QDir(resourceRoot).filePath(
        QStringLiteral("LICENSE"));
    const QString readmeFile = QDir(resourceRoot).filePath(
        QStringLiteral("README.md"));

    if (!QFileInfo(jpegPlugin).isFile()) {
        return fail(QStringLiteral(
            "The installed application does not contain libqjpeg.dylib."));
    }
    if (QFileInfo::exists(offscreenPlugin)) {
        return fail(QStringLiteral(
            "The production application unexpectedly contains "
            "libqoffscreen.dylib."));
    }
    if (!QFileInfo(licenseFile).isFile()
        || !QFileInfo(readmeFile).isFile()) {
        return fail(QStringLiteral(
            "The installed application does not contain its license "
            "and copyright notice."));
    }

    QCoreApplication::setLibraryPaths({pluginRoot});
    const QList<QByteArray> formats = QImageWriter::supportedImageFormats();
    if (!formats.contains(QByteArrayLiteral("jpeg"))
        && !formats.contains(QByteArrayLiteral("jpg"))) {
        return fail(QStringLiteral(
            "The installed application plugin set does not support JPEG."));
    }

    QTemporaryDir outputDirectory;
    if (!outputDirectory.isValid()) {
        return fail(QStringLiteral(
            "Could not create a temporary output directory."));
    }

    QImage source(8, 8, QImage::Format_RGB32);
    source.fill(QColor(0x31, 0x82, 0xce));
    const QString outputPath =
        outputDirectory.filePath(QStringLiteral("package-smoke.jpg"));
    QImageWriter writer(outputPath, QByteArrayLiteral("JPEG"));
    writer.setQuality(90);
    if (!writer.write(source)) {
        return fail(QStringLiteral("JPEG write failed: %1")
            .arg(writer.errorString()));
    }
    if (QFileInfo(outputPath).size() <= 0) {
        return fail(QStringLiteral("JPEG output file is empty."));
    }

    QImageReader reader(outputPath, QByteArrayLiteral("JPEG"));
    const QImage decoded = reader.read();
    if (decoded.isNull()) {
        return fail(QStringLiteral("JPEG read-back failed: %1")
            .arg(reader.errorString()));
    }
    if (decoded.size() != source.size()) {
        return fail(QStringLiteral(
            "JPEG read-back dimensions do not match the source image."));
    }

    return 0;
}

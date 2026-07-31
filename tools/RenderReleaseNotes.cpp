#include <QFile>
#include <QGuiApplication>
#include <QSaveFile>
#include <QTextDocument>

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
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    }
    QGuiApplication application(argc, argv);
    if (application.arguments().size() != 3) {
        return fail(QStringLiteral(
            "Usage: wobblepaint_render_release_notes <input.md> <output.html>"));
    }

    QFile input(application.arguments().at(1));
    if (!input.open(QIODevice::ReadOnly)) {
        return fail(QStringLiteral("Could not open %1: %2")
            .arg(input.fileName(), input.errorString()));
    }

    QTextDocument document;
    document.setMarkdown(
        QString::fromUtf8(input.readAll()),
        QTextDocument::MarkdownDialectGitHub);

    QSaveFile output(application.arguments().at(2));
    if (!output.open(QIODevice::WriteOnly)) {
        return fail(QStringLiteral("Could not open %1: %2")
            .arg(output.fileName(), output.errorString()));
    }
    const QByteArray html = document.toHtml().toUtf8();
    if (output.write(html) != html.size()) {
        return fail(QStringLiteral("Could not write %1: %2")
            .arg(output.fileName(), output.errorString()));
    }
    if (!output.commit()) {
        return fail(QStringLiteral("Could not commit %1: %2")
            .arg(output.fileName(), output.errorString()));
    }

    return 0;
}

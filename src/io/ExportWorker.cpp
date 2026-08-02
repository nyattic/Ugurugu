#include "io/ExportWorker.hpp"

#include "document/DocumentLimits.hpp"
#include "io/GifWriter.hpp"
#include "render/RenderEngine.hpp"

#include <QDeadlineTimer>
#include <QImageWriter>
#include <QMutexLocker>
#include <QSaveFile>

#include <algorithm>
#include <utility>

namespace wobble
{
namespace
{

QVector<int> frameDelays(int frameCount, qreal framesPerSecond)
{
    const qreal fps = std::clamp(framesPerSecond,
        DocumentLimits::minimumFramesPerSecond,
        DocumentLimits::maximumFramesPerSecond);
    QVector<int> delays;
    delays.reserve(frameCount);
    qint64 emittedCentiseconds = 0;
    for (int frame = 1; frame <= frameCount; ++frame)
    {
        const qint64 targetCentiseconds =
            qRound64(static_cast<qreal>(frame) * 100.0 / fps);
        const int delay = static_cast<int>(
            std::max<qint64>(1, targetCentiseconds - emittedCentiseconds));
        delays.append(delay);
        emittedCentiseconds += delay;
    }
    return delays;
}

}

ExportWorker::ExportWorker(QObject *parent)
    : QObject(parent)
{
    m_thread.setObjectName(QStringLiteral("ExportWorker"));
    m_workerContext.moveToThread(&m_thread);
    m_thread.start();
}

ExportWorker::~ExportWorker()
{
    cancel();
    waitForIdle();
    m_thread.quit();
    m_thread.wait();
}

bool ExportWorker::startImage(
    Document document, int frame, const QString &filePath, bool jpeg)
{
    return start({Kind::Image, std::move(document), frame, filePath, jpeg});
}

bool ExportWorker::startGif(Document document, const QString &filePath)
{
    return start({Kind::Gif, std::move(document), 0, filePath, false});
}

bool ExportWorker::start(Request request)
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_busy || m_request)
        {
            return false;
        }
        m_request = std::move(request);
        m_busy = true;
        m_cancelRequested = false;
    }
    QMetaObject::invokeMethod(
        &m_workerContext,
        [this]()
        {
            process();
        },
        Qt::QueuedConnection);
    return true;
}

void ExportWorker::cancel()
{
    QMutexLocker locker(&m_mutex);
    m_cancelRequested = true;
}

bool ExportWorker::isBusy() const
{
    QMutexLocker locker(&m_mutex);
    return m_busy;
}

bool ExportWorker::waitForIdle(int timeoutMilliseconds)
{
    QDeadlineTimer deadline = timeoutMilliseconds < 0
                                  ? QDeadlineTimer(QDeadlineTimer::Forever)
                                  : QDeadlineTimer(timeoutMilliseconds);
    QMutexLocker locker(&m_mutex);
    while (m_busy || m_request)
    {
        if (!m_idleCondition.wait(&m_mutex, deadline))
        {
            return false;
        }
    }
    return true;
}

void ExportWorker::process()
{
    Request request;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_request)
        {
            m_busy = false;
            m_idleCondition.wakeAll();
            return;
        }
        request = std::move(*m_request);
        m_request.reset();
    }

    QString error;
    bool success = false;
    if (!canceled())
    {
        success = request.kind == Kind::Image ? writeImage(request, &error)
                                              : writeGif(request, &error);
    }
    const bool wasCanceled = !success && canceled();
    complete(request, success, wasCanceled, error);
}

bool ExportWorker::canceled() const
{
    QMutexLocker locker(&m_mutex);
    return m_cancelRequested;
}

void ExportWorker::postProgress(Kind kind, int value, int maximum)
{
    QMetaObject::invokeMethod(
        this,
        [this, kind, value, maximum]()
        {
            emit progress(kind, value, maximum);
        },
        Qt::QueuedConnection);
}

void ExportWorker::complete(const Request &request,
    bool success,
    bool wasCanceled,
    const QString &error)
{
    {
        QMutexLocker locker(&m_mutex);
        m_busy = false;
        m_cancelRequested = false;
        m_idleCondition.wakeAll();
    }
    QMetaObject::invokeMethod(
        this,
        [this, request, success, wasCanceled, error]()
        {
            emit finished(
                request.kind, success, wasCanceled, request.filePath, error);
        },
        Qt::QueuedConnection);
}

bool ExportWorker::writeImage(const Request &request, QString *error)
{
    const QImage image = RenderEngine::render(request.document, request.frame);
    if (image.isNull())
    {
        *error = tr("The image could not be rendered.");
        return false;
    }
    if (canceled())
    {
        return false;
    }

    QSaveFile file(request.filePath);
    if (!file.open(QIODevice::WriteOnly))
    {
        *error = file.errorString();
        return false;
    }
    QImageWriter writer(&file, request.jpeg ? "JPEG" : "PNG");
    if (request.jpeg)
    {
        writer.setQuality(92);
    }
    if (!writer.write(image))
    {
        *error = writer.errorString();
        file.cancelWriting();
        return false;
    }
    if (canceled())
    {
        file.cancelWriting();
        return false;
    }
    if (!file.commit())
    {
        *error = file.errorString();
        return false;
    }
    return true;
}

bool ExportWorker::writeGif(const Request &request, QString *error)
{
    QVector<QImage> frames;
    frames.reserve(request.document.animationFrames);
    postProgress(Kind::Gif, 0, request.document.animationFrames);
    for (int frame = 0; frame < request.document.animationFrames; ++frame)
    {
        if (canceled())
        {
            return false;
        }
        QImage image = RenderEngine::render(request.document, frame);
        if (image.isNull())
        {
            *error = tr("An animation frame could not be rendered.");
            return false;
        }
        frames.append(std::move(image));
        postProgress(Kind::Gif, frame + 1, request.document.animationFrames);
    }
    if (canceled())
    {
        return false;
    }
    return GifWriter::write(request.filePath,
        frames,
        frameDelays(
            request.document.animationFrames, request.document.framesPerSecond),
        error,
        [this]()
        {
            return canceled();
        });
}

}

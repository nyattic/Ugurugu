#include "document/DocumentController.hpp"

#include "document/DocumentLimits.hpp"

#include <QSet>
#include <QUndoCommand>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace wobble {

namespace {

class LambdaCommand final : public QUndoCommand
{
public:
    LambdaCommand(
        QString text,
        std::function<void()> redoAction,
        std::function<void()> undoAction,
        int mergeId = -1,
        QUuid mergeScope = QUuid())
        : QUndoCommand(std::move(text))
        , m_redoAction(std::move(redoAction))
        , m_undoAction(std::move(undoAction))
        , m_mergeId(mergeId)
        , m_mergeScope(mergeScope)
    {
    }

    int id() const override
    {
        return m_mergeId;
    }

    bool mergeWith(const QUndoCommand *other) override
    {
        const auto *command = dynamic_cast<const LambdaCommand *>(other);
        if (!command
            || command->m_mergeId != m_mergeId
            || command->m_mergeScope != m_mergeScope) {
            return false;
        }
        m_redoAction = command->m_redoAction;
        return true;
    }

    void redo() override
    {
        m_redoAction();
    }

    void undo() override
    {
        m_undoAction();
    }

private:
    std::function<void()> m_redoAction;
    std::function<void()> m_undoAction;
    int m_mergeId = -1;
    QUuid m_mergeScope;
};

constexpr int wobbleAmountMergeId = 1;
constexpr int animationFramesMergeId = 2;
constexpr int framesPerSecondMergeId = 3;
constexpr int layerOpacityMergeId = 4;

qsizetype totalPointCount(const Document &document)
{
    qsizetype count = 0;
    for (const Layer &layer : document.layers) {
        for (const Stroke &stroke : layer.strokes) {
            if (stroke.points.size()
                > DocumentLimits::maximumTotalPoints - count) {
                return DocumentLimits::maximumTotalPoints + 1;
            }
            count += stroke.points.size();
        }
    }
    return count;
}

qsizetype totalStrokeCount(const Document &document)
{
    qsizetype count = 0;
    for (const Layer &layer : document.layers) {
        if (layer.strokes.size()
            > DocumentLimits::maximumTotalStrokes - count) {
            return DocumentLimits::maximumTotalStrokes + 1;
        }
        count += layer.strokes.size();
    }
    return count;
}

qsizetype layerPointCount(const Layer &layer)
{
    qsizetype count = 0;
    for (const Stroke &stroke : layer.strokes) {
        if (stroke.points.size()
            > DocumentLimits::maximumTotalPoints - count) {
            return DocumentLimits::maximumTotalPoints + 1;
        }
        count += stroke.points.size();
    }
    return count;
}

bool containsStrokeId(const Document &document, const QUuid &id)
{
    for (const Layer &layer : document.layers) {
        for (const Stroke &stroke : layer.strokes) {
            if (stroke.id == id) {
                return true;
            }
        }
    }
    return false;
}

bool isValidStrokePoint(const StrokePoint &point, const QSize &size)
{
    return std::isfinite(point.position.x())
        && std::isfinite(point.position.y())
        && std::isfinite(point.pressure)
        && point.position.x() >= 0.0
        && point.position.y() >= 0.0
        && point.position.x() <= size.width()
        && point.position.y() <= size.height()
        && point.pressure >= 0.0
        && point.pressure <= 1.0;
}

}

DocumentController::DocumentController(QObject *parent)
    : QObject(parent)
    , m_document(Document::createDefault())
{
    m_undoStack.setClean();
}

const Document &DocumentController::document() const
{
    return m_document;
}

QUndoStack *DocumentController::undoStack()
{
    return &m_undoStack;
}

bool DocumentController::isModified() const
{
    return m_currentContentRevision != m_savedContentRevision;
}

void DocumentController::pushTransientCommand(
    const QString &text,
    std::function<void()> redoAction,
    std::function<void()> undoAction)
{
    m_undoStack.push(new LambdaCommand(
        text,
        std::move(redoAction),
        std::move(undoAction)));
}

void DocumentController::newDocument(const QSize &size)
{
    const bool wasModified = isModified();
    const QSize normalized(
        std::clamp(
            size.width(),
            DocumentLimits::minimumCanvasEdge,
            DocumentLimits::maximumCanvasEdge),
        std::clamp(
            size.height(),
            DocumentLimits::minimumCanvasEdge,
            DocumentLimits::maximumCanvasEdge));
    m_document = Document::createDefault(normalized);
    m_undoStack.clear();
    m_undoStack.setClean();
    m_currentContentRevision = 0;
    m_savedContentRevision = 0;
    m_nextContentRevision = 0;
    emit documentChanged();
    emit activeLayerChanged(m_document.activeLayerId);
    if (wasModified) {
        emit modifiedChanged(false);
    }
}

void DocumentController::loadDocument(Document document)
{
    const bool wasModified = isModified();
    m_document = std::move(document);
    ensureActiveLayer();
    m_undoStack.clear();
    m_undoStack.setClean();
    m_currentContentRevision = 0;
    m_savedContentRevision = 0;
    m_nextContentRevision = 0;
    emit documentChanged();
    emit activeLayerChanged(m_document.activeLayerId);
    if (wasModified) {
        emit modifiedChanged(false);
    }
}

void DocumentController::markSaved()
{
    const bool wasModified = isModified();
    m_savedContentRevision = m_currentContentRevision;
    m_undoStack.setClean();
    if (wasModified) {
        emit modifiedChanged(false);
    }
}

void DocumentController::setActiveLayer(const QUuid &id)
{
    if (m_document.activeLayerId == id || !m_document.layer(id)) {
        return;
    }
    m_document.activeLayerId = id;
    emit activeLayerChanged(id);
}

void DocumentController::addStroke(const QUuid &layerId, Stroke stroke)
{
    Layer *layer = m_document.layer(layerId);
    if (!layer
        || layer->strokes.size() >= DocumentLimits::maximumStrokesPerLayer
        || totalStrokeCount(m_document)
            >= DocumentLimits::maximumTotalStrokes
        || stroke.id.isNull()
        || containsStrokeId(m_document, stroke.id)
        || (stroke.mode != StrokeMode::Paint
            && stroke.mode != StrokeMode::Erase
            && stroke.mode != StrokeMode::Fill)
        || !stroke.color.isValid()
        || !std::isfinite(stroke.width)
        || stroke.width < DocumentLimits::minimumStrokeWidth
        || stroke.width > DocumentLimits::maximumStrokeWidth
        || !isValidBrushSettings(stroke.brush)
        || stroke.points.isEmpty()) {
        return;
    }

    const qsizetype currentPointCount = totalPointCount(m_document);
    if (currentPointCount >= DocumentLimits::maximumTotalPoints) {
        return;
    }
    const qsizetype availablePoints =
        DocumentLimits::maximumTotalPoints - currentPointCount;
    const qsizetype acceptedPointCount = std::min(
        stroke.points.size(),
        std::min(
            static_cast<qsizetype>(DocumentLimits::maximumPointsPerStroke),
            availablePoints));
    stroke.points.resize(acceptedPointCount);
    if (!std::all_of(
            stroke.points.cbegin(),
            stroke.points.cend(),
            [this](const StrokePoint &point) {
                return isValidStrokePoint(point, m_document.size);
            })) {
        return;
    }

    const QUuid strokeId = stroke.id;
    auto redoAction = [
        this,
        layerId,
        stroke = std::move(stroke)
    ]() {
        if (Layer *target = m_document.layer(layerId)) {
            target->strokes.append(stroke);
            notifyDocumentChanged();
        }
    };
    auto undoAction = [this, layerId, strokeId]() {
        if (Layer *layer = m_document.layer(layerId)) {
            for (int index = layer->strokes.size() - 1; index >= 0; --index) {
                if (layer->strokes[index].id == strokeId) {
                    layer->strokes.removeAt(index);
                    notifyDocumentChanged();
                    return;
                }
            }
        }
    };
    pushDocumentCommand(
        tr("Draw stroke"),
        std::move(redoAction),
        std::move(undoAction));
}

void DocumentController::translateStrokes(
    const QUuid &layerId,
    const QVector<QUuid> &strokeIds,
    const QPointF &delta)
{
    const Layer *layer = m_document.layer(layerId);
    if (!layer
        || strokeIds.isEmpty()
        || !std::isfinite(delta.x())
        || !std::isfinite(delta.y())
        || (qFuzzyIsNull(delta.x()) && qFuzzyIsNull(delta.y()))) {
        return;
    }

    QSet<QUuid> requested(strokeIds.cbegin(), strokeIds.cend());
    QSet<QUuid> movable;
    for (const Stroke &stroke : layer->strokes) {
        if (!requested.contains(stroke.id)) {
            continue;
        }
        const bool staysInside = std::all_of(
            stroke.points.cbegin(),
            stroke.points.cend(),
            [this, &delta](const StrokePoint &point) {
                StrokePoint moved = point;
                moved.position += delta;
                return isValidStrokePoint(moved, m_document.size);
            });
        if (!staysInside) {
            return;
        }
        movable.insert(stroke.id);
    }
    if (movable.isEmpty()) {
        return;
    }

    const QVector<QUuid> movedStrokes(movable.cbegin(), movable.cend());
    auto shift = [this, layerId, movable, movedStrokes](const QPointF &offset) {
        if (Layer *target = m_document.layer(layerId)) {
            for (Stroke &stroke : target->strokes) {
                if (movable.contains(stroke.id)) {
                    for (StrokePoint &point : stroke.points) {
                        point.position += offset;
                    }
                }
            }
            notifyDocumentChanged();
            emit strokesTranslated(layerId, movedStrokes, offset);
        }
    };
    pushDocumentCommand(
        tr("Move selection"),
        [shift, delta]() { shift(delta); },
        [shift, delta]() { shift(-delta); });
}

void DocumentController::removeStrokes(
    const QUuid &layerId,
    const QVector<QUuid> &strokeIds)
{
    const Layer *layer = m_document.layer(layerId);
    if (!layer || strokeIds.isEmpty()) {
        return;
    }

    const QSet<QUuid> requested(strokeIds.cbegin(), strokeIds.cend());
    QVector<QPair<int, Stroke>> removed;
    for (int index = 0; index < layer->strokes.size(); ++index) {
        if (requested.contains(layer->strokes[index].id)) {
            removed.append({index, layer->strokes[index]});
        }
    }
    if (removed.isEmpty()) {
        return;
    }

    auto redoAction = [this, layerId, requested]() {
        if (Layer *target = m_document.layer(layerId)) {
            target->strokes.removeIf([&requested](const Stroke &stroke) {
                return requested.contains(stroke.id);
            });
            notifyDocumentChanged();
        }
    };
    auto undoAction = [this, layerId, removed]() {
        if (Layer *target = m_document.layer(layerId)) {
            for (const auto &entry : removed) {
                target->strokes.insert(
                    std::clamp(
                        entry.first,
                        0,
                        static_cast<int>(target->strokes.size())),
                    entry.second);
            }
            notifyDocumentChanged();
        }
    };
    pushDocumentCommand(
        tr("Delete selection"),
        std::move(redoAction),
        std::move(undoAction));
}

void DocumentController::addLayer()
{
    if (m_document.layers.size() >= DocumentLimits::maximumLayers) {
        return;
    }
    Layer layer;
    layer.name = nextLayerName();
    const int insertionIndex = m_document.layers.size();
    const QUuid previousActive = m_document.activeLayerId;
    const QUuid layerId = layer.id;
    auto redoAction = [this, layer, insertionIndex, layerId]() {
        const int index = std::clamp(
            insertionIndex,
            0,
            static_cast<int>(m_document.layers.size()));
        m_document.layers.insert(index, layer);
        m_document.activeLayerId = layerId;
        notifyDocumentChanged();
        emit activeLayerChanged(layerId);
    };
    auto undoAction = [this, layerId, previousActive]() {
        const int index = m_document.layerIndex(layerId);
        if (index >= 0) {
            m_document.layers.removeAt(index);
        }
        m_document.activeLayerId = previousActive;
        ensureActiveLayer();
        notifyDocumentChanged();
        emit activeLayerChanged(m_document.activeLayerId);
    };
    pushDocumentCommand(
        tr("Add layer"),
        std::move(redoAction),
        std::move(undoAction));
}

void DocumentController::duplicateLayer(const QUuid &id)
{
    const int sourceIndex = m_document.layerIndex(id);
    if (sourceIndex < 0
        || m_document.layers.size() >= DocumentLimits::maximumLayers) {
        return;
    }
    const qsizetype sourcePointCount =
        layerPointCount(m_document.layers[sourceIndex]);
    const qsizetype existingPointCount = totalPointCount(m_document);
    const qsizetype sourceStrokeCount =
        m_document.layers[sourceIndex].strokes.size();
    const qsizetype existingStrokeCount = totalStrokeCount(m_document);
    if (sourcePointCount > DocumentLimits::maximumTotalPoints
        || existingPointCount > DocumentLimits::maximumTotalPoints
        || sourcePointCount
            > DocumentLimits::maximumTotalPoints - existingPointCount
        || sourceStrokeCount > DocumentLimits::maximumTotalStrokes
        || existingStrokeCount > DocumentLimits::maximumTotalStrokes
        || sourceStrokeCount
            > DocumentLimits::maximumTotalStrokes - existingStrokeCount) {
        return;
    }
    Layer copy = m_document.layers[sourceIndex];
    copy.id = QUuid::createUuid();
    copy.name = tr("%1 copy").arg(copy.name);
    if (copy.name.size() > DocumentLimits::maximumLayerNameLength) {
        copy.name.truncate(DocumentLimits::maximumLayerNameLength);
    }
    for (Stroke &stroke : copy.strokes) {
        stroke.id = QUuid::createUuid();
    }
    const int insertionIndex = sourceIndex + 1;
    const QUuid copyId = copy.id;
    const QUuid previousActive = m_document.activeLayerId;
    auto redoAction = [this, copy, insertionIndex, copyId]() {
        m_document.layers.insert(
            std::clamp(
                insertionIndex,
                0,
                static_cast<int>(m_document.layers.size())),
            copy);
        m_document.activeLayerId = copyId;
        notifyDocumentChanged();
        emit activeLayerChanged(copyId);
    };
    auto undoAction = [this, copyId, previousActive]() {
        const int index = m_document.layerIndex(copyId);
        if (index >= 0) {
            m_document.layers.removeAt(index);
        }
        m_document.activeLayerId = previousActive;
        ensureActiveLayer();
        notifyDocumentChanged();
        emit activeLayerChanged(m_document.activeLayerId);
    };
    pushDocumentCommand(
        tr("Duplicate layer"),
        std::move(redoAction),
        std::move(undoAction));
}

void DocumentController::removeLayer(const QUuid &id)
{
    const int index = m_document.layerIndex(id);
    if (index < 0 || m_document.layers.size() <= 1) {
        return;
    }
    const Layer removedLayer = m_document.layers[index];
    const QUuid previousActive = m_document.activeLayerId;
    const int nextIndex = std::max(0, index - 1);
    const QUuid nextActive = m_document.layers[nextIndex].id;
    auto redoAction = [this, id, nextActive]() {
        const int currentIndex = m_document.layerIndex(id);
        if (currentIndex >= 0) {
            m_document.layers.removeAt(currentIndex);
        }
        m_document.activeLayerId = nextActive;
        ensureActiveLayer();
        notifyDocumentChanged();
        emit activeLayerChanged(m_document.activeLayerId);
    };
    auto undoAction = [this, removedLayer, index, previousActive]() {
        m_document.layers.insert(
            std::clamp(
                index,
                0,
                static_cast<int>(m_document.layers.size())),
            removedLayer);
        m_document.activeLayerId = previousActive;
        ensureActiveLayer();
        notifyDocumentChanged();
        emit activeLayerChanged(m_document.activeLayerId);
    };
    pushDocumentCommand(
        tr("Delete layer"),
        std::move(redoAction),
        std::move(undoAction));
}

void DocumentController::clearLayer(const QUuid &id)
{
    Layer *layer = m_document.layer(id);
    if (!layer || layer->strokes.isEmpty()) {
        return;
    }
    const QVector<Stroke> previousStrokes = layer->strokes;
    auto redoAction = [this, id]() {
        if (Layer *target = m_document.layer(id)) {
            target->strokes.clear();
            notifyDocumentChanged();
        }
    };
    auto undoAction = [this, id, previousStrokes]() {
        if (Layer *target = m_document.layer(id)) {
            target->strokes = previousStrokes;
            notifyDocumentChanged();
        }
    };
    pushDocumentCommand(
        tr("Clear layer"),
        std::move(redoAction),
        std::move(undoAction));
}

void DocumentController::renameLayer(const QUuid &id, const QString &name)
{
    Layer *layer = m_document.layer(id);
    const QString normalized = name.trimmed();
    if (!layer
        || normalized.isEmpty()
        || normalized.size() > DocumentLimits::maximumLayerNameLength
        || layer->name == normalized) {
        return;
    }
    const QString previousName = layer->name;
    auto redoAction = [this, id, normalized]() {
        if (Layer *target = m_document.layer(id)) {
            target->name = normalized;
            notifyDocumentChanged();
        }
    };
    auto undoAction = [this, id, previousName]() {
        if (Layer *target = m_document.layer(id)) {
            target->name = previousName;
            notifyDocumentChanged();
        }
    };
    pushDocumentCommand(
        tr("Rename layer"),
        std::move(redoAction),
        std::move(undoAction));
}

void DocumentController::setLayerVisible(const QUuid &id, bool visible)
{
    Layer *layer = m_document.layer(id);
    if (!layer || layer->visible == visible) {
        return;
    }
    const bool previous = layer->visible;
    auto redoAction = [this, id, visible]() {
        if (Layer *target = m_document.layer(id)) {
            target->visible = visible;
            notifyDocumentChanged();
        }
    };
    auto undoAction = [this, id, previous]() {
        if (Layer *target = m_document.layer(id)) {
            target->visible = previous;
            notifyDocumentChanged();
        }
    };
    pushDocumentCommand(
        tr("Toggle layer visibility"),
        std::move(redoAction),
        std::move(undoAction));
}

void DocumentController::setLayerOpacity(const QUuid &id, qreal opacity)
{
    Layer *layer = m_document.layer(id);
    if (!std::isfinite(opacity)) {
        return;
    }
    const qreal normalized = std::clamp(opacity, 0.0, 1.0);
    if (!layer || qFuzzyCompare(layer->opacity, normalized)) {
        return;
    }
    const qreal previous = layer->opacity;
    auto redoAction = [this, id, normalized]() {
        if (Layer *target = m_document.layer(id)) {
            target->opacity = normalized;
            notifyDocumentChanged();
        }
    };
    auto undoAction = [this, id, previous]() {
        if (Layer *target = m_document.layer(id)) {
            target->opacity = previous;
            notifyDocumentChanged();
        }
    };
    pushDocumentCommand(
        tr("Change layer opacity"),
        std::move(redoAction),
        std::move(undoAction),
        layerOpacityMergeId,
        id);
}

void DocumentController::moveLayer(const QUuid &id, int offset)
{
    const int from = m_document.layerIndex(id);
    const int to = from + offset;
    if (from < 0 || to < 0 || to >= m_document.layers.size() || from == to) {
        return;
    }
    auto move = [this](int source, int destination) {
        m_document.layers.move(source, destination);
        notifyDocumentChanged();
    };
    pushDocumentCommand(
        tr("Move layer"),
        [move, from, to]() { move(from, to); },
        [move, from, to]() { move(to, from); });
}

void DocumentController::setWobbleAmount(qreal amount)
{
    if (!std::isfinite(amount)) {
        return;
    }
    const qreal normalized = std::clamp(
        amount,
        DocumentLimits::minimumWobbleAmount,
        DocumentLimits::maximumWobbleAmount);
    if (qFuzzyCompare(m_document.wobbleAmount, normalized)) {
        return;
    }
    const qreal previous = m_document.wobbleAmount;
    auto apply = [this](qreal value) {
        m_document.wobbleAmount = value;
        notifyDocumentChanged();
    };
    pushDocumentCommand(
        tr("Change wobble"),
        [apply, normalized]() { apply(normalized); },
        [apply, previous]() { apply(previous); },
        wobbleAmountMergeId);
}

void DocumentController::setAnimationFrames(int frames)
{
    const int normalized = std::clamp(
        frames,
        DocumentLimits::minimumAnimationFrames,
        DocumentLimits::maximumAnimationFrames);
    if (m_document.animationFrames == normalized) {
        return;
    }
    const int previous = m_document.animationFrames;
    auto apply = [this](int value) {
        m_document.animationFrames = value;
        notifyDocumentChanged();
    };
    pushDocumentCommand(
        tr("Change animation frames"),
        [apply, normalized]() { apply(normalized); },
        [apply, previous]() { apply(previous); },
        animationFramesMergeId);
}

void DocumentController::setFramesPerSecond(qreal fps)
{
    if (!std::isfinite(fps)) {
        return;
    }
    const qreal normalized = std::clamp(
        fps,
        DocumentLimits::minimumFramesPerSecond,
        DocumentLimits::maximumFramesPerSecond);
    if (qFuzzyCompare(m_document.framesPerSecond, normalized)) {
        return;
    }
    const qreal previous = m_document.framesPerSecond;
    auto apply = [this](qreal value) {
        m_document.framesPerSecond = value;
        notifyDocumentChanged();
    };
    pushDocumentCommand(
        tr("Change animation speed"),
        [apply, normalized]() { apply(normalized); },
        [apply, previous]() { apply(previous); },
        framesPerSecondMergeId);
}

void DocumentController::pushDocumentCommand(
    QString text,
    std::function<void()> redoAction,
    std::function<void()> undoAction,
    int mergeId,
    const QUuid &mergeScope)
{
    const quint64 previousRevision = m_currentContentRevision;
    const quint64 nextRevision = ++m_nextContentRevision;
    auto trackedRedo = [
        this,
        redoAction = std::move(redoAction),
        nextRevision
    ]() {
        redoAction();
        setContentRevision(nextRevision);
    };
    auto trackedUndo = [
        this,
        undoAction = std::move(undoAction),
        previousRevision
    ]() {
        undoAction();
        setContentRevision(previousRevision);
    };
    m_undoStack.push(new LambdaCommand(
        std::move(text),
        std::move(trackedRedo),
        std::move(trackedUndo),
        mergeId,
        mergeScope));
}

void DocumentController::setContentRevision(quint64 revision)
{
    const bool wasModified = isModified();
    m_currentContentRevision = revision;
    const bool modified = isModified();
    if (modified != wasModified) {
        emit modifiedChanged(modified);
    }
}

void DocumentController::notifyDocumentChanged()
{
    emit documentChanged();
}

void DocumentController::ensureActiveLayer()
{
    if (m_document.layer(m_document.activeLayerId)) {
        return;
    }
    if (m_document.layers.isEmpty()) {
        Layer layer;
        layer.name = QStringLiteral("Layer 1");
        m_document.layers.append(layer);
    }
    m_document.activeLayerId = m_document.layers.constLast().id;
}

QString DocumentController::nextLayerName() const
{
    int number = m_document.layers.size() + 1;
    while (true) {
        const QString candidate = tr("Layer %1").arg(number);
        bool exists = false;
        for (const Layer &layer : m_document.layers) {
            if (layer.name == candidate) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            return candidate;
        }
        ++number;
    }
}

}

#include "document/DocumentBudget.hpp"
#include "document/DocumentController.hpp"
#include "document/DocumentLimits.hpp"
#include "document/LayerHierarchy.hpp"
#include "document/history/HistoryEffects.hpp"

#include <memory>
#include <utility>

namespace wobble
{

using DocumentBudget::distinctClipMaskBytes;
using DocumentBudget::totalPointCount;
using DocumentBudget::totalStrokeCount;
using history::layerOpacityMergeId;

namespace
{

qsizetype layerPointCount(const Layer &layer)
{
    qsizetype count = 0;
    for (const Stroke &stroke : layer.strokes)
    {
        if (stroke.points.size() > DocumentLimits::maximumTotalPoints - count)
        {
            return DocumentLimits::maximumTotalPoints + 1;
        }
        count += stroke.points.size();
    }
    return count;
}
}

void DocumentController::addLayer(const QUuid &parentGroupId)
{
    const Document &current = document();
    const Layer *requestedGroup = current.layer(parentGroupId);
    if (current.layers.size() >= DocumentLimits::maximumLayers
        || (!parentGroupId.isNull()
            && (!requestedGroup || requestedGroup->kind != LayerKind::Group)))
    {
        failHistoryMacro();
        return;
    }
    Layer layer;
    layer.name = nextLayerName();
    layer.initialCanvasSize = current.size;
    if (requestedGroup)
    {
        layer.parentGroupId = requestedGroup->id;
    }
    else if (const Layer *active = current.layer(current.activeLayerId))
    {
        layer.parentGroupId = active->parentGroupId;
    }
    const QUuid layerId = layer.id;
    Document candidate = current;
    const int activeIndex = current.layerIndex(current.activeLayerId);
    if (requestedGroup)
    {
        candidate.layers.append(std::move(layer));
    }
    else if (activeIndex >= 0)
    {
        candidate.layers.insert(activeIndex + 1, std::move(layer));
    }
    else
    {
        candidate.layers.append(std::move(layer));
    }
    candidate.activeLayerId = layerId;
    auto effects = std::make_shared<HistoryEffects>();
    effects->afterDocumentChanged.append(
        HistoryEffects::LayerThumbnail{layerId});
    effects->afterDocumentChanged.append(HistoryEffects::ActiveLayer{});
    tryCommitCandidate(tr("Add layer"),
        std::move(candidate),
        std::move(effects),
        ActiveLayerPolicy::UsePrepared);
}

void DocumentController::addLayerGroup(const QUuid &childId)
{
    const Document &current = document();
    if (current.layers.size() >= DocumentLimits::maximumLayers)
    {
        failHistoryMacro();
        return;
    }
    const Layer *child = current.layer(childId);
    Layer group;
    group.kind = LayerKind::Group;
    group.initialCanvasSize = current.size;
    group.parentGroupId = child ? child->parentGroupId : QUuid();
    int number = 1;
    do
    {
        group.name = tr("Group %1").arg(number++);
    } while (std::any_of(current.layers.cbegin(),
        current.layers.cend(),
        [&group](const Layer &layer)
        {
            return layer.name == group.name;
        }));

    Document candidate = current;
    const int childIndex = current.layerIndex(childId);
    if (childIndex >= 0)
    {
        candidate.layers.insert(childIndex + 1, group);
        candidate.layer(childId)->parentGroupId = group.id;
    }
    else
    {
        candidate.layers.append(group);
    }
    auto effects = std::make_shared<HistoryEffects>();
    effects->afterDocumentChanged.append(
        HistoryEffects::LayerThumbnailsReset{});
    tryCommitCandidate(
        tr("Add layer group"), std::move(candidate), std::move(effects));
}

void DocumentController::duplicateLayer(const QUuid &id)
{
    const Document &current = document();
    const int sourceIndex = current.layerIndex(id);
    if (sourceIndex < 0 || current.layers[sourceIndex].kind != LayerKind::Paint
        || current.layers.size() >= DocumentLimits::maximumLayers)
    {
        failHistoryMacro();
        return;
    }
    const qsizetype sourcePointCount =
        layerPointCount(current.layers[sourceIndex]);
    const qsizetype existingPointCount = totalPointCount(current);
    const qsizetype sourceStrokeCount =
        current.layers[sourceIndex].strokes.size();
    const qsizetype existingStrokeCount = totalStrokeCount(current);
    if (sourcePointCount > DocumentLimits::maximumTotalPoints
        || existingPointCount > DocumentLimits::maximumTotalPoints
        || sourcePointCount
               > DocumentLimits::maximumTotalPoints - existingPointCount
        || sourceStrokeCount > DocumentLimits::maximumTotalStrokes
        || existingStrokeCount > DocumentLimits::maximumTotalStrokes
        || sourceStrokeCount
               > DocumentLimits::maximumTotalStrokes - existingStrokeCount)
    {
        failHistoryMacro();
        return;
    }
    Layer copy = current.layers[sourceIndex];
    copy.id = QUuid::createUuid();
    copy.name = tr("%1 copy").arg(copy.name);
    if (copy.name.size() > DocumentLimits::maximumLayerNameLength)
    {
        copy.name.truncate(DocumentLimits::maximumLayerNameLength);
    }
    for (Stroke &stroke : copy.strokes)
    {
        stroke.id = QUuid::createUuid();
    }
    Document withCopy = current;
    withCopy.layers.insert(sourceIndex + 1, copy);
    withCopy.activeLayerId = copy.id;
    if (distinctClipMaskBytes(withCopy)
        > DocumentLimits::maximumDistinctClipMaskBytes)
    {
        failHistoryMacro();
        return;
    }
    auto effects = std::make_shared<HistoryEffects>();
    effects->afterDocumentChanged.append(
        HistoryEffects::LayerThumbnailsReset{});
    effects->afterDocumentChanged.append(HistoryEffects::ActiveLayer{});
    tryCommitCandidate(tr("Duplicate layer"),
        std::move(withCopy),
        std::move(effects),
        ActiveLayerPolicy::UsePrepared);
}

void DocumentController::removeLayer(const QUuid &id)
{
    const Document &current = document();
    const int index = current.layerIndex(id);
    if (index < 0)
    {
        failHistoryMacro();
        return;
    }
    Document candidate = current;
    QSet<QUuid> removedIds{id};
    if (current.layers[index].kind == LayerKind::Group)
    {
        const LayerHierarchyAnalysis hierarchy = analyzeLayerHierarchy(current);
        if (!hierarchy.isValid())
        {
            failHistoryMacro();
            return;
        }
        for (const Layer &layer : current.layers)
        {
            if (hierarchy.isDescendantOf(layer.id, id))
            {
                removedIds.insert(layer.id);
            }
        }
    }
    if (removedIds.contains(candidate.activeLayerId))
    {
        candidate.activeLayerId = QUuid();
        const auto choosePaint = [&](int begin, int end, int step)
        {
            for (int candidateIndex = begin; candidateIndex != end;
                candidateIndex += step)
            {
                const Layer &candidateLayer = current.layers[candidateIndex];
                if (!removedIds.contains(candidateLayer.id)
                    && candidateLayer.kind == LayerKind::Paint)
                {
                    candidate.activeLayerId = candidateLayer.id;
                    return true;
                }
            }
            return false;
        };
        if (!choosePaint(index - 1, -1, -1))
        {
            choosePaint(index + 1, current.layers.size(), 1);
        }
    }
    candidate.layers.removeIf(
        [&removedIds](const Layer &layer)
        {
            return removedIds.contains(layer.id);
        });
    ensureActiveLayer(candidate);
    auto effects = std::make_shared<HistoryEffects>();
    effects->afterDocumentChanged.append(
        HistoryEffects::LayerThumbnailsReset{});
    effects->afterDocumentChanged.append(HistoryEffects::ActiveLayer{});
    tryCommitCandidate(tr("Delete layer"),
        std::move(candidate),
        std::move(effects),
        ActiveLayerPolicy::UsePrepared);
}

void DocumentController::clearLayer(const QUuid &id)
{
    const Document &current = document();
    const Layer *layer = current.layer(id);
    if (!layer || layer->kind != LayerKind::Paint
        || (layer->strokes.isEmpty()
            && layer->initialCanvasSize == current.size))
    {
        failHistoryMacro();
        return;
    }
    Document candidate = current;
    Layer *target = candidate.layer(id);
    target->strokes.clear();
    target->initialCanvasSize = current.size;
    auto effects = std::make_shared<HistoryEffects>();
    effects->afterDocumentChanged.append(HistoryEffects::LayerThumbnail{id});
    tryCommitCandidate(
        tr("Clear layer"), std::move(candidate), std::move(effects));
}

DocumentController::RenameLayerResult DocumentController::renameLayer(
    const QUuid &id, const QString &name)
{
    const auto reject = [this](RenameLayerResult result)
    {
        failHistoryMacro();
        return result;
    };
    const Document &current = document();
    const Layer *layer = current.layer(id);
    const QString normalized = name.trimmed();
    if (!layer)
    {
        return reject(RenameLayerResult::RejectedInvalidLayer);
    }
    if (normalized.isEmpty())
    {
        return reject(RenameLayerResult::RejectedEmptyName);
    }
    if (normalized.size() > DocumentLimits::maximumLayerNameLength)
    {
        return reject(RenameLayerResult::RejectedNameTooLong);
    }
    if (layer->name == normalized)
    {
        return reject(RenameLayerResult::Unchanged);
    }
    Document candidate = current;
    candidate.layer(id)->name = normalized;
    return tryCommitCandidate(tr("Rename layer"), std::move(candidate))
               ? RenameLayerResult::Renamed
               : RenameLayerResult::RejectedCommit;
}

void DocumentController::setLayerVisible(const QUuid &id, bool visible)
{
    const Document &current = document();
    const Layer *layer = current.layer(id);
    if (!layer || layer->visible == visible)
    {
        failHistoryMacro();
        return;
    }
    Document candidate = current;
    candidate.layer(id)->visible = visible;
    auto effects = std::make_shared<HistoryEffects>();
    effects->afterDocumentChanged.append(
        HistoryEffects::LayerThumbnailsReset{});
    tryCommitCandidate(tr("Toggle layer visibility"),
        std::move(candidate),
        std::move(effects));
}

void DocumentController::setLayerReference(const QUuid &id, bool reference)
{
    const Document &current = document();
    const Layer *layer = current.layer(id);
    if (!layer || layer->kind != LayerKind::Paint
        || layer->reference == reference)
    {
        failHistoryMacro();
        return;
    }
    Document candidate = current;
    candidate.layer(id)->reference = reference;
    tryCommitCandidate(tr("Set reference layer"), std::move(candidate));
}

void DocumentController::setLayerOpacity(const QUuid &id, qreal opacity)
{
    const Document &current = document();
    const Layer *layer = current.layer(id);
    if (!std::isfinite(opacity))
    {
        failHistoryMacro();
        return;
    }
    const qreal normalized = std::clamp(opacity, 0.0, 1.0);
    if (!layer || qFuzzyCompare(layer->opacity, normalized))
    {
        failHistoryMacro();
        return;
    }
    Document candidate = current;
    candidate.layer(id)->opacity = normalized;
    auto effects = std::make_shared<HistoryEffects>();
    effects->afterDocumentChanged.append(
        HistoryEffects::LayerThumbnailsReset{});
    tryCommitCandidate(tr("Change layer opacity"),
        std::move(candidate),
        std::move(effects),
        ActiveLayerPolicy::PreserveCurrentIfPresent,
        layerOpacityMergeId,
        id);
}

void DocumentController::setLayerBlendMode(const QUuid &id, LayerBlendMode mode)
{
    const Document &current = document();
    const Layer *layer = current.layer(id);
    if (!layer || !isValidLayerBlendMode(mode) || layer->blendMode == mode)
    {
        failHistoryMacro();
        return;
    }
    Document candidate = current;
    candidate.layer(id)->blendMode = mode;
    auto effects = std::make_shared<HistoryEffects>();
    effects->afterDocumentChanged.append(
        HistoryEffects::LayerThumbnailsReset{});
    tryCommitCandidate(tr("Change layer blend mode"),
        std::move(candidate),
        std::move(effects));
}

void DocumentController::setLayerClipToBelow(const QUuid &id, bool clipped)
{
    const Document &current = document();
    const Layer *layer = current.layer(id);
    if (!layer || layer->kind != LayerKind::Paint
        || layer->clipToLayerBelow == clipped)
    {
        failHistoryMacro();
        return;
    }
    Document candidate = current;
    candidate.layer(id)->clipToLayerBelow = clipped;
    auto effects = std::make_shared<HistoryEffects>();
    effects->afterDocumentChanged.append(
        HistoryEffects::LayerThumbnailsReset{});
    tryCommitCandidate(
        tr("Change layer clipping"), std::move(candidate), std::move(effects));
}

void DocumentController::setLayerParentGroup(
    const QUuid &id, const QUuid &groupId)
{
    const Document &current = document();
    const Layer *layer = current.layer(id);
    const Layer *group = current.layer(groupId);
    const bool validParent =
        groupId.isNull() || (group && group->kind == LayerKind::Group);
    if (!layer || !validParent || layer->parentGroupId == groupId
        || id == groupId)
    {
        failHistoryMacro();
        return;
    }
    Document candidate = current;
    candidate.layer(id)->parentGroupId = groupId;
    auto effects = std::make_shared<HistoryEffects>();
    effects->afterDocumentChanged.append(
        HistoryEffects::LayerThumbnailsReset{});
    tryCommitCandidate(
        tr("Move layer into group"), std::move(candidate), std::move(effects));
}

void DocumentController::moveLayer(const QUuid &id, int offset)
{
    const Document &current = document();
    const int from = current.layerIndex(id);
    const Layer *layer = current.layer(id);
    if (from < 0 || !layer || offset == 0)
    {
        failHistoryMacro();
        return;
    }
    QVector<int> siblingIndexes;
    for (int index = 0; index < current.layers.size(); ++index)
    {
        if (current.layers[index].parentGroupId == layer->parentGroupId)
        {
            siblingIndexes.append(index);
        }
    }
    const int siblingPosition = siblingIndexes.indexOf(from);
    const int targetPosition = siblingPosition + offset;
    if (siblingPosition < 0 || targetPosition < 0
        || targetPosition >= siblingIndexes.size())
    {
        failHistoryMacro();
        return;
    }
    const int to = siblingIndexes[targetPosition];
    Document candidate = current;
    candidate.layers.move(from, to);
    auto effects = std::make_shared<HistoryEffects>();
    effects->afterDocumentChanged.append(
        HistoryEffects::LayerThumbnailsReset{});
    tryCommitCandidate(
        tr("Move layer"), std::move(candidate), std::move(effects));
}

}

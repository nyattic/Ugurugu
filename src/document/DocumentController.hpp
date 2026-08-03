#pragma once

#include "app/MemoryBudget.hpp"
#include "document/Document.hpp"
#include "document/history/HistoryTypes.hpp"
#include "io/DocumentSerializer.hpp"

#include <QObject>
#include <QTransform>

#include <memory>

class QAction;
class QUndoCommand;

namespace wobble
{

class DocumentController;
class DocumentControllerTestAccess;
class MainWindowTestAccess;

namespace history
{
struct HistoryEffects;
}

// Logical history prepares a complete target state before moving its cursor,
// so a failed restore cannot partially apply a document transaction or its
// UI side effects. Entries are bounded independently by count and resident
// payload bytes.
class DocumentUndoStack final : public QObject
{
    Q_OBJECT

public:
    using StorageStats = history::StorageStats;

    static constexpr qint64 maximumResidentBytes =
        MemoryBudget::historyResidentBytes;

    explicit DocumentUndoStack(DocumentController *owner);
    ~DocumentUndoStack() override;

    bool canUndo() const;
    bool canRedo() const;
    bool isClean() const;
    int count() const;
    int index() const;
    int undoLimit() const;
    void setUndoLimit(int limit);
    void setClean();
    void clear();
    void undo();
    void redo();
    void beginMacro(const QString &text);
    void endMacro();
    QAction *createUndoAction(QObject *parent);
    QAction *createRedoAction(QObject *parent);
    StorageStats storageStats() const;

private:
    struct Impl;

    void push(QUndoCommand *command);
    void updateActions();
    void enforceLimits();
    void failOpenMacro();
    bool hasOpenMacro() const;

    DocumentController *m_owner = nullptr;
    std::unique_ptr<Impl> m_impl;
    bool m_moving = false;
    qint64 m_maximumResidentBytes = maximumResidentBytes;

    friend class DocumentController;
    friend class DocumentControllerTestAccess;
};

class DocumentController final : public QObject
{
    Q_OBJECT

public:
    enum class AddStrokeResult
    {
        Added,
        AddedWithResampledPoints,
        RejectedInvalidLayer,
        RejectedStrokeLimit,
        RejectedPointLimit,
        RejectedInvalidStroke,
        RejectedMaskLimit,
        RejectedCommit
    };

    enum class RenameLayerResult
    {
        Renamed,
        Unchanged,
        RejectedInvalidLayer,
        RejectedEmptyName,
        RejectedNameTooLong,
        RejectedCommit
    };

    explicit DocumentController(QObject *parent = nullptr);
    ~DocumentController() override;

    const Document &document() const;
    DocumentUndoStack *undoStack();
    bool isModified() const;
    void releaseTransientCaches();
    bool selectionHasVisibleLayerPixels(const QUuid &layerId,
        const QImage &selectionMask,
        int preferredFrame = 0) const;
    void cacheSelectionVisibility(
        const QUuid &layerId, const QImage &selectionMask, bool visible);
    void pushSelectionStateCommand(const QString &text,
        const QUuid &beforeLayerId,
        const QImage &beforeMask,
        const QUuid &afterLayerId,
        const QImage &afterMask);

    bool newDocument(const QSize &size, QString *error = nullptr);
    bool loadDocument(Document document, QString *error = nullptr);
    bool loadRecoveredDocument(Document document, QString *error = nullptr);
    bool saveDocument(const QString &filePath, QString *error = nullptr);
    QByteArray serializeDocument(
        const QJsonObject &additionalRootFields, QString *error = nullptr);
    std::optional<DocumentSerializer::PreparedDocument> serializationSnapshot(
        QString *error = nullptr) const;
    void markSaved();
    bool resizeImage(const QSize &size);
    bool resizeCanvas(const QSize &size, const QPoint &contentOffset);

    void setActiveLayer(const QUuid &id);
    AddStrokeResult addStroke(const QUuid &layerId, Stroke stroke);
    bool moveStrokes(const QUuid &layerId,
        const QVector<QUuid> &strokeIds,
        const QPointF &delta,
        const QImage &selectionMask = {});
    bool scaleStrokes(const QUuid &layerId,
        const QVector<QUuid> &strokeIds,
        const QPointF &center,
        qreal factor,
        const QImage &selectionMask = {});
    bool rotateStrokes(const QUuid &layerId,
        const QVector<QUuid> &strokeIds,
        const QPointF &center,
        qreal degrees,
        const QImage &selectionMask = {});
    bool flipStrokes(const QUuid &layerId,
        const QVector<QUuid> &strokeIds,
        const QPointF &center,
        bool horizontal,
        const QImage &selectionMask = {});
    // Commits an already accumulated affine floating-selection transform as
    // one undoable document operation.
    bool transformSelection(const QUuid &layerId,
        const QVector<QUuid> &strokeIds,
        const QTransform &transform,
        const QImage &selectionMask = {});
    bool duplicateStrokes(const QUuid &layerId,
        const QVector<QUuid> &strokeIds,
        const QPointF &delta,
        const QImage &selectionMask = {});
    bool removeSelectedContent(const QUuid &layerId,
        const QVector<QUuid> &strokeIds,
        const QImage &selectionMask);
    bool updateStrokeAttributes(const QUuid &layerId,
        const QVector<QUuid> &strokeIds,
        const std::optional<QColor> &color,
        const std::optional<qreal> &width,
        const std::optional<qreal> &roughness);
    void removeStrokes(const QUuid &layerId, const QVector<QUuid> &strokeIds);
    void addLayer(const QUuid &parentGroupId = {});
    void addLayerGroup(const QUuid &childId = {});
    void duplicateLayer(const QUuid &id);
    void removeLayer(const QUuid &id);
    void clearLayer(const QUuid &id);
    RenameLayerResult renameLayer(const QUuid &id, const QString &name);
    void setLayerVisible(const QUuid &id, bool visible);
    void setLayerReference(const QUuid &id, bool reference);
    void setLayerOpacity(const QUuid &id, qreal opacity);
    void setLayerBlendMode(const QUuid &id, LayerBlendMode mode);
    void setLayerClipToBelow(const QUuid &id, bool clipped);
    void setLayerParentGroup(const QUuid &id, const QUuid &groupId);
    void moveLayer(const QUuid &id, int offset);
    void setWobbleAmount(qreal amount);
    void setAnimationFrames(int frames);
    void setFramesPerSecond(qreal fps);

signals:
    void documentReplaced();
    void documentChanged();
    void activeLayerChanged(const QUuid &id);
    void modifiedChanged(bool modified);
    void layerThumbnailChanged(const QUuid &id);
    void layerThumbnailsReset();
    void canvasResized(const QSize &previousSize,
        const QSize &currentSize,
        const QTransform &transform);
    void strokesTransformed(const QUuid &layerId,
        const QVector<QUuid> &strokeIds,
        const QTransform &transform);
    void strokesDuplicated(const QUuid &layerId,
        const QVector<QUuid> &sourceIds,
        const QVector<QUuid> &duplicateIds,
        const QPointF &delta,
        bool duplicated);
    // Exact UI selection transition emitted by selection-aware document
    // commands. Undo emits the same transition with both sides swapped.
    void selectionOverlayTransition(const QUuid &layerId,
        const QVector<QUuid> &fromStrokeIds,
        const QVector<QUuid> &toStrokeIds,
        const QImage &fromMask,
        const QImage &toMask);
    void strokePresenceChanged(const QUuid &layerId,
        const QUuid &strokeId,
        const QImage &clipMask,
        bool present);
    void selectionHistoryStateRequested(
        const QUuid &layerId, const QImage &mask);

private:
    using HistoryEffects = history::HistoryEffects;

    struct MacroTransaction;
    class DocumentCommand;
    class TransientCommand;

    enum class CommitDirection
    {
        Forward,
        Reverse
    };

    enum class ActiveLayerPolicy
    {
        PreserveCurrentIfPresent,
        UsePrepared
    };

    enum class DocumentReplacementDisposition
    {
        Clean,
        Recovered
    };

    using PreparedDocument = DocumentSerializer::PreparedDocument;
    using PreparedState = std::shared_ptr<const PreparedDocument>;

    bool transformStrokes(const QUuid &layerId,
        const QVector<QUuid> &strokeIds,
        const QTransform &transform,
        qreal widthScale,
        const QString &text,
        const QImage &selectionMask);
    bool tryCommitCandidate(QString text,
        Document candidate,
        std::shared_ptr<const HistoryEffects> effects = {},
        ActiveLayerPolicy activeLayerPolicy =
            ActiveLayerPolicy::PreserveCurrentIfPresent,
        int mergeId = -1,
        const QUuid &mergeScope = {});
    bool tryCommitPreparedCandidate(QString text,
        const PreparedState &before,
        PreparedState after,
        std::shared_ptr<const HistoryEffects> effects,
        ActiveLayerPolicy activeLayerPolicy,
        int mergeId,
        const QUuid &mergeScope,
        const QUuid &appendedStrokeLayerId = {});
    bool replaceDocument(Document document,
        DocumentReplacementDisposition disposition,
        QString *error);
    PreparedState prepareState(Document document,
        const PreparedDocument *base = nullptr,
        const DocumentSerializer::ImmutableBackingLease *trusted = nullptr,
        bool historyPreflight = false,
        QString *error = nullptr);
    void applyPreparedState(const PreparedState &state,
        ActiveLayerPolicy activeLayerPolicy,
        const HistoryEffects &effects,
        CommitDirection direction,
        quint64 historyNode,
        quint64 contentRevision);
    qsizetype macroPreparedDocumentCount() const;
    bool preflightHistoryMovement(const QUndoCommand *command, bool forward);
    void applyHistoryMovement(QUndoCommand *command, bool forward);
    void clearHistoryPreflight(const QUndoCommand *command);
    DocumentUndoStack::StorageStats historyStorageStats(
        const QUndoCommand *command) const;
    void notifyDocumentChanged();
    void dispatchHistoryEffects(const HistoryEffects &effects,
        CommitDirection direction,
        bool documentChanged);
    void beginHistoryMacro(const QString &text);
    void endHistoryMacro();
    void failHistoryMacro();
    bool rejectHistoryMutation();
    bool hasOpenHistoryMacro() const;
    const PreparedState &editableState() const;
    void normalizeMergedNoOp(quint64 historyNode, quint64 contentRevision);
    static void ensureActiveLayer(Document &document);
    QString nextLayerName() const;

    DocumentSerializer::SerializationCache m_serializationCache;
    PreparedState m_currentState;
    DocumentUndoStack m_undoStack;
    quint64 m_currentHistoryNode = 0;
    quint64 m_nextHistoryNode = 0;
    quint64 m_currentContentRevision = 0;
    quint64 m_savedContentRevision = 0;
    quint64 m_nextContentRevision = 0;
    mutable bool m_selectionVisibilityCacheValid = false;
    mutable QUuid m_selectionVisibilityLayerId;
    mutable qint64 m_selectionVisibilityMaskKey = 0;
    mutable bool m_selectionVisibilityCacheResult = false;
    bool m_documentReplacementInProgress = false;
    bool m_failNextDocumentReplacementPreparationForTesting = false;
    int m_historyPrepareFailureCountdownForTesting = -1;
    std::unique_ptr<MacroTransaction> m_macroTransaction;

    friend class DocumentUndoStack;
    friend class DocumentControllerTestAccess;
    friend class MainWindowTestAccess;
};

}

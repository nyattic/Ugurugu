#pragma once

#include "document/Document.hpp"

#include <QObject>
#include <QUndoStack>

namespace wobble {

class DocumentController final : public QObject
{
    Q_OBJECT

public:
    explicit DocumentController(QObject *parent = nullptr);

    const Document &document() const;
    QUndoStack *undoStack();
    bool isModified() const;

    void newDocument(const QSize &size);
    void loadDocument(Document document);
    void markSaved();

    void setActiveLayer(const QUuid &id);
    void addStroke(const QUuid &layerId, Stroke stroke);
    void translateStrokes(
        const QUuid &layerId,
        const QVector<QUuid> &strokeIds,
        const QPointF &delta);
    void removeStrokes(
        const QUuid &layerId,
        const QVector<QUuid> &strokeIds);
    void addLayer();
    void duplicateLayer(const QUuid &id);
    void removeLayer(const QUuid &id);
    void clearLayer(const QUuid &id);
    void renameLayer(const QUuid &id, const QString &name);
    void setLayerVisible(const QUuid &id, bool visible);
    void setLayerOpacity(const QUuid &id, qreal opacity);
    void moveLayer(const QUuid &id, int offset);
    void setWobbleAmount(qreal amount);
    void setAnimationFrames(int frames);
    void setFramesPerSecond(qreal fps);

signals:
    void documentChanged();
    void activeLayerChanged(const QUuid &id);
    void modifiedChanged(bool modified);

private:
    void notifyDocumentChanged();
    void ensureActiveLayer();
    QString nextLayerName() const;

    Document m_document;
    QUndoStack m_undoStack;
};

}

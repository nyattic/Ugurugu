#pragma once

#include <QDockWidget>
#include <QHash>
#include <QPixmap>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QUuid>

class QLabel;
class QListWidgetItem;
class QSlider;
class QToolButton;

namespace wobble {

class DocumentController;
class LayerListWidget;

class LayerDock final : public QDockWidget
{
    Q_OBJECT

public:
    explicit LayerDock(
        DocumentController *controller,
        QWidget *parent = nullptr);

private:
    void buildContent();
    void connectControls();
    void rebuild();
    void syncActiveLayer(const QUuid &id);
    void updateControls();
    void scheduleAllThumbnails();
    void scheduleLayerThumbnail(const QUuid &id);
    void regenerateThumbnails();
    void commitOpacity(const QUuid &id, int value);
    void handleReorder(int sourceRow, int insertRow);
    QUuid selectedLayerId() const;

    QPointer<DocumentController> m_controller;
    LayerListWidget *m_layerList = nullptr;
    QToolButton *m_addButton = nullptr;
    QToolButton *m_duplicateButton = nullptr;
    QToolButton *m_deleteButton = nullptr;
    QToolButton *m_moveUpButton = nullptr;
    QToolButton *m_moveDownButton = nullptr;
    QSlider *m_opacitySlider = nullptr;
    QLabel *m_opacityValue = nullptr;
    QTimer m_thumbnailTimer;
    QHash<QUuid, QPixmap> m_thumbnails;
    QSet<QUuid> m_pendingThumbnails;
    bool m_regenerateAllThumbnails = false;
    bool m_syncing = false;
    bool m_opacityDragging = false;
    QUuid m_opacityLayerId;
};

}

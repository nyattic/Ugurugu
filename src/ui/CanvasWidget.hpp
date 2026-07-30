#pragma once

#include "document/DocumentController.hpp"

#include <QCache>
#include <QColor>
#include <QHash>
#include <QImage>
#include <QPainterPath>
#include <QSet>
#include <QTimer>
#include <QWidget>

class QMouseEvent;
class QTabletEvent;
class QWheelEvent;

namespace wobble {

class CanvasWidget final : public QWidget
{
    Q_OBJECT

public:
    enum class Tool {
        Brush,
        Eraser,
        Lasso,
        Wand,
        Bucket
    };

    explicit CanvasWidget(
        DocumentController *controller,
        QWidget *parent = nullptr);

    Tool tool() const;
    QColor brushColor() const;
    qreal brushWidth() const;
    QString brushPresetId() const;
    bool isAnimating() const;
    int currentFrame() const;
    qreal zoom() const;
    bool hasSelection() const;

public slots:
    void setTool(Tool tool);
    void setBrushColor(const QColor &color);
    void setBrushWidth(qreal width);
    void setBrushPreset(const QString &presetId);
    void setAnimating(bool animating);
    void toggleAnimating();
    void setAnimateWhileDrawing(bool animate);
    void fitToWindow();
    void setCurrentFrame(int frame);
    void setPanModifierActive(bool active);

signals:
    void toolChanged(Tool tool);
    void brushColorChanged(const QColor &color);
    void brushWidthChanged(qreal width);
    void brushPresetChanged(const QString &presetId);
    void animatingChanged(bool animating);
    void currentFrameChanged(int frame);
    void zoomChanged(int percent);
    void pointerPositionChanged(const QPointF &position, bool inside);
    void interactionMessage(const QString &message);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void tabletEvent(QTabletEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    struct SelectionState {
        QSet<QUuid> strokes;
        QUuid layer;
        QImage mask;
    };

    QTransform documentTransform() const;
    QPointF mapToDocument(const QPointF &widgetPosition, bool *inside = nullptr) const;
    QPointF clampedDocumentPosition(const QPointF &position) const;
    QImage frameImage(int frame);
    void invalidateFrames();
    void updateTimerInterval();
    void advanceFrame();
    void beginStroke(
        const QPointF &widgetPosition,
        qreal pressure,
        bool tabletEraser);
    void continueStroke(const QPointF &widgetPosition, qreal pressure);
    void endStroke(const QPointF &widgetPosition, qreal pressure);
    void cancelStroke();
    void beginPan(const QPointF &widgetPosition);
    void continuePan(const QPointF &widgetPosition);
    void endPan();
    void updatePointerPosition(const QPointF &widgetPosition);
    void updateCursor();
    void notifyZoomChanged();
    bool selectionContains(const QPointF &documentPosition) const;
    void beginLasso(const QPointF &documentPosition);
    void finishLasso();
    void cancelLasso();
    void applySelectionMask(
        QImage mask,
        const SelectionState &previousSelection);
    SelectionState selectionStateForMask(QImage mask) const;
    SelectionState currentSelectionState() const;
    void restoreSelectionState(const SelectionState &state);
    void pushSelectionChange(
        const SelectionState &previousSelection,
        const SelectionState &nextSelection,
        const QString &text);
    void computeWandSelection(const QPointF &documentPosition);
    void applyBucketFill(const QPointF &documentPosition);
    void beginSelectionMove(const QPointF &documentPosition);
    void continueSelectionMove(const QPointF &documentPosition);
    void commitSelectionMove();
    void clearSelection();
    void pruneSelection();
    void translateSelectionOverlay(
        const QUuid &layerId,
        const QVector<QUuid> &strokeIds,
        const QPointF &delta);
    void rebuildSelectionOutline();
    void updateSelectionAnimation();
    QPointF clampedSelectionDelta(const QPointF &delta) const;
    QImage renderActiveLayerImage() const;
    void drawSelectionOverlay(QPainter &painter, const QTransform &transform);

    DocumentController *m_controller;
    Tool m_tool = Tool::Brush;
    QColor m_brushColor = Qt::black;
    qreal m_brushWidth = 6.0;
    QString m_brushPresetId;
    BrushSettings m_brushSettings;
    QHash<QString, qreal> m_presetWidths;
    bool m_animating = true;
    bool m_animateWhileDrawing = false;
    int m_currentFrame = 0;
    qreal m_zoom = 1.0;
    QPointF m_pan;
    QCache<int, QImage> m_frameCache;
    QTimer m_animationTimer;
    QTimer m_selectionAnimationTimer;
    Stroke m_activeStroke;
    QUuid m_activeStrokeLayer;
    bool m_drawing = false;
    bool m_panning = false;
    bool m_spacePressed = false;
    bool m_tabletSequence = false;
    QPointF m_lastPanPosition;
    QPointF m_pointerWidgetPosition;
    bool m_pointerInside = false;
    bool m_pointerOverWidget = false;
    QVector<QPointF> m_lassoPoints;
    bool m_lassoActive = false;
    SelectionState m_selectionBeforeLasso;
    bool m_hasSelectionBeforeLasso = false;
    QSet<QUuid> m_selectedStrokes;
    QUuid m_selectionLayer;
    QImage m_selectionMask;
    QPainterPath m_selectionOutline;
    qreal m_selectionDashOffset = 0.0;
    bool m_movingSelection = false;
    QPointF m_moveStartPosition;
    QPointF m_moveDelta;
};

}

#include "ui/LayerListWidget.hpp"

#include <QDropEvent>
#include <QKeyEvent>

namespace wobble
{

LayerListWidget::LayerListWidget(QWidget *parent)
    : QListWidget(parent)
{
    setSelectionMode(QAbstractItemView::SingleSelection);
    setDragDropMode(QAbstractItemView::InternalMove);
    setDefaultDropAction(Qt::MoveAction);
    setDropIndicatorShown(true);
}

void LayerListWidget::dropEvent(QDropEvent *event)
{
    if (event->source() != this)
    {
        event->ignore();
        return;
    }

    const QModelIndex target = indexAt(event->position().toPoint());
    int insertRow = target.isValid() ? target.row() : count();
    switch (dropIndicatorPosition())
    {
    case QAbstractItemView::BelowItem:
        insertRow += 1;
        break;
    case QAbstractItemView::OnViewport:
        insertRow = count();
        break;
    default:
        break;
    }

    event->setDropAction(Qt::IgnoreAction);
    event->accept();
    emit reorderRequested(currentRow(), insertRow);
}

void LayerListWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && event->modifiers() == Qt::NoModifier
        && !event->isAutoRepeat() && currentIndex().isValid())
    {
        emit visibilityToggleRequested(currentIndex());
        event->accept();
        return;
    }
    QListWidget::keyPressEvent(event);
}

}

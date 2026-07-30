#include "ui/LayerListWidget.hpp"

#include <QDropEvent>

namespace wobble {

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
    if (event->source() != this) {
        event->ignore();
        return;
    }

    const QModelIndex target = indexAt(event->position().toPoint());
    int insertRow = target.isValid() ? target.row() : count();
    switch (dropIndicatorPosition()) {
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

}

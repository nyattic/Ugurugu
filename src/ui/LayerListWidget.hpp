#pragma once

#include <QListWidget>

namespace wobble
{

class LayerListWidget final : public QListWidget
{
    Q_OBJECT

public:
    explicit LayerListWidget(QWidget *parent = nullptr);

signals:
    void reorderRequested(int sourceRow, int insertRow);
    void visibilityToggleRequested(const QModelIndex &index);

protected:
    void dropEvent(QDropEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
};

}

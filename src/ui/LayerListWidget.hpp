#pragma once

#include <QListWidget>

namespace ugurugu
{

class LayerListWidget final : public QListWidget
{
    Q_OBJECT

public:
    enum class DropPlacement
    {
        AboveTarget,
        BelowTarget,
        OnTarget,
        OnViewport,
    };
    Q_ENUM(DropPlacement)

    explicit LayerListWidget(QWidget *parent = nullptr);

signals:
    void dropRequested(int sourceRow, int targetRow, DropPlacement placement);
    void visibilityToggleRequested(const QModelIndex &index);

protected:
    void dropEvent(QDropEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
};

}

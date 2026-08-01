#pragma once

#include "ui/CanvasWidget.hpp"
#include "ui/PopoverOptionButton.hpp"

namespace wobble
{

class SelectionShapeButton final : public PopoverOptionButton
{
    Q_OBJECT

public:
    SelectionShapeButton(CanvasWidget::SelectionShape shape,
        QString title,
        QString description,
        QWidget *parent = nullptr);

    CanvasWidget::SelectionShape shape() const;

protected:
    void paintPreview(QPainter &painter, const QRectF &bounds) const override;

private:
    CanvasWidget::SelectionShape m_shape;
};

}

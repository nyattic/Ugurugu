#pragma once

#include "ui/CanvasTypes.hpp"
#include "ui/PopoverOptionButton.hpp"

namespace wobble
{

class SelectionShapeButton final : public PopoverOptionButton
{
    Q_OBJECT

public:
    SelectionShapeButton(CanvasSelectionShape shape,
        QString title,
        QString description,
        QWidget *parent = nullptr);

    CanvasSelectionShape shape() const;

protected:
    void paintPreview(QPainter &painter, const QRectF &bounds) const override;

private:
    CanvasSelectionShape m_shape;
};

}

#pragma once

#include "ui/CanvasWidget.hpp"
#include "ui/PopoverOptionButton.hpp"

namespace wobble
{

class WandReferenceButton final : public PopoverOptionButton
{
    Q_OBJECT

public:
    WandReferenceButton(CanvasWidget::WandReference reference,
        QString title,
        QString description,
        QWidget *parent = nullptr);

    CanvasWidget::WandReference reference() const;

protected:
    void paintPreview(QPainter &painter, const QRectF &bounds) const override;

private:
    CanvasWidget::WandReference m_reference;
};

}

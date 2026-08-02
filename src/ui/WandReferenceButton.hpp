#pragma once

#include "ui/CanvasTypes.hpp"
#include "ui/PopoverOptionButton.hpp"

namespace wobble
{

class WandReferenceButton final : public PopoverOptionButton
{
    Q_OBJECT

public:
    WandReferenceButton(CanvasWandReference reference,
        QString title,
        QString description,
        QWidget *parent = nullptr);

    CanvasWandReference reference() const;

protected:
    void paintPreview(QPainter &painter, const QRectF &bounds) const override;

private:
    CanvasWandReference m_reference;
};

}

#pragma once

#include <QAbstractButton>

class QPainter;

namespace ugurugu
{

class PopoverOptionButton : public QAbstractButton
{
public:
    PopoverOptionButton(
        QString title, QString description, QWidget *parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) final;
    virtual void paintPreview(
        QPainter &painter, const QRectF &bounds) const = 0;

private:
    QString m_title;
    QString m_description;
};

}

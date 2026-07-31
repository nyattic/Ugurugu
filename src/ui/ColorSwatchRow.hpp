#pragma once

#include <QColor>
#include <QVector>
#include <QWidget>

class QToolButton;

namespace wobble {

class ColorSwatchRow final : public QWidget
{
    Q_OBJECT

public:
    explicit ColorSwatchRow(QWidget *parent = nullptr);

    void setActiveColor(const QColor &color);

signals:
    void colorSelected(const QColor &color);

private:
    void refreshButtons();
    void persist() const;

    QVector<QColor> m_recentColors;
    QVector<QToolButton *> m_buttons;
    QColor m_activeColor;
};

}

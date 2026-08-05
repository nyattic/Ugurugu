#include "ui/ResponsiveGrid.hpp"

#include <QGridLayout>
#include <QMargins>
#include <QResizeEvent>
#include <QSizePolicy>

#include <algorithm>

namespace ugurugu
{

ResponsiveGrid::ResponsiveGrid(int minimumColumnWidth,
    int maximumColumnCount,
    int spacing,
    QWidget *parent)
    : QWidget(parent)
    , m_minimumColumnWidth(minimumColumnWidth)
    , m_maximumColumnCount(maximumColumnCount)
    , m_spacing(spacing)
{
    m_layout = new QGridLayout(this);
    m_layout->setSizeConstraint(QLayout::SetNoConstraint);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(spacing);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void ResponsiveGrid::addWidget(QWidget *widget)
{
    if (!widget || m_widgets.contains(widget))
    {
        return;
    }
    widget->setSizePolicy(
        QSizePolicy::Ignored, widget->sizePolicy().verticalPolicy());
    m_widgets.append(widget);
    m_columnCount = 0;
    relayout();
}

QSize ResponsiveGrid::minimumSizeHint() const
{
    const QMargins margins = contentsMargins();
    return QSize(m_minimumColumnWidth + margins.left() + margins.right(), 0);
}

void ResponsiveGrid::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    relayout();
}

void ResponsiveGrid::relayout()
{
    const int columns =
        std::clamp((std::max(1, contentsRect().width()) + m_spacing)
                       / (m_minimumColumnWidth + m_spacing),
            1,
            m_maximumColumnCount);
    if (columns == m_columnCount)
    {
        return;
    }
    m_columnCount = columns;
    for (QWidget *widget : m_widgets)
    {
        m_layout->removeWidget(widget);
    }
    for (int index = 0; index < m_widgets.size(); ++index)
    {
        m_layout->addWidget(
            m_widgets.at(index), index / columns, index % columns);
    }
    m_layout->activate();
    setMinimumHeight(m_layout->minimumSize().height());
    updateGeometry();
}

}

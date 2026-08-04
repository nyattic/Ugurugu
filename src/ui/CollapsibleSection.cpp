#include "ui/CollapsibleSection.hpp"

#include <QToolButton>
#include <QVBoxLayout>

namespace ugurugu
{

CollapsibleSection::CollapsibleSection(const QString &title, QWidget *parent)
    : QWidget(parent)
    , m_title(title)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_header = new QToolButton(this);
    m_header->setObjectName(QStringLiteral("sectionHeader"));
    m_header->setProperty("sectionHeader", true);
    m_header->setCheckable(true);
    m_header->setChecked(true);
    m_header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_header->setCursor(Qt::PointingHandCursor);
    layout->addWidget(m_header);

    m_body = new QWidget(this);
    m_body->setObjectName(QStringLiteral("sectionBody"));
    m_bodyLayout = new QVBoxLayout(m_body);
    m_bodyLayout->setContentsMargins(8, 6, 8, 10);
    m_bodyLayout->setSpacing(6);
    layout->addWidget(m_body);

    connect(m_header,
        &QToolButton::toggled,
        this,
        &CollapsibleSection::setExpanded);
    updateHeader();
}

void CollapsibleSection::setContentWidget(QWidget *content)
{
    if (m_content == content)
    {
        return;
    }
    if (m_content)
    {
        m_bodyLayout->removeWidget(m_content);
        m_content->setParent(nullptr);
    }
    m_content = content;
    if (m_content)
    {
        m_bodyLayout->addWidget(m_content);
        m_content->setVisible(true);
    }
}

QWidget *CollapsibleSection::contentWidget() const
{
    return m_content;
}

bool CollapsibleSection::isExpanded() const
{
    return m_expanded;
}

void CollapsibleSection::setExpanded(bool expanded)
{
    if (m_expanded == expanded)
    {
        // The header is the source of truth for its own checked state, so it
        // still needs syncing when the request came from elsewhere.
        if (m_header->isChecked() != expanded)
        {
            const QSignalBlocker blocker(m_header);
            m_header->setChecked(expanded);
        }
        return;
    }
    m_expanded = expanded;
    {
        const QSignalBlocker blocker(m_header);
        m_header->setChecked(expanded);
    }
    m_body->setVisible(expanded);
    updateHeader();
    Q_EMIT expandedChanged(expanded);
}

void CollapsibleSection::updateHeader()
{
    m_header->setText(
        (m_expanded ? QStringLiteral("▾  ") : QStringLiteral("▸  ")) + m_title);
}

}

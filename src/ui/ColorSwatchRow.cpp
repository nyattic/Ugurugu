#include "ui/ColorSwatchRow.hpp"

#include "ui/Theme.hpp"

#include <QHBoxLayout>
#include <QSettings>
#include <QStringList>
#include <QToolButton>

namespace ugurugu
{

namespace
{

constexpr int swatchCount = 6;

QVector<QColor> defaultSwatches()
{
    return {QColor(0x1A, 0x1A, 0x1A),
        QColor(0xE5, 0x48, 0x4D),
        QColor(0xFF, 0xC9, 0x4A),
        QColor(0x46, 0xA7, 0x58),
        QColor(0x00, 0x91, 0xFF),
        QColor(0x8E, 0x4E, 0xC6)};
}

}

ColorSwatchRow::ColorSwatchRow(QWidget *parent)
    : QWidget(parent)
{
    m_persistTimer.setSingleShot(true);
    m_persistTimer.setInterval(150);
    connect(&m_persistTimer,
        &QTimer::timeout,
        this,
        [this]()
        {
            persist();
        });

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(2, 0, 2, 0);
    layout->setSpacing(4);

    for (int index = 0; index < swatchCount; ++index)
    {
        auto *button = new QToolButton(this);
        button->setFixedSize(20, 20);
        button->setCursor(Qt::PointingHandCursor);
        connect(button,
            &QToolButton::clicked,
            this,
            [this, index]()
            {
                if (index < m_recentColors.size())
                {
                    emit colorSelected(m_recentColors[index]);
                }
            });
        layout->addWidget(button);
        m_buttons.append(button);
    }

    const QSettings settings;
    const QStringList stored =
        settings.value(QStringLiteral("brush/recentColors")).toStringList();
    for (const QString &name : stored)
    {
        const QColor color(name);
        if (color.isValid() && m_recentColors.size() < swatchCount)
        {
            m_recentColors.append(color);
        }
    }
    if (m_recentColors.isEmpty())
    {
        m_recentColors = defaultSwatches();
    }

    refreshButtons();
}

ColorSwatchRow::~ColorSwatchRow()
{
    if (m_persistTimer.isActive())
    {
        persist();
    }
}

void ColorSwatchRow::setActiveColor(const QColor &color)
{
    if (!color.isValid())
    {
        return;
    }
    m_activeColor = color;
    m_recentColors.removeAll(color);
    m_recentColors.prepend(color);
    while (m_recentColors.size() > swatchCount)
    {
        m_recentColors.removeLast();
    }
    refreshButtons();
    m_persistTimer.start();
}

void ColorSwatchRow::refreshButtons()
{
    for (int index = 0; index < m_buttons.size(); ++index)
    {
        QToolButton *button = m_buttons[index];
        const bool hasColor = index < m_recentColors.size();
        button->setEnabled(hasColor);
        if (!hasColor)
        {
            button->setStyleSheet(QString());
            continue;
        }
        const QColor color = m_recentColors[index];
        const bool active = color == m_activeColor;
        button->setToolTip(color.name(QColor::HexArgb));
        button->setAccessibleName(
            tr("Recent color %1").arg(color.name(QColor::HexArgb)));
        button->setStyleSheet(
            QStringLiteral("QToolButton { background: %1; border: %2; "
                           "border-radius: 5px; }"
                           "QToolButton:hover { border-color: %3; }"
                           "QToolButton:focus { border-color: %3; }")
                .arg(color.name(QColor::HexArgb),
                    active
                        ? QStringLiteral("2px solid %1")
                              .arg(Theme::accent().name())
                        : QStringLiteral("1px solid rgba(255, 255, 255, 60)"),
                    Theme::accent().name()));
    }
}

void ColorSwatchRow::persist() const
{
    QStringList names;
    names.reserve(m_recentColors.size());
    for (const QColor &color : m_recentColors)
    {
        names.append(color.name(QColor::HexArgb));
    }
    QSettings settings;
    settings.setValue(QStringLiteral("brush/recentColors"), names);
}

}

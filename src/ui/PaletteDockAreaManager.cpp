#include "ui/PaletteDockAreaManager.hpp"

#include <QCoreApplication>
#include <QDockWidget>
#include <QMainWindow>
#include <QSettings>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace ugurugu
{

namespace
{

constexpr int railWidth = 42;

bool isManagedArea(Qt::DockWidgetArea area)
{
    return area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea;
}

QString collapsedSettingKey(Qt::DockWidgetArea area)
{
    return area == Qt::LeftDockWidgetArea
               ? QStringLiteral("dock/leftAreaCollapsed")
               : QStringLiteral("dock/rightAreaCollapsed");
}

QString railObjectName(Qt::DockWidgetArea area)
{
    return area == Qt::LeftDockWidgetArea
               ? QStringLiteral("LeftPaletteRailDock")
               : QStringLiteral("RightPaletteRailDock");
}

QString expandButtonObjectName(Qt::DockWidgetArea area)
{
    return area == Qt::LeftDockWidgetArea
               ? QStringLiteral("expandLeftPaletteAreaButton")
               : QStringLiteral("expandRightPaletteAreaButton");
}

}

PaletteDockAreaManager::PaletteDockAreaManager(
    QMainWindow *window, int layoutVersion)
    : QObject(window)
    , m_window(window)
    , m_layoutVersion(layoutVersion)
{
    setObjectName(QStringLiteral("PaletteDockAreaManager"));
    m_left.rail = createRail(Qt::LeftDockWidgetArea);
    m_right.rail = createRail(Qt::RightDockWidgetArea);
    m_expandedLayoutState = m_window->saveState(m_layoutVersion);
}

void PaletteDockAreaManager::registerDock(QDockWidget *dock)
{
    if (!dock || m_docks.contains(dock))
    {
        return;
    }
    m_docks.append(dock);
    connect(dock,
        &QDockWidget::dockLocationChanged,
        this,
        [this, dock](Qt::DockWidgetArea area)
        {
            if (!isManagedArea(area) || !isCollapsed(area))
            {
                return;
            }
            AreaState &areaState = state(area);
            areaState.docks.insert(dock,
                {dock->isVisible(), dock->toggleViewAction()->isEnabled()});
            dock->toggleViewAction()->setEnabled(false);
            QTimer::singleShot(0, dock, &QDockWidget::hide);
        });
}

bool PaletteDockAreaManager::isCollapsed(Qt::DockWidgetArea area) const
{
    return isManagedArea(area) && state(area).collapsed;
}

void PaletteDockAreaManager::setCollapsed(
    Qt::DockWidgetArea area, bool collapsed)
{
    if (!isManagedArea(area) || state(area).collapsed == collapsed)
    {
        return;
    }
    if (collapsed)
    {
        captureExpandedLayoutState();
        collapseArea(area, true);
    }
    else
    {
        restoreExpandedLayout(area, true);
    }
}

void PaletteDockAreaManager::restorePersistedState()
{
    for (Qt::DockWidgetArea area :
        {Qt::LeftDockWidgetArea, Qt::RightDockWidgetArea})
    {
        if (QSettings().value(collapsedSettingKey(area), false).toBool())
        {
            collapseArea(area, false);
        }
    }
}

void PaletteDockAreaManager::suspendCollapsedAreas()
{
    for (Qt::DockWidgetArea area :
        {Qt::LeftDockWidgetArea, Qt::RightDockWidgetArea})
    {
        AreaState &areaState = state(area);
        if (!areaState.collapsed || areaState.suspended)
        {
            continue;
        }
        hideRail(area);
        for (auto iterator = areaState.docks.cbegin();
            iterator != areaState.docks.cend();
            ++iterator)
        {
            iterator.key()->toggleViewAction()->setEnabled(
                iterator.value().toggleEnabled);
            iterator.key()->setVisible(iterator.value().visible);
        }
        setAreaTabBarsVisible(area, true);
        areaState.suspended = true;
    }
}

void PaletteDockAreaManager::resumeCollapsedAreas()
{
    for (Qt::DockWidgetArea area :
        {Qt::LeftDockWidgetArea, Qt::RightDockWidgetArea})
    {
        AreaState &areaState = state(area);
        if (!areaState.collapsed || !areaState.suspended)
        {
            continue;
        }
        collapseArea(area, false);
    }
}

void PaletteDockAreaManager::resetCollapsedAreas()
{
    const bool leftCollapsed = m_left.collapsed;
    const bool rightCollapsed = m_right.collapsed;
    if (leftCollapsed)
    {
        restoreExpandedLayout(Qt::LeftDockWidgetArea, true);
    }
    if (rightCollapsed && m_right.collapsed)
    {
        restoreExpandedLayout(Qt::RightDockWidgetArea, true);
    }
    QSettings().setValue(collapsedSettingKey(Qt::LeftDockWidgetArea), false);
    QSettings().setValue(collapsedSettingKey(Qt::RightDockWidgetArea), false);
}

QByteArray PaletteDockAreaManager::layoutStateForPersistence()
{
    if (m_left.collapsed || m_right.collapsed)
    {
        suspendCollapsedAreas();
        if (!m_expandedLayoutState.isEmpty())
        {
            m_window->restoreState(m_expandedLayoutState, m_layoutVersion);
        }
        setAreaTabBarsVisible(Qt::LeftDockWidgetArea, true);
        setAreaTabBarsVisible(Qt::RightDockWidgetArea, true);
    }
    else
    {
        m_expandedLayoutState = m_window->saveState(m_layoutVersion);
    }
    return m_expandedLayoutState;
}

PaletteDockAreaManager *PaletteDockAreaManager::find(const QDockWidget *dock)
{
    if (!dock)
    {
        return nullptr;
    }
    auto *window = qobject_cast<QMainWindow *>(dock->window());
    return window ? window->findChild<PaletteDockAreaManager *>(
                        QStringLiteral("PaletteDockAreaManager"),
                        Qt::FindDirectChildrenOnly)
                  : nullptr;
}

PaletteDockAreaManager::AreaState &PaletteDockAreaManager::state(
    Qt::DockWidgetArea area)
{
    return area == Qt::LeftDockWidgetArea ? m_left : m_right;
}

const PaletteDockAreaManager::AreaState &PaletteDockAreaManager::state(
    Qt::DockWidgetArea area) const
{
    return area == Qt::LeftDockWidgetArea ? m_left : m_right;
}

QList<QDockWidget *> PaletteDockAreaManager::docksInArea(
    Qt::DockWidgetArea area) const
{
    QList<QDockWidget *> result;
    for (QDockWidget *dock : m_docks)
    {
        if (!dock->isFloating() && m_window->dockWidgetArea(dock) == area)
        {
            result.append(dock);
        }
    }
    return result;
}

void PaletteDockAreaManager::collapseArea(Qt::DockWidgetArea area, bool persist)
{
    AreaState &areaState = state(area);
    areaState.docks.clear();
    setAreaTabBarsVisible(area, false);
    for (QDockWidget *dock : docksInArea(area))
    {
        areaState.docks.insert(
            dock, {dock->isVisible(), dock->toggleViewAction()->isEnabled()});
        dock->toggleViewAction()->setEnabled(false);
        dock->hide();
    }
    areaState.collapsed = true;
    areaState.suspended = false;
    if (persist)
    {
        QSettings().setValue(collapsedSettingKey(area), true);
    }
    showRail(area);
    QTimer::singleShot(0,
        this,
        [this, area]()
        {
            if (isCollapsed(area) && !state(area).suspended)
            {
                setAreaTabBarsVisible(area, false);
            }
        });
}

void PaletteDockAreaManager::restoreExpandedLayout(
    Qt::DockWidgetArea expandedArea, bool persist)
{
    const bool keepLeftCollapsed =
        m_left.collapsed && expandedArea != Qt::LeftDockWidgetArea;
    const bool keepRightCollapsed =
        m_right.collapsed && expandedArea != Qt::RightDockWidgetArea;
    QDockWidget *leftRail = m_left.rail;
    QDockWidget *rightRail = m_right.rail;

    suspendCollapsedAreas();
    hideRail(Qt::LeftDockWidgetArea);
    hideRail(Qt::RightDockWidgetArea);
    m_left = {.rail = leftRail};
    m_right = {.rail = rightRail};
    if (!m_expandedLayoutState.isEmpty())
    {
        m_window->restoreState(m_expandedLayoutState, m_layoutVersion);
    }
    setAreaTabBarsVisible(Qt::LeftDockWidgetArea, true);
    setAreaTabBarsVisible(Qt::RightDockWidgetArea, true);
    if (persist)
    {
        QSettings().setValue(collapsedSettingKey(expandedArea), false);
    }
    if (keepLeftCollapsed)
    {
        collapseArea(Qt::LeftDockWidgetArea, false);
    }
    if (keepRightCollapsed)
    {
        collapseArea(Qt::RightDockWidgetArea, false);
    }
}

void PaletteDockAreaManager::showRail(Qt::DockWidgetArea area)
{
    AreaState &areaState = state(area);
    m_window->addDockWidget(area, areaState.rail);
    areaState.rail->show();
    m_window->resizeDocks({areaState.rail}, {railWidth}, Qt::Horizontal);
}

void PaletteDockAreaManager::hideRail(Qt::DockWidgetArea area)
{
    AreaState &areaState = state(area);
    areaState.rail->hide();
    m_window->removeDockWidget(areaState.rail);
}

void PaletteDockAreaManager::setAreaTabBarsVisible(
    Qt::DockWidgetArea area, bool visible)
{
    AreaState &areaState = state(area);
    if (visible)
    {
        for (QTabBar *tabBar : areaState.tabBars)
        {
            if (tabBar)
            {
                tabBar->show();
            }
        }
        return;
    }

    const QList<QDockWidget *> areaDocks = docksInArea(area);
    if (areaState.tabBars.isEmpty())
    {
        for (QTabBar *tabBar : m_window->findChildren<QTabBar *>())
        {
            bool belongsToArea = false;
            for (int index = 0; index < tabBar->count() && !belongsToArea;
                ++index)
            {
                for (const QDockWidget *dock : areaDocks)
                {
                    if (tabBar->tabText(index) == dock->windowTitle())
                    {
                        belongsToArea = true;
                        break;
                    }
                }
            }
            const int tabCenter =
                tabBar->mapTo(m_window, tabBar->rect().center()).x();
            const bool onAreaSide = area == Qt::LeftDockWidgetArea
                                        ? tabCenter < m_window->width() / 2
                                        : tabCenter > m_window->width() / 2;
            if (belongsToArea || onAreaSide)
            {
                areaState.tabBars.append(tabBar);
            }
        }
    }
    for (QTabBar *tabBar : areaState.tabBars)
    {
        if (tabBar)
        {
            tabBar->hide();
        }
    }
}

void PaletteDockAreaManager::captureExpandedLayoutState()
{
    if (!m_left.collapsed && !m_right.collapsed)
    {
        m_expandedLayoutState = m_window->saveState(m_layoutVersion);
        return;
    }
    const bool resumeLeft = m_left.collapsed && !m_left.suspended;
    const bool resumeRight = m_right.collapsed && !m_right.suspended;
    layoutStateForPersistence();
    m_expandedLayoutState = m_window->saveState(m_layoutVersion);
    if (resumeLeft || resumeRight)
    {
        resumeCollapsedAreas();
    }
}

QDockWidget *PaletteDockAreaManager::createRail(Qt::DockWidgetArea area)
{
    auto *rail = new QDockWidget(m_window);
    rail->setObjectName(railObjectName(area));
    rail->setAllowedAreas(area);
    rail->setFeatures(QDockWidget::NoDockWidgetFeatures);
    rail->setMinimumWidth(railWidth);
    rail->setMaximumWidth(railWidth);
    auto *titleBar = new QWidget(rail);
    titleBar->setFixedHeight(0);
    rail->setTitleBarWidget(titleBar);

    auto *body = new QWidget(rail);
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(5, 8, 5, 8);
    auto *expandButton = new QToolButton(body);
    expandButton->setObjectName(expandButtonObjectName(area));
    expandButton->setText(area == Qt::LeftDockWidgetArea ? QStringLiteral("›")
                                                         : QStringLiteral("‹"));
    expandButton->setFixedSize(30, 30);
    const QString label =
        QCoreApplication::translate("PaletteDockTitleBar", "Expand panel dock");
    expandButton->setToolTip(label);
    expandButton->setAccessibleName(label);
    layout->addWidget(expandButton, 0, Qt::AlignTop | Qt::AlignHCenter);
    layout->addStretch(1);
    rail->setWidget(body);
    connect(expandButton,
        &QToolButton::clicked,
        this,
        [this, area]()
        {
            setCollapsed(area, false);
        });
    return rail;
}

}

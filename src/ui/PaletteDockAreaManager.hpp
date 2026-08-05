#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>

class QDockWidget;
class QMainWindow;
class QTabBar;

namespace ugurugu
{

class PaletteDockAreaManager final : public QObject
{
    Q_OBJECT

public:
    explicit PaletteDockAreaManager(QMainWindow *window, int layoutVersion);

    void registerDock(QDockWidget *dock);
    bool isCollapsed(Qt::DockWidgetArea area) const;
    void setCollapsed(Qt::DockWidgetArea area, bool collapsed);
    void restorePersistedState();
    void suspendCollapsedAreas();
    void resumeCollapsedAreas();
    void resetCollapsedAreas();
    QByteArray layoutStateForPersistence();

    static PaletteDockAreaManager *find(const QDockWidget *dock);

private:
    struct DockState final
    {
        bool visible = true;
        bool toggleEnabled = true;
    };

    struct AreaState final
    {
        QDockWidget *rail = nullptr;
        QHash<QDockWidget *, DockState> docks;
        QList<QTabBar *> tabBars;
        bool collapsed = false;
        bool suspended = false;
    };

    AreaState &state(Qt::DockWidgetArea area);
    const AreaState &state(Qt::DockWidgetArea area) const;
    QList<QDockWidget *> docksInArea(Qt::DockWidgetArea area) const;
    void collapseArea(Qt::DockWidgetArea area, bool persist);
    void restoreExpandedLayout(Qt::DockWidgetArea expandedArea, bool persist);
    void showRail(Qt::DockWidgetArea area);
    void hideRail(Qt::DockWidgetArea area);
    void setAreaTabBarsVisible(Qt::DockWidgetArea area, bool visible);
    void captureExpandedLayoutState();
    QDockWidget *createRail(Qt::DockWidgetArea area);

    QMainWindow *m_window;
    int m_layoutVersion;
    QByteArray m_expandedLayoutState;
    QList<QDockWidget *> m_docks;
    AreaState m_left;
    AreaState m_right;
};

}

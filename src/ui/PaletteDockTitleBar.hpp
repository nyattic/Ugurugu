#pragma once

class QDockWidget;

namespace ugurugu
{

void installCompactPaletteTitleBar(QDockWidget *dock);
bool isPaletteDockCollapsed(const QDockWidget *dock);
void setPaletteDockCollapsed(QDockWidget *dock, bool collapsed);

}

#pragma once

#include <QColor>

class QApplication;

namespace wobble
{

struct Theme final
{
    static QColor chromeBackground();
    static QColor statusBackground();
    static QColor canvasBackground();
    static QColor panelBackground();
    static QColor controlBackground();
    static QColor hoverBackground();
    static QColor border();
    static QColor canvasBorder();
    static QColor textPrimary();
    static QColor textMuted();
    static QColor textDisabled();
    static QColor accent();
    static QColor accentPressed();
    static QColor accentText();

    static void apply(QApplication &application);
};

}

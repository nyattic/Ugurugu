#include "ui/MacWindowChrome.hpp"

#include "ui/Theme.hpp"

#include <QGuiApplication>
#include <QWidget>

#import <AppKit/AppKit.h>

namespace wobble
{

void applySeamlessTitleBar(QWidget *window)
{
    if (QGuiApplication::platformName() != QStringLiteral("cocoa"))
    {
        return;
    }
    NSView *view = (__bridge NSView *)reinterpret_cast<void *>(window->winId());
    NSWindow *nativeWindow = view.window;
    if (!nativeWindow)
    {
        return;
    }
    nativeWindow.titlebarAppearsTransparent = YES;
    nativeWindow.appearance =
        [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    const QColor chrome = Theme::chromeBackground();
    nativeWindow.backgroundColor = [NSColor colorWithSRGBRed:chrome.redF()
                                                       green:chrome.greenF()
                                                        blue:chrome.blueF()
                                                       alpha:1.0];
}

}

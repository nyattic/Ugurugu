#include "ui/Theme.hpp"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QPalette>
#include <QStringList>
#include <QStyleFactory>

namespace wobble
{

QColor Theme::chromeBackground()
{
    return QColor(0x20, 0x22, 0x26);
}

QColor Theme::statusBackground()
{
    return QColor(0x1B, 0x1D, 0x21);
}

QColor Theme::canvasBackground()
{
    return QColor(0x2A, 0x2C, 0x30);
}

QColor Theme::panelBackground()
{
    return QColor(0x24, 0x26, 0x2B);
}

QColor Theme::controlBackground()
{
    return QColor(0x34, 0x37, 0x3D);
}

QColor Theme::hoverBackground()
{
    return QColor(0x2E, 0x31, 0x38);
}

QColor Theme::border()
{
    return QColor(0x3F, 0x43, 0x4B);
}

QColor Theme::canvasBorder()
{
    return QColor(0x17, 0x18, 0x1B);
}

QColor Theme::textPrimary()
{
    return QColor(0xE8, 0xE8, 0xEA);
}

QColor Theme::textMuted()
{
    return QColor(0x9A, 0xA0, 0xA8);
}

QColor Theme::textDisabled()
{
    return QColor(0x6A, 0x6F, 0x78);
}

QColor Theme::accent()
{
    return QColor(0xFF, 0xC9, 0x4A);
}

QColor Theme::accentPressed()
{
    return QColor(0xE8, 0xB5, 0x3B);
}

QColor Theme::accentText()
{
    return QColor(0x26, 0x1E, 0x0A);
}

void Theme::apply(QApplication &application)
{
    application.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    const int fontId = QFontDatabase::addApplicationFont(
        QStringLiteral(":/fonts/PretendardJP-Medium.otf"));
    if (fontId >= 0)
    {
        const QStringList families =
            QFontDatabase::applicationFontFamilies(fontId);
        if (!families.isEmpty())
        {
            QFont font = application.font();
            font.setFamilies({families.first()});
            font.setWeight(QFont::Medium);
            application.setFont(font);
        }
    }

    QPalette palette;
    palette.setColor(QPalette::Window, chromeBackground());
    palette.setColor(QPalette::WindowText, textPrimary());
    palette.setColor(QPalette::Base, canvasBackground());
    palette.setColor(QPalette::AlternateBase, hoverBackground());
    palette.setColor(QPalette::Text, textPrimary());
    palette.setColor(QPalette::Button, controlBackground());
    palette.setColor(QPalette::ButtonText, textPrimary());
    palette.setColor(QPalette::BrightText, accent());
    palette.setColor(QPalette::Highlight, accent());
    palette.setColor(QPalette::HighlightedText, accentText());
    palette.setColor(QPalette::PlaceholderText, textDisabled());
    palette.setColor(QPalette::ToolTipBase, hoverBackground());
    palette.setColor(QPalette::ToolTipText, textPrimary());
    palette.setColor(QPalette::Link, accent());
    palette.setColor(QPalette::Disabled, QPalette::WindowText, textDisabled());
    palette.setColor(QPalette::Disabled, QPalette::Text, textDisabled());
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, textDisabled());
    palette.setColor(
        QPalette::Disabled, QPalette::Highlight, controlBackground());
    palette.setColor(
        QPalette::Disabled, QPalette::HighlightedText, textDisabled());
    application.setPalette(palette);

    const QString sheet = QStringLiteral(R"(
QToolBar {
    background: %CHROME%;
    border: none;
    padding: 7px 10px;
    spacing: 6px;
}
QToolBar::separator {
    background: %BORDER%;
    width: 1px;
    margin: 6px 8px;
}
QToolBar#ToolRail {
    padding: 8px 4px;
    spacing: 4px;
}
QToolBar QLabel {
    color: %MUTED%;
    padding: 0 2px;
}
QLabel[fieldLabel="true"] {
    color: %MUTED%;
    font-size: 10px;
    font-weight: 600;
    letter-spacing: 1px;
}
QToolButton {
    background: transparent;
    color: %TEXT%;
    border: 1px solid transparent;
    border-radius: 7px;
    padding: 4px;
}
QToolButton:hover {
    background: %HOVER%;
}
QToolButton:focus {
    border-color: %ACCENT%;
}
QToolButton:pressed {
    background: %CONTROL%;
}
QToolButton:checked {
    background: %ACCENT%;
    color: %ACCENTTEXT%;
}
QToolButton:checked:hover {
    background: %ACCENTPRESSED%;
}
QToolButton:disabled {
    color: %DISABLED%;
}
QToolButton[categoryTab="true"] {
    background: transparent;
    color: %MUTED%;
    border: 1px solid transparent;
    border-radius: 6px;
    padding: 3px 9px;
    font-size: 11px;
    font-weight: 600;
}
QToolButton[categoryTab="true"]:hover {
    background: %HOVER%;
    color: %TEXT%;
}
QToolButton[categoryTab="true"]:checked {
    background: %ACCENT%;
    color: %ACCENTTEXT%;
}
QSpinBox, QDoubleSpinBox {
    background: %BASE%;
    color: %TEXT%;
    border: 1px solid %BORDER%;
    border-radius: 6px;
    padding: 3px 8px;
    selection-background-color: %ACCENT%;
    selection-color: %ACCENTTEXT%;
}
QSpinBox:hover, QDoubleSpinBox:hover {
    background: %CONTROL%;
    border-color: %DISABLED%;
}
QSpinBox:focus, QDoubleSpinBox:focus {
    background: %BASE%;
    border-color: %ACCENT%;
}
QSpinBox::up-button, QSpinBox::down-button,
QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {
    width: 0;
    border: none;
}
QSlider::groove:horizontal {
    height: 5px;
    background: %BORDER%;
    border-radius: 2px;
}
QSlider::sub-page:horizontal {
    background: %ACCENT%;
    border-radius: 2px;
}
QSlider::add-page:horizontal {
    background: %BORDER%;
    border-radius: 2px;
}
QSlider::handle:horizontal {
    width: 16px;
    height: 16px;
    margin: -6px 0;
    border-radius: 8px;
    background: %TEXT%;
    border: 2px solid %CHROME%;
}
QSlider::handle:horizontal:hover {
    background: %ACCENT%;
}
QSlider::handle:horizontal:focus {
    border-color: %ACCENT%;
}
QSlider::handle:horizontal:disabled {
    background: %DISABLED%;
}
QSlider::sub-page:horizontal:disabled {
    background: %CONTROL%;
}
QListView {
    background: %PANEL%;
    border: none;
    color: %TEXT%;
}
QDockWidget {
    color: %TEXT%;
}
QMainWindow::separator {
    background: %STATUS%;
    width: 3px;
    height: 3px;
}
QStatusBar {
    background: %STATUS%;
    color: %MUTED%;
    min-height: 26px;
}
QStatusBar::item {
    border: none;
}
QStatusBar QLabel {
    color: %MUTED%;
    padding: 0 4px;
}
QStatusBar QToolButton {
    padding: 2px;
    border-radius: 5px;
}
QMenu {
    background: %PANEL%;
    color: %TEXT%;
    border: 1px solid %BORDER%;
}
QMenu::item:selected {
    background: %ACCENT%;
    color: %ACCENTTEXT%;
}
QToolTip {
    background: %HOVER%;
    color: %TEXT%;
    border: 1px solid %BORDER%;
    padding: 3px 6px;
}
QScrollBar:vertical {
    background: transparent;
    width: 10px;
    margin: 2px;
}
QScrollBar::handle:vertical {
    background: %CONTROL%;
    border-radius: 3px;
    min-height: 24px;
}
QScrollBar::handle:vertical:hover {
    background: %DISABLED%;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0;
}
QScrollBar:horizontal {
    background: transparent;
    height: 10px;
    margin: 2px;
}
QScrollBar::handle:horizontal {
    background: %CONTROL%;
    border-radius: 3px;
    min-width: 24px;
}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
    width: 0;
}
QWidget#TimelineBar {
    background: %CHROME%;
    border-top: 1px solid %STATUS%;
}
QWidget#TimelineBar QLabel {
    color: %MUTED%;
}
QLabel#LayerDockTitle {
    background: %CHROME%;
    color: %MUTED%;
    font-size: 10px;
    font-weight: 600;
    letter-spacing: 1px;
    padding: 9px 12px;
}
QWidget#LayerDockBody {
    background: %PANEL%;
}
)");

    QString resolved = sheet;
    resolved.replace(QStringLiteral("%CHROME%"), chromeBackground().name());
    resolved.replace(QStringLiteral("%STATUS%"), statusBackground().name());
    resolved.replace(QStringLiteral("%BASE%"), canvasBackground().name());
    resolved.replace(QStringLiteral("%PANEL%"), panelBackground().name());
    resolved.replace(QStringLiteral("%CONTROL%"), controlBackground().name());
    resolved.replace(QStringLiteral("%HOVER%"), hoverBackground().name());
    resolved.replace(QStringLiteral("%BORDER%"), border().name());
    resolved.replace(QStringLiteral("%TEXT%"), textPrimary().name());
    resolved.replace(QStringLiteral("%MUTED%"), textMuted().name());
    resolved.replace(QStringLiteral("%DISABLED%"), textDisabled().name());
    resolved.replace(QStringLiteral("%ACCENT%"), accent().name());
    resolved.replace(QStringLiteral("%ACCENTPRESSED%"), accentPressed().name());
    resolved.replace(QStringLiteral("%ACCENTTEXT%"), accentText().name());
    application.setStyleSheet(resolved);
}

}

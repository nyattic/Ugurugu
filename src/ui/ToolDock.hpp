#pragma once

#include "ui/CanvasTypes.hpp"

#include <QDockWidget>
#include <QHash>

class QComboBox;
class QStackedWidget;

namespace ugurugu
{

class CanvasWidget;
class CollapsibleSection;
class ColorSwatchRow;
class ColorWheel;
class DocumentController;
class WobblePopoverPanel;

// The always-visible counterpart to the tool rail: the options for whatever
// tool is selected, the colour picker, and the wobble settings, each in a
// section the artist can fold away.
class ToolDock final : public QDockWidget
{
    Q_OBJECT

public:
    ToolDock(DocumentController *controller,
        CanvasWidget *canvas,
        QWidget *parent = nullptr);

    void saveState() const;
    // Unfolds the wobble section and scrolls to it, for the rail button.
    void revealWobbleSettings();
    // The width this dock should be given on startup, remembered across runs.
    int preferredWidth() const;

private:
    void buildSections();
    void restoreState();
    void showToolPanel(CanvasTool tool);

    DocumentController *m_controller = nullptr;
    CanvasWidget *m_canvas = nullptr;
    CollapsibleSection *m_toolSection = nullptr;
    CollapsibleSection *m_colorSection = nullptr;
    CollapsibleSection *m_wobbleSection = nullptr;
    QStackedWidget *m_toolPanels = nullptr;
    QHash<int, int> m_toolPanelIndexes;
    ColorWheel *m_colorWheel = nullptr;
    ColorSwatchRow *m_swatches = nullptr;
    QComboBox *m_wobbleScope = nullptr;
    WobblePopoverPanel *m_wobblePanel = nullptr;
    bool m_updatingColor = false;
};

}

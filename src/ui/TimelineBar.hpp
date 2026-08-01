#pragma once

#include <QWidget>

class QDoubleSpinBox;
class QEvent;
class QLabel;
class QSlider;
class QSpinBox;

namespace wobble
{

class CanvasWidget;
class DocumentController;
class FrameScrubber;
class WobblePlayButton;
class WobblePreview;

class TimelineBar final : public QWidget
{
    Q_OBJECT

public:
    TimelineBar(DocumentController *controller,
        CanvasWidget *canvas,
        QWidget *parent = nullptr);
    ~TimelineBar() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildLayout();
    void connectControls();
    void syncFromDocument();

    DocumentController *m_controller;
    CanvasWidget *m_canvas;
    WobblePlayButton *m_playButton = nullptr;
    QSpinBox *m_currentFrameSpin = nullptr;
    FrameScrubber *m_scrubber = nullptr;
    WobblePreview *m_wobblePreview = nullptr;
    QSlider *m_wobbleSlider = nullptr;
    QDoubleSpinBox *m_wobbleSpin = nullptr;
    QSpinBox *m_framesSpin = nullptr;
    QSpinBox *m_fpsSpin = nullptr;
    bool m_syncing = false;
};

}

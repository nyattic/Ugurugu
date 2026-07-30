#include "ui/TimelineBar.hpp"

#include "document/DocumentController.hpp"
#include "document/DocumentLimits.hpp"
#include "ui/CanvasWidget.hpp"
#include "ui/WobblePlayButton.hpp"

#include <QApplication>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>

#include <algorithm>
#include <cmath>

namespace wobble {

namespace {

bool isObjectOrDescendantOf(QObject *object, const QObject *ancestor)
{
    while (object) {
        if (object == ancestor) {
            return true;
        }
        object = object->parent();
    }
    return false;
}

}

TimelineBar::TimelineBar(
    DocumentController *controller,
    CanvasWidget *canvas,
    QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_canvas(canvas)
{
    setObjectName(QStringLiteral("TimelineBar"));
    buildLayout();
    connectControls();
    syncFromDocument();
    qApp->installEventFilter(this);
}

TimelineBar::~TimelineBar()
{
    if (qApp) {
        qApp->removeEventFilter(this);
    }
}

bool TimelineBar::eventFilter(QObject *watched, QEvent *event)
{
    const bool frameControlEvent =
        isObjectOrDescendantOf(watched, m_currentFrameSpin);
    if (frameControlEvent
        && (event->type() == QEvent::FocusIn
            || event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::Wheel
            || event->type() == QEvent::KeyPress)) {
        m_canvas->setAnimating(false);
    }
    return QWidget::eventFilter(watched, event);
}

void TimelineBar::buildLayout()
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 9, 14, 9);
    layout->setSpacing(10);

    m_playButton = new WobblePlayButton(this);
    m_playButton->setObjectName(QStringLiteral("timelinePlayButton"));
    m_playButton->setToolTip(tr("Play preview (P)"));
    m_playButton->setAccessibleName(tr("Play preview"));
    layout->addWidget(m_playButton);

    m_currentFrameSpin = new QSpinBox(this);
    m_currentFrameSpin->setObjectName(QStringLiteral("currentFrameSpin"));
    m_currentFrameSpin->setRange(1, DocumentLimits::maximumAnimationFrames);
    m_currentFrameSpin->setAccessibleName(tr("Current frame"));
    m_currentFrameSpin->setAlignment(Qt::AlignCenter);
    m_currentFrameSpin->setFixedWidth(46);
    layout->addWidget(m_currentFrameSpin);

    m_frameCountLabel = new QLabel(this);
    layout->addWidget(m_frameCountLabel);

    layout->addStretch(1);

    auto *wobbleLabel = new QLabel(tr("WOBBLE"), this);
    wobbleLabel->setProperty("fieldLabel", true);
    layout->addWidget(wobbleLabel);

    m_wobbleSlider = new QSlider(Qt::Horizontal, this);
    m_wobbleSlider->setObjectName(QStringLiteral("wobbleSlider"));
    m_wobbleSlider->setRange(
        qRound(DocumentLimits::minimumWobbleAmount * 10.0),
        qRound(DocumentLimits::maximumWobbleAmount * 10.0));
    m_wobbleSlider->setFixedWidth(96);
    m_wobbleSlider->setToolTip(tr("Wobble strength"));
    m_wobbleSlider->setAccessibleName(tr("Wobble strength"));
    layout->addWidget(m_wobbleSlider);

    m_wobbleSpin = new QDoubleSpinBox(this);
    m_wobbleSpin->setObjectName(QStringLiteral("wobbleSpin"));
    m_wobbleSpin->setRange(
        DocumentLimits::minimumWobbleAmount,
        DocumentLimits::maximumWobbleAmount);
    m_wobbleSpin->setDecimals(1);
    m_wobbleSpin->setSingleStep(0.1);
    m_wobbleSpin->setSuffix(tr(" px"));
    wobbleLabel->setBuddy(m_wobbleSpin);
    layout->addWidget(m_wobbleSpin);

    layout->addSpacing(12);

    auto *framesLabel = new QLabel(tr("FRAMES"), this);
    framesLabel->setProperty("fieldLabel", true);
    layout->addWidget(framesLabel);

    m_framesSpin = new QSpinBox(this);
    m_framesSpin->setObjectName(QStringLiteral("framesSpin"));
    m_framesSpin->setRange(
        DocumentLimits::minimumAnimationFrames,
        DocumentLimits::maximumAnimationFrames);
    framesLabel->setBuddy(m_framesSpin);
    layout->addWidget(m_framesSpin);

    auto *fpsLabel = new QLabel(tr("FPS"), this);
    fpsLabel->setProperty("fieldLabel", true);
    layout->addWidget(fpsLabel);

    m_fpsSpin = new QSpinBox(this);
    m_fpsSpin->setObjectName(QStringLiteral("fpsSpin"));
    m_fpsSpin->setRange(
        qRound(DocumentLimits::minimumFramesPerSecond),
        qRound(DocumentLimits::maximumFramesPerSecond));
    fpsLabel->setBuddy(m_fpsSpin);
    layout->addWidget(m_fpsSpin);
}

void TimelineBar::connectControls()
{
    m_playButton->setChecked(m_canvas->isAnimating());
    connect(
        m_playButton,
        &QToolButton::toggled,
        m_canvas,
        &CanvasWidget::setAnimating);
    connect(
        m_canvas,
        &CanvasWidget::animatingChanged,
        m_playButton,
        &WobblePlayButton::setChecked);

    connect(
        m_currentFrameSpin,
        &QSpinBox::valueChanged,
        this,
        [this](int value) {
            if (!m_syncing) {
                m_canvas->setAnimating(false);
                m_canvas->setCurrentFrame(value - 1);
            }
        });
    connect(
        m_canvas,
        &CanvasWidget::currentFrameChanged,
        this,
        [this](int frame) {
            if (m_canvas->isAnimating()) {
                return;
            }
            const QSignalBlocker spinBlocker(m_currentFrameSpin);
            m_currentFrameSpin->setValue(frame + 1);
        });
    connect(
        m_canvas,
        &CanvasWidget::animatingChanged,
        this,
        [this](bool animating) {
            if (animating) {
                return;
            }
            const QSignalBlocker spinBlocker(m_currentFrameSpin);
            m_currentFrameSpin->setValue(m_canvas->currentFrame() + 1);
        });

    connect(m_wobbleSlider, &QSlider::valueChanged, this, [this](int value) {
        if (!m_syncing) {
            m_controller->setWobbleAmount(value / 10.0);
        }
    });
    connect(
        m_wobbleSpin,
        &QDoubleSpinBox::valueChanged,
        this,
        [this](double value) {
            if (!m_syncing) {
                m_controller->setWobbleAmount(value);
            }
        });
    connect(m_framesSpin, &QSpinBox::valueChanged, this, [this](int value) {
        if (!m_syncing) {
            m_controller->setAnimationFrames(value);
        }
    });
    connect(m_fpsSpin, &QSpinBox::valueChanged, this, [this](int value) {
        if (!m_syncing) {
            m_controller->setFramesPerSecond(value);
        }
    });

    connect(
        m_controller,
        &DocumentController::documentChanged,
        this,
        &TimelineBar::syncFromDocument);
}

void TimelineBar::syncFromDocument()
{
    m_syncing = true;
    const Document &document = m_controller->document();
    const int frames = std::max(1, document.animationFrames);

    m_currentFrameSpin->setMaximum(frames);
    if (!m_canvas->isAnimating()) {
        m_currentFrameSpin->setValue(m_canvas->currentFrame() + 1);
    }
    m_frameCountLabel->setText(tr("/ %1").arg(frames));

    m_wobbleSlider->setValue(qRound(document.wobbleAmount * 10.0));
    m_wobbleSpin->setValue(document.wobbleAmount);
    m_framesSpin->setValue(document.animationFrames);
    m_fpsSpin->setValue(qRound(document.framesPerSecond));
    m_syncing = false;
}

}

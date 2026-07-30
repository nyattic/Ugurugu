#include "ui/SettingsDialog.hpp"

#include <QDialogButtonBox>
#include <QLabel>
#include <QRadioButton>
#include <QSettings>
#include <QTabWidget>
#include <QVBoxLayout>

namespace wobble {

namespace {

const QString animateWhileDrawingKey =
    QStringLiteral("canvas/animateWhileDrawing");

}

bool SettingsDialog::animateWhileDrawing()
{
    return QSettings()
        .value(animateWhileDrawingKey, false)
        .toBool();
}

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Settings"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 12);
    layout->setSpacing(10);

    auto *tabs = new QTabWidget(this);
    layout->addWidget(tabs);

    auto *drawingTab = new QWidget(tabs);
    auto *drawingLayout = new QVBoxLayout(drawingTab);
    drawingLayout->setContentsMargins(14, 14, 14, 14);
    drawingLayout->setSpacing(8);

    auto *previewLabel = new QLabel(
        tr("Wobble preview while drawing a stroke"),
        drawingTab);
    drawingLayout->addWidget(previewLabel);

    m_pauseWhileDrawing = new QRadioButton(
        tr("Pause the wobble until the stroke is finished"),
        drawingTab);
    drawingLayout->addWidget(m_pauseWhileDrawing);

    m_keepWobbling = new QRadioButton(
        tr("Keep wobbling while drawing"),
        drawingTab);
    drawingLayout->addWidget(m_keepWobbling);

    drawingLayout->addStretch(1);
    tabs->addTab(drawingTab, tr("Drawing"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    if (animateWhileDrawing()) {
        m_keepWobbling->setChecked(true);
    } else {
        m_pauseWhileDrawing->setChecked(true);
    }

    connect(
        m_keepWobbling,
        &QRadioButton::toggled,
        this,
        [this](bool keepWobbling) {
            QSettings settings;
            settings.setValue(animateWhileDrawingKey, keepWobbling);
            emit animateWhileDrawingChanged(keepWobbling);
        });

    resize(420, 220);
}

}

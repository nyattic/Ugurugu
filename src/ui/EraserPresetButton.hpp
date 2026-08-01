#pragma once

#include <QAbstractButton>
#include <QImage>

namespace wobble
{

struct EraserPreset;

class EraserPresetButton final : public QAbstractButton
{
    Q_OBJECT

public:
    EraserPresetButton(const EraserPreset &preset, QWidget *parent = nullptr);

    QString presetId() const;
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    const QImage &previewImage();

    const EraserPreset *m_preset;
    QImage m_preview;
};

}

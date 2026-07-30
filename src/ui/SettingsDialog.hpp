#pragma once

#include <QDialog>

class QRadioButton;

namespace wobble {

class SettingsDialog final : public QDialog
{
    Q_OBJECT

public:
    static bool animateWhileDrawing();

    explicit SettingsDialog(QWidget *parent = nullptr);

signals:
    void animateWhileDrawingChanged(bool animate);

private:
    QRadioButton *m_pauseWhileDrawing = nullptr;
    QRadioButton *m_keepWobbling = nullptr;
};

}

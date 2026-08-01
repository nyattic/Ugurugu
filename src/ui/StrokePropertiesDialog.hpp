#pragma once

#include <QColor>
#include <QDialog>

#include <optional>

class QCheckBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QPushButton;

namespace wobble
{

class StrokePropertiesDialog final : public QDialog
{
    Q_OBJECT

public:
    struct Values
    {
        bool colorSupported = false;
        bool widthSupported = false;
        bool roughnessSupported = false;
        std::optional<QColor> color;
        std::optional<qreal> width;
        std::optional<qreal> roughness;
    };

    explicit StrokePropertiesDialog(
        const Values &values, QWidget *parent = nullptr);

    std::optional<QColor> color() const;
    std::optional<qreal> width() const;
    std::optional<qreal> roughness() const;

private:
    void chooseColor();
    void updateColorButton();
    void updateAcceptState();

    QColor m_color = Qt::black;
    QCheckBox *m_colorCheck = nullptr;
    QCheckBox *m_widthCheck = nullptr;
    QCheckBox *m_roughnessCheck = nullptr;
    QPushButton *m_colorButton = nullptr;
    QDoubleSpinBox *m_widthSpin = nullptr;
    QDoubleSpinBox *m_roughnessSpin = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};

}

#pragma once

#include <QDialog>
#include <QHash>
#include <QKeySequence>
#include <QList>

class QAction;
class QCheckBox;
class QComboBox;
class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QRadioButton;
class QSlider;
class QSpinBox;

namespace wobble
{

class SettingsDialog final : public QDialog
{
    Q_OBJECT

public:
    static bool animateWhileDrawing();
    static bool wobbleAnimationEnabled();
    static qreal strokeStabilization();
    static QString defaultSaveFolder();
    static QString uiLanguage();
    explicit SettingsDialog(QWidget *parent = nullptr,
        const QList<QAction *> &shortcutActions = {});

signals:
    void animateWhileDrawingChanged(bool animate);
    void wobbleAnimationEnabledChanged(bool enabled);
    void strokeStabilizationChanged(qreal strength);

private:
    void updateDrawingOptionsEnabled();
    void setShortcut(QAction *action, const QKeySequence &shortcut);
    void chooseDefaultSaveFolder();
    void resetDefaultSaveFolder();
    void restoreDefaults();

    QCheckBox *m_wobbleAnimation = nullptr;
    QSlider *m_strokeStabilizationSlider = nullptr;
    QSpinBox *m_strokeStabilizationSpin = nullptr;
    QLabel *m_drawingOptionsLabel = nullptr;
    QRadioButton *m_pauseWhileDrawing = nullptr;
    QRadioButton *m_keepWobbling = nullptr;
    QComboBox *m_languageCombo = nullptr;
    QList<QAction *> m_shortcutActions;
    QHash<QAction *, QKeySequenceEdit *> m_shortcutEditors;
    QLabel *m_shortcutMessage = nullptr;
    QLineEdit *m_defaultSaveFolderEdit = nullptr;
};

}

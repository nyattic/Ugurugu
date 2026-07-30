#pragma once

#include <QDialog>
#include <QHash>
#include <QKeySequence>
#include <QList>

class QAction;
class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QRadioButton;

namespace wobble {

class SettingsDialog final : public QDialog
{
    Q_OBJECT

public:
    static bool animateWhileDrawing();
    static QString defaultSaveFolder();
    static QKeySequence shortcutForAction(
        const QString &actionName,
        const QKeySequence &defaultShortcut);

    explicit SettingsDialog(
        QWidget *parent = nullptr,
        const QList<QAction *> &shortcutActions = {});

signals:
    void animateWhileDrawingChanged(bool animate);

private:
    void setShortcut(QAction *action, const QKeySequence &shortcut);
    void chooseDefaultSaveFolder();
    void resetDefaultSaveFolder();
    void restoreDefaults();

    QRadioButton *m_pauseWhileDrawing = nullptr;
    QRadioButton *m_keepWobbling = nullptr;
    QList<QAction *> m_shortcutActions;
    QHash<QAction *, QKeySequenceEdit *> m_shortcutEditors;
    QLabel *m_shortcutMessage = nullptr;
    QLineEdit *m_defaultSaveFolderEdit = nullptr;
};

}

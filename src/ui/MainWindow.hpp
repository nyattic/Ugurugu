#pragma once

#include "document/DocumentController.hpp"

#include <QList>
#include <QMainWindow>

class QAction;
class QCloseEvent;
class QComboBox;
class QEvent;
class QLabel;
class QPushButton;
class QSpinBox;

namespace wobble {

class CanvasWidget;
class ColorSwatchRow;
class LayerDock;
class TimelineBar;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    bool openFile(const QString &filePath);

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void createActions();
    void createMenus();
    void createToolBars();
    void createStatusBar();
    void connectDocument();
    void updateWindowTitle();
    void updateColorButton();
    bool maybeSave();
    bool save();
    bool saveAs();
    bool saveToFile(const QString &filePath);
    void newDocument();
    void chooseOpenFile();
    void exportGif();
    void exportPng();
    QString normalizedPath(
        const QString &filePath,
        const QString &extension) const;
    QString saveDialogStartPath(const QString &extension) const;

    DocumentController m_controller;
    CanvasWidget *m_canvas = nullptr;
    TimelineBar *m_timeline = nullptr;
    LayerDock *m_layerDock = nullptr;
    QString m_currentFilePath;
    QAction *m_saveAction = nullptr;
    QAction *m_playAction = nullptr;
    QAction *m_brushAction = nullptr;
    QAction *m_eraserAction = nullptr;
    QAction *m_lassoAction = nullptr;
    QAction *m_wandAction = nullptr;
    QAction *m_bucketAction = nullptr;
    QList<QAction *> m_shortcutActions;
    QPushButton *m_colorButton = nullptr;
    ColorSwatchRow *m_swatchRow = nullptr;
    QComboBox *m_brushPresetCombo = nullptr;
    QSpinBox *m_brushSizeSpin = nullptr;
    QLabel *m_pointerLabel = nullptr;
    QLabel *m_zoomLabel = nullptr;
};

}

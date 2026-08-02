#pragma once

#include "app/RecoveryWriter.hpp"
#include "document/DocumentController.hpp"

#include <QList>
#include <QMainWindow>
#include <QTimer>
#include <QUuid>

#include <optional>

class QAction;
class QCloseEvent;
class QEvent;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;

namespace wobble
{

class CanvasWidget;
class ColorSwatchRow;
class LayerDock;
class MainWindowTestAccess;
class TimelineBar;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    enum class StartupResult
    {
        Ready,
        Recovered,
        OpenedRequestedFile,
        Canceled,
        Failed
    };

    explicit MainWindow(QWidget *parent = nullptr);
    bool openFile(const QString &filePath);
    bool offerRecovery();
    StartupResult initializeSession(const QString &requestedFilePath = {});

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void createActions();
    void createMenus();
    void createToolBars();
    void createStatusBar();
    void connectDocument();
    void restoreDrawingToolSettings();
    void connectDrawingToolSettings();
    void scheduleDrawingToolSettingsSave();
    void saveDrawingToolSettings();
    void updateWindowTitle();
    void updateColorButton();
    bool hasUnsavedWork() const;
    void refreshUnsavedState();
    bool maybeSave();
    bool save();
    bool saveAs();
    bool saveToFile(const QString &filePath);
    bool rejectReservedRecoveryPath(
        const QString &filePath, const QString &title);
    std::optional<Document> readProject(const QString &filePath);
    bool activateProject(const QString &filePath, Document document);
    bool recoverAutosave();
    bool discardAutosave();
    QString preserveAutosave();
    void clearRecoveryMetadata();
    void handleAutosaveWritten(
        bool success, quint64 revision, const QString &error);
    void warnLegacyLayerHierarchy();
    void writeAutosave();
    bool clearAutosave();
    void newDocument();
    void resizeCanvas();
    void resizeImage();
    void scaleSelection();
    void rotateSelection();
    void editSelectedStrokeProperties();
    void chooseOpenFile();
    void exportGif();
    void exportImage();
    void applyWobbleAnimationEnabled(bool enabled);
    QString normalizedPath(
        const QString &filePath, const QString &extension) const;
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
    QAction *m_scaleSelectionAction = nullptr;
    QAction *m_rotateSelectionAction = nullptr;
    QAction *m_duplicateSelectionAction = nullptr;
    QAction *m_editStrokePropertiesAction = nullptr;
    QAction *m_moveSelectionAction = nullptr;
    QAction *m_flipSelectionHorizontalAction = nullptr;
    QAction *m_flipSelectionVerticalAction = nullptr;
    QAction *m_applySelectionTransformAction = nullptr;
    QAction *m_cancelSelectionTransformAction = nullptr;
    QAction *m_deleteSelectionAction = nullptr;
    QAction *m_deselectSelectionAction = nullptr;
    QAction *m_mirrorCanvasAction = nullptr;
    QList<QAction *> m_shortcutActions;
    QPushButton *m_colorButton = nullptr;
    ColorSwatchRow *m_swatchRow = nullptr;
    QLabel *m_pointerLabel = nullptr;
    QSlider *m_zoomSlider = nullptr;
    QSpinBox *m_zoomSpin = nullptr;
    QTimer m_autosaveTimer;
    QTimer m_drawingToolSettingsSaveTimer;
    bool m_autosavePending = false;
    bool m_initialFitApplied = false;
    bool m_startupResolved = false;
    bool m_recoveryOwnedBySession = false;
    QUuid m_recoverySessionId = QUuid::createUuid();
    quint64 m_recoveryRevision = 0;
    quint64 m_submittedRecoveryRevision = 0;
    quint64 m_autosaveEditGeneration = 0;
    quint64 m_submittedEditGeneration = 0;
    RecoveryWriter m_recoveryWriter;

    friend class MainWindowTestAccess;
};

}

#pragma once

#include <QTimer>
#include <QWidget>

namespace wobble
{

class DocumentController;

class WobblePreview final : public QWidget
{
    Q_OBJECT

public:
    explicit WobblePreview(
        DocumentController *controller, QWidget *parent = nullptr);

    QSize sizeHint() const override;
    bool isAnimationActive() const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void syncAnimationState();

    DocumentController *m_controller;
    QTimer m_timer;
    qreal m_phase = 0.0;
    bool m_shown = false;
};

}

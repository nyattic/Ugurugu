#pragma once

#include <QTimer>
#include <QWidget>

namespace wobble {

class DocumentController;

class WobblePreview final : public QWidget
{
    Q_OBJECT

public:
    explicit WobblePreview(
        DocumentController *controller,
        QWidget *parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    DocumentController *m_controller;
    QTimer m_timer;
    qreal m_phase = 0.0;
};

}

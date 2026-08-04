#pragma once

#include <QString>
#include <QWidget>

class QToolButton;
class QVBoxLayout;

namespace ugurugu
{

// A titled block that folds away to just its header. The dock stacks these so
// every panel stays on screen without any one of them forcing the others out.
class CollapsibleSection final : public QWidget
{
    Q_OBJECT

public:
    explicit CollapsibleSection(const QString &title, QWidget *parent = nullptr);

    void setContentWidget(QWidget *content);
    QWidget *contentWidget() const;
    bool isExpanded() const;

public Q_SLOTS:
    void setExpanded(bool expanded);

Q_SIGNALS:
    void expandedChanged(bool expanded);

private:
    void updateHeader();

    QToolButton *m_header = nullptr;
    QWidget *m_body = nullptr;
    QVBoxLayout *m_bodyLayout = nullptr;
    QWidget *m_content = nullptr;
    QString m_title;
    bool m_expanded = true;
};

}

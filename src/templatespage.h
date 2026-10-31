#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QTreeView;

// The Templates tab: a row of buttons over a task view of the templates (one
// group per template file, drawn like the task list). The window owns the
// view, the model and the logic; this is just the frame.
class TemplatesPage : public QWidget
{
    Q_OBJECT
public:
    explicit TemplatesPage(QTreeView *view, QWidget *parent = nullptr);

    // The template the current row belongs to (empty = none); enables the buttons.
    void setCurrentTemplate(const QString &name);
    void setEmpty(bool empty, bool haveDirectory); // shows the hint instead of an empty list
    QPushButton *button(const QString &name) const; // "new", "rename", "delete", "insert", "open"

signals:
    void newRequested();
    void renameRequested();
    void deleteRequested();
    void insertRequested();
    void openRequested();

private:
    QLabel *m_title, *m_hint;
    QPushButton *m_new, *m_rename, *m_delete, *m_insert, *m_open;
};

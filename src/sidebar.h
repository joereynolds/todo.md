#pragma once

#include "task.h"

#include <QCalendarWidget>
#include <QHash>
#include <QWidget>

#include <vector>

class AgendaModel;
class QLabel;
class QLineEdit;
class StatButton;
class TaskStore;
class TaskView;

// Calendar that marks days with open, due tasks (dots: one per task, up to
// three; red when overdue, amber today).
class TaskCalendar : public QCalendarWidget
{
    Q_OBJECT
public:
    explicit TaskCalendar(QWidget *parent = nullptr);
    void setCounts(QHash<QDate, int> counts);
    void repaintCells() { updateCells(); }
    void restyle(); // after a palette change

protected:
    void paintCell(QPainter *p, const QRect &rect, QDate date) const override;

private:
    QHash<QDate, int> m_counts;
};

// Left-hand panel: "at a glance" counts that double as quick filters, a
// calendar of due dates, and the tasks due on the selected day.
class Sidebar : public QWidget
{
    Q_OBJECT
public:
    Sidebar(TaskStore *store, QWidget *parent = nullptr);

    AgendaModel *dayModel() const { return m_dayModel; }
    TaskView *dayView() const { return m_dayView; }
    void setActiveFilter(const QString &text); // highlights the matching stat
    void setStateVisible(TaskState state, bool visible);
    void refreshColors();
    void showDay(const QDate &day);
    QLineEdit *addBox() const { return m_add; }

signals:
    void filterRequested(const QString &text); // empty = clear
    void taskActivated(Task *task);            // double-click: show it in the task list
    void toggleRequested(Task *task);          // checkbox click
    // Enter in the add box: a new task due `day`, after `after` (the selected
    // task in the day list) or, when that is null, in the default file.
    void addRequested(const QString &text, const QDate &day, Task *after);

private:
    void refresh();
    void updateAddHint();
    Task *selectedDayTask() const;

    TaskStore *m_store;
    TaskCalendar *m_calendar;
    AgendaModel *m_dayModel;
    TaskView *m_dayView;
    QLineEdit *m_add;
    std::vector<StatButton *> m_stats;
};

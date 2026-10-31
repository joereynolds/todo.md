#pragma once

#include "task.h"

#include <QCalendarWidget>
#include <QHash>
#include <QWidget>

#include <vector>

class AgendaModel;
class QLabel;
class QLineEdit;
class QScrollArea;
class TagCloud;
class TagIndex;
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

// Tags with open tasks as wrapped chips ("#work 12"); a click asks to filter
// by the tag, or to clear the filter when it's the active one.
class TagCloud : public QWidget
{
    Q_OBJECT
public:
    struct Item {
        QString tag;
        int count;
    };
    explicit TagCloud(QWidget *parent = nullptr);
    void setItems(std::vector<Item> items);
    const std::vector<Item> &items() const { return m_items; }
    void setActive(const QString &tag); // without '#'; empty = none
    std::vector<QRect> chipRects(int width) const;

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;
    QSize sizeHint() const override;

signals:
    void tagClicked(const QString &tag, bool active);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    int hitTest(const QPoint &pos) const;
    QFont chipFont() const;
    std::vector<Item> m_items;
    QString m_active;
    int m_hover = -1;
};

// Left-hand panel: "at a glance" counts that double as quick filters, a
// calendar of due dates, and the tasks due on the selected day.
class Sidebar : public QWidget
{
    Q_OBJECT
public:
    Sidebar(TaskStore *store, TagIndex *tags, QWidget *parent = nullptr);

    AgendaModel *dayModel() const { return m_dayModel; }
    TaskView *dayView() const { return m_dayView; }
    void setActiveFilter(const QString &text); // highlights the matching stat
    void setStateVisible(TaskState state, bool visible);
    void refreshColors();
    void showDay(const QDate &day);
    QLineEdit *addBox() const { return m_add; }
    TagCloud *tagCloud() const { return m_tagCloud; }

protected:
    void resizeEvent(QResizeEvent *event) override;

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
    void updateTags();
    Task *selectedDayTask() const;

    TaskStore *m_store;
    TaskCalendar *m_calendar;
    AgendaModel *m_dayModel;
    TaskView *m_dayView;
    QLineEdit *m_add;
    TagIndex *m_tags = nullptr;
    QWidget *m_tagHeading = nullptr;
    QScrollArea *m_tagArea = nullptr;
    TagCloud *m_tagCloud = nullptr;
    std::vector<StatButton *> m_stats;
};

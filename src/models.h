#pragma once

#include "task.h"

#include <QAbstractItemModel>
#include <QColor>
#include <QFont>
#include <QHash>
#include <QSortFilterProxyModel>

class TaskStore;

// What the filter box, state toggles and ETA preset select. A task matches
// when its state is visible, every text term occurs in its raw content or file
// path, and every ETA condition holds.
//
// Text terms starting with '~' are ETA conditions:
//   ~<=15m  ~<1h  ~>2h  ~>=1d  ~30m (exact)  ~none  ~any
class TaskFilter
{
public:
    struct EtaCond {
        enum Op { Lt, Le, Gt, Ge, Eq, None, Any } op;
        qint64 minutes = 0;
        bool holds(qint64 eta) const;
    };

    void setText(const QString &text);
    void setStateVisible(TaskState state, bool visible);
    bool stateVisible(TaskState state) const { return m_states & (1u << int(state)); }
    void setEtaPreset(const std::vector<EtaCond> &conds) { m_presetConds = conds; }

    bool matches(const Task &t) const;
    bool showsEverything() const;
    // True when text or ETA conditions narrow the result (state toggles aside).
    bool narrows() const { return !m_terms.isEmpty() || !m_textConds.empty() || !m_presetConds.empty(); }

    static bool parseEtaCond(QStringView term, EtaCond *out);

private:
    QStringList m_terms;
    std::vector<EtaCond> m_textConds, m_presetConds;
    unsigned m_states = 0xF;
};

// Shared behaviour of the task list and the agenda: how a task row looks and
// inline editing of its content.
class TaskModelBase : public QAbstractItemModel
{
    Q_OBJECT
public:
    enum Roles { DepthRole = Qt::UserRole + 1 };

    explicit TaskModelBase(TaskStore *store, QObject *parent = nullptr);

    virtual Task *taskAt(const QModelIndex &index) const = 0;
    virtual TaskFile *fileAt(const QModelIndex &index) const = 0; // file of a task or file row
    virtual QModelIndex indexOf(const Task *task) const = 0;

    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;

    // Re-reads palette dependent colours (after a theme change).
    void refreshColors();

signals:
    // Emitted when a row is edited inline; the window applies it.
    void contentEdited(const QString &path, int line, const QString &before, const QString &after);

protected:
    QVariant taskData(const Task *t, int role) const;
    QVariant dueData(const Task *t, int role) const;
    QVariant etaData(const Task *t, int role) const;

    TaskStore *m_store;
    bool m_resetting = false;
    QFont m_bold, m_strike, m_strikeItalic;
    QColor m_dim, m_doing, m_overdue, m_todayColor, m_quick;
};

// Every visible task as one flat row, in file order, preceded by a header row
// per file. Subtasks are always shown: indentation comes from DepthRole and is
// drawn by the view's delegate. Filtering is built in (a matching task keeps
// its ancestors visible), which is much cheaper than a proxy over a tree.
class TaskListModel : public TaskModelBase
{
    Q_OBJECT
public:
    enum Column { TextColumn, EtaColumn, DueColumn, ColumnCount };

    explicit TaskListModel(TaskStore *store, QObject *parent = nullptr);

    TaskFilter &filter() { return m_filter; }
    void refilter(); // call after changing filter()

    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    Task *taskAt(const QModelIndex &index) const override;
    TaskFile *fileAt(const QModelIndex &index) const override;
    QModelIndex indexOf(const Task *task) const override;

private:
    struct Row {
        Node *node;
        int depth; // 0 = file header
    };
    void rebuild();

    std::vector<Row> m_rows;
    TaskFilter m_filter;
};

// Open tasks with a due date, grouped under one row per day.
class AgendaModel : public TaskModelBase
{
    Q_OBJECT
public:
    enum Column { TextColumn, EtaColumn, FileColumn, ColumnCount };

    explicit AgendaModel(TaskStore *store, QObject *parent = nullptr);

    void setShowEmptyDays(bool on);
    bool showEmptyDays() const { return m_showEmpty; }
    void refreshIfDateChanged();

    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    Task *taskAt(const QModelIndex &index) const override;
    TaskFile *fileAt(const QModelIndex &index) const override;
    QModelIndex indexOf(const Task *task) const override;

private:
    struct Day {
        QDate date;
        std::vector<Task *> tasks;
    };
    void rebuild();

    std::vector<Day> m_days;
    QHash<const Task *, QModelIndex> m_index;
    QDate m_today;
    bool m_showEmpty = false;
};

// Applies a TaskFilter to the agenda. Day rows stay visible while no text
// filter is active (so "show empty days" works), otherwise only with matches.
class AgendaFilterProxy : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    explicit AgendaFilterProxy(QObject *parent = nullptr);
    TaskFilter &filter() { return m_filter; }
    void refilter();

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;

private:
    TaskFilter m_filter;
};

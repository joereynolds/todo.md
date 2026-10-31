#pragma once

#include "task.h"

#include <QAbstractItemModel>
#include <QColor>
#include <QFont>
#include <QHash>
#include <QPalette>
#include <QSet>

class TaskStore;

// What the filter box, state toggles and ETA preset select. A task matches
// when its state is visible and every term holds. Explicit is: and done: terms
// override the state toggles (typing is:done shows done tasks even when the
// Done toggle is off). Terms:
//   word            occurs in the raw content or the file path (case-insensitive)
//   ~<=15m ~<1h ~>2h ~>=1d ~30m ~none ~any       ETA conditions
//   due:overdue due:today due:week due:none due:any   (week = next 7 days)
//   is:todo is:doing is:done is:cancelled is:open
//   done:today done:yesterday done:week done:any      (week = last 7 days, by x-date)
//   #tag            it or an ancestor has the tag, or one nested under it (#work matches #work/meeting)
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
    // True when terms or ETA conditions narrow the result (state toggles aside).
    bool narrows() const;

    static bool parseEtaCond(QStringView term, EtaCond *out);

private:
    enum class Due { Overdue, Today, Week, None, Any };
    enum class Done { Today, Yesterday, Week, Any };
    QStringList m_terms;
    QStringList m_tags; // lower case, without '#'
    std::vector<EtaCond> m_textConds, m_presetConds;
    std::vector<Due> m_due;
    std::vector<Done> m_done;
    unsigned m_isMask = 0; // from is: terms, 0 = no constraint
    unsigned m_states = 0xF;
    QDate m_today;
};

// Every tag in the store with its task counts, rebuilt on each store change.
// A task counts towards its own tags and every parent of a nested one
// (#work/meeting also counts for #work), matching what the #tag filter shows
// apart from inheritance by subtasks.
class TagIndex : public QObject
{
    Q_OBJECT
public:
    struct Entry {
        QString tag; // as first written, without '#'
        int open = 0;
        int total = 0;
    };
    explicit TagIndex(TaskStore *store, QObject *parent = nullptr);
    const std::vector<Entry> &entries() const { return m_entries; } // case-insensitive order

signals:
    void changed();

private:
    void rebuild();
    TaskStore *m_store;
    std::vector<Entry> m_entries;
};

// Tasks as a plain bulleted list for sharing: "- text" per task (display text, so
// dates, ETAs and trailing tags are left out), each indented two spaces per
// ancestor that is also in the list. `tasks` is in display order.
QString tasksAsBulletList(const std::vector<Task *> &tasks);

// Colours that depend on the palette (and so on the theme).
struct TaskColors {
    bool dark = false;
    QColor dim, doing, overdue, today, quick, done, band, guide, checkBorder, pill, pillText;
    static TaskColors fromPalette(const QPalette &pal);
};

// Shared behaviour of the task list and the agenda: what a task row exposes
// (text plus roles the delegate draws from) and inline editing of its content.
class TaskModelBase : public QAbstractItemModel
{
    Q_OBJECT
public:
    enum Roles {
        DepthRole = Qt::UserRole + 1, // int; indentation level
        StateRole,                    // int(TaskState) on task rows, invalid otherwise
        ProgressRole,                 // QPoint(done, total) when there is something to count
        HeaderRole,                   // bool; file and day header rows
        HasChildrenRole,              // bool; row can be expanded/collapsed
        ExpandedRole,                 // bool
        TagsRole,                     // QStringList (without '#'), on tag cells of task rows
    };

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
    QVariant tagsData(const Task *t, int role) const;

    TaskStore *m_store;
    QFont m_bold, m_strike, m_strikeItalic;
    TaskColors m_colors;
};

// A tree presented as a flat list: per group (a file, an agenda day) a header
// row followed by its tasks and their subtasks in depth-first order, with
// DepthRole for indentation (drawn by TaskDelegate). A node's descendants are
// a contiguous run of rows, so collapse/expand is a single remove/insert and
// nothing has to walk an expansion state over 100k+ rows.
//
// Filtering is built in: a matching task keeps its ancestors visible. While
// the filter narrows, the remembered collapse state is ignored so every match
// shows; toggles made then are temporary.
class FlatTaskModel : public TaskModelBase
{
    Q_OBJECT
public:
    explicit FlatTaskModel(TaskStore *store, QObject *parent = nullptr);

    TaskFilter &filter() { return m_filter; }
    const TaskFilter &filter() const { return m_filter; }
    void refilter(); // call after changing filter()

    void setExpanded(int row, bool expanded);
    void expandAll();
    void collapseAll(); // collapses the groups, leaving only their headers
    int parentRow(int row) const; // -1 for headers
    QStringList collapsedKeys() const; // for persisting
    void setCollapsedKeys(const QStringList &keys);

    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;

    Task *taskAt(const QModelIndex &index) const override;
    TaskFile *fileAt(const QModelIndex &index) const override;
    QModelIndex indexOf(const Task *task) const override;

protected:
    struct Group {
        QString key;              // collapse key of the header
        TaskFile *file = nullptr; // for file groups
        QDate date;               // for day groups
        std::vector<Task *> roots;
        bool keepWhenEmpty = false; // shown even with no visible tasks (unless filtering)
    };
    virtual void collectGroups(std::vector<Group> &out) const = 0;
    virtual QVariant groupData(const Group &g, int column, int role) const = 0;
    virtual QVariant taskRowData(const Task *t, int column, int role) const = 0;
    // Updates the task -> row lookup for rows >= first (default: rebuilds a hash).
    virtual void reindex(size_t first);
    virtual void clearIndex() { m_rowOf.clear(); }

    void reset(); // rebuild inside begin/endResetModel
    void forgetCollapsed(const QString &key) { m_collapsed.remove(key); m_tempCollapsed.remove(key); }
    static QString taskKey(const Task *t);

    struct Row {
        Task *task; // nullptr for the group header
        int group;
        int depth; // 0 = header
        bool hasChildren;
        bool expanded;
    };
    std::vector<Group> m_groups;
    std::vector<Row> m_rows;
    QHash<const Task *, int> m_rowOf;

private:
    void rebuild();
    void appendGroupRows(int g, std::vector<Row> &out) const;
    bool appendTask(Task *t, int g, int depth, bool hidden, std::vector<Row> &out) const;
    bool isCollapsed(const QString &key) const;
    QSet<QString> &collapsed() { return m_filter.narrows() ? m_tempCollapsed : m_collapsed; }
    const QSet<QString> &collapsed() const { return m_filter.narrows() ? m_tempCollapsed : m_collapsed; }
    QString rowKey(const Row &r) const;

    TaskFilter m_filter;
    bool m_all = true; // filter shows everything (fast path)
    bool m_resetting = false;
    QSet<QString> m_collapsed;     // group keys and file path + '\x1f' + task text
    QSet<QString> m_tempCollapsed; // while the filter narrows
};

// Every file with tasks. Task::listRow is this model's row lookup.
class TaskListModel : public FlatTaskModel
{
    Q_OBJECT
public:
    enum Column { TextColumn, TagsColumn, EtaColumn, DueColumn, ColumnCount };

    explicit TaskListModel(TaskStore *store, QObject *parent = nullptr);

    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    QModelIndex indexOf(const Task *task) const override;

    // Expands the task's file and ancestors so it gets a row; returns its index
    // (invalid if the filter hides it).
    QModelIndex reveal(const Task *task);

protected:
    void collectGroups(std::vector<Group> &out) const override;
    QVariant groupData(const Group &g, int column, int role) const override;
    QVariant taskRowData(const Task *t, int column, int role) const override;
    void reindex(size_t first) override;
    void clearIndex() override;
};

// Open tasks with a due date, one group per day, each with its subtasks.
// In single-day mode (the sidebar) only the chosen day is shown.
class AgendaModel : public FlatTaskModel
{
    Q_OBJECT
public:
    enum Column { TextColumn, TagsColumn, EtaColumn, ColumnCount };

    explicit AgendaModel(TaskStore *store, QObject *parent = nullptr);

    void setShowEmptyDays(bool on);
    bool showEmptyDays() const { return m_showEmpty; }
    void setSingleDay(const QDate &day); // invalid = all days
    QDate singleDay() const { return m_day; }
    void refreshIfDateChanged();

    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

protected:
    void collectGroups(std::vector<Group> &out) const override;
    QVariant groupData(const Group &g, int column, int role) const override;
    QVariant taskRowData(const Task *t, int column, int role) const override;

private:
    QDate m_day;
    bool m_showEmpty = false;
    mutable QDate m_today; // refreshed in collectGroups
};

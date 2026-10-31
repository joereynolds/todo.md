#include "models.h"

#include "parser.h"
#include "store.h"

#include <QApplication>
#include <QPalette>

#include <algorithm>

// ---------------------------------------------------------------- filter

bool TaskFilter::EtaCond::holds(qint64 eta) const
{
    switch (op) {
    case None: return eta == 0;
    case Any: return eta > 0;
    case Lt: return eta > 0 && eta < minutes;
    case Le: return eta > 0 && eta <= minutes;
    case Gt: return eta > 0 && eta > minutes;
    case Ge: return eta > 0 && eta >= minutes;
    case Eq: return eta > 0 && eta == minutes;
    }
    return true;
}

bool TaskFilter::parseEtaCond(QStringView term, EtaCond *out)
{
    if (!term.startsWith(u'~'))
        return false;
    QStringView rest = term.mid(1);
    if (rest.compare(u"none", Qt::CaseInsensitive) == 0) {
        *out = {EtaCond::None, 0};
        return true;
    }
    if (rest.compare(u"any", Qt::CaseInsensitive) == 0) {
        *out = {EtaCond::Any, 0};
        return true;
    }
    EtaCond::Op op = EtaCond::Eq;
    if (rest.startsWith(u"<=")) {
        op = EtaCond::Le;
        rest = rest.mid(2);
    } else if (rest.startsWith(u">=")) {
        op = EtaCond::Ge;
        rest = rest.mid(2);
    } else if (rest.startsWith(u'<')) {
        op = EtaCond::Lt;
        rest = rest.mid(1);
    } else if (rest.startsWith(u'>')) {
        op = EtaCond::Gt;
        rest = rest.mid(1);
    } else if (rest.startsWith(u'=')) {
        rest = rest.mid(1);
    }
    const qint64 minutes = parser::parseEta(rest);
    if (minutes < 0)
        return false;
    *out = {op, minutes};
    return true;
}

void TaskFilter::setText(const QString &text)
{
    m_terms.clear();
    m_textConds.clear();
    for (const QString &term : text.split(u' ', Qt::SkipEmptyParts)) {
        EtaCond c;
        if (parseEtaCond(term, &c))
            m_textConds.push_back(c);
        else
            m_terms << term;
    }
}

void TaskFilter::setStateVisible(TaskState state, bool visible)
{
    const unsigned bit = 1u << int(state);
    m_states = visible ? (m_states | bit) : (m_states & ~bit);
}

bool TaskFilter::showsEverything() const
{
    return m_states == 0xF && m_terms.isEmpty() && m_textConds.empty() && m_presetConds.empty();
}

bool TaskFilter::matches(const Task &t) const
{
    if (!(m_states & (1u << int(t.state))))
        return false;
    for (const EtaCond &c : m_presetConds) {
        if (!c.holds(t.eta))
            return false;
    }
    for (const EtaCond &c : m_textConds) {
        if (!c.holds(t.eta))
            return false;
    }
    for (const QString &term : m_terms) {
        if (!t.content.contains(term, Qt::CaseInsensitive) && !t.file->relPath.contains(term, Qt::CaseInsensitive))
            return false;
    }
    return true;
}

// ---------------------------------------------------------------- base

TaskModelBase::TaskModelBase(TaskStore *store, QObject *parent)
    : QAbstractItemModel(parent), m_store(store)
{
    const QFont base = QApplication::font();
    m_bold = base;
    m_bold.setBold(true);
    m_strike = base;
    m_strike.setStrikeOut(true);
    m_strikeItalic = m_strike;
    m_strikeItalic.setItalic(true);
    refreshColors();

    // Both models hold raw task pointers, which a store mutation invalidates,
    // so they rebuild wholesale around every change.
    connect(store, &TaskStore::aboutToChange, this, [this] {
        if (!m_resetting) {
            m_resetting = true;
            beginResetModel();
        }
    });
}

void TaskModelBase::refreshColors()
{
    const QPalette pal = QApplication::palette();
    const bool dark = pal.color(QPalette::Window).lightness() < 128;
    m_dim = pal.color(QPalette::Disabled, QPalette::Text);
    m_doing = dark ? QColor(0x6c, 0xaa, 0xf0) : QColor(0x1f, 0x6f, 0xd0);
    m_overdue = dark ? QColor(0xf0, 0x70, 0x70) : QColor(0xc8, 0x2a, 0x2a);
    m_todayColor = dark ? QColor(0xe8, 0xa8, 0x40) : QColor(0xb8, 0x70, 0x00);
    m_quick = dark ? QColor(0x7c, 0xc8, 0x7c) : QColor(0x2a, 0x8a, 0x2a);
}

Qt::ItemFlags TaskModelBase::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == 0 && taskAt(index))
        f |= Qt::ItemIsEditable;
    return f;
}

bool TaskModelBase::setData(const QModelIndex &index, const QVariant &value, int role)
{
    Task *t = taskAt(index);
    if (role != Qt::EditRole || index.column() != 0 || !t)
        return false;
    QString content = value.toString();
    content.replace(u'\n', u' ');
    if (content == t->content)
        return false;
    // Applying rewrites the model, which must not happen inside the delegate's
    // commit, hence the queued connection on the receiving side.
    emit contentEdited(t->file->path, t->line, t->raw(), parser::setContent(t->raw(), content));
    return true;
}

QVariant TaskModelBase::taskData(const Task *t, int role) const
{
    switch (role) {
    case Qt::DisplayRole: {
        static const QLatin1String glyph[] = {QLatin1String("[ ] "), QLatin1String("[/] "),
                                              QLatin1String("[x] "), QLatin1String("[-] ")};
        QString s = glyph[int(t->state)] + t->text;
        if (t->subTotal > 0)
            s += QStringLiteral("  (%1/%2)").arg(t->subDone).arg(t->subTotal);
        return s;
    }
    case Qt::EditRole:
        return t->content;
    case Qt::ForegroundRole:
        if (t->state == TaskState::Done || t->state == TaskState::Cancelled)
            return m_dim;
        if (t->state == TaskState::Doing)
            return m_doing;
        return {};
    case Qt::FontRole:
        switch (t->state) {
        case TaskState::Done: return m_strike;
        case TaskState::Cancelled: return m_strikeItalic;
        case TaskState::Doing: return m_bold;
        default: return {};
        }
    case Qt::ToolTipRole:
        return QStringLiteral("%1:%2\n%3").arg(t->file->relPath).arg(t->line + 1).arg(t->raw());
    default:
        return {};
    }
}

QVariant TaskModelBase::dueData(const Task *t, int role) const
{
    if (!t->due.isValid())
        return {};
    switch (role) {
    case Qt::DisplayRole:
        return t->due.toString(Qt::ISODate);
    case Qt::ForegroundRole: {
        if (!t->isOpen())
            return m_dim;
        const QDate today = QDate::currentDate();
        if (t->due < today)
            return m_overdue;
        if (t->due == today)
            return m_todayColor;
        return {};
    }
    default:
        return {};
    }
}

QVariant TaskModelBase::etaData(const Task *t, int role) const
{
    if (t->eta <= 0)
        return {};
    switch (role) {
    case Qt::DisplayRole:
        return t->etaText;
    case Qt::TextAlignmentRole:
        return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
    case Qt::ForegroundRole:
        if (!t->isOpen())
            return m_dim;
        return t->eta <= 2 ? QVariant(m_quick) : QVariant(); // GTD: do it now
    case Qt::ToolTipRole:
        return t->eta <= 2 ? tr("Two minutes or less: do it now") : QVariant();
    default:
        return {};
    }
}

// ---------------------------------------------------------------- list

TaskListModel::TaskListModel(TaskStore *store, QObject *parent) : TaskModelBase(store, parent)
{
    connect(store, &TaskStore::changed, this, [this] {
        rebuild();
        if (m_resetting) {
            m_resetting = false;
            endResetModel();
        }
    });
    rebuild();
}

void TaskListModel::refilter()
{
    beginResetModel();
    rebuild();
    endResetModel();
}

void TaskListModel::rebuild()
{
    m_rows.clear();
    const bool all = m_filter.showsEverything();
    std::vector<char> visible;
    for (const auto &fp : m_store->files()) {
        TaskFile *f = fp.get();
        std::vector<Task> &tasks = f->tasks;
        const size_t n = tasks.size();
        bool any = all && n > 0;
        if (!all) {
            // Children follow their parent, so a reverse pass sees every
            // descendant's verdict before deciding on the parent.
            visible.assign(n, 0);
            for (size_t i = n; i-- > 0;) {
                Task &t = tasks[i];
                if (!visible[i] && m_filter.matches(t))
                    visible[i] = 1;
                if (visible[i]) {
                    any = true;
                    if (t.parent)
                        visible[size_t(t.parent - tasks.data())] = 1;
                }
            }
        }
        if (!any) {
            for (Task &t : tasks)
                t.listRow = -1;
            continue;
        }
        m_rows.push_back({f, 0});
        for (size_t i = 0; i < n; ++i) {
            Task &t = tasks[i];
            if (all || visible[i]) {
                t.listRow = int(m_rows.size());
                m_rows.push_back({&t, t.depth + 1});
            } else {
                t.listRow = -1;
            }
        }
    }
}

QModelIndex TaskListModel::index(int row, int column, const QModelIndex &parent) const
{
    if (parent.isValid() || row < 0 || size_t(row) >= m_rows.size() || column < 0 || column >= ColumnCount)
        return {};
    return createIndex(row, column);
}

QModelIndex TaskListModel::parent(const QModelIndex &) const
{
    return {};
}

int TaskListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

int TaskListModel::columnCount(const QModelIndex &) const
{
    return ColumnCount;
}

QVariant TaskListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};
    const Row &r = m_rows[size_t(index.row())];
    if (role == DepthRole)
        return r.depth;
    if (r.node->isFile) {
        const TaskFile *f = static_cast<TaskFile *>(r.node);
        if (index.column() != TextColumn)
            return {};
        switch (role) {
        case Qt::DisplayRole:
            return QStringLiteral("%1  (%2/%3)").arg(f->relPath).arg(f->done).arg(f->total);
        case Qt::FontRole:
            return m_bold;
        case Qt::ToolTipRole:
            return f->path;
        default:
            return {};
        }
    }
    const Task *t = static_cast<Task *>(r.node);
    switch (index.column()) {
    case TextColumn: return taskData(t, role);
    case EtaColumn: return etaData(t, role);
    case DueColumn: return dueData(t, role);
    default: return {};
    }
}

QVariant TaskListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case TextColumn: return tr("Task");
    case EtaColumn: return tr("ETA");
    case DueColumn: return tr("Due");
    default: return {};
    }
}

Task *TaskListModel::taskAt(const QModelIndex &index) const
{
    if (!index.isValid() || index.model() != this)
        return nullptr;
    Node *n = m_rows[size_t(index.row())].node;
    return n->isFile ? nullptr : static_cast<Task *>(n);
}

TaskFile *TaskListModel::fileAt(const QModelIndex &index) const
{
    if (!index.isValid() || index.model() != this)
        return nullptr;
    Node *n = m_rows[size_t(index.row())].node;
    return n->isFile ? static_cast<TaskFile *>(n) : static_cast<Task *>(n)->file;
}

QModelIndex TaskListModel::indexOf(const Task *task) const
{
    return task && task->listRow >= 0 ? createIndex(task->listRow, 0) : QModelIndex();
}

// ---------------------------------------------------------------- agenda

AgendaModel::AgendaModel(TaskStore *store, QObject *parent) : TaskModelBase(store, parent)
{
    connect(store, &TaskStore::changed, this, [this] {
        rebuild();
        if (m_resetting) {
            m_resetting = false;
            endResetModel();
        }
    });
    rebuild();
}

void AgendaModel::setShowEmptyDays(bool on)
{
    if (on == m_showEmpty)
        return;
    beginResetModel();
    m_showEmpty = on;
    rebuild();
    endResetModel();
}

void AgendaModel::refreshIfDateChanged()
{
    if (QDate::currentDate() == m_today)
        return;
    beginResetModel();
    rebuild();
    endResetModel();
}

void AgendaModel::rebuild()
{
    m_today = QDate::currentDate();
    m_days.clear();
    m_index.clear();

    std::vector<Task *> due;
    for (const auto &f : m_store->files()) {
        for (Task &t : f->tasks) {
            if (t.due.isValid() && t.isOpen())
                due.push_back(&t);
        }
    }
    std::sort(due.begin(), due.end(), [](const Task *a, const Task *b) {
        if (a->due != b->due)
            return a->due < b->due;
        if (a->file != b->file)
            return a->file->row < b->file->row;
        return a->line < b->line;
    });

    for (Task *t : due) {
        if (m_days.empty() || m_days.back().date != t->due)
            m_days.push_back({t->due, {}});
        m_days.back().tasks.push_back(t);
    }

    if (m_showEmpty) {
        // Every day from today to the last due date (at least two weeks ahead).
        // Overdue days still only appear when they have tasks.
        QDate last = m_today.addDays(13);
        if (!m_days.empty())
            last = std::min(std::max(last, m_days.back().date), m_today.addDays(366));
        std::vector<Day> filled;
        size_t i = 0;
        for (; i < m_days.size() && m_days[i].date < m_today; ++i)
            filled.push_back(std::move(m_days[i]));
        for (QDate d = m_today; d <= last; d = d.addDays(1)) {
            if (i < m_days.size() && m_days[i].date == d)
                filled.push_back(std::move(m_days[i++]));
            else
                filled.push_back({d, {}});
        }
        for (; i < m_days.size(); ++i)
            filled.push_back(std::move(m_days[i]));
        m_days = std::move(filled);
    }

    for (size_t d = 0; d < m_days.size(); ++d) {
        for (size_t r = 0; r < m_days[d].tasks.size(); ++r)
            m_index.insert(m_days[d].tasks[r], createIndex(int(r), 0, quintptr(d + 1)));
    }
}

QModelIndex AgendaModel::index(int row, int column, const QModelIndex &parent) const
{
    if (row < 0 || column < 0 || column >= ColumnCount)
        return {};
    if (!parent.isValid())
        return size_t(row) < m_days.size() ? createIndex(row, column, quintptr(0)) : QModelIndex();
    if (parent.internalId() != 0 || size_t(parent.row()) >= m_days.size())
        return {};
    if (size_t(row) >= m_days[size_t(parent.row())].tasks.size())
        return {};
    return createIndex(row, column, quintptr(parent.row() + 1));
}

QModelIndex AgendaModel::parent(const QModelIndex &child) const
{
    if (!child.isValid() || child.internalId() == 0)
        return {};
    return createIndex(int(child.internalId() - 1), 0, quintptr(0));
}

int AgendaModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return int(m_days.size());
    if (parent.internalId() != 0 || parent.column() != 0)
        return 0;
    return int(m_days[size_t(parent.row())].tasks.size());
}

int AgendaModel::columnCount(const QModelIndex &) const
{
    return ColumnCount;
}

QVariant AgendaModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};
    if (index.internalId() == 0) {
        const Day &day = m_days[size_t(index.row())];
        if (index.column() != TextColumn)
            return {};
        switch (role) {
        case Qt::DisplayRole: {
            const qint64 n = m_today.daysTo(day.date);
            QString rel;
            if (n == 0)
                rel = tr("today");
            else if (n == 1)
                rel = tr("tomorrow");
            else if (n == -1)
                rel = tr("yesterday");
            else if (n < 0)
                rel = tr("%1 days overdue").arg(-n);
            else
                rel = tr("in %1 days").arg(n);
            QString s = day.date.toString(QStringLiteral("ddd yyyy-MM-dd")) + QLatin1String("  ") + rel;
            if (!day.tasks.empty())
                s += QStringLiteral("  (%1)").arg(day.tasks.size());
            return s;
        }
        case Qt::FontRole:
            return m_bold;
        case Qt::ForegroundRole:
            if (day.date < m_today)
                return m_overdue;
            if (day.date == m_today)
                return m_todayColor;
            if (day.tasks.empty())
                return m_dim;
            return {};
        default:
            return {};
        }
    }
    const Task *t = m_days[size_t(index.internalId() - 1)].tasks[size_t(index.row())];
    switch (index.column()) {
    case TextColumn:
        return taskData(t, role);
    case EtaColumn:
        return etaData(t, role);
    case FileColumn:
        if (role == Qt::DisplayRole)
            return QStringLiteral("%1:%2").arg(t->file->relPath).arg(t->line + 1);
        if (role == Qt::ForegroundRole)
            return m_dim;
        return {};
    default:
        return {};
    }
}

QVariant AgendaModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case TextColumn: return tr("Task");
    case EtaColumn: return tr("ETA");
    case FileColumn: return tr("File");
    default: return {};
    }
}

Task *AgendaModel::taskAt(const QModelIndex &index) const
{
    if (!index.isValid() || index.model() != this || index.internalId() == 0)
        return nullptr;
    return m_days[size_t(index.internalId() - 1)].tasks[size_t(index.row())];
}

TaskFile *AgendaModel::fileAt(const QModelIndex &index) const
{
    Task *t = taskAt(index);
    return t ? t->file : nullptr;
}

QModelIndex AgendaModel::indexOf(const Task *task) const
{
    return m_index.value(task);
}

// ---------------------------------------------------------------- agenda filter

AgendaFilterProxy::AgendaFilterProxy(QObject *parent) : QSortFilterProxyModel(parent)
{
    setRecursiveFilteringEnabled(true);
}

void AgendaFilterProxy::refilter()
{
    // begin/endFilterChange arrived in Qt 6.10; older versions only have invalidate.
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    endFilterChange(Direction::Rows);
#else
    invalidateRowsFilter();
#endif
}

bool AgendaFilterProxy::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    auto *src = static_cast<TaskModelBase *>(sourceModel());
    const Task *t = src->taskAt(src->index(row, 0, parent));
    if (!t)
        return !m_filter.narrows(); // day header
    return m_filter.matches(*t);
}

#include "models.h"
#include "theme.h"

#include "parser.h"
#include "store.h"

#include <QApplication>
#include <QPalette>
#include <QPoint>

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
    m_tags.clear();
    m_textConds.clear();
    m_due.clear();
    m_done.clear();
    m_isMask = 0;
    m_today = QDate::currentDate();
    for (const QString &term : text.split(u' ', Qt::SkipEmptyParts)) {
        EtaCond c;
        if (parseEtaCond(term, &c)) {
            m_textConds.push_back(c);
            continue;
        }
        const QString lower = term.toLower();
        if (const QStringView tag = parser::tagOf(lower); !tag.isEmpty() && tag.size() + 1 == lower.size()) {
            m_tags << tag.toString();
            continue;
        }
        if (lower.startsWith(QLatin1String("due:"))) {
            static const std::pair<const char *, Due> dues[] = {
                {"overdue", Due::Overdue}, {"today", Due::Today}, {"week", Due::Week},
                {"none", Due::None}, {"any", Due::Any}};
            bool known = false;
            for (const auto &[name, d] : dues) {
                if (lower.mid(4) == QLatin1String(name)) {
                    m_due.push_back(d);
                    known = true;
                }
            }
            if (known)
                continue;
        } else if (lower.startsWith(QLatin1String("done:"))) {
            static const std::pair<const char *, Done> dones[] = {
                {"today", Done::Today}, {"yesterday", Done::Yesterday}, {"week", Done::Week}, {"any", Done::Any}};
            bool known = false;
            for (const auto &[name, d] : dones) {
                if (lower.mid(5) == QLatin1String(name)) {
                    m_done.push_back(d);
                    known = true;
                }
            }
            if (known)
                continue;
        } else if (lower.startsWith(QLatin1String("is:"))) {
            static const std::pair<const char *, unsigned> states[] = {
                {"todo", 1u << int(TaskState::Todo)}, {"doing", 1u << int(TaskState::Doing)},
                {"done", 1u << int(TaskState::Done)}, {"cancelled", 1u << int(TaskState::Cancelled)},
                {"open", (1u << int(TaskState::Todo)) | (1u << int(TaskState::Doing))}};
            bool known = false;
            for (const auto &[name, bits] : states) {
                if (lower.mid(3) == QLatin1String(name)) {
                    m_isMask |= bits;
                    known = true;
                }
            }
            if (known)
                continue;
        }
        m_terms << term;
    }
}

void TaskFilter::setStateVisible(TaskState state, bool visible)
{
    const unsigned bit = 1u << int(state);
    m_states = visible ? (m_states | bit) : (m_states & ~bit);
}

bool TaskFilter::narrows() const
{
    return !m_terms.isEmpty() || !m_tags.isEmpty() || !m_textConds.empty() || !m_presetConds.empty() || !m_due.empty() || !m_done.empty() || m_isMask;
}

bool TaskFilter::showsEverything() const
{
    return m_states == 0xF && !narrows();
}

bool TaskFilter::matches(const Task &t) const
{
    const unsigned bit = 1u << int(t.state);
    if (m_isMask || !m_done.empty()) {
        // An explicit state term decides on its own; the toolbar toggles don't apply.
        if (m_isMask && !(m_isMask & bit))
            return false;
    } else if (!(m_states & bit)) {
        return false;
    }
    for (Done d : m_done) {
        if (t.state != TaskState::Done)
            return false;
        bool ok = false;
        switch (d) {
        case Done::Today: ok = t.completed == m_today; break;
        case Done::Yesterday: ok = t.completed == m_today.addDays(-1); break;
        case Done::Week: ok = t.completed.isValid() && t.completed > m_today.addDays(-7) && t.completed <= m_today; break;
        case Done::Any: ok = true; break;
        }
        if (!ok)
            return false;
    }
    for (const EtaCond &c : m_presetConds) {
        if (!c.holds(t.eta))
            return false;
    }
    for (const EtaCond &c : m_textConds) {
        if (!c.holds(t.eta))
            return false;
    }
    for (Due d : m_due) {
        bool ok = false;
        switch (d) {
        case Due::Overdue: ok = t.isOpen() && t.due.isValid() && t.due < m_today; break;
        case Due::Today: ok = t.due == m_today; break;
        case Due::Week: ok = t.due.isValid() && t.due >= m_today && t.due < m_today.addDays(7); break;
        case Due::None: ok = !t.due.isValid(); break;
        case Due::Any: ok = t.due.isValid(); break;
        }
        if (!ok)
            return false;
    }
    for (const QString &tag : m_tags) {
        // Subtasks inherit the tags of their ancestors.
        bool has = false;
        for (const Task *a = &t; a && !has; a = a->parent) {
            has = std::any_of(a->tags.cbegin(), a->tags.cend(), [&](const QString &x) {
                return x.startsWith(tag, Qt::CaseInsensitive) && (x.size() == tag.size() || x[tag.size()] == u'/');
            });
        }
        if (!has)
            return false;
    }
    for (const QString &term : m_terms) {
        if (!t.content.contains(term, Qt::CaseInsensitive) && !t.file->relPath.contains(term, Qt::CaseInsensitive))
            return false;
    }
    return true;
}

TagIndex::TagIndex(TaskStore *store, QObject *parent) : QObject(parent), m_store(store)
{
    connect(store, &TaskStore::changed, this, &TagIndex::rebuild);
    rebuild();
}

void TagIndex::rebuild()
{
    QHash<QString, size_t> at; // lower case -> index in m_entries
    m_entries.clear();
    QStringList keys;
    for (const auto &f : m_store->files()) {
        for (const Task &t : f->tasks) {
            if (t.tags.isEmpty())
                continue;
            keys.clear();
            for (const QString &tag : t.tags) {
                // The tag and each parent of it: work/meeting/notes -> work, work/meeting, ...
                for (qsizetype i = tag.indexOf(u'/'); ; i = tag.indexOf(u'/', i + 1)) {
                    const QString part = i < 0 ? tag : tag.left(i);
                    if (!part.isEmpty()) {
                        const QString key = part.toLower();
                        if (!keys.contains(key)) {
                            keys << key;
                            auto it = at.constFind(key);
                            if (it == at.cend()) {
                                it = at.insert(key, m_entries.size());
                                m_entries.push_back({part});
                            }
                            Entry &e = m_entries[*it];
                            ++e.total;
                            if (t.isOpen())
                                ++e.open;
                        }
                    }
                    if (i < 0)
                        break;
                }
            }
        }
    }
    std::sort(m_entries.begin(), m_entries.end(), [](const Entry &a, const Entry &b) {
        return a.tag.compare(b.tag, Qt::CaseInsensitive) < 0;
    });
    emit changed();
}

QString tasksAsBulletList(const std::vector<Task *> &tasks)
{
    const QSet<const Task *> in(tasks.begin(), tasks.end());
    QString out;
    for (const Task *t : tasks) {
        int level = 0;
        for (const Task *a = t->parent; a; a = a->parent)
            level += in.contains(a) ? 1 : 0;
        out += QString(level * 2, u' ') + QStringLiteral("- ") + (t->text.isEmpty() ? t->content : t->text) + u'\n';
    }
    return out;
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
}

static QColor mix(const QColor &a, const QColor &b, double t)
{
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t), float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF() + (b.blueF() - a.blueF()) * t));
}

TaskColors TaskColors::fromPalette(const QPalette &pal)
{
    TaskColors c;
    const QColor base = pal.color(QPalette::Base), text = pal.color(QPalette::Text);
    c.dark = pal.color(QPalette::Window).lightness() < 128;
    c.dim = pal.color(QPalette::Disabled, QPalette::Text);
    c.doing = c.dark ? QColor(0x6c, 0xaa, 0xf0) : QColor(0x1f, 0x6f, 0xd0);
    if (const QColor a = theme::accent(); a.isValid())
        c.doing = a;
    c.overdue = c.dark ? QColor(0xf0, 0x70, 0x70) : QColor(0xc8, 0x2a, 0x2a);
    c.today = c.dark ? QColor(0xe8, 0xa8, 0x40) : QColor(0xb0, 0x6a, 0x00);
    c.quick = c.dark ? QColor(0x7c, 0xc8, 0x7c) : QColor(0x24, 0x80, 0x3a);
    c.done = c.dark ? QColor(0x4c, 0xa8, 0x6a) : QColor(0x2e, 0x9a, 0x57);
    c.band = mix(base, text, c.dark ? 0.08 : 0.055);
    c.guide = mix(base, text, 0.2);
    c.checkBorder = mix(base, text, 0.45);
    c.pill = mix(base, text, c.dark ? 0.22 : 0.12);
    c.pillText = mix(base, text, c.dark ? 0.85 : 0.78);
    return c;
}

void TaskModelBase::refreshColors()
{
    m_colors = TaskColors::fromPalette(QApplication::palette());
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
    case Qt::DisplayRole:
        return t->text;
    case StateRole:
        return int(t->state);
    case ProgressRole:
        return t->subTotal > 0 ? QVariant(QPoint(t->subDone, t->subTotal)) : QVariant();
    case Qt::EditRole:
        return t->content;
    case Qt::ForegroundRole:
        if (t->state == TaskState::Done || t->state == TaskState::Cancelled)
            return m_colors.dim;
        if (t->state == TaskState::Doing)
            return m_colors.doing;
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
            return m_colors.dim;
        const QDate today = QDate::currentDate();
        if (t->due < today)
            return m_colors.overdue;
        if (t->due == today)
            return m_colors.today;
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
            return m_colors.dim;
        return t->eta <= 2 ? QVariant(m_colors.quick) : QVariant(); // GTD: do it now
    case Qt::ToolTipRole:
        return t->eta <= 2 ? tr("Two minutes or less: do it now") : QVariant();
    default:
        return {};
    }
}

QVariant TaskModelBase::tagsData(const Task *t, int role) const
{
    if (t->tags.isEmpty())
        return {};
    switch (role) {
    case TagsRole:
        return t->tags;
    case Qt::DisplayRole:
    case Qt::ToolTipRole: {
        QString s;
        for (const QString &tag : t->tags)
            s += (s.isEmpty() ? QStringLiteral("#") : QStringLiteral(" #")) + tag;
        if (role == Qt::ToolTipRole)
            s += u'\n' + tr("Click a tag to show everything tagged with it");
        return s;
    }
    default:
        return {};
    }
}

// ---------------------------------------------------------------- flat tree

FlatTaskModel::FlatTaskModel(TaskStore *store, QObject *parent) : TaskModelBase(store, parent)
{
    // Rows hold raw task pointers, which a store mutation invalidates, so the
    // model rebuilds wholesale around every change.
    connect(store, &TaskStore::aboutToChange, this, [this] {
        if (!m_resetting) {
            m_resetting = true;
            beginResetModel();
        }
    });
    connect(store, &TaskStore::changed, this, [this] {
        rebuild();
        if (m_resetting) {
            m_resetting = false;
            endResetModel();
        }
    });
}

QString FlatTaskModel::taskKey(const Task *t)
{
    return t->file->path + QChar(0x1f) + t->text;
}

QString FlatTaskModel::rowKey(const Row &r) const
{
    return r.task ? taskKey(r.task) : m_groups[size_t(r.group)].key;
}

bool FlatTaskModel::isCollapsed(const QString &key) const
{
    const QSet<QString> &c = collapsed();
    return !c.isEmpty() && c.contains(key);
}

void FlatTaskModel::reset()
{
    beginResetModel();
    rebuild();
    endResetModel();
}

void FlatTaskModel::refilter()
{
    m_tempCollapsed.clear();
    reset();
}

void FlatTaskModel::rebuild()
{
    m_groups.clear();
    collectGroups(m_groups);
    m_rows.clear();
    clearIndex();
    m_all = m_filter.showsEverything();
    for (size_t g = 0; g < m_groups.size(); ++g)
        appendGroupRows(int(g), m_rows);
    reindex(0);
}

void FlatTaskModel::reindex(size_t)
{
    m_rowOf.clear();
    for (size_t i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].task && !m_rowOf.contains(m_rows[i].task))
            m_rowOf.insert(m_rows[i].task, int(i));
    }
}

void FlatTaskModel::appendGroupRows(int g, std::vector<Row> &out) const
{
    const Group &group = m_groups[size_t(g)];
    const size_t mark = out.size();
    const bool open = !isCollapsed(group.key);
    out.push_back({nullptr, g, 0, false, open});
    bool any = false;
    for (Task *t : group.roots)
        any |= appendTask(t, g, 1, !open, out);
    if (!any && !(group.keepWhenEmpty && !m_filter.narrows())) {
        out.resize(mark);
        return;
    }
    out[mark].hasChildren = any;
}

// Appends t and its visible descendants (unless `hidden`, i.e. under a
// collapsed node, in which case only visibility is computed). Returns whether
// t is visible: it matches or has a visible descendant.
bool FlatTaskModel::appendTask(Task *t, int g, int depth, bool hidden, std::vector<Row> &out) const
{
    if (m_all) {
        if (hidden)
            return true;
        const bool kids = !t->children.empty();
        const bool open = !kids || !isCollapsed(taskKey(t));
        out.push_back({t, g, depth, kids, open});
        if (open) {
            for (Task *c : t->children)
                appendTask(c, g, depth + 1, false, out);
        }
        return true;
    }
    const size_t mark = out.size();
    if (!hidden)
        out.push_back({t, g, depth, false, true});
    const bool open = t->children.empty() || !isCollapsed(taskKey(t));
    bool anyChild = false;
    for (Task *c : t->children)
        anyChild |= appendTask(c, g, depth + 1, hidden || !open, out);
    if (!anyChild && !m_filter.matches(*t)) {
        if (!hidden)
            out.resize(mark);
        return false;
    }
    if (!hidden) {
        out[mark].hasChildren = anyChild;
        out[mark].expanded = open || !anyChild;
    }
    return true;
}

void FlatTaskModel::setExpanded(int row, bool expand)
{
    if (row < 0 || size_t(row) >= m_rows.size())
        return;
    const Row r = m_rows[size_t(row)];
    if (!r.hasChildren || r.expanded == expand)
        return;
    if (expand)
        collapsed().remove(rowKey(r));
    else
        collapsed().insert(rowKey(r));
    m_rows[size_t(row)].expanded = expand;

    if (!expand) {
        size_t end = size_t(row) + 1;
        while (end < m_rows.size() && m_rows[end].depth > r.depth)
            ++end;
        if (end > size_t(row) + 1) {
            beginRemoveRows({}, row + 1, int(end) - 1);
            m_rows.erase(m_rows.begin() + row + 1, m_rows.begin() + qsizetype(end));
            clearIndex();
            reindex(0);
            endRemoveRows();
        }
    } else {
        // Regenerate the group's rows and take the run below the node.
        std::vector<Row> groupRows;
        appendGroupRows(r.group, groupRows);
        auto it = std::find_if(groupRows.begin(), groupRows.end(),
                               [&](const Row &x) { return x.task == r.task && x.depth == r.depth; });
        if (it != groupRows.end()) {
            auto first = it + 1, last = first;
            while (last != groupRows.end() && last->depth > r.depth)
                ++last;
            if (last != first) {
                beginInsertRows({}, row + 1, row + int(last - first));
                m_rows.insert(m_rows.begin() + row + 1, first, last);
                reindex(size_t(row) + 1);
                endInsertRows();
            }
        }
    }
    const QModelIndex i = index(row, 0);
    emit dataChanged(i, i, {ExpandedRole});
}

void FlatTaskModel::expandAll()
{
    collapsed().clear();
    reset();
}

void FlatTaskModel::collapseAll()
{
    for (const Group &g : m_groups)
        collapsed().insert(g.key);
    reset();
}

int FlatTaskModel::parentRow(int row) const
{
    if (row <= 0 || size_t(row) >= m_rows.size())
        return -1;
    const int depth = m_rows[size_t(row)].depth;
    for (int r = row - 1; r >= 0; --r) {
        if (m_rows[size_t(r)].depth < depth)
            return r;
    }
    return -1;
}

QStringList FlatTaskModel::collapsedKeys() const
{
    return QStringList(m_collapsed.begin(), m_collapsed.end());
}

void FlatTaskModel::setCollapsedKeys(const QStringList &keys)
{
    m_collapsed = QSet<QString>(keys.begin(), keys.end());
    reset();
}

QModelIndex FlatTaskModel::index(int row, int column, const QModelIndex &parent) const
{
    if (parent.isValid() || row < 0 || size_t(row) >= m_rows.size() || column < 0 || column >= columnCount())
        return {};
    return createIndex(row, column);
}

QModelIndex FlatTaskModel::parent(const QModelIndex &) const
{
    return {};
}

int FlatTaskModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant FlatTaskModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};
    const Row &r = m_rows[size_t(index.row())];
    switch (role) {
    case DepthRole: return r.depth;
    case HasChildrenRole: return r.hasChildren;
    case ExpandedRole: return r.expanded;
    case HeaderRole: return r.task == nullptr;
    default: break;
    }
    return r.task ? taskRowData(r.task, index.column(), role) : groupData(m_groups[size_t(r.group)], index.column(), role);
}

Task *FlatTaskModel::taskAt(const QModelIndex &index) const
{
    if (!index.isValid() || index.model() != this)
        return nullptr;
    return m_rows[size_t(index.row())].task;
}

TaskFile *FlatTaskModel::fileAt(const QModelIndex &index) const
{
    if (!index.isValid() || index.model() != this)
        return nullptr;
    const Row &r = m_rows[size_t(index.row())];
    return r.task ? r.task->file : m_groups[size_t(r.group)].file;
}

QModelIndex FlatTaskModel::indexOf(const Task *task) const
{
    const auto it = m_rowOf.constFind(task);
    return it == m_rowOf.cend() ? QModelIndex() : createIndex(*it, 0);
}

// ---------------------------------------------------------------- list

TaskListModel::TaskListModel(TaskStore *store, QObject *parent) : FlatTaskModel(store, parent)
{
    reset();
}

int TaskListModel::columnCount(const QModelIndex &) const
{
    return ColumnCount;
}

void TaskListModel::collectGroups(std::vector<Group> &out) const
{
    out.reserve(m_store->files().size());
    for (const auto &f : m_store->files()) {
        Group g;
        g.key = f->path;
        g.file = f.get();
        g.roots = f->roots;
        out.push_back(std::move(g));
    }
}

void TaskListModel::clearIndex()
{
    for (const auto &f : m_store->files()) {
        for (Task &t : f->tasks)
            t.listRow = -1;
    }
}

void TaskListModel::reindex(size_t first)
{
    for (size_t i = first; i < m_rows.size(); ++i) {
        if (m_rows[i].task)
            m_rows[i].task->listRow = int(i);
    }
}

QModelIndex TaskListModel::indexOf(const Task *task) const
{
    return task && task->listRow >= 0 ? createIndex(task->listRow, 0) : QModelIndex();
}

QModelIndex TaskListModel::reveal(const Task *task)
{
    if (!task)
        return {};
    if (task->listRow < 0) {
        forgetCollapsed(task->file->path);
        for (const Task *a = task->parent; a; a = a->parent)
            forgetCollapsed(taskKey(a));
        reset();
    }
    return indexOf(task);
}

QVariant TaskListModel::groupData(const Group &g, int column, int role) const
{
    if (column != TextColumn)
        return {};
    switch (role) {
    case Qt::DisplayRole:
        if (m_templateHeaders) {
            QString name = g.file->relPath;
            if (name.endsWith(QLatin1String(".md"), Qt::CaseInsensitive))
                name.chop(3);
            return name;
        }
        return g.file->relPath;
    case ProgressRole:
        if (m_templateHeaders)
            return {}; // progress means nothing for a template (states reset on insert)
        return QPoint(g.file->done, g.file->total);
    case Qt::FontRole: return m_bold;
    case Qt::ToolTipRole: return g.file->path;
    default: return {};
    }
}

QVariant TaskListModel::taskRowData(const Task *t, int column, int role) const
{
    switch (column) {
    case TextColumn: return taskData(t, role);
    case TagsColumn: return tagsData(t, role);
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
    case TagsColumn: return tr("Tags");
    case EtaColumn: return tr("ETA");
    case DueColumn: return tr("Due");
    default: return {};
    }
}

// ---------------------------------------------------------------- agenda

AgendaModel::AgendaModel(TaskStore *store, QObject *parent) : FlatTaskModel(store, parent)
{
    reset();
}

int AgendaModel::columnCount(const QModelIndex &) const
{
    return ColumnCount;
}

void AgendaModel::setShowEmptyDays(bool on)
{
    if (on == m_showEmpty)
        return;
    m_showEmpty = on;
    reset();
}

void AgendaModel::setSingleDay(const QDate &day)
{
    if (day == m_day)
        return;
    m_day = day;
    reset();
}

void AgendaModel::refreshIfDateChanged()
{
    if (QDate::currentDate() != m_today)
        reset();
}

void AgendaModel::collectGroups(std::vector<Group> &out) const
{
    m_today = QDate::currentDate();
    std::vector<Task *> due;
    for (const auto &f : m_store->files()) {
        for (Task &t : f->tasks) {
            if (t.due.isValid() && t.isOpen() && (!m_day.isValid() || t.due == m_day))
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

    auto makeDay = [](const QDate &d) {
        Group g;
        g.key = QStringLiteral("day:") + d.toString(Qt::ISODate);
        g.date = d;
        return g;
    };
    std::vector<Group> days;
    for (size_t i = 0; i < due.size();) {
        Group g = makeDay(due[i]->due);
        // A subtask due the same day as an ancestor already shows under it.
        QSet<const Task *> inDay;
        size_t j = i;
        for (; j < due.size() && due[j]->due == g.date; ++j)
            inDay.insert(due[j]);
        for (size_t k = i; k < j; ++k) {
            bool nested = false;
            for (const Task *a = due[k]->parent; a && !nested; a = a->parent)
                nested = inDay.contains(a);
            if (!nested)
                g.roots.push_back(due[k]);
        }
        days.push_back(std::move(g));
        i = j;
    }

    if (m_day.isValid()) {
        if (days.empty())
            days.push_back(makeDay(m_day));
        days.front().keepWhenEmpty = true;
    } else if (m_showEmpty) {
        // Every day from today to the last due date (at least two weeks ahead).
        // Overdue days still only appear when they have tasks.
        QDate last = m_today.addDays(13);
        if (!days.empty())
            last = std::min(std::max(last, days.back().date), m_today.addDays(366));
        std::vector<Group> filled;
        size_t i = 0;
        for (; i < days.size() && days[i].date < m_today; ++i)
            filled.push_back(std::move(days[i]));
        for (QDate d = m_today; d <= last; d = d.addDays(1)) {
            if (i < days.size() && days[i].date == d) {
                filled.push_back(std::move(days[i++]));
            } else {
                filled.push_back(makeDay(d));
                filled.back().keepWhenEmpty = true;
            }
        }
        for (; i < days.size(); ++i)
            filled.push_back(std::move(days[i]));
        days = std::move(filled);
    }
    out = std::move(days);
}

QVariant AgendaModel::groupData(const Group &g, int column, int role) const
{
    if (column != TextColumn)
        return {};
    switch (role) {
    case Qt::DisplayRole: {
        // Relative wording only within two weeks; "overdue" only when the day
        // actually has open tasks (an empty past day in the sidebar is just past).
        const qint64 n = m_today.daysTo(g.date);
        QString rel;
        if (n == 0)
            rel = tr("today");
        else if (n == 1)
            rel = tr("tomorrow");
        else if (n == -1)
            rel = g.roots.empty() ? tr("yesterday") : tr("1 day overdue");
        else if (n < 0 && n >= -14)
            rel = g.roots.empty() ? tr("%1 days ago").arg(-n) : tr("%1 days overdue").arg(-n);
        else if (n > 0 && n <= 14)
            rel = tr("in %1 days").arg(n);
        QString s = g.date.toString(QStringLiteral("ddd yyyy-MM-dd"));
        if (!rel.isEmpty())
            s += QLatin1String("  ") + rel;
        if (!g.roots.empty())
            s += QStringLiteral("  (%1)").arg(g.roots.size());
        return s;
    }
    case Qt::FontRole:
        return m_bold;
    case Qt::ForegroundRole:
        if (g.roots.empty())
            return g.date == m_today ? m_colors.today : m_colors.dim;
        if (g.date < m_today)
            return m_colors.overdue;
        if (g.date == m_today)
            return m_colors.today;
        return {};
    default:
        return {};
    }
}

QVariant AgendaModel::taskRowData(const Task *t, int column, int role) const
{
    switch (column) {
    case TextColumn:
        return taskData(t, role);
    case TagsColumn:
        return tagsData(t, role);
    case EtaColumn:
        return etaData(t, role);
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
    case TagsColumn: return tr("Tags");
    case EtaColumn: return tr("ETA");
    default: return {};
    }
}

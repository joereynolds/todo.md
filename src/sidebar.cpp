#include "sidebar.h"

#include "models.h"
#include "store.h"
#include "taskview.h"

#include <QAbstractButton>
#include <cmath>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QDesktopServices>
#include <QPainter>
#include <QToolButton>
#include <QScrollBar>
#include <QScrollArea>
#include <QMouseEvent>
#include <QPushButton>
#include <QTextCharFormat>
#include <QVBoxLayout>

// A count with a label, drawn as a small tile; clicking applies `filter`.
class StatButton : public QAbstractButton
{
public:
    StatButton(const QString &label, const QString &filter, QWidget *parent)
        : QAbstractButton(parent), m_label(label), m_filter(filter)
    {
        setCheckable(!filter.isEmpty());
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(this, &QAbstractButton::toggled, this, [this] { updateClickable(); });
        updateClickable();
    }

    QString filter() const { return m_filter; }
    void setCount(int n)
    {
        m_count = n;
        updateClickable();
        update();
    }

private:
    // Nothing to show at zero, so no click; an active filter stays clickable so
    // it can still be cleared after its count drops to zero.
    void updateClickable()
    {
        const bool on = !m_filter.isEmpty() && (m_count > 0 || isChecked());
        setEnabled(on);
        setCursor(on ? Qt::PointingHandCursor : Qt::ArrowCursor);
        setToolTip(on ? tr("Filter: %1 (click again to clear)").arg(m_filter) : QString());
    }

public:

    QSize sizeHint() const override
    {
        return {110, fontMetrics().height() + 10};
    }

protected:
    // Neutral on purpose: the count in the text colour (dim when zero) and the
    // label beside it; the active filter gets a slightly darker tile and outline.
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const TaskColors c = TaskColors::fromPalette(palette());
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QColor bg = Qt::transparent;
        if (isChecked())
            bg = c.pill;
        else if (underMouse() && isEnabled())
            bg = c.band;
        p.setPen(isChecked() ? QPen(c.checkBorder, 1) : Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(r, 4, 4);

        const QRect inner = rect().adjusted(8, 0, -6, 0);
        QFont bold = font();
        bold.setBold(true);
        const QString count = QString::number(m_count);
        p.setFont(bold);
        p.setPen(m_count > 0 ? palette().color(QPalette::Text) : c.dim);
        p.drawText(inner, Qt::AlignLeft | Qt::AlignVCenter, count);
        p.setFont(font());
        p.setPen(m_count > 0 ? c.pillText : c.dim);
        p.drawText(inner.adjusted(QFontMetrics(bold).horizontalAdvance(count) + 6, 0, 0, 0),
                   Qt::AlignLeft | Qt::AlignVCenter, m_label);
    }
    void enterEvent(QEnterEvent *) override { update(); }
    void leaveEvent(QEvent *) override { update(); }

private:
    QString m_label, m_filter;
    int m_count = 0;
};

// ---------------------------------------------------------------- calendar

TaskCalendar::TaskCalendar(QWidget *parent) : QCalendarWidget(parent)
{
    setGridVisible(false);
    setFirstDayOfWeek(Qt::Monday);
    setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    setHorizontalHeaderFormat(QCalendarWidget::ShortDayNames);
    setMinimumHeight(230); // room for the dots under the day numbers
    // Text chevrons instead of the style's arrow pixmaps, which are dark on every
    // theme (invisible on a dark one); text follows the palette.
    for (const auto &[name, glyph] : {std::pair{"qt_calendar_prevmonth", u"‹"}, std::pair{"qt_calendar_nextmonth", u"›"}}) {
        if (auto *b = findChild<QToolButton *>(QLatin1String(name))) {
            b->setIcon(QIcon());
            b->setToolButtonStyle(Qt::ToolButtonTextOnly);
            b->setText(QString::fromUtf16(glyph));
            QFont f = b->font();
            f.setPointSizeF(f.pointSizeF() * 1.5);
            b->setFont(f);
        }
    }
    restyle();
}

void TaskCalendar::restyle()
{
    // Weekends in the normal text colour (red means overdue here), and a quiet
    // navigation bar instead of the style's highlight colour.
    QTextCharFormat weekend;
    weekend.setForeground(palette().color(QPalette::Text));
    setWeekdayTextFormat(Qt::Saturday, weekend);
    setWeekdayTextFormat(Qt::Sunday, weekend);
    if (QWidget *nav = findChild<QWidget *>(QStringLiteral("qt_calendar_navigationbar"))) {
        const TaskColors c = TaskColors::fromPalette(palette());
        // The bar paints with the Highlight role, its buttons with HighlightedText.
        QPalette pal = palette();
        pal.setColor(QPalette::Highlight, c.band);
        pal.setColor(QPalette::HighlightedText, palette().color(QPalette::Text));
        nav->setPalette(pal);
        nav->setAutoFillBackground(true);
        for (QWidget *child : nav->findChildren<QWidget *>())
            child->setPalette(pal);
    }
}

void TaskCalendar::setCounts(QHash<QDate, int> counts)
{
    // Bold the marked days; setDateTextFormat with a null date clears all.
    setDateTextFormat(QDate(), QTextCharFormat());
    QTextCharFormat bold;
    bold.setFontWeight(QFont::Bold);
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        setDateTextFormat(it.key(), bold);
    m_counts = std::move(counts);
    updateCells();
}

void TaskCalendar::paintCell(QPainter *p, const QRect &rect, QDate date) const
{
    QCalendarWidget::paintCell(p, rect, date);
    const QDate today = QDate::currentDate();
    if (date == today && date != selectedDate()) {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(QPen(TaskColors::fromPalette(palette()).today, 1.4));
        p->setBrush(Qt::NoBrush);
        p->drawRoundedRect(QRectF(rect).adjusted(2.5, 2.5, -2.5, -2.5), 4, 4);
        p->restore();
    }
    const int n = m_counts.value(date);
    if (n <= 0)
        return;
    const TaskColors c = TaskColors::fromPalette(palette());
    const QColor color = date < today ? c.overdue : date == today ? c.today : c.doing;
    const int dots = std::min(n, 3);
    const qreal d = 4, gap = 2.5;
    const qreal width = dots * d + (dots - 1) * gap;
    qreal x = rect.center().x() - width / 2 + 0.5;
    const qreal y = rect.bottom() - d - 1;
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    p->setPen(Qt::NoPen);
    p->setBrush(color);
    for (int i = 0; i < dots; ++i, x += d + gap)
        p->drawEllipse(QRectF(x, y, d, d));
    p->restore();
}

// ---------------------------------------------------------------- tag cloud

namespace {
constexpr int kChipPadX = 7, kChipGap = 4, kCountGap = 6; // gap between the tag and its count
}

TagCloud::TagCloud(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
}

QFont TagCloud::chipFont() const
{
    QFont f = font();
    if (f.pointSizeF() > 0)
        f.setPointSizeF(f.pointSizeF() * 0.92);
    return f;
}

void TagCloud::setItems(std::vector<Item> items)
{
    m_items = std::move(items);
    m_hover = -1;
    updateGeometry();
    update();
}

void TagCloud::setActive(const QString &tag)
{
    if (tag.compare(m_active, Qt::CaseInsensitive) == 0 && tag.size() == m_active.size())
        return;
    m_active = tag;
    update();
}

std::vector<QRect> TagCloud::chipRects(int width) const
{
    const QFontMetrics fm(chipFont());
    const int h = fm.height() + 6;
    std::vector<QRect> out;
    int x = 0, y = 0;
    for (const Item &it : m_items) {
        const int textW = fm.horizontalAdvance(u'#' + it.tag) + kCountGap + fm.horizontalAdvance(QString::number(it.count));
        // A few pixels of slack: an exact fit can still elide after sub-pixel rounding.
        const int w = std::min(textW + 2 * kChipPadX + 4, std::max(20, width));
        if (x > 0 && x + w > width) {
            x = 0;
            y += h + kChipGap;
        }
        out.emplace_back(x, y, w, h);
        x += w + kChipGap;
    }
    return out;
}

int TagCloud::heightForWidth(int width) const
{
    const auto rects = chipRects(width);
    return rects.empty() ? 0 : rects.back().bottom() + 2;
}

QSize TagCloud::sizeHint() const
{
    return {200, heightForWidth(200)};
}

int TagCloud::hitTest(const QPoint &pos) const
{
    const auto rects = chipRects(width());
    for (size_t i = 0; i < rects.size(); ++i) {
        if (rects[i].contains(pos))
            return int(i);
    }
    return -1;
}

void TagCloud::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const TaskColors c = TaskColors::fromPalette(palette());
    const QFont f = chipFont();
    const QFontMetrics fm(f);
    p.setFont(f);
    const auto rects = chipRects(width());
    for (size_t i = 0; i < rects.size(); ++i) {
        const Item &it = m_items[i];
        const bool active = it.tag.compare(m_active, Qt::CaseInsensitive) == 0;
        const QRectF r = QRectF(rects[i]).adjusted(0.5, 0.5, -0.5, -0.5);
        // Same quiet look as the filter tiles: outlined when active, tinted on hover.
        p.setPen(active ? QPen(c.checkBorder, 1) : Qt::NoPen);
        p.setBrush(active || int(i) == m_hover ? c.pill : c.band);
        p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
        const QRectF inner = r.adjusted(kChipPadX, 0, -kChipPadX, 0);
        const QString count = QString::number(it.count);
        const int countW = fm.horizontalAdvance(count);
        p.setPen(c.dim);
        p.drawText(inner, Qt::AlignRight | Qt::AlignVCenter, count);
        p.setPen(c.pillText);
        const QRectF tagRect = inner.adjusted(0, 0, -(countW + kCountGap), 0);
        p.drawText(tagRect, Qt::AlignLeft | Qt::AlignVCenter,
                   fm.elidedText(u'#' + it.tag, Qt::ElideRight, int(std::ceil(tagRect.width())) + 2));
    }
}

void TagCloud::mouseMoveEvent(QMouseEvent *event)
{
    const int hit = hitTest(event->position().toPoint());
    if (hit != m_hover) {
        m_hover = hit;
        setCursor(hit >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        setToolTip(hit >= 0 ? tr("Show tasks tagged #%1 (click again to clear)").arg(m_items[size_t(hit)].tag)
                            : QString());
        update();
    }
}

void TagCloud::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}

void TagCloud::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const int hit = hitTest(event->position().toPoint());
    if (hit < 0)
        return;
    const QString &tag = m_items[size_t(hit)].tag;
    emit tagClicked(tag, tag.compare(m_active, Qt::CaseInsensitive) == 0);
}

// ---------------------------------------------------------------- sidebar

Sidebar::Sidebar(TaskStore *store, TagIndex *tags, QWidget *parent) : QWidget(parent), m_store(store), m_tags(tags)
{
    setObjectName(QStringLiteral("sidebar"));
    setAttribute(Qt::WA_StyledBackground); // so a theme's style sheet can colour it
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    auto heading = [this](const QString &text) {
        auto *l = new QLabel(text, this);
        l->setObjectName(QStringLiteral("heading"));
        QFont f = l->font();
        f.setBold(true);
        l->setFont(f);
        return l;
    };

    auto *grid = new QGridLayout;
    grid->setSpacing(2);
    const std::pair<QString, QString> stats[] = {
        {tr("overdue"), QStringLiteral("due:overdue")},
        {tr("due today"), QStringLiteral("due:today")},
        {tr("next 7 days"), QStringLiteral("due:week")},
        {tr("in progress"), QStringLiteral("is:doing")},
        {tr("quick (≤ 2m)"), QStringLiteral("~<=2m is:open")},
        {tr("done today"), QStringLiteral("done:today")},
    };
    int i = 0;
    for (const auto &[label, filter] : stats) {
        auto *b = new StatButton(label, filter, this);
        connect(b, &QAbstractButton::clicked, this, [this, b] {
            emit filterRequested(b->isChecked() ? b->filter() : QString());
        });
        grid->addWidget(b, i / 2, i % 2);
        m_stats.push_back(b);
        ++i;
    }

    auto *calHeader = new QHBoxLayout;
    calHeader->addWidget(heading(tr("Calendar")));
    calHeader->addStretch();
    auto *todayButton = new QPushButton(tr("Today"), this);
    todayButton->setFlat(true);
    todayButton->setCursor(Qt::PointingHandCursor);
    calHeader->addWidget(todayButton);
    layout->addLayout(calHeader);

    m_calendar = new TaskCalendar(this);
    layout->addWidget(m_calendar);

    m_dayModel = new AgendaModel(store, this);
    m_dayModel->setSingleDay(QDate::currentDate());
    m_dayView = new TaskView(true, this);
    m_dayView->setModel(m_dayModel);
    m_dayView->setSpanHeaders(true);
    m_dayView->setHeaderHidden(true);
    m_dayView->setRootIsDecorated(false);
    m_dayView->setItemsExpandable(false);
    m_dayView->setUniformRowHeights(true);
    m_dayView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_dayView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_dayView->hideColumn(AgendaModel::TagsColumn);
    m_dayView->header()->setStretchLastSection(false);
    m_dayView->header()->setSectionResizeMode(AgendaModel::TextColumn, QHeaderView::Stretch);
    m_dayView->header()->setSectionResizeMode(AgendaModel::EtaColumn, QHeaderView::ResizeToContents);
    m_dayView->setToolTip(tr("Double-click to show the task in the task list"));
    layout->addWidget(m_dayView, 1);

    m_add = new QLineEdit(this);
    m_add->setClearButtonEnabled(true);
    m_add->setToolTip(tr("Type a task and press Enter to add it, due on the selected day.\n"
                         "It goes after the task selected above, or else into todo.md."));
    layout->addWidget(m_add);

    layout->addWidget(heading(tr("Filters")));
    layout->addLayout(grid);

    m_tagHeading = heading(tr("Tags"));
    layout->addWidget(m_tagHeading);
    m_tagCloud = new TagCloud(this);
    m_tagArea = new QScrollArea(this);
    m_tagArea->setWidget(m_tagCloud);
    m_tagArea->setWidgetResizable(true);
    m_tagArea->setFrameShape(QFrame::NoFrame);
    m_tagArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tagArea->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(m_tagArea);
    connect(m_tagCloud, &TagCloud::tagClicked, this, [this](const QString &tag, bool active) {
        emit filterRequested(active ? QString() : u'#' + tag);
    });
    connect(m_tags, &TagIndex::changed, this, &Sidebar::updateTags);

    connect(m_calendar, &QCalendarWidget::selectionChanged, this, [this] { showDay(m_calendar->selectedDate()); });
    connect(todayButton, &QPushButton::clicked, this, [this] { showDay(QDate::currentDate()); });
    connect(m_dayView, &TaskView::linkActivated, this, [](const QUrl &url) { QDesktopServices::openUrl(url); });
    connect(m_dayView, &TaskView::checkClicked, this, [this](const QModelIndex &index) {
        if (Task *t = m_dayModel->taskAt(index))
            emit toggleRequested(t);
    });
    connect(m_dayView, &QAbstractItemView::doubleClicked, this, [this](const QModelIndex &index) {
        if (Task *t = m_dayModel->taskAt(index))
            emit taskActivated(t);
    });
    connect(m_add, &QLineEdit::returnPressed, this, [this] {
        const QString text = m_add->text().simplified();
        if (text.isEmpty())
            return;
        Task *after = selectedDayTask();
        m_add->clear(); // before emitting: the add rebuilds the models
        emit addRequested(text, m_dayModel->singleDay(), after);
    });
    connect(m_dayView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &Sidebar::updateAddHint);
    connect(m_dayModel, &QAbstractItemModel::modelReset, this, &Sidebar::updateAddHint);
    connect(store, &TaskStore::changed, this, &Sidebar::refresh);
    refresh();
    updateAddHint();
    updateTags();
}

void Sidebar::updateTags()
{
    if (!m_tagArea)
        return;
    std::vector<TagCloud::Item> items;
    for (const TagIndex::Entry &e : m_tags->entries()) {
        if (e.open > 0) // nothing to show for tags on finished tasks only
            items.push_back({e.tag, e.open});
    }
    const bool any = !items.empty();
    m_tagCloud->setItems(std::move(items));
    m_tagHeading->setVisible(any);
    m_tagArea->setVisible(any);
    // As tall as the chips, up to about five rows, then it scrolls.
    const int rowH = QFontMetrics(m_tagCloud->font()).height() + 10;
    const int width = std::max(50, m_tagArea->viewport()->width());
    m_tagArea->setFixedHeight(std::min(m_tagCloud->heightForWidth(width), rowH * 5));
}

void Sidebar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateTags();
}

Task *Sidebar::selectedDayTask() const
{
    const QModelIndexList rows = m_dayView->selectionModel()->selectedRows(0);
    return rows.isEmpty() ? nullptr : m_dayModel->taskAt(rows.front());
}

void Sidebar::updateAddHint()
{
    const QString day = m_dayModel->singleDay().toString(QStringLiteral("ddd d MMM"));
    const Task *after = selectedDayTask();
    m_add->setPlaceholderText(after ? tr("+ Add for %1 (in %2)").arg(day, after->file->relPath)
                                    : tr("+ Add for %1 (to todo.md)").arg(day));
}

void Sidebar::showDay(const QDate &day)
{
    if (m_calendar->selectedDate() != day)
        m_calendar->setSelectedDate(day); // re-enters via selectionChanged
    m_dayModel->setSingleDay(day);
    m_calendar->repaintCells();
}

void Sidebar::setActiveFilter(const QString &text)
{
    const QString t = text.trimmed();
    m_tagCloud->setActive(t.startsWith(u'#') && !t.contains(u' ') ? t.mid(1) : QString());
    for (StatButton *b : m_stats) {
        if (b->isCheckable())
            b->setChecked(!t.isEmpty() && b->filter() == t);
    }
}

void Sidebar::setStateVisible(TaskState state, bool visible)
{
    m_dayModel->filter().setStateVisible(state, visible);
    m_dayModel->refilter();
}

void Sidebar::refreshColors()
{
    m_calendar->restyle();
    m_dayModel->refreshColors();
    update();
    m_calendar->repaintCells();
}

void Sidebar::refresh()
{
    const QDate today = QDate::currentDate();
    int overdue = 0, dueToday = 0, week = 0, doing = 0, quick = 0, doneToday = 0;
    QHash<QDate, int> counts;
    for (const auto &f : m_store->files()) {
        for (const Task &t : f->tasks) {
            if (t.state == TaskState::Done && t.completed == today)
                ++doneToday;
            if (!t.isOpen())
                continue;
            if (t.state == TaskState::Doing)
                ++doing;
            if (t.eta > 0 && t.eta <= 2)
                ++quick;
            if (!t.due.isValid())
                continue;
            ++counts[t.due];
            if (t.due < today)
                ++overdue;
            else if (t.due == today)
                ++dueToday;
            if (t.due >= today && t.due < today.addDays(7))
                ++week;
        }
    }
    const int values[] = {overdue, dueToday, week, doing, quick, doneToday};
    for (size_t i = 0; i < m_stats.size(); ++i)
        m_stats[i]->setCount(values[i]);
    m_calendar->setCounts(std::move(counts));
}

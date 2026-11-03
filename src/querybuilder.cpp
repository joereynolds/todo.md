#include "querybuilder.h"

#include "models.h"

#include <QGridLayout>
#include <QLabel>
#include <QLayout>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>

namespace {

// Lays children out left to right, wrapping (after Qt's flow layout example).
class FlowLayout : public QLayout
{
public:
    explicit FlowLayout(QWidget *parent) : QLayout(parent) { setContentsMargins(0, 0, 0, 0); }
    ~FlowLayout() override
    {
        while (QLayoutItem *i = takeAt(0))
            delete i;
    }
    void addItem(QLayoutItem *item) override { m_items.append(item); }
    int count() const override { return int(m_items.size()); }
    QLayoutItem *itemAt(int i) const override { return m_items.value(i); }
    QLayoutItem *takeAt(int i) override { return i >= 0 && i < m_items.size() ? m_items.takeAt(i) : nullptr; }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return layoutIn(QRect(0, 0, w, 0), false); }
    void setGeometry(const QRect &r) override
    {
        QLayout::setGeometry(r);
        layoutIn(r, true);
    }
    QSize sizeHint() const override { return minimumSize(); }
    QSize minimumSize() const override
    {
        QSize s;
        for (const QLayoutItem *i : m_items)
            s = s.expandedTo(i->minimumSize());
        const QMargins m = contentsMargins();
        return s + QSize(m.left() + m.right(), m.top() + m.bottom());
    }

private:
    static constexpr int kGap = 4;
    int layoutIn(const QRect &r, bool apply) const
    {
        int x = r.x(), y = r.y(), lineH = 0;
        for (QLayoutItem *i : m_items) {
            if (i->isEmpty())
                continue;
            const QSize s = i->sizeHint();
            if (x + s.width() > r.right() + 1 && lineH > 0) {
                x = r.x();
                y += lineH + kGap;
                lineH = 0;
            }
            if (apply)
                i->setGeometry(QRect(QPoint(x, y), s));
            x += s.width() + kGap;
            lineH = std::max(lineH, s.height());
        }
        return y + lineH - r.y();
    }
    QList<QLayoutItem *> m_items;
};

bool containsCi(const QStringList &list, const QString &s)
{
    return list.contains(s, Qt::CaseInsensitive);
}

} // namespace

// ---------------------------------------------------------------- chip

Chip::Chip(const QString &text, const QString &key, QWidget *parent) : QAbstractButton(parent), m_key(key)
{
    setText(text);
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
}

QSize Chip::sizeHint() const
{
    const QFontMetrics fm(font());
    int w = fm.horizontalAdvance(text()) + 20;
    if (m_count >= 0)
        w += fm.horizontalAdvance(QString::number(m_count)) + 6;
    return {w, fm.height() + 8};
}

void Chip::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const TaskColors c = TaskColors::fromPalette(palette());
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QColor fill = underMouse() ? c.pill : c.band, ink = c.pillText;
    QPen border(c.guide, 1); // outlined, so chips read as buttons on any background
    if (isChecked()) {
        fill = c.doing;
        fill.setAlphaF(c.dark ? 0.30f : 0.15f);
        ink = c.doing;
        border = QPen(c.doing, 1);
    }
    if (hasFocus() && !isChecked())
        border = QPen(c.checkBorder, 1);
    p.setPen(border);
    p.setBrush(fill);
    p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
    const QRectF inner = r.adjusted(10, 0, -10, 0);
    if (m_count >= 0) {
        p.setPen(isChecked() ? ink : c.dim);
        p.drawText(inner, Qt::AlignRight | Qt::AlignVCenter, QString::number(m_count));
    }
    p.setPen(ink);
    p.drawText(inner, Qt::AlignLeft | Qt::AlignVCenter, text());
}

// ---------------------------------------------------------------- builder

QueryBuilder::QueryBuilder(TagIndex *tags, QWidget *parent) : QWidget(parent), m_tags(tags)
{
    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(8);
    grid->setColumnStretch(1, 1);
    int row = 0;
    auto label = [&](const QString &text) {
        auto *l = new QLabel(text, this);
        l->setEnabled(false); // dim
        l->setContentsMargins(0, 4, 0, 0); // level with the first row of chips
        grid->addWidget(l, row, 0, Qt::AlignTop | Qt::AlignLeft);
    };
    // A row of chips; multi-select unless `single`.
    auto group = [&](const QString &title, std::initializer_list<std::pair<const char *, const char *>> items,
                     std::vector<Chip *> &out, QStringList Query::*field, bool single) {
        label(title);
        auto *box = new QWidget(this);
        auto *flow = new FlowLayout(box);
        for (const auto &[text, key] : items) {
            auto *chip = new Chip(tr(text), QString::fromLatin1(key), box);
            flow->addWidget(chip);
            out.push_back(chip);
            connect(chip, &QAbstractButton::clicked, this, [this, chip, field, single](bool on) {
                if (single) {
                    m_query.eta = on ? chip->key() : QString();
                } else {
                    QStringList &list = m_query.*field;
                    list.removeAll(chip->key());
                    if (on)
                        list << chip->key();
                }
                sync();
                changed();
            });
        }
        grid->addWidget(box, row++, 1);
    };

    // Tags: any/all switch, then a chip per tag (scrolls when there are many).
    label(tr("Tags"));
    auto *tagCol = new QWidget(this);
    auto *tagLayout = new QVBoxLayout(tagCol);
    tagLayout->setContentsMargins(0, 0, 0, 0);
    tagLayout->setSpacing(4);
    auto *modeRow = new QHBoxLayout;
    auto *matching = new QLabel(tr("matching"), tagCol);
    matching->setEnabled(false);
    m_any = new Chip(tr("any"), QStringLiteral("any"), tagCol);
    m_all = new Chip(tr("all"), QStringLiteral("all"), tagCol);
    m_any->setToolTip(tr("Tasks with at least one of the chosen tags (#a|#b)"));
    m_all->setToolTip(tr("Tasks with every chosen tag (#a #b)"));
    modeRow->addWidget(matching);
    modeRow->addWidget(m_any);
    modeRow->addWidget(m_all);
    modeRow->addStretch();
    tagLayout->addLayout(modeRow);
    for (Chip *mode : {m_any, m_all}) {
        connect(mode, &QAbstractButton::clicked, this, [this, mode] {
            m_query.allTags = mode == m_all;
            sync();
            changed();
        });
    }
    m_tagArea = new QScrollArea(tagCol);
    m_tagArea->setFrameShape(QFrame::NoFrame);
    m_tagArea->setWidgetResizable(true);
    m_tagArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tagBox = new QWidget;
    new FlowLayout(m_tagBox);
    m_tagArea->setWidget(m_tagBox);
    tagLayout->addWidget(m_tagArea);
    m_noTags = new QLabel(tr("No tags yet: add #tags to tasks to pick them here."), tagCol);
    m_noTags->setEnabled(false);
    tagLayout->addWidget(m_noTags);
    grid->addWidget(tagCol, row++, 1);

    group(tr("Due"), {{"Overdue", "overdue"}, {"Today", "today"}, {"Next 7 days", "week"}, {"Has a date", "any"}, {"No date", "none"}},
          m_due, &Query::due, false);
    group(tr("State"), {{"To do", "todo"}, {"In progress", "doing"}, {"Done", "done"}, {"Cancelled", "cancelled"}},
          m_states, &Query::states, false);
    group(tr("Completed"), {{"Today", "today"}, {"Yesterday", "yesterday"}, {"Last 7 days", "week"}, {"Any time", "any"}},
          m_done, &Query::done, false);
    group(tr("Estimate"),
          {{"≤ 2m", "<=2m"}, {"≤ 15m", "<=15m"}, {"≤ 1h", "<=1h"}, {"> 1h", ">1h"}, {"> 1d", ">1d"}, {"Has one", "any"}, {"None", "none"}},
          m_eta, nullptr, true);

    auto *clear = new QPushButton(tr("Clear all"), this);
    clear->setFlat(true);
    clear->setCursor(Qt::PointingHandCursor);
    clear->setToolTip(tr("Empty the filter"));
    connect(clear, &QPushButton::clicked, this, [this] {
        m_query = Query();
        rebuildTags();
        changed();
    });
    grid->addWidget(clear, row++, 1, Qt::AlignRight);

    connect(m_tags, &TagIndex::changed, this, &QueryBuilder::rebuildTags);
    rebuildTags();
}

std::vector<Chip *> QueryBuilder::chips() const
{
    std::vector<Chip *> all{m_any, m_all};
    for (const auto *v : {&m_tagChips, &m_due, &m_states, &m_done, &m_eta})
        all.insert(all.end(), v->begin(), v->end());
    return all;
}

void QueryBuilder::setQuery(const QString &text)
{
    const bool keepMode = m_query.allTags;
    m_query = Query::parse(text);
    if (m_query.tags.size() < 2)
        m_query.allTags = keepMode; // one tag reads the same either way; keep the switch
    rebuildTags();
}

void QueryBuilder::rebuildTags()
{
    for (Chip *c : m_tagChips)
        delete c;
    m_tagChips.clear();
    // Tags with open tasks, then any chosen tag the index doesn't list (typed, or only on done tasks).
    std::vector<std::pair<QString, int>> tags;
    for (const TagIndex::Entry &e : m_tags->entries()) {
        if (e.open > 0 || containsCi(m_query.tags, e.tag))
            tags.emplace_back(e.tag, e.open);
    }
    for (const QString &t : m_query.tags) {
        const bool listed = std::any_of(tags.cbegin(), tags.cend(),
                                        [&](const auto &x) { return x.first.compare(t, Qt::CaseInsensitive) == 0; });
        if (!listed)
            tags.emplace_back(t, 0);
    }
    auto *flow = m_tagBox->layout();
    for (const auto &[tag, count] : tags) {
        auto *chip = new Chip(u'#' + tag, tag, m_tagBox);
        chip->setCount(count);
        chip->setToolTip(tr("%n open task(s) tagged #%1", nullptr, count).arg(tag));
        flow->addWidget(chip);
        m_tagChips.push_back(chip);
        connect(chip, &QAbstractButton::clicked, this, [this, tag](bool on) {
            for (qsizetype i = m_query.tags.size() - 1; i >= 0; --i) {
                if (m_query.tags[i].compare(tag, Qt::CaseInsensitive) == 0)
                    m_query.tags.removeAt(i);
            }
            if (on)
                m_query.tags << tag;
            sync();
            changed();
        });
    }
    m_noTags->setVisible(tags.empty());
    m_tagArea->setVisible(!tags.empty());
    fitTags();
    sync();
}

void QueryBuilder::fitTags()
{
    const int rowH = QFontMetrics(font()).height() + 12;
    const int width = std::max(200, m_tagArea->viewport()->width());
    m_tagArea->setFixedHeight(std::min(m_tagBox->layout()->heightForWidth(width), rowH * 4));
}

void QueryBuilder::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    fitTags();
}

void QueryBuilder::sync()
{
    for (Chip *c : m_tagChips)
        c->setChecked(containsCi(m_query.tags, c->key()));
    m_any->setChecked(!m_query.allTags);
    m_all->setChecked(m_query.allTags);
    for (Chip *c : m_due)
        c->setChecked(m_query.due.contains(c->key()));
    for (Chip *c : m_states)
        c->setChecked(m_query.states.contains(c->key()));
    for (Chip *c : m_done)
        c->setChecked(m_query.done.contains(c->key()));
    for (Chip *c : m_eta)
        c->setChecked(m_query.eta == c->key());
}

void QueryBuilder::changed()
{
    emit queryChanged(m_query.toString());
}

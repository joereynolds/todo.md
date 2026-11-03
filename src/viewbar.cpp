#include "viewbar.h"

#include "models.h"
#include "querybuilder.h"
#include "store.h"
#include "views.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QToolButton>

ViewBar::ViewBar(ViewList *views, TaskStore *store, QWidget *parent) : QWidget(parent), m_views(views), m_store(store)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 2, 6, 2);
    layout->setSpacing(4);
    auto *line = new QFrame(this);
    line->setFrameShape(QFrame::VLine);
    line->setFrameShadow(QFrame::Plain);
    line->setEnabled(false);
    line->setFixedHeight(fontMetrics().height() + 4);
    layout->addWidget(line);
    layout->addSpacing(6);
    auto *label = new QLabel(tr("Views"), this);
    label->setEnabled(false); // dim
    layout->addWidget(label);
    layout->addSpacing(2);
    // Chips are inserted here, before More and +.
    m_more = new QToolButton(this);
    m_more->setText(tr("More ▾"));
    m_more->setAutoRaise(true);
    m_more->setFocusPolicy(Qt::NoFocus);
    m_menu = new QMenu(m_more);
    // Opened by hand rather than setMenu(), which would add the style's own arrow next to "▾".
    connect(m_more, &QToolButton::clicked, this, [this] { m_menu->exec(m_more->mapToGlobal(QPoint(0, m_more->height()))); });
    layout->addWidget(m_more);
    m_new = new QToolButton(this);
    m_new->setObjectName(QStringLiteral("newView"));
    m_new->setText(tr("+"));
    m_new->setAutoRaise(true);
    m_new->setFocusPolicy(Qt::NoFocus);
    m_new->setToolTip(tr("New view: save a filter (starts from the current one; Ctrl+S)"));
    connect(m_new, &QToolButton::clicked, this, &ViewBar::newRequested);
    layout->addWidget(m_new);

    connect(m_views, &ViewList::changed, this, &ViewBar::rebuild);
    connect(m_store, &TaskStore::changed, this, &ViewBar::updateCounts);
    rebuild();
}

void ViewBar::rebuild()
{
    for (Chip *c : m_chips)
        delete c;
    m_chips.clear();
    auto *layout = static_cast<QHBoxLayout *>(this->layout());
    const int at = layout->indexOf(m_more);
    const auto &views = m_views->views();
    for (size_t i = 0; i < views.size(); ++i) {
        auto *c = new Chip(views[i].name, views[i].query, this);
        c->setFocusPolicy(Qt::NoFocus);
        c->setContextMenuPolicy(Qt::CustomContextMenu);
        c->setToolTip(i < 9 ? tr("%1\nCtrl+Alt+%2 · click again to clear · right-click to edit").arg(views[i].query).arg(i + 1)
                            : tr("%1\nclick again to clear · right-click to edit").arg(views[i].query));
        const int index = int(i);
        connect(c, &QAbstractButton::clicked, this, [this, c, index] {
            c->setChecked(index == m_active); // the window decides, via setActive
            emit applyRequested(index);
        });
        connect(c, &QWidget::customContextMenuRequested, this,
                [this, c, index](const QPoint &pos) { emit menuRequested(index, c->mapToGlobal(pos)); });
        layout->insertWidget(at + int(i), c);
        m_chips.push_back(c);
    }
    m_new->setText(views.empty() ? tr("+ New view") : tr("+"));
    updateCounts();
    setActive(m_active < int(views.size()) ? m_active : -1);
    layoutChips();
}

void ViewBar::updateCounts()
{
    const auto &views = m_views->views();
    for (size_t i = 0; i < m_chips.size() && i < views.size(); ++i)
        m_chips[i]->setCount(countMatching(*m_store, views[i].query));
    layoutChips();
}

void ViewBar::setActive(int index)
{
    m_active = index;
    for (size_t i = 0; i < m_chips.size(); ++i)
        m_chips[i]->setChecked(int(i) == index);
    layoutChips(); // the active view is always shown
}

void ViewBar::fit(int width)
{
    m_width = width;
    layoutChips();
}

void ViewBar::layoutChips()
{
    // Fixed parts: everything but the chips and More.
    const auto *layout = this->layout();
    const QMargins m = layout->contentsMargins();
    int fixed = m.left() + m.right();
    for (int i = 0; i < layout->count(); ++i) {
        QWidget *w = layout->itemAt(i)->widget();
        if (w && w != m_more && std::find(m_chips.begin(), m_chips.end(), w) == m_chips.end())
            fixed += w->sizeHint().width() + layout->spacing();
        else if (!w)
            fixed += layout->itemAt(i)->sizeHint().width();
    }
    const int moreW = m_more->sizeHint().width() + layout->spacing();
    int total = fixed;
    for (Chip *c : m_chips)
        total += c->sizeHint().width() + layout->spacing();
    const bool overflow = m_width > 0 && total > m_width;
    int room = overflow ? m_width - fixed - moreW : INT_MAX;
    // The active chip first, so it's never hidden; then the rest in order.
    if (overflow && m_active >= 0 && m_active < int(m_chips.size()))
        room -= m_chips[size_t(m_active)]->sizeHint().width() + layout->spacing();
    QMenu *menu = m_menu;
    menu->clear();
    const auto &views = m_views->views();
    for (size_t i = 0; i < m_chips.size(); ++i) {
        Chip *c = m_chips[i];
        bool show = true;
        if (overflow && int(i) != m_active) {
            const int w = c->sizeHint().width() + layout->spacing();
            show = w <= room;
            if (show)
                room -= w;
            else
                room = -1; // keep the order: nothing after a hidden chip is shown
        }
        c->setVisible(show);
        if (!show && i < views.size()) {
            QString label = views[i].name;
            label.replace(u'&', QLatin1String("&&"));
            menu->addAction(label, this, [this, i] { emit applyRequested(int(i)); });
        }
    }
    m_more->setVisible(!menu->isEmpty());
    updateGeometry();
}

#include "taskview.h"

#include "models.h"
#include "theme.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace {

constexpr int kPad = 4;

void drawChevron(QPainter *p, const QRect &slot, bool open, const QColor &color)
{
    const QPointF c = QRectF(slot).center();
    const qreal s = 3.5;
    QPainterPath path;
    if (open) {
        path.moveTo(c.x() - s, c.y() - s / 2);
        path.lineTo(c.x(), c.y() + s / 2);
        path.lineTo(c.x() + s, c.y() - s / 2);
    } else {
        path.moveTo(c.x() - s / 2, c.y() - s);
        path.lineTo(c.x() + s / 2, c.y());
        path.lineTo(c.x() - s / 2, c.y() + s);
    }
    p->setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p->setBrush(Qt::NoBrush);
    p->drawPath(path);
}

void drawCheckbox(QPainter *p, const QRect &r, TaskState state, const TaskColors &c, const QColor &base)
{
    const QRectF b = QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = 3.5;
    switch (state) {
    case TaskState::Todo:
        p->setPen(QPen(c.checkBorder, 1.2));
        p->setBrush(base);
        p->drawRoundedRect(b, radius, radius);
        break;
    case TaskState::Doing:
        p->setPen(QPen(c.doing, 1.4));
        p->setBrush(base);
        p->drawRoundedRect(b, radius, radius);
        p->setPen(Qt::NoPen);
        p->setBrush(c.doing);
        p->drawRoundedRect(b.adjusted(3.5, 3.5, -3.5, -3.5), 1.5, 1.5);
        break;
    case TaskState::Done: {
        p->setPen(Qt::NoPen);
        p->setBrush(c.done);
        p->drawRoundedRect(b, radius, radius);
        QPainterPath tick;
        tick.moveTo(b.left() + b.width() * 0.25, b.top() + b.height() * 0.52);
        tick.lineTo(b.left() + b.width() * 0.43, b.top() + b.height() * 0.70);
        tick.lineTo(b.left() + b.width() * 0.76, b.top() + b.height() * 0.32);
        p->setPen(QPen(Qt::white, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->setBrush(Qt::NoBrush);
        p->drawPath(tick);
        break;
    }
    case TaskState::Cancelled: {
        p->setPen(QPen(c.dim, 1.2));
        p->setBrush(c.pill);
        p->drawRoundedRect(b, radius, radius);
        const QRectF x = b.adjusted(4, 4, -4, -4);
        p->setPen(QPen(c.dim, 1.4, Qt::SolidLine, Qt::RoundCap));
        p->drawLine(x.topLeft(), x.bottomRight());
        p->drawLine(x.topRight(), x.bottomLeft());
        break;
    }
    }
}

} // namespace

// ---------------------------------------------------------------- delegate

TaskDelegate::TaskDelegate(bool tree, QObject *parent) : QStyledItemDelegate(parent), m_tree(tree) {}

TaskDelegate::Layout TaskDelegate::layoutFor(const QRect &cell, const QModelIndex &index, bool tree)
{
    Layout l;
    int x = cell.left() + kPad;
    if (tree) {
        x += index.data(TaskModelBase::DepthRole).toInt() * kStep;
        l.arrow = QRect(x, cell.top(), kStep, cell.height());
        x += kStep;
    }
    if (index.data(TaskModelBase::StateRole).isValid()) {
        l.check = QRect(x, cell.top() + (cell.height() - kBox) / 2, kBox, kBox);
        x += kBox + 7;
    }
    l.text = QRect(x, cell.top(), std::max(0, cell.right() - x - kPad), cell.height());
    return l;
}

QFont TaskDelegate::smallFont(const QFont &font)
{
    QFont f = font;
    if (font.pointSizeF() > 0)
        f.setPointSizeF(font.pointSizeF() * 0.88);
    else
        f.setPixelSize(std::max(8, int(font.pixelSize() * 0.88)));
    f.setBold(false);
    f.setStrikeOut(false);
    f.setItalic(false);
    return f;
}

std::vector<QRect> TaskDelegate::tagRects(const QRect &cell, const QStringList &tags, const QFont &font)
{
    const QFontMetrics fm(smallFont(font));
    const int h = fm.height() + 3;
    const int top = cell.top() + (cell.height() - h) / 2;
    const int right = cell.right() - kPad;
    std::vector<QRect> out;
    int x = cell.left() + kPad;
    for (const QString &tag : tags) {
        const int w = fm.horizontalAdvance(u'#' + tag) + 12;
        if (x + w > right + 1)
            break;
        out.emplace_back(x, top, w, h);
        x += w + 4;
    }
    return out;
}

void TaskDelegate::paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    const bool header = index.data(TaskModelBase::HeaderRole).toBool();
    const TaskColors colors = TaskColors::fromPalette(option.palette);
    const bool selected = option.state & QStyle::State_Selected;
    if (header && !selected)
        p->fillRect(option.rect, colors.band);

    if (const QVariant tagsVar = index.data(TaskModelBase::TagsRole); tagsVar.isValid()) {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const QWidget *w = option.widget;
        (w ? w->style() : QApplication::style())->drawPrimitive(QStyle::PE_PanelItemViewItem, &opt, p, w);
        const QStringList tags = tagsVar.toStringList();
        const std::vector<QRect> rects = tagRects(option.rect, tags, option.font);
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->setFont(smallFont(option.font));
        QColor fill = colors.pill, ink = colors.pillText;
        if (selected) {
            fill = opt.palette.color(QPalette::HighlightedText);
            fill.setAlphaF(0.22f);
            ink = opt.palette.color(QPalette::HighlightedText);
        }
        for (size_t i = 0; i < rects.size(); ++i) {
            const QRectF r = QRectF(rects[i]).adjusted(0.5, 0.5, -0.5, -0.5);
            p->setPen(Qt::NoPen);
            p->setBrush(fill);
            p->drawRoundedRect(r, r.height() / 2, r.height() / 2);
            p->setPen(ink);
            p->drawText(r, Qt::AlignCenter, u'#' + tags[int(i)]);
        }
        if (rects.size() < size_t(tags.size())) {
            const QRect last = rects.empty() ? option.rect.adjusted(kPad, 0, 0, 0) : rects.back();
            const QRect more(last.right() + (rects.empty() ? 0 : 4), option.rect.top(),
                             option.rect.right() - last.right(), option.rect.height());
            p->setPen(selected ? ink : colors.dim);
            p->drawText(more, Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("+%1").arg(tags.size() - int(rects.size())));
        }
        p->restore();
        return;
    }
    if (index.column() != 0) {
        QStyledItemDelegate::paint(p, option, index);
        return;
    }

    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);
    const QWidget *w = option.widget;
    QStyle *style = w ? w->style() : QApplication::style();
    // Selection / hover background only; content is drawn below.
    style->drawPrimitive(QStyle::PE_PanelItemViewItem, &opt, p, w);

    const Layout l = layoutFor(option.rect, index, m_tree);
    p->save();
    p->setRenderHint(QPainter::Antialiasing);

    if (m_tree) {
        // A guide under the chevron of every ancestor.
        const int depth = index.data(TaskModelBase::DepthRole).toInt();
        p->setPen(QPen(colors.guide, 1));
        for (int j = 0; j < depth; ++j) {
            const qreal x = option.rect.left() + kPad + j * kStep + kStep / 2.0 + 0.5;
            p->drawLine(QPointF(x, option.rect.top()), QPointF(x, option.rect.bottom() + 1));
        }
        if (index.data(TaskModelBase::HasChildrenRole).toBool())
            drawChevron(p, l.arrow, index.data(TaskModelBase::ExpandedRole).toBool(),
                        selected ? opt.palette.color(QPalette::HighlightedText) : colors.checkBorder);
    }

    const QVariant state = index.data(TaskModelBase::StateRole);
    if (state.isValid())
        drawCheckbox(p, l.check, TaskState(state.toInt()), colors, opt.palette.color(QPalette::Base));

    // Progress pill, right after the text: neutral when nothing is done,
    // blue while under way, green when complete.
    QString pill;
    int done = 0, total = 0;
    const QVariant progress = index.data(TaskModelBase::ProgressRole);
    if (progress.isValid()) {
        done = progress.toPoint().x();
        total = progress.toPoint().y();
        if (total > 0)
            pill = QStringLiteral("%1/%2").arg(done).arg(total);
    }
    QFont pillFont = smallFont(option.font);
    pillFont.setBold(true);
    const QFontMetrics pfm(pillFont);
    const int pillW = pill.isEmpty() ? 0 : pfm.horizontalAdvance(pill) + 14;

    const QFontMetrics fm(opt.font);
    const QRect textRect = l.text;
    const QString text = fm.elidedText(opt.text, Qt::ElideRight, std::max(0, textRect.width() - (pillW ? pillW + 8 : 0)));
    p->setFont(opt.font);
    p->setPen(selected ? opt.palette.color(QPalette::HighlightedText) : opt.palette.color(QPalette::Text));
    p->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, text);

    if (pillW) {
        const int h = pfm.height() + 3;
        const QRectF pr(textRect.left() + fm.horizontalAdvance(text) + 8, textRect.top() + (textRect.height() - h) / 2.0,
                        pillW, h);
        QColor fill = colors.pill, ink = colors.pillText;
        if (done == total) {
            fill = colors.done;
            ink = colors.done;
        } else if (done > 0) {
            fill = colors.doing;
            ink = colors.doing;
        }
        if (fill != colors.pill)
            fill.setAlphaF(colors.dark ? 0.30f : 0.16f);
        p->setPen(Qt::NoPen);
        p->setBrush(fill);
        p->drawRoundedRect(pr, h / 2.0, h / 2.0);
        p->setFont(pillFont);
        p->setPen(ink);
        p->drawText(pr, Qt::AlignCenter, pill);
    }
    p->restore();
}

QSize TaskDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QSize s = QStyledItemDelegate::sizeHint(option, index);
    s.setHeight(std::max(s.height(), option.fontMetrics.height() + theme::rowPadding()));
    if (index.column() == 0) {
        const Layout l = layoutFor(QRect(0, 0, 10000, s.height()), index, m_tree);
        s.setWidth(s.width() + l.text.left() + 40);
    }
    return s;
}

QWidget *TaskDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                                    const QModelIndex &index) const
{
    QWidget *w = QStyledItemDelegate::createEditor(parent, option, index);
    if (auto *edit = qobject_cast<QLineEdit *>(w); edit && m_editorHook)
        m_editorHook(edit);
    return w;
}

void TaskDelegate::updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option,
                                        const QModelIndex &index) const
{
    QStyledItemDelegate::updateEditorGeometry(editor, option, index);
    if (index.column() != 0)
        return;
    const Layout l = layoutFor(option.rect, index, m_tree);
    QRect g = editor->geometry();
    g.setLeft(l.text.left() - 3);
    editor->setGeometry(g);
}

// ---------------------------------------------------------------- view

TaskView::TaskView(bool tree, QWidget *parent) : QTreeView(parent), m_tree(tree)
{
    setItemDelegate(new TaskDelegate(tree, this));
}

void TaskView::setSpanHeaders(bool on)
{
    m_spanHeaders = on;
    updateSpans();
}

void TaskView::setModel(QAbstractItemModel *m)
{
    QTreeView::setModel(m);
    connect(m, &QAbstractItemModel::modelReset, this, &TaskView::updateSpans);
    connect(m, &QAbstractItemModel::rowsInserted, this, &TaskView::updateSpans);
    updateSpans();
}

void TaskView::updateSpans()
{
    if (!m_spanHeaders || !model())
        return;
    for (int r = 0, n = model()->rowCount(); r < n; ++r) {
        const bool header = model()->index(r, 0).data(TaskModelBase::HeaderRole).toBool();
        if (header != isFirstColumnSpanned(r, {}))
            setFirstColumnSpanned(r, {}, header);
    }
}

FlatTaskModel *TaskView::listModel() const
{
    return m_tree ? qobject_cast<FlatTaskModel *>(model()) : nullptr;
}

TaskView::Hit TaskView::hitTest(const QPoint &pos, QModelIndex *out, QString *tag) const
{
    const QModelIndex index = indexAt(pos);
    if (!index.isValid())
        return Hit::None;
    if (const QVariant tags = index.data(TaskModelBase::TagsRole); tags.isValid()) {
        const QStringList list = tags.toStringList();
        const std::vector<QRect> rects = TaskDelegate::tagRects(visualRect(index), list, font());
        for (size_t i = 0; i < rects.size(); ++i) {
            if (rects[i].contains(pos)) {
                *out = index;
                if (tag)
                    *tag = list[int(i)];
                return Hit::Tag;
            }
        }
        return Hit::None;
    }
    if (index.column() != 0)
        return Hit::None;
    const TaskDelegate::Layout l = TaskDelegate::layoutFor(visualRect(index), index, m_tree);
    *out = index;
    if (m_tree && l.arrow.contains(pos) && index.data(TaskModelBase::HasChildrenRole).toBool())
        return Hit::Arrow;
    if (!l.check.isEmpty() && l.check.adjusted(-3, -3, 3, 3).contains(pos))
        return Hit::Check;
    return Hit::None;
}

bool TaskView::handleHit(const QPoint &pos)
{
    QModelIndex index;
    QString tag;
    switch (hitTest(pos, &index, &tag)) {
    case Hit::Tag:
        emit tagClicked(tag);
        return true;
    case Hit::Arrow:
        if (FlatTaskModel *m = listModel())
            m->setExpanded(index.row(), !index.data(TaskModelBase::ExpandedRole).toBool());
        return true;
    case Hit::Check:
        emit checkClicked(index);
        return true;
    case Hit::None:
        return false;
    }
    return false;
}

void TaskView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && event->modifiers() == Qt::NoModifier && handleHit(event->pos())) {
        event->accept();
        return;
    }
    QTreeView::mousePressEvent(event);
}

void TaskView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // A quick second click on a chevron/checkbox is just another click.
        if (handleHit(event->pos())) {
            event->accept();
            return;
        }
        const QModelIndex index = indexAt(event->pos());
        if (FlatTaskModel *m = listModel(); m && index.data(TaskModelBase::HeaderRole).toBool()) {
            m->setExpanded(index.row(), !index.data(TaskModelBase::ExpandedRole).toBool());
            event->accept();
            return;
        }
    }
    QTreeView::mouseDoubleClickEvent(event);
}

void TaskView::selectRow(int row)
{
    const QModelIndex i = model()->index(row, 0);
    selectionModel()->setCurrentIndex(i, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    scrollTo(i);
}

void TaskView::keyPressEvent(QKeyEvent *event)
{
    FlatTaskModel *m = listModel();
    const QModelIndex cur = currentIndex();
    if (m && cur.isValid() && event->modifiers() == Qt::NoModifier
        && (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)) {
        const int row = cur.row();
        const bool kids = cur.siblingAtColumn(0).data(TaskModelBase::HasChildrenRole).toBool();
        const bool open = cur.siblingAtColumn(0).data(TaskModelBase::ExpandedRole).toBool();
        if (event->key() == Qt::Key_Left) {
            if (kids && open) {
                m->setExpanded(row, false);
            } else if (const int parent = m->parentRow(row); parent >= 0) {
                selectRow(parent);
            }
        } else if (kids) {
            if (!open)
                m->setExpanded(row, true);
            else
                selectRow(row + 1);
        }
        event->accept();
        return;
    }
    QTreeView::keyPressEvent(event);
}

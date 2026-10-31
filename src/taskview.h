#pragma once

#include <QStyledItemDelegate>
#include <QTreeView>

class FlatTaskModel;

// Draws column 0 of task rows: indentation guides and an expand chevron (in
// tree mode), a state checkbox, the text, and a small progress pill. Header
// rows (files, agenda days) get a tinted band. Everything else is drawn by
// QStyledItemDelegate.
class TaskDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    static constexpr int kStep = 18; // indentation per level, also the chevron slot
    static constexpr int kBox = 14;  // checkbox size

    struct Layout {
        QRect arrow; // empty unless tree mode
        QRect check; // empty on non-task rows
        QRect text;
    };
    static Layout layoutFor(const QRect &cell, const QModelIndex &index, bool tree);

    TaskDelegate(bool tree, QObject *parent = nullptr);

    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option,
                              const QModelIndex &index) const override;

private:
    bool m_tree;
};

// Item view for tasks: a press on the chevron expands/collapses (tree mode),
// a press on the checkbox emits checkClicked, both without touching the
// selection. In tree mode Left/Right collapse/expand or move to parent/child,
// and double-clicking a file header toggles it.
class TaskView : public QTreeView
{
    Q_OBJECT
public:
    TaskView(bool tree, QWidget *parent = nullptr);

    // Header rows span every column (for small models such as the agenda;
    // it walks all rows on each reset).
    void setSpanHeaders(bool on);
    void setModel(QAbstractItemModel *model) override;

signals:
    void checkClicked(const QModelIndex &index);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    enum class Hit { None, Arrow, Check };
    Hit hitTest(const QPoint &pos, QModelIndex *index) const;
    bool handleHit(const QPoint &pos);
    FlatTaskModel *listModel() const;
    void selectRow(int row);
    void updateSpans();

    bool m_tree;
    bool m_spanHeaders = false;
};

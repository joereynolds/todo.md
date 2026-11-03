#pragma once

#include <QWidget>

#include <vector>

class Chip;
class QToolButton;
class TaskStore;
class ViewList;

// The saved views as chips on the right of the tab row, after a divider and a
// "Views" label, so they read as their own group next to Tasks / Agenda /
// Templates. Each chip shows the view's count (countMatching); the active view
// is checked. Chips that don't fit (fit()) move to a "More ▾" menu.
class ViewBar : public QWidget
{
    Q_OBJECT
public:
    ViewBar(ViewList *views, TaskStore *store, QWidget *parent = nullptr);
    void setActive(int index); // -1 = none
    void fit(int width);       // show as many chips as fit in `width` pixels
    std::vector<Chip *> chips() const { return m_chips; } // for tests

signals:
    void applyRequested(int index); // a click: apply (or, on the active view, clear)
    void newRequested();
    void menuRequested(int index, const QPoint &globalPos); // right-click

private:
    void rebuild();
    void updateCounts();
    void layoutChips();

    ViewList *m_views;
    TaskStore *m_store;
    std::vector<Chip *> m_chips;
    QToolButton *m_more, *m_new;
    class QMenu *m_menu;
    int m_active = -1;
    int m_width = 0; // from fit(); 0 = no limit
};

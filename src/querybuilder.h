#pragma once

#include "query.h"

#include <QAbstractButton>
#include <QWidget>

#include <vector>

class QLabel;
class QScrollArea;
class TagIndex;

// A clickable chip: a toggle drawn like the sidebar's tag chips.
class Chip : public QAbstractButton
{
    Q_OBJECT
public:
    Chip(const QString &text, const QString &key, QWidget *parent = nullptr);
    QString key() const { return m_key; }
    void setCount(int n) { m_count = n; updateGeometry(); update(); } // shown dimmed after the text; < 0 = none
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override { update(); }
    void leaveEvent(QEvent *) override { update(); }

private:
    QString m_key;
    int m_count = -1;
};

// Point-and-click filter: chips for tags (any / all), due, state, completed and
// estimate. Every click rewrites the query (queryChanged) so the text box next
// to it shows what's being built; setQuery() goes the other way, from typed
// text to chips. Terms it has no chips for are kept as typed (Query::other).
class QueryBuilder : public QWidget
{
    Q_OBJECT
public:
    explicit QueryBuilder(TagIndex *tags, QWidget *parent = nullptr);
    void setQuery(const QString &text); // no queryChanged
    QString query() const { return m_query.toString(); }
    std::vector<Chip *> chips() const; // for tests

signals:
    void queryChanged(const QString &text);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void rebuildTags();
    void fitTags(); // tag area as tall as its chips, up to ~4 rows
    void sync(); // chips from m_query
    void changed();

    TagIndex *m_tags;
    Query m_query;
    QWidget *m_tagBox;
    QScrollArea *m_tagArea;
    QLabel *m_noTags;
    Chip *m_any, *m_all;
    std::vector<Chip *> m_tagChips, m_due, m_states, m_done, m_eta;
};

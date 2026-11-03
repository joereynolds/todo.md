#pragma once

#include <QObject>
#include <QString>

#include <vector>

// Saved views: a name plus a filter query (the filter box language, e.g.
// "#music|#video due:week"). Kept in QSettings under "views", in the order the
// user arranged them.
struct CustomView {
    QString name;
    QString query;
};

class ViewList : public QObject
{
    Q_OBJECT
public:
    explicit ViewList(QObject *parent = nullptr); // loads from QSettings

    const std::vector<CustomView> &views() const { return m_views; }
    int indexOf(const QString &name) const;        // case-insensitive, -1 if none
    int indexOfQuery(const QString &query) const;  // first view with this query (trimmed), -1 if none

    void add(const CustomView &view);
    void update(int index, const CustomView &view);
    void remove(int index);
    void move(int index, int to);

signals:
    void changed();

private:
    void save() const;
    std::vector<CustomView> m_views;
};

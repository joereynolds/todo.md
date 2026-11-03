#include "views.h"

#include <QSettings>

ViewList::ViewList(QObject *parent) : QObject(parent)
{
    QSettings s;
    const int n = s.beginReadArray(QStringLiteral("views"));
    for (int i = 0; i < n; ++i) {
        s.setArrayIndex(i);
        CustomView v{s.value(QStringLiteral("name")).toString(), s.value(QStringLiteral("query")).toString()};
        if (!v.name.isEmpty())
            m_views.push_back(std::move(v));
    }
    s.endArray();
}

int ViewList::indexOf(const QString &name) const
{
    for (size_t i = 0; i < m_views.size(); ++i) {
        if (m_views[i].name.compare(name.trimmed(), Qt::CaseInsensitive) == 0)
            return int(i);
    }
    return -1;
}

int ViewList::indexOfQuery(const QString &query) const
{
    const QString q = query.simplified();
    if (q.isEmpty())
        return -1;
    for (size_t i = 0; i < m_views.size(); ++i) {
        if (m_views[i].query.simplified() == q)
            return int(i);
    }
    return -1;
}

void ViewList::add(const CustomView &view)
{
    m_views.push_back(view);
    save();
    emit changed();
}

void ViewList::update(int index, const CustomView &view)
{
    if (index < 0 || index >= int(m_views.size()))
        return;
    m_views[size_t(index)] = view;
    save();
    emit changed();
}

void ViewList::remove(int index)
{
    if (index < 0 || index >= int(m_views.size()))
        return;
    m_views.erase(m_views.begin() + index);
    save();
    emit changed();
}

void ViewList::move(int index, int to)
{
    const int n = int(m_views.size());
    if (index < 0 || index >= n || to < 0 || to >= n || index == to)
        return;
    CustomView v = std::move(m_views[size_t(index)]);
    m_views.erase(m_views.begin() + index);
    m_views.insert(m_views.begin() + to, std::move(v));
    save();
    emit changed();
}

void ViewList::save() const
{
    QSettings s;
    s.remove(QStringLiteral("views"));
    s.beginWriteArray(QStringLiteral("views"), int(m_views.size()));
    for (size_t i = 0; i < m_views.size(); ++i) {
        s.setArrayIndex(int(i));
        s.setValue(QStringLiteral("name"), m_views[i].name);
        s.setValue(QStringLiteral("query"), m_views[i].query);
    }
    s.endArray();
}

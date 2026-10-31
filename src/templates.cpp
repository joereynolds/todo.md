#include "templates.h"

#include "parser.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>

#include <algorithm>

namespace templates {

QString dirFor(const QStringList &roots)
{
    for (const QString &r : roots) {
        if (QFileInfo(r + QStringLiteral("/.templates")).isDir())
            return r + QStringLiteral("/.templates");
    }
    return roots.isEmpty() ? QString() : roots.first() + QStringLiteral("/.templates");
}

QStringList names(const QString &dir)
{
    QStringList out;
    if (dir.isEmpty())
        return out;
    for (const QFileInfo &fi : QDir(dir).entryInfoList({QStringLiteral("*.md")}, QDir::Files))
        out << fi.completeBaseName();
    std::sort(out.begin(), out.end(), [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return out;
}

QString pathFor(const QString &dir, const QString &name)
{
    return dir + u'/' + name + QStringLiteral(".md");
}

bool isValidName(const QString &name)
{
    return !name.trimmed().isEmpty() && !name.startsWith(u'.') && !name.contains(u'/') && !name.contains(u'\\');
}

QStringList fromTasks(const std::vector<Task *> &tasks)
{
    const QSet<const Task *> in(tasks.begin(), tasks.end());
    QStringList out;
    for (const Task *t : tasks) {
        int level = 0;
        for (const Task *a = t->parent; a; a = a->parent)
            level += in.contains(a) ? 1 : 0;
        QString line = parser::setState(t->raw(), TaskState::Todo, QDate());
        line = parser::removeDates(line, u"!+x-");
        const QString lead = parser::leadingWhitespace(line);
        out << QString(level * 2, u' ') + line.mid(lead.size());
    }
    return out;
}

QStringList instantiate(const QByteArray &data, const QString &lead, const QString &unit, const QDate &today)
{
    QStringList out;
    const auto file = parser::parse(QString(), QString(), data);
    for (const Task &t : file->tasks) {
        QString line = t.raw();
        line = parser::setState(line, TaskState::Todo, today);
        line = parser::removeDates(line, u"+x-"); // created today, whatever the file says
        QString indent = lead;
        for (int i = 0; i < t.depth; ++i)
            indent += unit;
        line = indent + line.mid(parser::leadingWhitespace(line).size());
        out << parser::setCreated(line, today);
    }
    return out;
}

bool save(const QString &path, const QStringList &lines)
{
    QDir().mkpath(QFileInfo(path).path());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write((lines.join(u'\n') + u'\n').toUtf8());
    return f.commit();
}

} // namespace templates

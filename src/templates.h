#pragma once

#include "task.h"

#include <QByteArray>
#include <QDate>
#include <QString>
#include <QStringList>

#include <vector>

// List templates: reusable sets of tasks, kept as markdown files in a hidden
// ".templates" directory inside a root (hidden directories aren't scanned, so
// template tasks never show up as real tasks). Inserting a template copies
// its tasks in as new todo tasks.
namespace templates {

// ".templates" in the first root that has one, else in the first root; empty
// when there are no roots. The directory may not exist yet.
QString dirFor(const QStringList &roots);
QStringList names(const QString &dir); // file names without ".md", sorted case-insensitively
QString pathFor(const QString &dir, const QString &name);
// Usable as a file name: not empty, no '/' or '\', doesn't start with '.'.
bool isValidName(const QString &name);

// Template lines for `tasks` (in display order, subtasks included as wanted):
// state reset to todo, dates removed (ETAs and tags kept), indented two spaces
// per ancestor that is also in the list.
QStringList fromTasks(const std::vector<Task *> &tasks);

// The task lines of a template file, ready to insert: todo, created `today`
// (other dates in the file are dropped, except a hand-written !due), each
// indented `lead` + `unit` per nesting level. Non-task lines are dropped.
QStringList instantiate(const QByteArray &data, const QString &lead, const QString &unit, const QDate &today);

bool save(const QString &path, const QStringList &lines); // creates the directory

} // namespace templates

#pragma once

#include "task.h"

#include <QByteArray>
#include <QStringView>

#include <memory>

// Parsing of the task syntax described in docs/task-syntax.md, plus pure
// string transformations of single task lines.
namespace parser {

QChar stateChar(TaskState s);

// Recognises "<ws>- [c] rest". On success sets prefixLen (length up to and
// including the space after ']'), the indent in columns and the state.
bool splitTaskLine(QStringView line, int *prefixLen, int *indent, TaskState *state);

// "90m", "2h", "7d", "5w", "85y" -> minutes; -1 if not an ETA.
qint64 parseEta(QStringView text);

// Fills text, dates and ETA from task.content.
void parseMeta(Task &task);

std::unique_ptr<TaskFile> parse(const QString &path, const QString &relPath, const QByteArray &data);
// Returns nullptr if the file cannot be read.
std::unique_ptr<TaskFile> parseFile(const QString &path, const QString &relPath);

// Line transformations. Each takes a full raw task line and returns the new
// line; non-task lines are returned unchanged.
QString setState(const QString &line, TaskState state, const QDate &today);
QString setDue(const QString &line, const QDate &due); // invalid date removes it
QString setEta(const QString &line, const QString &eta); // "30m"; empty removes it
QString setContent(const QString &line, const QString &content);

QString leadingWhitespace(const QString &line);

} // namespace parser

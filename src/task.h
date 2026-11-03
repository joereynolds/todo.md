#pragma once

#include <QDate>
#include <QString>
#include <QStringList>
#include <Qt>

#include <vector>

enum class TaskState : char { Todo, Doing, Done, Cancelled };

struct TaskFile;

// Common base so a model index's internal pointer can refer to a file or a task.
struct Node {
    explicit Node(bool file) : isFile(file) {}
    bool isFile;
};

// A hyperlink in Task::text: [start, start + length) is what's shown (the URL itself,
// or the label of a markdown [label](url) link); url is what opens.
struct TaskLink {
    int start = 0;
    int length = 0;
    QString url;
};

struct Task : Node {
    Task() : Node(false) {}

    TaskFile *file = nullptr;
    Task *parent = nullptr;
    std::vector<Task *> children;

    int line = 0;   // 0-based line number in the file
    int row = 0;    // index within parent->children, or file->roots
    int indent = 0; // leading whitespace in columns (tab stops of 4)
    int depth = 0;  // 0 for top-level tasks
    int listRow = -1; // row in TaskListModel, -1 when filtered out (owned by that model)
    TaskState state = TaskState::Todo;

    QString prefix;  // "  - [ ] ": everything up to and including the space after ']'
    QString content; // raw text after the prefix, metadata tokens included
    QString text;    // content with date tokens stripped, for display

    QDate due, created, completed, cancelled;
    qint64 eta = 0;  // estimate in minutes, 0 = none
    QString etaText; // as written, without the '~' (e.g. "90m")
    QStringList tags; // "#work" -> "work", as written, in order, no duplicates (case-insensitive)
    std::vector<TaskLink> links; // in text, left to right

    int subDone = 0;  // completed descendants
    int subTotal = 0; // non-cancelled descendants

    QString raw() const { return prefix + content; }
    bool isOpen() const { return state == TaskState::Todo || state == TaskState::Doing; }
};

struct TaskFile : Node {
    TaskFile() : Node(true) {}
    TaskFile(const TaskFile &) = delete;
    TaskFile &operator=(const TaskFile &) = delete;

    QString path;    // absolute, '/' separated
    QString relPath; // relative to the store root
    std::vector<Task> tasks; // in line order; never resized after parsing (pointers into it)
    std::vector<Task *> roots;
    int done = 0;  // completed tasks in the file
    int total = 0; // non-cancelled tasks in the file
    size_t hash = 0; // hash of the file bytes, to skip no-op reloads
    int row = 0;     // index in TaskStore::files()
};

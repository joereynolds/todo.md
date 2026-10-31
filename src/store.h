#pragma once

#include "edits.h"
#include "task.h"

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QTimer>

#include <memory>
#include <vector>

// Owns every parsed file under a root directory, keeps them in sync with disk
// and is the only thing that writes to disk.
//
// Several root directories can be tracked; files found under more than one
// are held once. Every mutation is wrapped in aboutToChange()/changed(); task
// pointers are invalid after changed(). Files are kept sorted by relPath and
// only files containing at least one task are held.
class TaskStore : public QObject
{
    Q_OBJECT
public:
    explicit TaskStore(QObject *parent = nullptr);
    ~TaskStore() override;

    QStringList roots() const { return m_roots; }
    const std::vector<std::unique_ptr<TaskFile>> &files() const { return m_files; }
    TaskFile *fileByPath(const QString &path) const { return m_byPath.value(path); }
    Task *taskAt(const QString &path, int line) const;
    bool isLoading() const { return m_loading; }

    void setRoots(const QStringList &roots); // asynchronous load
    void reload();

    // Display path: relative to its root, prefixed with the root's name when
    // there are several roots.
    static QString relPathFor(const QString &path, const QStringList &roots);
    static QString normalizeRoot(const QString &dir);

    // Writes edits to disk, reloads the touched files and records an undo step.
    // A missing file is created when every edit to it is an append (line -1).
    // Returns the number of edits applied.
    int apply(const std::vector<Edit> &edits, const QString &label);

    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    QString undoLabel() const { return m_undo.empty() ? QString() : m_undo.back().label; }
    QString redoLabel() const { return m_redo.empty() ? QString() : m_redo.back().label; }
    void undo();
    void redo();

    static bool isMarkdown(const QString &name);
    static bool isIgnoredDir(const QString &name);

signals:
    void aboutToChange();
    void changed();

    void loadStarted(const QStringList &roots);
    void loadFinished(int files, int tasks, qint64 ms);
    void message(const QString &text);
    void undoStateChanged();

private:
    struct Step {
        QString label;
        std::vector<Edit> edits; // applying these performs the step
    };
    struct Outcome {
        int applied = 0;
        int conflicts = 0;
        QStringList errors;
        std::vector<Edit> inverse;
    };

    Outcome applyRaw(const std::vector<Edit> &edits);
    void reportOutcome(const Outcome &o, const QString &label);
    void reloadFile(const QString &path);
    void insertFile(std::unique_ptr<TaskFile> file);
    void removeFile(TaskFile *file);
    void replaceFile(TaskFile *file, std::unique_ptr<TaskFile> fresh);
    void renumber();
    void scanNewDir(const QString &dir);
    void rescanDir(const QString &dir);
    void queueFile(const QString &path);
    void queueDir(const QString &dir);
    void flushPending();
    void beginChange();
    void endChange();

    QStringList m_roots;
    std::vector<std::unique_ptr<TaskFile>> m_files;
    QHash<QString, TaskFile *> m_byPath;
    QSet<QString> m_known; // every markdown file seen, with or without tasks
    QSet<QString> m_dirs;
    QFileSystemWatcher m_watcher;
    QSet<QString> m_pendingFiles, m_pendingDirs;
    QTimer m_flushTimer;
    QStringList m_watchQueue;
    QTimer m_watchTimer;
    int m_generation = 0;
    bool m_loading = false;
    bool m_changing = false;
    std::vector<Step> m_undo, m_redo;
};

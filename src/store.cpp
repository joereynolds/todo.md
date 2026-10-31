#include "store.h"

#include "parser.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QSaveFile>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <thread>

namespace {

constexpr size_t kMaxUndo = 200;

struct LoadResult {
    std::vector<std::unique_ptr<TaskFile>> files; // only those with tasks, sorted
    QStringList mdFiles;
    QStringList dirs;
    qint64 ms = 0;
};

bool lessPath(const QString &a, const QString &b)
{
    const int c = a.compare(b, Qt::CaseInsensitive);
    return c != 0 ? c < 0 : a < b;
}

QString toQString(const std::filesystem::path &p)
{
    return QDir::fromNativeSeparators(QString::fromStdU16String(p.u16string()));
}

std::shared_ptr<LoadResult> loadTree(const QStringList &roots)
{
    namespace fs = std::filesystem;
    QElapsedTimer timer;
    timer.start();
    auto res = std::make_shared<LoadResult>();
    QSet<QString> seenFiles, seenDirs;

    for (const QString &root : roots) {
        if (seenDirs.contains(root))
            continue; // nested inside an earlier root
        seenDirs.insert(root);
        res->dirs << root;
        std::error_code ec;
        fs::recursive_directory_iterator it(fs::path(root.toStdU16String()),
                                            fs::directory_options::skip_permission_denied, ec);
        for (const fs::recursive_directory_iterator end; !ec && it != end; it.increment(ec)) {
            const fs::directory_entry &entry = *it;
            const QString name = QString::fromStdU16String(entry.path().filename().u16string());
            std::error_code ec2;
            if (entry.is_directory(ec2)) {
                if (TaskStore::isIgnoredDir(name)) {
                    it.disable_recursion_pending();
                    continue;
                }
                const QString dir = toQString(entry.path());
                if (seenDirs.contains(dir)) {
                    it.disable_recursion_pending();
                    continue;
                }
                seenDirs.insert(dir);
                res->dirs << dir;
            } else if (TaskStore::isMarkdown(name) && entry.is_regular_file(ec2)) {
                const QString file = toQString(entry.path());
                if (!seenFiles.contains(file)) {
                    seenFiles.insert(file);
                    res->mdFiles << file;
                }
            }
        }
    }

    // Parse in parallel; files are independent.
    const qsizetype n = res->mdFiles.size();
    std::vector<std::unique_ptr<TaskFile>> parsed(static_cast<size_t>(n));
    std::atomic<qsizetype> next{0};
    auto work = [&] {
        for (qsizetype i; (i = next++) < n;) {
            const QString &path = res->mdFiles[i];
            parsed[size_t(i)] = parser::parseFile(path, TaskStore::relPathFor(path, roots));
        }
    };
    const unsigned threads = std::clamp(std::thread::hardware_concurrency(), 1u, 16u);
    std::vector<std::thread> pool;
    for (unsigned t = 1; t < threads && qsizetype(t) * 8 < n; ++t)
        pool.emplace_back(work);
    work();
    for (auto &t : pool)
        t.join();

    for (auto &f : parsed) {
        if (f && !f->tasks.empty())
            res->files.push_back(std::move(f));
    }
    std::sort(res->files.begin(), res->files.end(),
              [](const auto &a, const auto &b) { return lessPath(a->relPath, b->relPath); });
    res->ms = timer.elapsed();
    return res;
}

} // namespace

TaskStore::TaskStore(QObject *parent) : QObject(parent)
{
    m_flushTimer.setSingleShot(true);
    m_flushTimer.setInterval(120);
    connect(&m_flushTimer, &QTimer::timeout, this, &TaskStore::flushPending);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &TaskStore::queueFile);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &TaskStore::queueDir);
    m_watchTimer.setInterval(0);
    connect(&m_watchTimer, &QTimer::timeout, this, [this] {
        const qsizetype n = std::min<qsizetype>(m_watchQueue.size(), 256);
        if (n > 0) {
            m_watcher.addPaths(m_watchQueue.first(n));
            m_watchQueue.remove(0, n);
        }
        if (m_watchQueue.isEmpty())
            m_watchTimer.stop();
    });
}

TaskStore::~TaskStore() = default;

bool TaskStore::isMarkdown(const QString &name)
{
    return name.endsWith(QLatin1String(".md"), Qt::CaseInsensitive)
        || name.endsWith(QLatin1String(".markdown"), Qt::CaseInsensitive);
}

bool TaskStore::isIgnoredDir(const QString &name)
{
    return name.startsWith(u'.') || name == QLatin1String("node_modules");
}

Task *TaskStore::taskAt(const QString &path, int line) const
{
    TaskFile *f = fileByPath(path);
    if (!f)
        return nullptr;
    auto it = std::lower_bound(f->tasks.begin(), f->tasks.end(), line,
                               [](const Task &t, int l) { return t.line < l; });
    return it != f->tasks.end() && it->line == line ? &*it : nullptr;
}

QString TaskStore::normalizeRoot(const QString &dir)
{
    return QDir::cleanPath(QFileInfo(dir).absoluteFilePath());
}

QString TaskStore::relPathFor(const QString &path, const QStringList &roots)
{
    const QString *best = nullptr;
    for (const QString &r : roots) {
        if (path.startsWith(r) && path.size() > r.size() && path[r.size()] == u'/'
            && (!best || r.size() > best->size()))
            best = &r;
    }
    if (!best)
        return path;
    const QString rel = path.mid(best->size() + 1);
    if (roots.size() == 1)
        return rel;
    const QString name = QFileInfo(*best).fileName();
    return (name.isEmpty() ? *best : name) + u'/' + rel;
}

void TaskStore::beginChange()
{
    if (!m_changing) {
        m_changing = true;
        emit aboutToChange();
    }
}

void TaskStore::endChange()
{
    if (m_changing) {
        m_changing = false;
        emit changed();
    }
}

void TaskStore::setRoots(const QStringList &roots)
{
    m_roots.clear();
    for (const QString &r : roots) {
        const QString n = normalizeRoot(r);
        if (!m_roots.contains(n))
            m_roots << n;
    }
    const int gen = ++m_generation;
    m_loading = true;
    m_pendingFiles.clear();
    m_pendingDirs.clear();
    m_watchQueue.clear();
    m_watchTimer.stop();
    emit loadStarted(m_roots);

    auto *watcher = new QFutureWatcher<std::shared_ptr<LoadResult>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, gen] {
        watcher->deleteLater();
        if (gen != m_generation)
            return; // superseded by a newer setRoots()
        std::shared_ptr<LoadResult> res = watcher->result();

        beginChange();
        m_files = std::move(res->files);
        m_byPath.clear();
        for (auto &f : m_files)
            m_byPath.insert(f->path, f.get());
        renumber();

        if (!m_watcher.files().isEmpty())
            m_watcher.removePaths(m_watcher.files());
        if (!m_watcher.directories().isEmpty())
            m_watcher.removePaths(m_watcher.directories());
        m_known = QSet<QString>(res->mdFiles.begin(), res->mdFiles.end());
        m_dirs = QSet<QString>(res->dirs.begin(), res->dirs.end());
        // Adding thousands of inotify watches takes ~20µs each; do it in
        // slices after the first paint instead of blocking the load.
        m_watchQueue = res->dirs + res->mdFiles;
        m_watchTimer.start();

        m_loading = false;
        endChange();

        int tasks = 0;
        for (auto &f : m_files)
            tasks += int(f->tasks.size());
        emit loadFinished(int(m_files.size()), tasks, res->ms);
    });
    watcher->setFuture(QtConcurrent::run(loadTree, m_roots));
}

void TaskStore::reload()
{
    setRoots(m_roots);
}

void TaskStore::renumber()
{
    for (size_t i = 0; i < m_files.size(); ++i)
        m_files[i]->row = int(i);
}

void TaskStore::insertFile(std::unique_ptr<TaskFile> file)
{
    auto it = std::lower_bound(m_files.begin(), m_files.end(), file->relPath,
                               [](const auto &f, const QString &p) { return lessPath(f->relPath, p); });
    m_byPath.insert(file->path, file.get());
    m_files.insert(it, std::move(file));
    renumber();
}

void TaskStore::removeFile(TaskFile *file)
{
    m_byPath.remove(file->path);
    m_files.erase(m_files.begin() + file->row);
    renumber();
}

void TaskStore::replaceFile(TaskFile *file, std::unique_ptr<TaskFile> fresh)
{
    // Keep the TaskFile object (its address may be held across the change).
    file->tasks = std::move(fresh->tasks);
    file->roots = std::move(fresh->roots);
    file->done = fresh->done;
    file->total = fresh->total;
    file->hash = fresh->hash;
    for (Task &t : file->tasks)
        t.file = file;
}

void TaskStore::reloadFile(const QString &path)
{
    std::unique_ptr<TaskFile> fresh;
    if (QFileInfo(path).isFile()) {
        fresh = parser::parseFile(path, relPathFor(path, m_roots));
        if (fresh && fresh->tasks.empty())
            fresh.reset();
    }
    TaskFile *old = fileByPath(path);
    if (!old && !fresh)
        return;
    if (old && fresh && old->hash == fresh->hash)
        return;
    beginChange();
    if (!fresh)
        removeFile(old);
    else if (!old)
        insertFile(std::move(fresh));
    else
        replaceFile(old, std::move(fresh));
}

void TaskStore::queueFile(const QString &path)
{
    m_pendingFiles.insert(path);
    m_flushTimer.start();
}

void TaskStore::queueDir(const QString &dir)
{
    m_pendingDirs.insert(dir);
    m_flushTimer.start();
}

void TaskStore::flushPending()
{
    if (m_loading) {
        m_flushTimer.start();
        return;
    }
    const QSet<QString> dirs = std::exchange(m_pendingDirs, {});
    const QSet<QString> files = std::exchange(m_pendingFiles, {});
    for (const QString &d : dirs)
        rescanDir(d);
    for (const QString &f : files) {
        reloadFile(f);
        if (QFileInfo::exists(f))
            m_watcher.addPath(f); // atomic saves replace the inode and drop the watch
        else
            m_known.remove(f);
    }
    endChange();
}

void TaskStore::scanNewDir(const QString &dir)
{
    m_dirs.insert(dir);
    m_watcher.addPath(dir);
    QDirIterator it(dir, QDir::AllEntries | QDir::NoDotAndDotDot);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        if (fi.isDir()) {
            if (!isIgnoredDir(fi.fileName()))
                scanNewDir(fi.filePath());
        } else if (isMarkdown(fi.fileName()) && !m_known.contains(fi.filePath())) {
            m_known.insert(fi.filePath());
            m_watcher.addPath(fi.filePath());
            reloadFile(fi.filePath());
        }
    }
}

void TaskStore::rescanDir(const QString &dir)
{
    const QString prefix = dir + u'/';
    if (!QFileInfo(dir).isDir()) {
        // Directory removed: drop everything beneath it.
        for (auto it = m_dirs.begin(); it != m_dirs.end();)
            it = (*it == dir || it->startsWith(prefix)) ? m_dirs.erase(it) : std::next(it);
        const QSet<QString> known = m_known;
        for (const QString &f : known) {
            if (f.startsWith(prefix)) {
                m_known.remove(f);
                reloadFile(f);
            }
        }
        return;
    }
    QDirIterator it(dir, QDir::AllEntries | QDir::NoDotAndDotDot);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        if (fi.isDir()) {
            if (!isIgnoredDir(fi.fileName()) && !m_dirs.contains(fi.filePath()))
                scanNewDir(fi.filePath());
        } else if (isMarkdown(fi.fileName()) && !m_known.contains(fi.filePath())) {
            m_known.insert(fi.filePath());
            m_watcher.addPath(fi.filePath());
            reloadFile(fi.filePath());
        }
    }
    // Files removed or renamed away from this directory.
    const QSet<QString> known = m_known;
    for (const QString &f : known) {
        if (f.startsWith(prefix) && f.indexOf(u'/', prefix.size()) < 0 && !QFileInfo::exists(f)) {
            m_known.remove(f);
            reloadFile(f);
        }
    }
}

TaskStore::Outcome TaskStore::applyRaw(const std::vector<Edit> &edits)
{
    QHash<QString, std::vector<Edit>> byFile;
    QStringList order;
    for (const Edit &e : edits) {
        auto &v = byFile[e.path];
        if (v.empty())
            order << e.path;
        v.push_back(e);
    }

    Outcome out;
    beginChange();
    for (const QString &path : order) {
        std::vector<Edit> &fileEdits = byFile[path];
        QFile in(path);
        if (!in.open(QIODevice::ReadOnly)) {
            out.errors << path;
            continue;
        }
        LineBuffer buf = LineBuffer::fromData(in.readAll());
        in.close();

        ApplyResult r = applyEdits(buf, std::move(fileEdits));
        out.conflicts += r.conflicts;
        if (r.applied > 0) {
            QSaveFile file(path);
            if (file.open(QIODevice::WriteOnly) && file.write(buf.toData()) >= 0 && file.commit()) {
                out.applied += r.applied;
                out.inverse.insert(out.inverse.end(), r.inverse.begin(), r.inverse.end());
            } else {
                out.errors << path;
            }
        }
        reloadFile(path);
        m_watcher.addPath(path);
    }
    endChange();
    return out;
}

void TaskStore::reportOutcome(const Outcome &o, const QString &label)
{
    QStringList parts;
    if (o.conflicts > 0)
        parts << tr("%n line(s) changed on disk and were skipped", nullptr, o.conflicts);
    if (!o.errors.isEmpty())
        parts << tr("could not write %1").arg(o.errors.join(QLatin1String(", ")));
    if (!parts.isEmpty())
        emit message(label + QLatin1String(": ") + parts.join(QLatin1String("; ")));
}

int TaskStore::apply(const std::vector<Edit> &edits, const QString &label)
{
    if (edits.empty())
        return 0;
    Outcome o = applyRaw(edits);
    reportOutcome(o, label);
    if (o.applied > 0) {
        m_undo.push_back({label, std::move(o.inverse)});
        if (m_undo.size() > kMaxUndo)
            m_undo.erase(m_undo.begin());
        m_redo.clear();
        emit undoStateChanged();
    }
    return o.applied;
}

void TaskStore::undo()
{
    if (m_undo.empty())
        return;
    Step step = std::move(m_undo.back());
    m_undo.pop_back();
    Outcome o = applyRaw(step.edits);
    emit message(tr("Undid: %1").arg(step.label));
    reportOutcome(o, tr("Undo %1").arg(step.label));
    if (o.applied > 0)
        m_redo.push_back({step.label, std::move(o.inverse)});
    emit undoStateChanged();
}

void TaskStore::redo()
{
    if (m_redo.empty())
        return;
    Step step = std::move(m_redo.back());
    m_redo.pop_back();
    Outcome o = applyRaw(step.edits);
    emit message(tr("Redid: %1").arg(step.label));
    reportOutcome(o, tr("Redo %1").arg(step.label));
    if (o.applied > 0)
        m_undo.push_back({step.label, std::move(o.inverse)});
    emit undoStateChanged();
}

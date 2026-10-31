// Store + models against real temporary directories. QAbstractItemModelTester
// checks every model signal for consistency (fatal on violation).

#include "models.h"
#include "parser.h"
#include "store.h"

#include <QAbstractItemModelTester>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TestStore : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    TaskStore *m_store = nullptr;
    TaskListModel *m_list = nullptr;
    AgendaModel *m_agenda = nullptr;

    QString path(const QString &rel) const { return m_dir.path() + u'/' + rel; }

    static void write(const QString &p, const QByteArray &data)
    {
        QDir().mkpath(QFileInfo(p).path());
        QFile f(p);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(data);
    }

    static QByteArray read(const QString &p)
    {
        QFile f(p);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

    int taskCount() const
    {
        int n = 0;
        for (const auto &f : m_store->files())
            n += int(f->tasks.size());
        return n;
    }

    // "  [x] text  (1/2)" from the roles the delegate draws.
    QStringList listRows() const
    {
        QStringList rows;
        for (int r = 0; r < m_list->rowCount(); ++r) {
            const QModelIndex i = m_list->index(r, 0);
            QString s(i.data(TaskModelBase::DepthRole).toInt() * 2, u' ');
            const QVariant st = i.data(TaskModelBase::StateRole);
            if (st.isValid())
                s += QStringLiteral("[%1] ").arg(parser::stateChar(TaskState(st.toInt())));
            s += i.data().toString();
            const QVariant pr = i.data(TaskModelBase::ProgressRole);
            if (pr.isValid())
                s += QStringLiteral("  (%1/%2)").arg(pr.toPoint().x()).arg(pr.toPoint().y());
            rows << s;
        }
        return rows;
    }

    int agendaDays() const
    {
        int n = 0;
        for (int r = 0; r < m_agenda->rowCount(); ++r)
            n += m_agenda->index(r, 0).data(TaskModelBase::HeaderRole).toBool();
        return n;
    }

    QStringList agendaRows() const
    {
        QStringList rows;
        for (int r = 0; r < m_agenda->rowCount(); ++r) {
            const QModelIndex i = m_agenda->index(r, 0);
            rows << QString(i.data(TaskModelBase::DepthRole).toInt() * 2, u' ')
                        + (i.data(TaskModelBase::HeaderRole).toBool() ? i.data().toString().left(14) : i.data().toString());
        }
        return rows;
    }

    void load(const QStringList &roots)
    {
        QSignalSpy loaded(m_store, &TaskStore::loadFinished);
        m_store->setRoots(roots);
        QVERIFY(loaded.wait(5000));
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        write(path("a.md"), "# A\n- [ ] one !2026-11-02 ~1h\n  - [x] one.a\n  - [ ] one.b ~2m\n- [/] two ~3d\n");
        write(path("sub/b.md"), "- [ ] bee +2026-10-01\n");
        write(path("sub/empty.md"), "no tasks here\n");
        write(path(".hidden/c.md"), "- [ ] hidden\n");
        write(path("node_modules/d.md"), "- [ ] vendored\n");

        m_store = new TaskStore(this);
        m_list = new TaskListModel(m_store, this);
        m_agenda = new AgendaModel(m_store, this);
        new QAbstractItemModelTester(m_list, QAbstractItemModelTester::FailureReportingMode::Fatal, this);
        new QAbstractItemModelTester(m_agenda, QAbstractItemModelTester::FailureReportingMode::Fatal, this);
        load({m_dir.path()});
    }

    void loaded()
    {
        QCOMPARE(m_store->files().size(), size_t(2)); // hidden, node_modules and empty skipped
        QCOMPARE(taskCount(), 5);
        QCOMPARE(listRows(), QStringList({
                                 "a.md  (1/4)",
                                 "  [ ] one  (1/2)",
                                 "    [x] one.a",
                                 "    [ ] one.b",
                                 "  [/] two",
                                 "sub/b.md  (0/1)",
                                 "  [ ] bee",
                             }));
        QCOMPARE(m_list->index(1, TaskListModel::EtaColumn).data().toString(), QString("1h"));
        QCOMPARE(agendaDays(), 1);
    }

    void listFilterKeepsAncestors()
    {
        m_list->filter().setStateVisible(TaskState::Done, false);
        m_list->refilter();
        QCOMPARE(m_list->rowCount(), 6);

        // Ancestors of a match stay visible for context.
        m_list->filter().setText("one.b");
        m_list->refilter();
        QCOMPARE(listRows(), QStringList({"a.md  (1/4)", "  [ ] one  (1/2)", "    [ ] one.b"}));

        m_list->filter().setText("sub/");
        m_list->refilter();
        QCOMPARE(m_list->rowCount(), 2);
        m_list->filter().setText("!2026-11");
        m_list->refilter();
        QCOMPARE(m_list->rowCount(), 2);
        m_list->filter().setText("");
        m_list->filter().setStateVisible(TaskState::Done, true);
        m_list->refilter();
        QCOMPARE(m_list->rowCount(), 7);
    }

    void collapseAndExpand()
    {
        const QStringList all = listRows();
        // Collapse "one": its two subtasks go, in place.
        QCOMPARE(m_list->index(1, 0).data(TaskModelBase::HasChildrenRole).toBool(), true);
        m_list->setExpanded(1, false);
        QCOMPARE(listRows(), QStringList({"a.md  (1/4)", "  [ ] one  (1/2)", "  [/] two", "sub/b.md  (0/1)", "  [ ] bee"}));
        QCOMPARE(m_list->index(1, 0).data(TaskModelBase::ExpandedRole).toBool(), false);
        QCOMPARE(m_store->taskAt(path("a.md"), 2)->listRow, -1);
        QCOMPARE(m_list->indexOf(m_store->taskAt(path("sub/b.md"), 0)).row(), 4);

        // Collapse the whole file; the inner collapse is remembered.
        m_list->setExpanded(0, false);
        QCOMPARE(listRows(), QStringList({"a.md  (1/4)", "sub/b.md  (0/1)", "  [ ] bee"}));
        m_list->setExpanded(0, true);
        QCOMPARE(listRows(), QStringList({"a.md  (1/4)", "  [ ] one  (1/2)", "  [/] two", "sub/b.md  (0/1)", "  [ ] bee"}));

        // Survives a store change (rebuild).
        QSignalSpy loaded(m_store, &TaskStore::loadFinished);
        m_store->reload(); // F5
        QVERIFY(loaded.wait(5000));
        QCOMPARE(m_store->files().size(), size_t(2));
        QCOMPARE(m_list->rowCount(), 5);

        // A narrowing filter ignores it, so matches inside show.
        m_list->filter().setText("one.b");
        m_list->refilter();
        QCOMPARE(listRows(), QStringList({"a.md  (1/4)", "  [ ] one  (1/2)", "    [ ] one.b"}));
        m_list->filter().setText("");
        m_list->refilter();
        QCOMPARE(m_list->rowCount(), 5);

        QCOMPARE(m_list->parentRow(4), 3);
        QCOMPARE(m_list->parentRow(0), -1);
        m_list->setExpanded(1, true);
        QCOMPARE(listRows(), all);

        m_list->collapseAll();
        QCOMPARE(listRows(), QStringList({"a.md  (1/4)", "sub/b.md  (0/1)"}));
        QCOMPARE(m_list->collapsedKeys().size(), 2);
        m_list->expandAll();
        QCOMPARE(listRows(), all);
    }

    void etaFilter()
    {
        using C = TaskFilter::EtaCond;
        auto texts = [&] {
            QStringList out;
            for (int r = 0; r < m_list->rowCount(); ++r) {
                if (const Task *t = m_list->taskAt(m_list->index(r, 0)))
                    out << t->text;
            }
            return out;
        };
        m_list->filter().setText("~<=2m");
        m_list->refilter();
        QCOMPARE(texts(), QStringList({"one", "one.b"})); // parent kept for context
        m_list->filter().setText("~>1d");
        m_list->refilter();
        QCOMPARE(texts(), QStringList({"two"}));
        m_list->filter().setText("~>=1h ~<1d");
        m_list->refilter();
        QCOMPARE(texts(), QStringList({"one"}));
        m_list->filter().setText("~none");
        m_list->refilter();
        QCOMPARE(texts(), QStringList({"one", "one.a", "bee"}));
        m_list->filter().setText("");
        m_list->filter().setEtaPreset({{C::Any, 0}});
        m_list->refilter();
        QCOMPARE(texts(), QStringList({"one", "one.b", "two"}));
        m_list->filter().setEtaPreset({});
        m_list->refilter();
        QCOMPARE(m_list->rowCount(), 7);

        m_agenda->filter().setText("~>1d");
        m_agenda->refilter();
        QCOMPARE(agendaDays(), 0); // the only due task is ~1h
        m_agenda->filter().setText("");
        m_agenda->refilter();
        QCOMPARE(agendaDays(), 1);
    }

    void agendaTree()
    {
        // The due task carries its subtasks, collapsible like the task list.
        QCOMPARE(agendaRows(), QStringList({"Mon 2026-11-02", "  one", "    one.a", "    one.b"}));
        m_agenda->setExpanded(1, false);
        QCOMPARE(agendaRows(), QStringList({"Mon 2026-11-02", "  one"}));
        m_agenda->setExpanded(0, false);
        QCOMPARE(m_agenda->rowCount(), 1);
        m_agenda->expandAll();
        QCOMPARE(m_agenda->rowCount(), 4);
        QCOMPARE(m_agenda->indexOf(m_store->taskAt(path("a.md"), 3)).row(), 3);

        // Single-day mode (sidebar): the chosen day only, even when empty.
        AgendaModel day(m_store);
        new QAbstractItemModelTester(&day, QAbstractItemModelTester::FailureReportingMode::Fatal, this);
        day.setSingleDay(QDate(2026, 11, 2));
        QCOMPARE(day.rowCount(), 4);
        day.setSingleDay(QDate(2026, 11, 3));
        QCOMPARE(day.rowCount(), 1);
        QVERIFY(day.index(0, 0).data(TaskModelBase::HeaderRole).toBool());

        // Labels: relative only within 14 days; empty past days aren't "overdue".
        const QDate today = QDate::currentDate();
        auto label = [&](int offset) {
            day.setSingleDay(today.addDays(offset));
            return day.index(0, 0).data().toString();
        };
        QVERIFY(label(0).endsWith("today"));
        QVERIFY(label(-3).endsWith("3 days ago"));
        QVERIFY(label(5).endsWith("in 5 days"));
        QCOMPARE(label(-174), today.addDays(-174).toString("ddd yyyy-MM-dd"));
        QCOMPARE(label(30), today.addDays(30).toString("ddd yyyy-MM-dd"));
    }

    void dueAndStateTerms()
    {
        TaskFilter f;
        auto file = parser::parse("/x.md", "x.md",
                                  "- [ ] late !2000-01-01\n- [x] done late !2000-01-01\n- [/] doing\n- [ ] none\n");
        auto names = [&](const QString &text) {
            f.setText(text);
            QStringList out;
            for (const Task &t : file->tasks) {
                if (f.matches(t))
                    out << t.text;
            }
            return out;
        };
        QCOMPARE(names("due:overdue"), QStringList({"late"}));
        QCOMPARE(names("due:none"), QStringList({"doing", "none"}));
        QCOMPARE(names("due:any is:done"), QStringList({"done late"}));
        QCOMPARE(names("is:open"), QStringList({"late", "doing", "none"}));
        QCOMPARE(names("is:doing"), QStringList({"doing"}));
        QCOMPARE(names("due:bogus"), QStringList()); // unknown: a plain word
        QVERIFY(f.narrows());
    }

    void tagTerms()
    {
        auto f = parser::parse("/t.md", "t.md",
                               "- [ ] a #work\n- [ ] b #work/meeting\n- [ ] c #workshop\n- [ ] d #Home #work\n");
        auto names = [&](const QString &text) {
            TaskFilter filter;
            filter.setText(text);
            QStringList out;
            for (const Task &t : f->tasks) {
                if (filter.matches(t))
                    out << t.text;
            }
            return out;
        };
        QCOMPARE(names("#work"), QStringList({"a", "b", "d"})); // nested tags match, prefixes don't
        QCOMPARE(names("#WORK/meeting"), QStringList({"b"}));
        QCOMPARE(names("#home #work"), QStringList({"d"}));
        QCOMPARE(names("#nope"), QStringList());

        // Subtasks inherit their ancestors' tags.
        auto g = parser::parse("/u.md", "u.md", "- [ ] p #work\n  - [ ] c\n    - [ ] gc #home\n- [ ] q\n");
        TaskFilter work, home;
        work.setText("#work");
        home.setText("#work #home");
        QVERIFY(work.matches(g->tasks[0]) && work.matches(g->tasks[1]) && work.matches(g->tasks[2]));
        QVERIFY(!work.matches(g->tasks[3]));
        QVERIFY(!home.matches(g->tasks[1]) && home.matches(g->tasks[2]));
        TaskFilter plain;
        plain.setText("#work");
        QVERIFY(plain.narrows());
    }

    void bulkEditUndoRedo()
    {
        const QByteArray before = read(path("a.md"));
        std::vector<Edit> edits;
        for (const auto &f : m_store->files()) {
            for (const Task &t : f->tasks) {
                if (t.state != TaskState::Done)
                    edits.push_back({f->path, t.line, {t.raw()}, {parser::setState(t.raw(), TaskState::Done, QDate(2026, 10, 31))}});
            }
        }
        QCOMPARE(m_store->apply(edits, "done"), 4);
        QCOMPARE(read(path("a.md")),
                 QByteArray("# A\n- [x] one !2026-11-02 ~1h x2026-10-31\n  - [x] one.a\n  - [x] one.b ~2m x2026-10-31\n"
                            "- [x] two ~3d x2026-10-31\n"));
        QCOMPARE(agendaDays(), 0); // no open tasks with a due date

        m_store->undo();
        QCOMPARE(read(path("a.md")), before);
        QCOMPARE(agendaDays(), 1);
        m_store->redo();
        QCOMPARE(m_store->taskAt(path("sub/b.md"), 0)->state, TaskState::Done);
        m_store->undo();
        QCOMPARE(read(path("a.md")), before);
    }

    void appendCreatesFile()
    {
        const QString p = path("todo.md");
        QVERIFY(!QFile::exists(p));
        const size_t files = m_store->files().size();
        // Only an append may create a file; a replace into a missing file is an error.
        QCOMPARE(m_store->apply({{path("missing.md"), 0, {"x"}, {"y"}}}, "bad"), 0);
        QVERIFY(!QFile::exists(path("missing.md")));
        QCOMPARE(m_store->apply({{p, -1, {}, {"- [ ] new"}}}, "add"), 1);
        QCOMPARE(read(p), QByteArray("- [ ] new\n"));
        QCOMPARE(m_store->files().size(), files + 1);
        QVERIFY(m_store->taskAt(p, 0));
        QTest::qWait(300); // the directory watcher must not load it a second time
        QCOMPARE(m_store->files().size(), files + 1);
        m_store->undo();
        QCOMPARE(read(p), QByteArray());
        QCOMPARE(m_store->files().size(), files);
        QVERIFY(QFile::remove(p));
        QTest::qWait(300);
        QCOMPARE(m_store->files().size(), files);
    }

    void conflictIsSkipped()
    {
        Task *t = m_store->taskAt(path("sub/b.md"), 0);
        QVERIFY(t);
        Edit stale{t->file->path, 0, {"- [ ] something else"}, {"- [x] clobber"}};
        QSignalSpy msg(m_store, &TaskStore::message);
        QCOMPARE(m_store->apply({stale}, "stale"), 0);
        QCOMPARE(msg.size(), 1);
        QCOMPARE(read(path("sub/b.md")), QByteArray("- [ ] bee +2026-10-01\n"));
    }

    void insertAndDeleteLines()
    {
        Task *one = m_store->taskAt(path("a.md"), 1);
        Task *last = one->children.back();
        QCOMPARE(m_store->apply({{one->file->path, last->line, {last->raw()}, {last->raw(), "  - [ ] one.c"}}}, "add"), 1);
        one = m_store->taskAt(path("a.md"), 1);
        QCOMPARE(one->children.size(), size_t(3));
        QCOMPARE(one->subTotal, 3);
        QCOMPARE(m_list->index(one->children.back()->listRow, 0).data(TaskModelBase::DepthRole).toInt(), 2);

        std::vector<Edit> del;
        del.push_back({one->file->path, one->line, {one->raw()}, {}});
        for (Task *c : one->children)
            del.push_back({c->file->path, c->line, {c->raw()}, {}});
        QCOMPARE(m_store->apply(del, "delete"), 4);
        QCOMPARE(read(path("a.md")), QByteArray("# A\n- [/] two ~3d\n"));
        m_store->undo();
        QCOMPARE(read(path("a.md")),
                 QByteArray("# A\n- [ ] one !2026-11-02 ~1h\n  - [x] one.a\n  - [ ] one.b ~2m\n  - [ ] one.c\n- [/] two ~3d\n"));
    }

    void externalChanges()
    {
        QTest::qWait(200); // let the deferred watcher registration finish

        // New file in a new directory.
        write(path("new/dir/n.md"), "- [ ] fresh !2026-11-05\n");
        QTRY_COMPARE_WITH_TIMEOUT(m_store->files().size(), size_t(3), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(agendaDays(), 2, 5000);

        // Previously empty file gains a task.
        write(path("sub/empty.md"), "no tasks here\n- [ ] now one\n");
        QTRY_COMPARE_WITH_TIMEOUT(m_store->files().size(), size_t(4), 5000);

        // In-place modification.
        write(path("sub/b.md"), "- [ ] bee\n- [ ] bee2\n");
        QTRY_COMPARE_WITH_TIMEOUT(m_store->fileByPath(path("sub/b.md"))->tasks.size(), size_t(2), 5000);

        // Removal.
        QVERIFY(QFile::remove(path("sub/empty.md")));
        QTRY_COMPARE_WITH_TIMEOUT(m_store->files().size(), size_t(3), 5000);
        QVERIFY(QDir(path("new")).removeRecursively());
        QTRY_COMPARE_WITH_TIMEOUT(m_store->files().size(), size_t(2), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(agendaDays(), 1, 5000);
    }

    void multipleRoots()
    {
        QTemporaryDir other;
        write(other.path() + "/o.md", "- [ ] other\n");
        // Overlapping roots: sub/ is inside the first root; its files are held once.
        load({m_dir.path(), other.path(), path("sub")});
        QCOMPARE(m_store->roots().size(), 3);
        QCOMPARE(m_store->files().size(), size_t(3));
        const QString name = QFileInfo(m_dir.path()).fileName();
        QVERIFY(m_store->fileByPath(path("a.md")));
        QCOMPARE(m_store->fileByPath(path("a.md"))->relPath, name + "/a.md");
        // The innermost root names a file.
        QCOMPARE(m_store->fileByPath(path("sub/b.md"))->relPath, QString("sub/b.md"));
        QCOMPARE(m_store->fileByPath(other.path() + "/o.md")->relPath, QFileInfo(other.path()).fileName() + "/o.md");

        load({});
        QCOMPARE(m_store->files().size(), size_t(0));
        QCOMPARE(m_list->rowCount(), 0);
        load({m_dir.path()});
        QCOMPARE(m_store->files().size(), size_t(2));
    }
};

QTEST_MAIN(TestStore)
#include "test_store.moc"

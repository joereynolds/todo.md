#include "edits.h"
#include "editor.h"
#include "parser.h"
#include "templates.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <QTest>

class TestCore : public QObject
{
    Q_OBJECT

private slots:
    void states()
    {
        auto f = parser::parse("/a.md", "a.md",
                               "- [ ] todo\n- [-] cancelled\n- [/] doing\n- [x] done\n- [X] done2\n"
                               "- [] not a task\n- [ ]no space\n- [?] unknown\n* [ ] star\n+ [ ] plus\n");
        QCOMPARE(f->tasks.size(), size_t(7));
        QCOMPARE(f->tasks[0].state, TaskState::Todo);
        QCOMPARE(f->tasks[1].state, TaskState::Cancelled);
        QCOMPARE(f->tasks[2].state, TaskState::Doing);
        QCOMPARE(f->tasks[3].state, TaskState::Done);
        QCOMPARE(f->tasks[4].state, TaskState::Done);
        QCOMPARE(f->tasks[5].text, QString("star"));
        QCOMPARE(f->tasks[6].line, 9);
    }

    void dates()
    {
        auto f = parser::parse("/a.md", "a.md",
                               "- [ ] Buy !2026-12-25 gifts +2026-12-01 x2026-12-24 -2026-12-23\n"
                               "- [ ] bad !2026-13-01 and a-2026-12-25 and !2026-02-30\n");
        const Task &t = f->tasks[0];
        QCOMPARE(t.due, QDate(2026, 12, 25));
        QCOMPARE(t.created, QDate(2026, 12, 1));
        QCOMPARE(t.completed, QDate(2026, 12, 24));
        QCOMPARE(t.cancelled, QDate(2026, 12, 23));
        QCOMPARE(t.text, QString("Buy gifts"));
        const Task &b = f->tasks[1];
        QVERIFY(!b.due.isValid());
        QCOMPARE(b.text, QString("bad !2026-13-01 and a-2026-12-25 and !2026-02-30"));
    }

    void nesting()
    {
        auto f = parser::parse("/a.md", "a.md",
                               "# Kitchen\n"
                               "- [ ] Sort the kitchen\n"
                               "  - [x] Fix the tap\n"
                               "    - [x] Call a plumber\n"
                               "\n"
                               "  - [ ] clean counter tops\n"
                               "  - [-] take bins out\n"
                               "\tsome note\n"
                               "  - [ ] last\n"
                               "Paragraph\n"
                               "  - [ ] root after paragraph\n");
        QCOMPARE(f->tasks.size(), size_t(7));
        const Task &root = f->tasks[0];
        QCOMPARE(root.children.size(), size_t(4));
        QCOMPARE(root.subTotal, 4); // 5 descendants, one cancelled
        QCOMPARE(root.subDone, 2);
        QCOMPARE(f->tasks[1].subTotal, 1);
        QCOMPARE(f->tasks[1].subDone, 1);
        QCOMPARE(f->roots.size(), size_t(2));
        QCOMPARE(f->tasks[6].parent, nullptr);
        QCOMPARE(f->total, 6);
        QCOMPARE(f->done, 2);
    }

    void fences()
    {
        auto f = parser::parse("/a.md", "a.md",
                               "```\n- [ ] example\n```\n- [ ] real\n~~~~\n```\n- [ ] still code\n~~~~\n- [ ] real2\n");
        QCOMPARE(f->tasks.size(), size_t(2));
        QCOMPARE(f->tasks[0].text, QString("real"));
        QCOMPARE(f->tasks[1].text, QString("real2"));
    }

    void crlfAndUtf8()
    {
        auto f = parser::parse("/a.md", "a.md", "- [ ] café ☕ !2026-01-02\r\n- [x] b\r\n");
        QCOMPARE(f->tasks.size(), size_t(2));
        QCOMPARE(f->tasks[0].content, QString::fromUtf8("café ☕ !2026-01-02"));
        QCOMPARE(f->tasks[0].due, QDate(2026, 1, 2));
    }

    void lineTransforms()
    {
        const QDate today(2026, 10, 31);
        QCOMPARE(parser::setState("  - [ ] a !2026-11-01", TaskState::Done, today),
                 QString("  - [x] a !2026-11-01 x2026-10-31"));
        QCOMPARE(parser::setState("- [x] a x2026-10-30", TaskState::Todo, today), QString("- [ ] a"));
        QCOMPARE(parser::setState("- [x] a x2026-10-30", TaskState::Cancelled, today),
                 QString("- [-] a -2026-10-31"));
        QCOMPARE(parser::setState("- [/] a", TaskState::Doing, today), QString("- [/] a"));
        QCOMPARE(parser::setState("x2026-10-30 not a task", TaskState::Done, today), QString("x2026-10-30 not a task"));
        QCOMPARE(parser::setDue("- [ ] a !2026-01-01 b", QDate(2026, 2, 2)), QString("- [ ] a b !2026-02-02"));
        QCOMPARE(parser::setDue("- [ ] !2026-01-01 a", QDate()), QString("- [ ] a"));
        QCOMPARE(parser::setContent("\t* [/] old", "new"), QString("\t* [/] new"));
        QCOMPARE(parser::setCreated("  - [ ] new !2026-11-02", today), QString("  - [ ] new !2026-11-02 +2026-10-31"));
        QCOMPARE(parser::setCreated("- [ ] typed +2026-01-01", today), QString("- [ ] typed +2026-01-01"));
    }

    void applyAndUndo()
    {
        const QByteArray original = "# T\r\n- [ ] a\r\n- [ ] b\r\n  - [ ] c\r\n";
        LineBuffer buf = LineBuffer::fromData(original);
        QVERIFY(buf.crlf);
        std::vector<Edit> edits = {
            {"/a.md", 1, {"- [ ] a"}, {"- [x] a"}},
            {"/a.md", 2, {"- [ ] b"}, {"- [ ] b", "- [ ] new"}}, // insert
            {"/a.md", 3, {"  - [ ] c"}, {}},                     // delete
            {"/a.md", -1, {}, {"- [ ] appended"}},
            {"/a.md", 0, {"# wrong"}, {"# X"}},                  // conflict
        };
        ApplyResult r = applyEdits(buf, edits);
        QCOMPARE(r.applied, 4);
        QCOMPARE(r.conflicts, 1);
        QCOMPARE(buf.toData(), QByteArray("# T\r\n- [x] a\r\n- [ ] b\r\n- [ ] new\r\n- [ ] appended\r\n"));

        ApplyResult u = applyEdits(buf, r.inverse);
        QCOMPARE(u.applied, 4);
        QCOMPARE(u.conflicts, 0);
        QCOMPARE(buf.toData(), original);

        ApplyResult redo = applyEdits(buf, u.inverse);
        QCOMPARE(redo.applied, 4);
        QCOMPARE(buf.toData(), QByteArray("# T\r\n- [x] a\r\n- [ ] b\r\n- [ ] new\r\n- [ ] appended\r\n"));
    }

    void undoContiguousDelete()
    {
        const QByteArray original = "a\nb\nc\nd\ne\n";
        LineBuffer buf = LineBuffer::fromData(original);
        ApplyResult r = applyEdits(buf, {{"/a.md", 1, {"b"}, {}}, {"/a.md", 2, {"c"}, {}}, {"/a.md", 3, {"d"}, {}}});
        QCOMPARE(buf.toData(), QByteArray("a\ne\n"));
        applyEdits(buf, r.inverse);
        QCOMPARE(buf.toData(), original);

        // Insertions at the same line land in list order.
        LineBuffer ins = LineBuffer::fromData("a\nz\n");
        ApplyResult i = applyEdits(ins, {{"/a.md", 1, {}, {"b"}}, {"/a.md", 1, {}, {"c"}}, {"/a.md", 1, {"z"}, {"y"}}});
        QCOMPARE(ins.toData(), QByteArray("a\nb\nc\ny\n"));
        applyEdits(ins, i.inverse);
        QCOMPARE(ins.toData(), QByteArray("a\nz\n"));
    }

    void eta()
    {
        auto f = parser::parse("/a.md", "a.md",
                               "- [ ] Wash hands ~2m\n- [ ] Movie ~90m !2026-12-01\n- [ ] Wales ~7d\n"
                               "- [ ] UK ~5w\n- [ ] Life ~85y\n- [ ] not ~2x ~ ~m a~2m\n");
        QCOMPARE(f->tasks[0].eta, 2);
        QCOMPARE(f->tasks[0].text, QString("Wash hands"));
        QCOMPARE(f->tasks[1].eta, 90);
        QCOMPARE(f->tasks[1].etaText, QString("90m"));
        QCOMPARE(f->tasks[1].text, QString("Movie"));
        QCOMPARE(f->tasks[2].eta, 7 * 24 * 60);
        QCOMPARE(f->tasks[3].eta, 5 * 7 * 24 * 60);
        QCOMPARE(f->tasks[4].eta, qint64(85) * 365 * 24 * 60);
        QCOMPARE(f->tasks[5].eta, 0);
        QCOMPARE(f->tasks[5].text, QString("not ~2x ~ ~m a~2m"));
        QCOMPARE(parser::setEta("- [ ] a ~2m b", "1h"), QString("- [ ] a b ~1h"));
        QCOMPARE(parser::setEta("- [ ] ~2m a", ""), QString("- [ ] a"));
    }

    void links()
    {
        auto check = [](QString text, const QString &shown, const std::vector<std::pair<QString, QString>> &expect) {
            const auto links = parser::extractLinks(&text);
            QCOMPARE(text, shown);
            QCOMPARE(links.size(), expect.size());
            for (size_t i = 0; i < links.size(); ++i) {
                QCOMPARE(text.mid(links[i].start, links[i].length), expect[i].first);
                QCOMPARE(links[i].url, expect[i].second);
            }
        };
        check("no links here", "no links here", {});
        check("See https://example.com/a?b=1#c.", "See https://example.com/a?b=1#c.",
              {{"https://example.com/a?b=1#c", "https://example.com/a?b=1#c"}});
        check("(docs at http://x.org/wiki/Foo_(bar))", "(docs at http://x.org/wiki/Foo_(bar))",
              {{"http://x.org/wiki/Foo_(bar)", "http://x.org/wiki/Foo_(bar)"}});
        check("go to www.site.com, then", "go to www.site.com, then", {{"www.site.com", "https://www.site.com"}});
        check("awww.nope and https:// alone", "awww.nope and https:// alone", {});
        check("Read [the spec](https://s.io/spec) and https://b.io", "Read the spec and https://b.io",
              {{"the spec", "https://s.io/spec"}, {"https://b.io", "https://b.io"}});
        check("[a](https://a.io) [b](<https://b.io/x y>)", "a [b](<https://b.io/x y>)",
              {{"a", "https://a.io"}, {"https://b.io/x", "https://b.io/x"}}); // a space ends a bare URL
        check("mail mailto:me@x.org", "mail mailto:me@x.org", {{"mailto:me@x.org", "mailto:me@x.org"}});

        // Through the parser: dates are stripped first, positions refer to Task::text.
        auto f = parser::parse("/a.md", "a.md", "- [ ] Check [PR](https://gh.com/1) !2026-01-02 https://ci.io #work\n");
        QCOMPARE(f->tasks[0].text, QString("Check PR https://ci.io"));
        QCOMPARE(f->tasks[0].links.size(), size_t(2));
        QCOMPARE(f->tasks[0].links[1].start, 9);
    }

    void addTag()
    {
        QCOMPARE(parser::addTag("- [ ] a", "work"), QString("- [ ] a #work"));
        QCOMPARE(parser::addTag("- [ ] a !2026-01-02 ", "x"), QString("- [ ] a !2026-01-02 #x"));
        QCOMPARE(parser::addTag("- [ ] a #Work, b", "work"), QString("- [ ] a #Work, b")); // already there
        QCOMPARE(parser::addTag("- [ ] a #work/meeting", "work"), QString("- [ ] a #work/meeting #work"));
        QCOMPARE(parser::addTag("not a task", "work"), QString("not a task"));
    }

    void tags()
    {
        auto f = parser::parse("/a.md", "a.md",
                               "- [ ] Do the thing at work #office\n"
                               "- [ ] Call #mum about #dinner, then #Home/kitchen #home/kitchen ~5m\n"
                               "- [ ] issue #123 and C# and # heading and #a#b\n"
                               "- [ ] #only\n");
        QCOMPARE(f->tasks[0].tags, QStringList({"office"}));
        QCOMPARE(f->tasks[0].text, QString("Do the thing at work")); // trailing tags are hidden
        QCOMPARE(f->tasks[1].tags, QStringList({"mum", "dinner", "Home/kitchen"}));
        QCOMPARE(f->tasks[1].text, QString("Call #mum about #dinner, then")); // inline ones stay
        QCOMPARE(f->tasks[2].tags, QStringList({"a"}));
        QCOMPARE(f->tasks[2].text, QString("issue #123 and C# and # heading and #a#b"));
        QCOMPARE(f->tasks[3].tags, QStringList({"only"}));
        QCOMPARE(f->tasks[3].text, QString());
        QCOMPARE(f->tasks[3].content, QString("#only"));
    }

    void listTemplates()
    {
        // Saving: todo, dates gone, ETA and tags kept, indented by selected nesting.
        auto f = parser::parse("/a.md", "a.md",
                               "  - [x] Seeing GF !2026-11-01 +2026-10-01 x2026-10-02 #gf\n"
                               "    * [/] Trim nails ~5m\n"
                               "    - [-] Cut hair -2026-10-03\n"
                               "      - [ ] Book barber\n");
        auto &t = f->tasks;
        QCOMPARE(templates::fromTasks({&t[0], &t[1], &t[2], &t[3]}),
                 QStringList({"- [ ] Seeing GF #gf", "  * [ ] Trim nails ~5m", "  - [ ] Cut hair", "    - [ ] Book barber"}));
        QCOMPARE(templates::fromTasks({&t[1], &t[3]}), QStringList({"* [ ] Trim nails ~5m", "- [ ] Book barber"}));

        // Inserting: re-indented under `lead`, todo, created today, non-task lines dropped.
        const QDate today(2026, 10, 31);
        const QByteArray file = "# Seeing GF\n- [ ] Trim nails ~5m\n- [x] Cut hair +2020-01-01 x2020-01-02\n"
                                "  - [ ] Book barber !2026-12-01\nnotes here\n";
        QCOMPARE(templates::instantiate(file, "\t", "\t", today),
                 QStringList({"\t- [ ] Trim nails ~5m +2026-10-31", "\t- [ ] Cut hair +2026-10-31",
                              "\t\t- [ ] Book barber !2026-12-01 +2026-10-31"}));
        QCOMPARE(templates::instantiate("just text\n", "", "  ", today), QStringList());

        QVERIFY(templates::isValidName("seeing-gf"));
        QVERIFY(!templates::isValidName(".hidden") && !templates::isValidName("a/b") && !templates::isValidName(" "));

        // Where templates live, listing, saving.
        QTemporaryDir a, b;
        QCOMPARE(templates::dirFor({}), QString());
        QCOMPARE(templates::dirFor({a.path(), b.path()}), a.path() + "/.templates");
        QDir().mkpath(b.path() + "/.templates");
        QCOMPARE(templates::dirFor({a.path(), b.path()}), b.path() + "/.templates"); // the one that exists
        const QString dir = templates::dirFor({a.path()});
        QVERIFY(templates::save(templates::pathFor(dir, "Zebra"), {"- [ ] z"}));
        QVERIFY(templates::save(templates::pathFor(dir, "apple"), {"- [ ] a", "  - [ ] b"}));
        QCOMPARE(templates::names(dir), QStringList({"apple", "Zebra"}));
        QFile saved(templates::pathFor(dir, "apple"));
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(saved.readAll(), QByteArray("- [ ] a\n  - [ ] b\n"));
    }

    void depth()
    {
        auto f = parser::parse("/a.md", "a.md", "- [ ] a\n  - [ ] b\n    - [ ] c\n  - [ ] d\n- [ ] e\n");
        QCOMPARE(f->tasks[0].depth, 0);
        QCOMPARE(f->tasks[1].depth, 1);
        QCOMPARE(f->tasks[2].depth, 2);
        QCOMPARE(f->tasks[3].depth, 1);
        QCOMPARE(f->tasks[4].depth, 0);
    }

    void editorLaunch()
    {
        auto l = editor::build("nvim", "/usr/bin/xfce4-terminal", "/n/t.md", 12);
        QCOMPARE(l.program, QString("/usr/bin/xfce4-terminal"));
        QCOMPARE(l.args, QStringList({"-x", "nvim", "+12", "/n/t.md"}));
        l = editor::build("vim -p", "kitty", "/n/t.md", 3);
        QCOMPARE(l.program, QString("kitty"));
        QCOMPARE(l.args, QStringList({"vim", "-p", "+3", "/n/t.md"}));
        l = editor::build("hx", "alacritty", "/n/t.md", 3);
        QCOMPARE(l.args, QStringList({"-e", "hx", "/n/t.md:3"}));
        QVERIFY(!editor::build("nvim", "", "/n/t.md", 1).isValid()); // needs a terminal
        l = editor::build("code --wait", "", "/n/t.md", 7);
        QCOMPARE(l.program, QString("code"));
        QCOMPARE(l.args, QStringList({"--wait", "-g", "/n/t.md:7"}));
        l = editor::build("gedit", "", "/n/t.md", 7);
        QCOMPARE(l.args, QStringList({"/n/t.md"}));
    }

    void noTrailingNewline()
    {
        LineBuffer buf = LineBuffer::fromData("- [ ] a");
        applyEdits(buf, {{"/a.md", -1, {}, {"- [ ] b"}}});
        QCOMPARE(buf.toData(), QByteArray("- [ ] a\n- [ ] b"));
        LineBuffer empty = LineBuffer::fromData("");
        applyEdits(empty, {{"/a.md", -1, {}, {"- [ ] b"}}});
        QCOMPARE(empty.toData(), QByteArray("- [ ] b\n"));
    }
};

QTEST_APPLESS_MAIN(TestCore)
#include "test_core.moc"

// Drives the real window through its keyboard shortcuts.

#include "mainwindow.h"
#include "models.h"
#include "store.h"
#include "dialogs.h"
#include "sidebar.h"
#include "views.h"
#include "viewbar.h"
#include "querybuilder.h"
#include "taskview.h"
#include "templatespage.h"

#include <QAbstractButton>
#include <QAction>
#include <QAbstractItemModel>
#include <QCalendarWidget>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCompleter>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSpinBox>
#include <QDir>
#include <QFile>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QMessageBox>
#include <QListWidget>
#include <QMenu>
#include <QUrl>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTabWidget>
#include <QTreeView>

class TestWindow : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    TaskStore *m_store = nullptr;
    MainWindow *m_window = nullptr;
    QTreeView *m_view = nullptr;

    QString file() const { return m_dir.path() + QStringLiteral("/t.md"); }
    QByteArray contents() const
    {
        QFile f(file());
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }
    void key(int k, Qt::KeyboardModifiers mods = Qt::NoModifier)
    {
        // Window shortcuts need an active window; a closed modal dialog leaves none offscreen.
        m_window->activateWindow();
        m_view->setFocus();
        QVERIFY(QTest::qWaitForWindowActive(m_window));
        QTest::keyClick(m_view, Qt::Key(k), mods);
    }

private slots:
    void initTestCase()
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir.path() + "/settings");
        QFile f(file());
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("- [ ] a\n- [ ] b\n  - [ ] b1\n- [ ] c\n");
        f.close();

        m_store = new TaskStore(this);
        m_window = new MainWindow(m_store);
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window));
        QSignalSpy loaded(m_store, &TaskStore::loadFinished);
        m_window->setDirectories({m_dir.path()});
        QVERIFY(loaded.wait(5000));
        m_view = m_window->findChild<QTabWidget *>()->widget(0)->findChild<QTreeView *>();
        if (!m_view)
            m_view = qobject_cast<QTreeView *>(m_window->findChild<QTabWidget *>()->widget(0));
        QVERIFY(m_view);
        m_view->setFocus();
        m_window->activateWindow();
    }

    void cleanupTestCase() { delete m_window; }

    // Rows in the flat list: 0 file header, 1 a, 2 b, 3 b1 (indented), 4 c.
    void select(int row)
    {
        const QModelIndex i = m_view->model()->index(row, 0);
        m_view->selectionModel()->setCurrentIndex(i, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }

    void layout()
    {
        QCOMPARE(m_view->model()->rowCount(), 5);
        QCOMPARE(m_view->model()->index(3, 0).data(Qt::UserRole + 1).toInt(), 2); // b1 is one level under b
        QCOMPARE(m_view->model()->index(2, 0).data().toString(), QString("b"));
        QCOMPARE(m_view->model()->index(2, 0).data(Qt::UserRole + 3).toPoint(), QPoint(0, 1)); // ProgressRole
    }

    QPoint hotspot(int row, bool checkbox)
    {
        const QModelIndex i = m_view->model()->index(row, 0);
        const auto l = TaskDelegate::layoutFor(m_view->visualRect(i), i, true);
        return (checkbox ? l.check : l.arrow).center();
    }

    void checkboxAndChevron()
    {
        select(1);
        // Checkbox on b: b and its subtask done, selection untouched.
        QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, hotspot(2, true));
        const QString today = QDate::currentDate().toString(Qt::ISODate);
        QCOMPARE(contents(), QStringLiteral("- [ ] a\n- [x] b x%1\n  - [x] b1 x%1\n- [ ] c\n").arg(today).toUtf8());
        key(Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(contents(), QByteArray("- [ ] a\n- [ ] b\n  - [ ] b1\n- [ ] c\n"));

        // Chevron on b collapses b1, again on the file header collapses everything.
        QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, hotspot(2, false));
        QCOMPARE(m_view->model()->rowCount(), 4);
        QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, hotspot(0, false));
        QCOMPARE(m_view->model()->rowCount(), 1);
        select(0);
        key(Qt::Key_Right);
        QCOMPARE(m_view->model()->rowCount(), 4);
        QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, hotspot(2, false));
        QCOMPARE(m_view->model()->rowCount(), 5);
        // Left on a subtask jumps to its parent, Left again collapses it.
        select(3);
        key(Qt::Key_Left);
        QCOMPARE(m_view->currentIndex().row(), 2);
        key(Qt::Key_Left);
        QCOMPARE(m_view->model()->rowCount(), 4);
        key(Qt::Key_Right);
        QCOMPARE(m_view->model()->rowCount(), 5);
    }

    void bulkDoneIncludesSubtasks()
    {
        key(Qt::Key_A, Qt::ControlModifier);
        key(Qt::Key_X);
        const QString today = QDate::currentDate().toString(Qt::ISODate);
        QCOMPARE(contents(),
                 QStringLiteral("- [x] a x%1\n- [x] b x%1\n  - [x] b1 x%1\n- [x] c x%1\n").arg(today).toUtf8());
        key(Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(contents(), QByteArray("- [ ] a\n- [ ] b\n  - [ ] b1\n- [ ] c\n"));
    }

    void stateOnParentAppliesToSubtasks()
    {
        select(2);
        key(Qt::Key_P);
        QCOMPARE(contents(), QByteArray("- [ ] a\n- [/] b\n  - [/] b1\n- [ ] c\n"));
        select(3);
        key(Qt::Key_T); // a subtask alone
        QCOMPARE(contents(), QByteArray("- [ ] a\n- [/] b\n  - [ ] b1\n- [ ] c\n"));
        key(Qt::Key_Z, Qt::ControlModifier);
        key(Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(contents(), QByteArray("- [ ] a\n- [ ] b\n  - [ ] b1\n- [ ] c\n"));
    }

    void doneTasksStayUntilFilterChanges()
    {
        // Done is hidden by default, but a task just ticked off stays, checked.
        QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, hotspot(2, true));
        QCOMPARE(m_view->model()->rowCount(), 5);
        QCOMPARE(m_view->model()->index(2, 0).data(TaskModelBase::StateRole).toInt(), int(TaskState::Done));
        QCOMPARE(m_view->model()->index(3, 0).data(TaskModelBase::StateRole).toInt(), int(TaskState::Done));
        select(1);
        key(Qt::Key_X);
        QCOMPARE(m_view->model()->rowCount(), 5);

        // Unticking it makes it todo again, still in place.
        QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, hotspot(2, true));
        QCOMPARE(contents(), QStringLiteral("- [x] a x%1\n- [ ] b\n  - [ ] b1\n- [ ] c\n")
                                 .arg(QDate::currentDate().toString(Qt::ISODate))
                                 .toUtf8());
        QCOMPARE(m_view->model()->rowCount(), 5);

        // A filter change lets the done ones go.
        QLineEdit *filter = nullptr;
        for (QLineEdit *e : m_window->findChildren<QLineEdit *>()) {
            if (e->placeholderText().contains("Filter"))
                filter = e;
        }
        QVERIFY(filter);
        filter->setText("x");
        filter->setText("");
        QTRY_COMPARE(m_view->model()->rowCount(), 4);

        key(Qt::Key_Z, Qt::ControlModifier);
        key(Qt::Key_Z, Qt::ControlModifier);
        key(Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(contents(), QByteArray("- [ ] a\n- [ ] b\n  - [ ] b1\n- [ ] c\n"));
        QTRY_COMPARE(m_view->model()->rowCount(), 5);
    }

    void setEta()
    {
        select(1);
        QTimer::singleShot(100, this, [] {
            auto *dlg = qobject_cast<EtaDialog *>(QApplication::activeModalWidget());
            QVERIFY(dlg);
            dlg->findChild<QSpinBox *>()->setValue(45);
            auto *unit = dlg->findChild<QComboBox *>();
            unit->setCurrentIndex(unit->findData(QChar(u'h')));
            dlg->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        key(Qt::Key_AsciiTilde, Qt::ShiftModifier);
        QCOMPARE(contents(), QByteArray("- [ ] a ~45h\n- [ ] b\n  - [ ] b1\n- [ ] c\n"));

        // Reopens on the current value; a preset button applies and closes.
        QTimer::singleShot(100, this, [] {
            auto *dlg = qobject_cast<EtaDialog *>(QApplication::activeModalWidget());
            QVERIFY(dlg);
            QCOMPARE(dlg->findChild<QSpinBox *>()->value(), 45);
            for (QPushButton *b : dlg->findChildren<QPushButton *>()) {
                if (b->text() == QLatin1String("15m"))
                    b->click();
            }
        });
        key(Qt::Key_AsciiTilde, Qt::ShiftModifier);
        QCOMPARE(contents(), QByteArray("- [ ] a ~15m\n- [ ] b\n  - [ ] b1\n- [ ] c\n"));
        key(Qt::Key_Z, Qt::ControlModifier);
        key(Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(contents(), QByteArray("- [ ] a\n- [ ] b\n  - [ ] b1\n- [ ] c\n"));
    }

    void sidebarToggle()
    {
        auto *sidebar = m_window->findChild<Sidebar *>();
        QVERIFY(sidebar);
        const bool before = sidebar->isVisible();
        key(Qt::Key_B, Qt::ControlModifier);
        QCOMPARE(sidebar->isVisible(), !before);
        key(Qt::Key_B, Qt::ControlModifier);
        QCOMPARE(sidebar->isVisible(), before);
    }

    void addSubtask()
    {
        select(2);
        QTimer::singleShot(100, this, [] {
            auto *dlg = qobject_cast<AddTaskDialog *>(QApplication::activeModalWidget());
            QVERIFY(dlg);
            QVERIFY(dlg->dueButton("none")->isChecked()); // no due date unless chosen
            dlg->lineEdit()->setText(QStringLiteral("b2"));
            dlg->accept();
        });
        key(Qt::Key_N, Qt::ShiftModifier);
        const QString today = QDate::currentDate().toString(Qt::ISODate);
        QCOMPARE(contents(), QStringLiteral("- [ ] a\n- [ ] b\n  - [ ] b1\n  - [ ] b2 +%1\n- [ ] c\n").arg(today).toUtf8());
        // The new task is selected.
        QCOMPARE(m_view->currentIndex().row(), 4);
    }

    void addTaskWithDue()
    {
        const QByteArray before = contents();
        const QDate today = QDate::currentDate();
        const QString t = today.toString(Qt::ISODate);

        // Tomorrow: one click, then Enter in the text box.
        select(1); // a
        QTimer::singleShot(100, this, [] {
            auto *dlg = qobject_cast<AddTaskDialog *>(QApplication::activeModalWidget());
            QVERIFY(dlg);
            dlg->dueButton("tomorrow")->click();
            QTest::keyClicks(dlg->lineEdit(), "buy milk");
            QTest::keyClick(dlg->lineEdit(), Qt::Key_Return);
        });
        key(Qt::Key_N);
        QCOMPARE(contents(), QByteArray(before).replace("- [ ] a\n", QStringLiteral("- [ ] a\n- [ ] buy milk !%1 +%2\n")
                                                                         .arg(today.addDays(1).toString(Qt::ISODate), t)
                                                                         .toUtf8()));
        key(Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(contents(), before);

        // Pick…: the calendar dialog chooses the date; a hand-typed !date still wins.
        select(1);
        QTimer::singleShot(100, this, [this] {
            auto *dlg = qobject_cast<AddTaskDialog *>(QApplication::activeModalWidget());
            QVERIFY(dlg);
            dlg->lineEdit()->setText(QStringLiteral("dentist"));
            QTimer::singleShot(100, this, [] {
                auto *cal = qobject_cast<DueDateDialog *>(QApplication::activeModalWidget());
                QVERIFY(cal);
                cal->findChild<QCalendarWidget *>()->setSelectedDate(QDate(2030, 1, 2));
                cal->accept();
            });
            dlg->dueButton("pick")->click();
            QVERIFY(dlg->dueButton("pick")->isChecked());
            QCOMPARE(dlg->due(), QDate(2030, 1, 2));
            dlg->accept();
        });
        key(Qt::Key_N);
        QCOMPARE(contents(), QByteArray(before).replace("- [ ] a\n", QStringLiteral("- [ ] a\n- [ ] dentist !2030-01-02 +%1\n")
                                                                         .arg(t)
                                                                         .toUtf8()));
        key(Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(contents(), before);
    }

    void copyAsList()
    {
        select(2); // b, with subtasks b1 and b2 (added above)
        QApplication::clipboard()->clear();
        key(Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(QApplication::clipboard()->text(), QString("- b\n  - b1\n  - b2\n"));
    }

    void saveAndInsertTemplate()
    {
        const QString today = QDate::currentDate().toString(Qt::ISODate);
        const QByteArray before = contents();
        QCOMPARE(before, QStringLiteral("- [ ] a\n- [ ] b\n  - [ ] b1\n  - [ ] b2 +%1\n- [ ] c\n").arg(today).toUtf8());
        auto action = [&](const QString &text) -> QAction * {
            for (QAction *a : m_window->findChildren<QAction *>()) {
                if (a->text() == text)
                    return a;
            }
            return nullptr;
        };

        // Save b and its subtasks as "errands" (the suggested name is "b").
        select(2);
        QTimer::singleShot(100, this, [] {
            auto *dlg = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
            QVERIFY(dlg);
            QCOMPARE(dlg->textValue(), QString("b"));
            dlg->setTextValue(QStringLiteral("errands"));
            dlg->accept();
        });
        QAction *save = action(QStringLiteral("Save as te&mplate…"));
        QVERIFY(save);
        save->trigger();
        QFile tpl(m_dir.path() + "/.templates/errands.md");
        QVERIFY(tpl.open(QIODevice::ReadOnly));
        QCOMPARE(tpl.readAll(), QByteArray("- [ ] b\n  - [ ] b1\n  - [ ] b2\n"));
        QCOMPARE(m_view->model()->rowCount(), 6); // .templates is hidden: no new file group

        // Insert it after a: three new tasks, created today, selected.
        select(1);
        QMenu *menu = nullptr;
        for (QMenu *m : m_window->findChildren<QMenu *>()) {
            if (m->title() == QStringLiteral("&Insert template"))
                menu = m;
        }
        QVERIFY(menu);
        emit menu->aboutToShow();
        QAction *errands = nullptr;
        for (QAction *a : menu->actions()) {
            if (a->text() == QStringLiteral("errands"))
                errands = a;
        }
        QVERIFY(errands);
        errands->trigger();
        QCOMPARE(contents(), QStringLiteral("- [ ] a\n- [ ] b +%1\n  - [ ] b1 +%1\n  - [ ] b2 +%1\n"
                                            "- [ ] b\n  - [ ] b1\n  - [ ] b2 +%1\n- [ ] c\n")
                                 .arg(today)
                                 .toUtf8());
        QCOMPARE(m_view->selectionModel()->selectedRows().size(), 3);
        QCOMPARE(m_view->currentIndex().row(), 2);
        key(Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(contents(), before);
    }

    // Runs after saveAndInsertTemplate, which left the template "errands".
    void templatesTab()
    {
        const QByteArray tasksBefore = contents();
        const QString today = QDate::currentDate().toString(Qt::ISODate);
        auto *tabs = m_window->findChild<QTabWidget *>();
        auto *page = m_window->findChild<TemplatesPage *>();
        QVERIFY(page);
        auto *tv = page->findChild<QTreeView *>();
        QVERIFY(tv);
        QAbstractItemModel *tm = tv->model();
        const QString dir = m_dir.path() + "/.templates/";
        auto read = [](const QString &p) {
            QFile f(p);
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<missing>");
        };
        auto answerNextDialog = [this](std::function<void(QWidget *)> fn) {
            QTimer::singleShot(100, this, [fn] {
                QWidget *w = QApplication::activeModalWidget();
                QVERIFY(w);
                fn(w);
            });
        };
        auto rowOf = [&](const QString &text) {
            for (int r = 0; r < tm->rowCount(); ++r) {
                if (tm->index(r, 0).data().toString() == text)
                    return r;
            }
            return -1;
        };
        auto setCurrent = [&](int row) {
            tv->selectionModel()->setCurrentIndex(tm->index(row, 0),
                                                  QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        };

        // The templates as a task list: a header per template, checkboxes and all.
        key(Qt::Key_3, Qt::ControlModifier);
        QCOMPARE(tabs->currentWidget(), page);
        QTRY_COMPARE(tm->rowCount(), 4);
        QCOMPARE(tm->index(0, 0).data().toString(), QString("errands"));
        QCOMPARE(tm->index(0, 0).data(TaskModelBase::HeaderRole).toBool(), true);
        QCOMPARE(tm->index(2, 0).data().toString(), QString("b1"));
        QCOMPARE(tm->index(2, 0).data(TaskModelBase::StateRole).toInt(), int(TaskState::Todo));

        // The usual task keys edit the template (not the tasks), with their own undo.
        setCurrent(1); // b
        key(Qt::Key_X);
        QCOMPARE(read(dir + "errands.md"), QStringLiteral("- [x] b x%1\n  - [x] b1 x%1\n  - [x] b2 x%1\n").arg(today).toUtf8());
        QCOMPARE(contents(), tasksBefore);
        key(Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(read(dir + "errands.md"), QByteArray("- [ ] b\n  - [ ] b1\n  - [ ] b2\n"));
        QCOMPARE(contents(), tasksBefore);
        setCurrent(1);
        answerNextDialog([](QWidget *w) {
            auto *dlg = qobject_cast<AddTaskDialog *>(w);
            QVERIFY(dlg);
            dlg->lineEdit()->setText("milk");
            dlg->accept();
        });
        key(Qt::Key_N);
        QCOMPARE(read(dir + "errands.md"), QStringLiteral("- [ ] b\n  - [ ] b1\n  - [ ] b2\n- [ ] milk +%1\n").arg(today).toUtf8());

        // New template: created with one task, opened for editing straight away.
        answerNextDialog([](QWidget *w) {
            auto *dlg = qobject_cast<QInputDialog *>(w);
            QVERIFY(dlg);
            dlg->setTextValue("trip");
            dlg->accept();
        });
        page->button("new")->click();
        QTRY_VERIFY(rowOf("trip") >= 0);
        QLineEdit *editor = nullptr; // the inline editor
        QTRY_VERIFY((editor = tv->viewport()->findChild<QLineEdit *>()) && editor->isVisible());
        QCOMPARE(tv->currentIndex().data().toString(), QString("New task"));
        QCOMPARE(editor->text(), QString("New task"));
        editor->setText("Passport");
        QTest::keyClick(editor, Qt::Key_Return);
        QTRY_COMPARE(read(dir + "trip.md"), QByteArray("- [ ] Passport\n"));

        // Rename and delete act on the template of the current row.
        QTRY_VERIFY(rowOf("Passport") >= 0);
        setCurrent(rowOf("Passport"));
        answerNextDialog([](QWidget *w) {
            auto *dlg = qobject_cast<QInputDialog *>(w);
            QVERIFY(dlg);
            QCOMPARE(dlg->textValue(), QString("trip"));
            dlg->setTextValue("holiday");
            dlg->accept();
        });
        page->button("rename")->click();
        QTRY_VERIFY(rowOf("holiday") >= 0);
        QVERIFY(!QFile::exists(dir + "trip.md"));
        setCurrent(rowOf("holiday"));
        answerNextDialog([](QWidget *w) {
            auto *box = qobject_cast<QMessageBox *>(w);
            QVERIFY(box);
            box->button(QMessageBox::Yes)->click();
        });
        page->button("delete")->click();
        QVERIFY(!QFile::exists(dir + "holiday.md"));
        QTRY_COMPARE(rowOf("holiday"), -1);
        QCOMPARE(contents(), tasksBefore);

        // Insert into tasks: back on the Tasks tab, after the selected task there.
        key(Qt::Key_1, Qt::ControlModifier);
        select(5); // c
        key(Qt::Key_3, Qt::ControlModifier);
        setCurrent(rowOf("errands"));
        page->button("insert")->click();
        QCOMPARE(tabs->currentIndex(), 0);
        QCOMPARE(contents(), tasksBefore + QStringLiteral("- [ ] b +%1\n  - [ ] b1 +%1\n  - [ ] b2 +%1\n- [ ] milk +%1\n")
                                               .arg(today)
                                               .toUtf8());
        key(Qt::Key_Z, Qt::ControlModifier); // the Tasks tab's undo
        QCOMPARE(contents(), tasksBefore);
    }

    void directoriesRemembered()
    {
        QCOMPARE(QSettings().value("directories").toStringList(), QStringList({QDir::cleanPath(m_dir.path())}));
    }

    // Rewrites the file, so it runs last.
    void tagClick()
    {
        QSignalSpy changed(m_store, &TaskStore::changed);
        QFile f(file());
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("- [ ] a #home\n- [ ] b #work\n  - [ ] b1 #work/x\n- [ ] c\n");
        f.close();
        QVERIFY(changed.wait(5000));
        QCOMPARE(m_view->model()->rowCount(), 5);
        QCOMPARE(m_view->model()->index(1, TaskListModel::TagsColumn).data().toString(), QString("#home"));

        QLineEdit *filter = nullptr;
        for (QLineEdit *e : m_window->findChildren<QLineEdit *>()) {
            if (e->placeholderText().startsWith("Filter"))
                filter = e;
        }
        QVERIFY(filter);
        auto clickTag = [&](int row) {
            const QModelIndex i = m_view->model()->index(row, TaskListModel::TagsColumn);
            const auto rects = TaskDelegate::tagRects(m_view->visualRect(i), i.data(TaskModelBase::TagsRole).toStringList(),
                                                      m_view->font());
            QVERIFY(!rects.empty());
            QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, rects.front().center());
        };
        clickTag(2); // #work on b: b and its #work/x subtask
        QCOMPARE(filter->text(), QString("#work"));
        QCOMPARE(m_view->model()->rowCount(), 3);
        clickTag(1); // the same tag again clears the filter
        QCOMPARE(filter->text(), QString());
        QCOMPARE(m_view->model()->rowCount(), 5);
    }

    // Creates todo.md, so it runs after the tests that count rows.
    void sidebarAddForDay()
    {
        const QString todo = m_dir.path() + QStringLiteral("/todo.md");
        QVERIFY(!QFile::exists(todo));
        auto *sidebar = m_window->findChild<Sidebar *>();
        QVERIFY(sidebar);
        const QDate day = QDate::currentDate().addDays(3);
        const QString today = QDate::currentDate().toString(Qt::ISODate), due = day.toString(Qt::ISODate);
        sidebar->showDay(day);
        QLineEdit *box = sidebar->addBox();
        QVERIFY(box->placeholderText().contains("todo.md"));

        // Nothing selected in the day list: into todo.md, which is created.
        box->setText("buy milk");
        QTest::keyClick(box, Qt::Key_Return);
        QCOMPARE(box->text(), QString());
        QFile f(todo);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QStringLiteral("- [ ] buy milk !%1 +%2\n").arg(due, today).toUtf8());
        f.close();
        QCOMPARE(sidebar->dayModel()->rowCount(), 2); // day header + the new task

        // A task selected in the day list: the new one goes after it, in its file.
        // A hand-typed due date wins over the selected day.
        sidebar->dayView()->setCurrentIndex(sidebar->dayModel()->index(1, 0));
        QVERIFY(box->placeholderText().contains("todo.md")); // "in todo.md"
        box->setText("pay rent !2030-01-01");
        QTest::keyClick(box, Qt::Key_Return);
        box->setText("bin day");
        sidebar->dayView()->setCurrentIndex(sidebar->dayModel()->index(1, 0));
        QTest::keyClick(box, Qt::Key_Return);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QStringLiteral("- [ ] buy milk !%1 +%2\n- [ ] bin day !%1 +%2\n- [ ] pay rent !2030-01-01 +%2\n")
                                  .arg(due, today)
                                  .toUtf8());

        // Only filter tiles with something to show are clickable: here just "next 7 days".
        QStringList clickable;
        for (QAbstractButton *b : sidebar->findChildren<QAbstractButton *>()) {
            if (b->isCheckable() && b->isEnabled())
                clickable << b->toolTip();
        }
        QCOMPARE(clickable, QStringList({"Filter: due:week (click again to clear)"}));
    }

    // Runs after tagClick: t.md has #home, #work and #work/x.
    void addTagMenu()
    {
        QMenu *menu = nullptr;
        for (QMenu *m : m_window->findChildren<QMenu *>()) {
            if (m->title() == QLatin1String("Add ta&g"))
                menu = m;
        }
        QVERIFY(menu);
        const QByteArray before = contents();
        select(1); // a, already #home
        QMetaObject::invokeMethod(menu, "aboutToShow");
        QStringList labels;
        for (QAction *a : menu->actions())
            labels << a->text();
        QCOMPARE(labels, QStringList({"#home", "#work", "#work/x"}));
        QVERIFY(menu->actions()[0]->isChecked());
        QVERIFY(!menu->actions()[0]->isEnabled());
        QVERIFY(menu->actions()[1]->isEnabled());
        menu->actions()[1]->trigger();
        QCOMPARE(contents(), QByteArray(before).replace("- [ ] a #home\n", "- [ ] a #home #work\n"));
        key(Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(contents(), before);
    }

    // Runs after tagClick: t.md has #home, #work and #work/x.
    void tagCompletionAndCloud()
    {
        auto *sidebar = m_window->findChild<Sidebar *>();
        QLineEdit *box = sidebar->addBox();
        auto *completer = box->findChild<QCompleter *>();
        QVERIFY(completer);
        m_window->activateWindow();
        box->setFocus();
        QTest::keyClicks(box, "buy #wo");
        QVERIFY(completer->popup()->isVisible());
        QCOMPARE(completer->completionCount(), 2); // #work, #work/x
        QTest::keyClick(completer->popup(), Qt::Key_Down);
        QTest::keyClick(completer->popup(), Qt::Key_Return);
        QCOMPARE(box->text(), QString("buy #work ")); // only the word is replaced, nothing submitted
        QTest::keyClicks(box, "#zz");
        QVERIFY(!completer->popup()->isVisible());
        box->clear();

        // The sidebar tag cloud: tags with open tasks and their counts; click filters, again clears.
        TagCloud *cloud = sidebar->tagCloud();
        QStringList items;
        for (const auto &it : cloud->items())
            items << QStringLiteral("%1 %2").arg(it.tag).arg(it.count);
        QCOMPARE(items, QStringList({"home 1", "work 2", "work/x 1"}));
        QLineEdit *filter = nullptr;
        for (QLineEdit *e : m_window->findChildren<QLineEdit *>()) {
            if (e->placeholderText().startsWith("Filter"))
                filter = e;
        }
        const auto rects = cloud->chipRects(cloud->width());
        QTest::mouseClick(cloud, Qt::LeftButton, {}, rects[1].center());
        QCOMPARE(filter->text(), QString("#work"));
        QTest::mouseClick(cloud, Qt::LeftButton, {}, cloud->chipRects(cloud->width())[1].center());
        QCOMPARE(filter->text(), QString());
    }

    // Last: rewrites t.md. A click on a link emits linkActivated and leaves the
    // selection alone; the context menu copies the address.
    void links()
    {
        QSignalSpy changed(m_store, &TaskStore::changed);
        QFile f(file());
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("- [ ] Read [the spec](https://s.io/spec) now\n- [ ] see https://a.io and https://b.io\n");
        f.close();
        QVERIFY(changed.wait(5000));
        auto *view = static_cast<TaskView *>(m_view);
        const QModelIndex row1 = m_view->model()->index(1, 0);
        QCOMPARE(row1.data().toString(), QString("Read the spec now"));
        const auto rects = TaskDelegate::linkRects(m_view->visualRect(row1), row1, m_view->font(), true);
        QCOMPARE(rects.size(), size_t(1));
        QCOMPARE(view->linkAt(rects[0].rect.center()), QString("https://s.io/spec"));

        select(2);
        QSignalSpy opened(view, &TaskView::linkActivated);
        QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, rects[0].rect.center());
        QCOMPARE(opened.size(), 1);
        QCOMPARE(opened.at(0).at(0).toUrl(), QUrl("https://s.io/spec"));
        QCOMPARE(m_view->currentIndex().row(), 2); // selection untouched
        // Clicking the plain text still selects.
        QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, QPoint(rects[0].rect.left() - 20, rects[0].rect.center().y()));
        QCOMPARE(m_view->currentIndex().row(), 1);
        QCOMPARE(opened.size(), 1);

        // Right-click on the link: "Copy link address" copies it.
        QApplication::clipboard()->clear();
        QTimer::singleShot(100, this, [] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            QVERIFY(menu);
            QCOMPARE(menu->actions().at(0)->text(), QString("Open &link"));
            menu->actions().at(1)->trigger();
            menu->close();
        });
        emit m_view->customContextMenuRequested(rects[0].rect.center());
        QCOMPARE(QApplication::clipboard()->text(), QString("https://s.io/spec"));

        // Elsewhere on a task with two links: a submenu of both.
        const QModelIndex row2 = m_view->model()->index(2, 0);
        const QPoint plain = TaskDelegate::layoutFor(m_view->visualRect(row2), row2, true).text.topLeft() + QPoint(3, 8);
        QTimer::singleShot(100, this, [] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            QVERIFY(menu);
            QMenu *sub = menu->actions().at(0)->menu();
            QVERIFY(sub);
            QCOMPARE(sub->actions().size(), 2);
            sub->actions().at(1)->trigger();
            menu->close();
        });
        emit m_view->customContextMenuRequested(plain);
        QCOMPARE(QApplication::clipboard()->text(), QString("https://b.io"));
    }

    // After links(): t.md is "Read the spec now" / "see https://a.io and https://b.io".
    void views()
    {
        QLineEdit *filter = nullptr;
        for (QLineEdit *e : m_window->findChildren<QLineEdit *>()) {
            if (e->placeholderText().startsWith("Filter"))
                filter = e;
        }
        QVERIFY(filter);
        auto *bar = m_window->findChild<ViewBar *>();
        QVERIFY(bar);
        auto viewButtons = [&] {
            QStringList out;
            for (Chip *c : bar->chips())
                out << c->key(); // the query
            return out;
        };
        QCOMPARE(viewButtons(), QStringList());

        // Ctrl+S: a new view from the current filter; it's applied on save.
        filter->setText("spec");
        QTimer::singleShot(100, this, [] {
            auto *dlg = qobject_cast<ViewDialog *>(QApplication::activeModalWidget());
            QVERIFY(dlg);
            QCOMPARE(dlg->queryEdit()->text(), QString("spec"));
            auto *ok = dlg->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
            QVERIFY(!ok->isEnabled()); // no name yet
            dlg->nameEdit()->setText("Reading");
            dlg->queryEdit()->setText("spec|a.io");
            QVERIFY(ok->isEnabled());
            ok->click();
        });
        key(Qt::Key_S, Qt::ControlModifier);
        QCOMPARE(filter->text(), QString("spec|a.io"));
        QCOMPARE(m_view->model()->rowCount(), 3); // header + both tasks (or)
        QCOMPARE(viewButtons(), QStringList({"spec|a.io"}));

        // Ctrl+Alt+1 toggles it: on again clears, then applies.
        key(Qt::Key_1, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(filter->text(), QString());
        key(Qt::Key_1, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(filter->text(), QString("spec|a.io"));
        QVERIFY(bar->chips()[0]->isChecked());

        // Clicking the chip in the tab row clears, again applies; from Templates it goes to Tasks.
        QTest::mouseClick(bar->chips()[0], Qt::LeftButton);
        QCOMPARE(filter->text(), QString());
        QVERIFY(!bar->chips()[0]->isChecked());
        auto *tabs = m_window->findChild<QTabWidget *>();
        tabs->setCurrentIndex(2);
        QTest::mouseClick(bar->chips()[0], Qt::LeftButton);
        QCOMPARE(filter->text(), QString("spec|a.io"));
        QCOMPARE(tabs->currentIndex(), 0);
        QVERIFY(bar->chips()[0]->isChecked());

        // A second view can't reuse the name.
        QTimer::singleShot(100, this, [] {
            auto *dlg = qobject_cast<ViewDialog *>(QApplication::activeModalWidget());
            QVERIFY(dlg);
            dlg->nameEdit()->setText("reading");
            QVERIFY(!dlg->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->isEnabled());
            dlg->reject();
        });
        key(Qt::Key_S, Qt::ControlModifier);

        // Edit the active view (Views menu): the new filter is applied.
        QAction *edit = nullptr, *del = nullptr;
        for (QAction *a : m_window->findChildren<QAction *>()) {
            if (a->text().startsWith("&Edit view"))
                edit = a;
            if (a->text().startsWith("&Delete view"))
                del = a;
        }
        QVERIFY(edit && del);
        QTimer::singleShot(100, this, [] {
            auto *dlg = qobject_cast<ViewDialog *>(QApplication::activeModalWidget());
            QVERIFY(dlg);
            QCOMPARE(dlg->nameEdit()->text(), QString("Reading"));
            dlg->queryEdit()->setText("b.io");
            dlg->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        edit->trigger();
        QCOMPARE(filter->text(), QString("b.io"));
        QCOMPARE(m_view->model()->rowCount(), 2);

        // Saved in the settings.
        ViewList reloaded;
        QCOMPARE(reloaded.views().size(), size_t(1));
        QCOMPARE(reloaded.views()[0].name, QString("Reading"));
        QCOMPARE(reloaded.views()[0].query, QString("b.io"));

        // Delete (confirmed): gone, and the filter it had applied is cleared.
        QTimer::singleShot(100, this, [] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            QVERIFY(box);
            box->button(QMessageBox::Yes)->click();
        });
        del->trigger();
        QCOMPARE(filter->text(), QString());
        QCOMPARE(viewButtons(), QStringList());
        QCOMPARE(ViewList().views().size(), size_t(0));
    }

    // Choose ▾ (Ctrl+Shift+F): chips edit the filter box live, and the box's text
    // sets the chips when it opens. The view dialog has the same builder.
    void filterBuilder()
    {
        QLineEdit *filter = nullptr;
        for (QLineEdit *e : m_window->findChildren<QLineEdit *>()) {
            if (e->placeholderText().startsWith("Filter"))
                filter = e;
        }
        QVERIFY(filter);
        auto chip = [](QueryBuilder *b, const QString &text) -> Chip * {
            for (Chip *c : b->chips()) {
                if (c->text() == text)
                    return c;
            }
            return nullptr;
        };
        filter->setText("~<=15m spec");
        key(Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
        auto *b = m_window->findChild<QueryBuilder *>();
        QVERIFY(b && b->isVisible());
        QVERIFY(chip(b, "≤ 15m")->isChecked());
        QTest::mouseClick(chip(b, "In progress"), Qt::LeftButton);
        QCOMPARE(filter->text(), QString("is:doing ~<=15m spec")); // words kept, chips normalised first
        QTest::mouseClick(chip(b, "To do"), Qt::LeftButton);
        QCOMPARE(filter->text(), QString("is:doing|is:todo ~<=15m spec"));
        QTest::mouseClick(chip(b, "≤ 15m"), Qt::LeftButton); // estimate is single choice: off again
        QCOMPARE(filter->text(), QString("is:doing|is:todo spec"));
        QTest::mouseClick(chip(b, "> 1h"), Qt::LeftButton);
        QTest::mouseClick(chip(b, "None"), Qt::LeftButton); // replaces > 1h
        QCOMPARE(filter->text(), QString("is:doing|is:todo ~none spec"));
        b->window()->close();
        filter->clear();

        // In the view dialog: clicks write the filter field, typing there sets the chips.
        QTimer::singleShot(100, this, [chip] {
            auto *dlg = qobject_cast<ViewDialog *>(QApplication::activeModalWidget());
            QVERIFY(dlg && dlg->builder());
            QTest::mouseClick(chip(dlg->builder(), "Overdue"), Qt::LeftButton);
            QTest::mouseClick(chip(dlg->builder(), "Next 7 days"), Qt::LeftButton);
            QCOMPARE(dlg->queryEdit()->text(), QString("due:overdue|due:week"));
            dlg->queryEdit()->setFocus();
            QTest::keyClicks(dlg->queryEdit(), " is:doing");
            QVERIFY(chip(dlg->builder(), "In progress")->isChecked());
            QTest::mouseClick(chip(dlg->builder(), "Overdue"), Qt::LeftButton);
            QCOMPARE(dlg->queryEdit()->text(), QString("due:week is:doing"));
            dlg->reject();
        });
        key(Qt::Key_S, Qt::ControlModifier);
    }
};

QTEST_MAIN(TestWindow)
#include "test_window.moc"

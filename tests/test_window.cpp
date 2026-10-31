// Drives the real window through its keyboard shortcuts.

#include "mainwindow.h"
#include "store.h"
#include "dialogs.h"
#include "sidebar.h"
#include "taskview.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSpinBox>
#include <QDir>
#include <QFile>
#include <QInputDialog>
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
            auto *dlg = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
            QVERIFY(dlg);
            dlg->setTextValue(QStringLiteral("b2"));
            dlg->accept();
        });
        key(Qt::Key_N, Qt::ShiftModifier);
        QCOMPARE(contents(), QByteArray("- [ ] a\n- [ ] b\n  - [ ] b1\n  - [ ] b2\n- [ ] c\n"));
        // The new task is selected.
        QCOMPARE(m_view->currentIndex().row(), 4);
    }

    void directoriesRemembered()
    {
        QCOMPARE(QSettings().value("directories").toStringList(), QStringList({QDir::cleanPath(m_dir.path())}));
    }
};

QTEST_MAIN(TestWindow)
#include "test_window.moc"

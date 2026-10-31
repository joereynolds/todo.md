#include "mainwindow.h"

#include "dialogs.h"
#include "editor.h"
#include "models.h"
#include "parser.h"
#include "sidebar.h"
#include "store.h"
#include "taskview.h"
#include "theme.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

namespace {

const QString kDirsKey = QStringLiteral("directories");

// Forwards Escape/Down from the filter box to the current view.
class FilterKeys : public QObject
{
public:
    FilterKeys(QLineEdit *edit, std::function<QTreeView *()> view)
        : QObject(edit), m_edit(edit), m_view(std::move(view)) {}

protected:
    bool eventFilter(QObject *, QEvent *e) override
    {
        if (e->type() != QEvent::KeyPress)
            return false;
        auto *k = static_cast<QKeyEvent *>(e);
        if (k->key() == Qt::Key_Escape) {
            if (!m_edit->text().isEmpty())
                m_edit->clear();
            else
                m_view()->setFocus();
            return true;
        }
        if (k->key() == Qt::Key_Down || k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) {
            m_view()->setFocus();
            return true;
        }
        return false;
    }

private:
    QLineEdit *m_edit;
    std::function<QTreeView *()> m_view;
};

Task *lastDescendant(Task *t)
{
    while (!t->children.empty())
        t = t->children.back();
    return t;
}

// Indentation step used for nesting in this file, guessed from existing subtasks.
QString indentUnit(const TaskFile *f)
{
    for (const Task &t : f->tasks) {
        if (!t.parent)
            continue;
        const QString child = parser::leadingWhitespace(t.prefix);
        const QString parent = parser::leadingWhitespace(t.parent->prefix);
        if (child.startsWith(parent) && child.size() > parent.size())
            return child.mid(parent.size());
    }
    return QStringLiteral("  ");
}

struct EtaPreset {
    const char *label;
    std::vector<TaskFilter::EtaCond> conds;
};

const std::vector<EtaPreset> &etaPresets()
{
    using C = TaskFilter::EtaCond;
    static const std::vector<EtaPreset> presets = {
        {QT_TRANSLATE_NOOP("MainWindow", "Any ETA"), {}},
        {QT_TRANSLATE_NOOP("MainWindow", "≤ 2m (do it now)"), {{C::Le, 2}}},
        {QT_TRANSLATE_NOOP("MainWindow", "≤ 15m (quick)"), {{C::Le, 15}}},
        {QT_TRANSLATE_NOOP("MainWindow", "≤ 1h"), {{C::Le, 60}}},
        {QT_TRANSLATE_NOOP("MainWindow", "1h – 1d"), {{C::Gt, 60}, {C::Le, 60 * 24}}},
        {QT_TRANSLATE_NOOP("MainWindow", "> 1d (involved)"), {{C::Gt, 60 * 24}}},
        {QT_TRANSLATE_NOOP("MainWindow", "Has ETA"), {{C::Any, 0}}},
        {QT_TRANSLATE_NOOP("MainWindow", "No ETA"), {{C::None, 0}}},
    };
    return presets;
}

} // namespace

MainWindow::MainWindow(TaskStore *store, QWidget *parent) : QMainWindow(parent), m_store(store)
{
    // Connected before the models exist, so this runs while their rows are
    // still valid: remember selection and scroll to restore after the change.
    connect(store, &TaskStore::aboutToChange, this, [this] {
        m_saved[0] = saveView(m_list);
        m_saved[1] = saveView(m_agenda);
    });

    m_listModel = new TaskListModel(store, this);
    m_agendaModel = new AgendaModel(store, this);
    m_list.model = m_listModel;
    m_agenda.model = m_agendaModel;
    m_sidebar = new Sidebar(store, this);

    buildActions();
    buildUi();

    connect(store, &TaskStore::changed, this, [this] {
        const bool forced = m_forceSelect.has_value();
        if (forced) {
            SavedView &s = m_saved[&current() == &m_agenda ? 1 : 0];
            s.selection = *m_forceSelect;
            s.current = m_forceSelect->empty() ? std::nullopt : std::optional<LineKey>(m_forceSelect->front());
            m_forceSelect.reset();
        }
        restoreView(m_list, m_saved[0], forced);
        restoreView(m_agenda, m_saved[1], forced);
        updateStatus();
    });

    for (TaskModelBase *m : {static_cast<TaskModelBase *>(m_listModel), static_cast<TaskModelBase *>(m_agendaModel)}) {
        connect(m, &TaskModelBase::contentEdited, this,
                [this](const QString &path, int line, const QString &before, const QString &after) {
                    runEdits({{path, line, {before}, {after}}}, tr("Edit task"));
                }, Qt::QueuedConnection);
    }

    connect(store, &TaskStore::undoStateChanged, this, &MainWindow::updateUndoActions);
    connect(store, &TaskStore::message, this, [this](const QString &m) { statusBar()->showMessage(m, 6000); });
    connect(store, &TaskStore::loadStarted, this, [this](const QStringList &roots) {
        updateTitle();
        if (!roots.isEmpty())
            m_status->setText(tr("Loading %n director(y|ies) …", nullptr, int(roots.size())));
    });
    connect(store, &TaskStore::loadFinished, this, [this](int, int, qint64 ms) {
        m_lastLoadMs = ms;
        updateStatus();
    });

    m_filterTimer.setSingleShot(true);
    m_filterTimer.setInterval(120);
    connect(m_filter, &QLineEdit::textChanged, &m_filterTimer, qOverload<>(&QTimer::start));
    connect(&m_filterTimer, &QTimer::timeout, this, [this] {
        const QString text = m_filter->text();
        forEachFilter([&](TaskFilter &f) { f.setText(text); });
    });

    // Keeps "today"/"overdue" in the agenda correct across midnight.
    m_clock.setInterval(60 * 1000);
    connect(&m_clock, &QTimer::timeout, m_agendaModel, &AgendaModel::refreshIfDateChanged);
    m_clock.start();

    QSettings s;
    m_listModel->setCollapsedKeys(s.value(QStringLiteral("view/collapsed")).toStringList());
    m_agendaModel->setCollapsedKeys(s.value(QStringLiteral("view/agendaCollapsed")).toStringList());
    m_splitter->restoreState(s.value(QStringLiteral("view/splitter")).toByteArray());
    m_toggleSidebar->setChecked(s.value(QStringLiteral("view/sidebar"), true).toBool());
    m_sidebar->setVisible(m_toggleSidebar->isChecked());
    restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray());
    m_tabs->setCurrentIndex(s.value(QStringLiteral("window/tab"), 0).toInt());
    updateUndoActions();
    updateTitle();
    updateStatus();
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent *event)
{
    QSettings s;
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    s.setValue(QStringLiteral("window/tab"), m_tabs->currentIndex());
    s.setValue(QStringLiteral("view/collapsed"), m_listModel->collapsedKeys());
    s.setValue(QStringLiteral("view/agendaCollapsed"), m_agendaModel->collapsedKeys());
    s.setValue(QStringLiteral("view/splitter"), m_splitter->saveState());
    QMainWindow::closeEvent(event);
}

void MainWindow::buildActions()
{
    auto make = [this](const QString &text, const QKeySequence &key, auto slot) {
        auto *a = new QAction(text, this);
        if (!key.isEmpty())
            a->setShortcut(key);
        connect(a, &QAction::triggered, this, slot);
        addAction(a); // shortcuts work even when the menu bar is hidden
        return a;
    };
    m_addDir = make(tr("&Add directory…"), QKeySequence::Open, [this] { chooseDirectory(); });
    m_reload = make(tr("&Reload"), QKeySequence(Qt::Key_F5), [this] { m_store->reload(); });
    m_quit = make(tr("&Quit"), QKeySequence::Quit, [this] { close(); });

    m_undo = make(tr("&Undo"), QKeySequence::Undo, [this] { m_store->undo(); });
    m_redo = make(tr("&Redo"), QKeySequence::Redo, [this] { m_store->redo(); });

    m_toggle = make(tr("Toggle &done"), QKeySequence(Qt::Key_Space), [this] { toggleDone(); });
    m_todo = make(tr("Mark &todo"), QKeySequence(Qt::Key_T), [this] { setState(TaskState::Todo); });
    m_doing = make(tr("Mark in &progress"), QKeySequence(Qt::Key_P), [this] { setState(TaskState::Doing); });
    m_done = make(tr("Mark do&ne"), QKeySequence(Qt::Key_X), [this] { setState(TaskState::Done); });
    m_cancel = make(tr("Mark &cancelled"), QKeySequence(Qt::Key_C), [this] { setState(TaskState::Cancelled); });

    m_due = make(tr("Set d&ue date…"), QKeySequence(Qt::Key_D), [this] { setDue(); });
    m_clearDue = make(tr("C&lear due date"), QKeySequence(Qt::SHIFT | Qt::Key_D), [this] { clearDue(); });
    m_eta = make(tr("Set ET&A…"), QKeySequence(Qt::Key_AsciiTilde), [this] { setEta(); });
    // '~' arrives with Shift on most layouts; accept both forms.
    m_eta->setShortcuts({QKeySequence(Qt::Key_AsciiTilde), QKeySequence(Qt::SHIFT | Qt::Key_AsciiTilde)});
    m_edit = make(tr("&Edit text"), QKeySequence(Qt::Key_F2), [this] { editText(); });
    m_replace = make(tr("&Replace in selection…"), QKeySequence(Qt::CTRL | Qt::Key_H), [this] { replaceText(); });
    m_add = make(tr("&Add task below…"), QKeySequence(Qt::Key_N), [this] { addTask(false); });
    m_addSub = make(tr("Add &subtask…"), QKeySequence(Qt::SHIFT | Qt::Key_N), [this] { addTask(true); });
    m_delete = make(tr("De&lete…"), QKeySequence::Delete, [this] { deleteTasks(); });
    m_openEditor = make(tr("Open in edi&tor ($EDITOR)"), QKeySequence(Qt::Key_E), [this] { openInEditor(); });
    m_copyList = make(tr("&Copy as list"), QKeySequence::Copy, [this] { copyAsList(); });
    m_copyList->setToolTip(tr("Copy the selected tasks and their subtasks as a bulleted list"));

    QSettings s;
    static const char *names[] = {"Todo", "Doing", "Done", "Cancelled"};
    static const bool defaults[] = {true, true, false, false};
    for (int i = 0; i < 4; ++i) {
        const auto state = TaskState(i);
        const QString key = QStringLiteral("filter/show%1").arg(QLatin1String(names[i]));
        m_showState[i] = make(tr(names[i]), QKeySequence(Qt::ALT | (Qt::Key_1 + i)), [this, state, key](bool on) {
            QSettings().setValue(key, on);
            forEachFilter([&](TaskFilter &f) { f.setStateVisible(state, on); });
            m_sidebar->setStateVisible(state, on);
        });
        m_showState[i]->setCheckable(true);
        const bool on = s.value(key, defaults[i]).toBool();
        m_showState[i]->setChecked(on);
        m_sidebar->setStateVisible(state, on);
        m_listModel->filter().setStateVisible(state, on);
        m_agendaModel->filter().setStateVisible(state, on);
        m_showState[i]->setToolTip(tr("Show %1 tasks (Alt+%2)").arg(tr(names[i]).toLower()).arg(i + 1));
    }
    m_listModel->refilter();
    m_agendaModel->refilter();

    m_focusFilter = make(tr("&Filter"), QKeySequence::Find, [this] { m_filter->setFocus(); m_filter->selectAll(); });
    m_focusFilter2 = make(tr("Filter"), QKeySequence(Qt::Key_Slash), [this] { m_filter->setFocus(); m_filter->selectAll(); });
    m_expandAll = make(tr("E&xpand all"), QKeySequence(Qt::CTRL | Qt::Key_Period), [this] {
        Pane &p = current();
        const SavedView v = saveView(p);
        static_cast<FlatTaskModel *>(p.model)->expandAll();
        restoreView(p, v, true);
    });
    m_collapseAll = make(tr("&Collapse all groups"), QKeySequence(Qt::CTRL | Qt::Key_Comma), [this] {
        static_cast<FlatTaskModel *>(current().model)->collapseAll();
    });
    m_toggleSidebar = make(tr("&Sidebar"), QKeySequence(Qt::CTRL | Qt::Key_B), [this](bool on) {
        m_sidebar->setVisible(on);
        if (on && m_splitter->sizes().value(0) == 0)
            m_splitter->setSizes({300, std::max(400, width() - 300)});
        QSettings().setValue(QStringLiteral("view/sidebar"), on);
    });
    m_toggleSidebar->setCheckable(true);
    m_toggleSidebar->setToolTip(tr("Show or hide the sidebar (Ctrl+B)"));
    m_tasksTab = make(tr("&Tasks"), QKeySequence(Qt::CTRL | Qt::Key_1), [this] { m_tabs->setCurrentIndex(0); });
    m_agendaTab = make(tr("&Agenda"), QKeySequence(Qt::CTRL | Qt::Key_2), [this] { m_tabs->setCurrentIndex(1); });

    m_themeGroup = new QActionGroup(this);
    const QString current = s.value(QStringLiteral("view/theme"), QStringLiteral("light")).toString();
    for (const auto &[name, label] : {std::pair{QStringLiteral("light"), tr("&Light")},
                                      std::pair{QStringLiteral("soft"), tr("S&oft (grey light)")},
                                      std::pair{QStringLiteral("dark"), tr("&Dark")},
                                      std::pair{QStringLiteral("system"), tr("&System")}}) {
        QAction *a = m_themeGroup->addAction(label);
        a->setCheckable(true);
        a->setChecked(name == current);
        connect(a, &QAction::triggered, this, [this, n = name] { applyTheme(n); });
    }
}

QTreeView *MainWindow::makeView(bool tree)
{
    auto *v = new TaskView(tree, this);
    connect(v, &TaskView::checkClicked, this, [this, v](const QModelIndex &index) {
        const Pane &p = v == m_agenda.view ? m_agenda : m_list;
        if (Task *t = p.model->taskAt(index))
            toggleTask(t);
    });
    connect(v, &TaskView::tagClicked, this, [this](const QString &tag) {
        // Show everything with the tag; clicking the same tag again clears it.
        const QString term = u'#' + tag;
        setFilterText(m_filter->text().trimmed().compare(term, Qt::CaseInsensitive) == 0 ? QString() : term);
    });
    v->setUniformRowHeights(true); // large win for big lists
    v->setSelectionMode(QAbstractItemView::ExtendedSelection); // click-drag, shift, ctrl
    v->setSelectionBehavior(QAbstractItemView::SelectRows);
    v->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::DoubleClicked);
    v->setAllColumnsShowFocus(true);
    v->setAlternatingRowColors(true);
    v->setContextMenuPolicy(Qt::CustomContextMenu);
    v->setRootIsDecorated(false);
    v->setItemsExpandable(false);
    v->setExpandsOnDoubleClick(false);
    connect(v, &QWidget::customContextMenuRequested, this, &MainWindow::showContextMenu);
    return v;
}

void MainWindow::buildUi()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(m_addDir);
    m_removeDirMenu = file->addMenu(tr("Re&move directory"));
    connect(m_removeDirMenu, &QMenu::aboutToShow, this, [this] {
        m_removeDirMenu->clear();
        for (const QString &d : m_store->roots())
            m_removeDirMenu->addAction(QDir::toNativeSeparators(d), this, [this, d] { removeDirectory(d); });
        if (m_store->roots().isEmpty())
            m_removeDirMenu->addAction(tr("(none)"))->setEnabled(false);
    });
    file->addAction(m_reload);
    file->addSeparator();
    file->addAction(m_quit);

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    edit->addActions({m_undo, m_redo});
    edit->addSeparator();
    edit->addActions({m_toggle, m_todo, m_doing, m_done, m_cancel});
    edit->addSeparator();
    edit->addActions({m_due, m_clearDue, m_eta});
    edit->addSeparator();
    edit->addActions({m_edit, m_replace, m_add, m_addSub, m_delete});
    edit->addSeparator();
    edit->addActions({m_copyList, m_openEditor});

    QMenu *view = menuBar()->addMenu(tr("&View"));
    view->addActions({m_toggleSidebar, m_tasksTab, m_agendaTab});
    view->addSeparator();
    view->addAction(m_focusFilter);
    for (QAction *a : m_showState)
        view->addAction(a);
    view->addSeparator();
    view->addActions({m_expandAll, m_collapseAll});
    view->addSeparator();
    QMenu *themes = view->addMenu(tr("T&heme"));
    themes->addActions(m_themeGroup->actions());

    QToolBar *bar = addToolBar(tr("Main"));
    bar->setObjectName(QStringLiteral("main"));
    bar->setMovable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    bar->addActions({m_toggleSidebar, m_reload});
    bar->addSeparator();
    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("Filter: words, paths, !2026-11, ~<=15m …  (Ctrl+F or /)"));
    m_filter->setToolTip(tr("Space separated terms, all must match.\n"
                            "ETA: ~<=15m  ~<1h  ~>2h  ~>=1d  ~30m  ~none  ~any\n"
                            "Due: due:overdue  due:today  due:week  due:none  due:any\n"
                            "State: is:todo  is:doing  is:done  is:cancelled  is:open\n"
                            "Tags: #work (also matches #work/meeting); click a tag to filter by it"));
    m_filter->setClearButtonEnabled(true);
    m_filter->setMinimumWidth(220);
    m_filter->installEventFilter(new FilterKeys(m_filter, [this] { return current().view; }));
    bar->addWidget(m_filter);
    m_etaFilter = new QComboBox(this);
    for (const EtaPreset &p : etaPresets())
        m_etaFilter->addItem(tr(p.label));
    m_etaFilter->setToolTip(tr("Filter by ETA (~2m, ~1h, ~3d …)"));
    m_etaFilter->setFocusPolicy(Qt::NoFocus);
    connect(m_etaFilter, &QComboBox::currentIndexChanged, this, [this](int i) {
        const auto &conds = etaPresets()[size_t(std::max(i, 0))].conds;
        forEachFilter([&](TaskFilter &f) { f.setEtaPreset(conds); });
    });
    bar->addWidget(m_etaFilter);
    bar->addSeparator();
    for (QAction *a : m_showState)
        bar->addAction(a);

    m_list.view = makeView(true);
    m_list.view->setModel(m_listModel);
    const int dateWidth = fontMetrics().horizontalAdvance(QStringLiteral("0000-00-00")) + 24;
    const int etaWidth = fontMetrics().horizontalAdvance(QStringLiteral("ETA 999m")) + 12;
    const int tagsWidth = fontMetrics().horizontalAdvance(QStringLiteral("#errands #someday")) + 32;
    QHeaderView *h = m_list.view->header();
    h->setStretchLastSection(false);
    h->setSectionResizeMode(TaskListModel::TextColumn, QHeaderView::Stretch);
    h->setSectionResizeMode(TaskListModel::TagsColumn, QHeaderView::Interactive);
    h->resizeSection(TaskListModel::TagsColumn, tagsWidth);
    h->setSectionResizeMode(TaskListModel::EtaColumn, QHeaderView::Fixed);
    h->setSectionResizeMode(TaskListModel::DueColumn, QHeaderView::Fixed);
    h->resizeSection(TaskListModel::EtaColumn, etaWidth);
    h->resizeSection(TaskListModel::DueColumn, dateWidth);

    m_agenda.view = makeView(true);
    m_agenda.view->setModel(m_agendaModel);
    static_cast<TaskView *>(m_agenda.view)->setSpanHeaders(true);
    h = m_agenda.view->header();
    h->setStretchLastSection(false);
    h->setSectionResizeMode(AgendaModel::TextColumn, QHeaderView::Stretch);
    h->setSectionResizeMode(AgendaModel::TagsColumn, QHeaderView::Interactive);
    h->resizeSection(AgendaModel::TagsColumn, tagsWidth);
    h->setSectionResizeMode(AgendaModel::EtaColumn, QHeaderView::Fixed);
    h->resizeSection(AgendaModel::EtaColumn, etaWidth);

    auto *agendaPage = new QWidget(this);
    auto *agendaLayout = new QVBoxLayout(agendaPage);
    agendaLayout->setContentsMargins(0, 0, 0, 0);
    agendaLayout->setSpacing(2);
    auto *agendaBar = new QHBoxLayout;
    agendaBar->setContentsMargins(6, 4, 6, 0);
    m_emptyDays = new QCheckBox(tr("Show empty days"), agendaPage);
    m_emptyDays->setChecked(QSettings().value(QStringLiteral("agenda/emptyDays"), false).toBool());
    m_agendaModel->setShowEmptyDays(m_emptyDays->isChecked());
    connect(m_emptyDays, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue(QStringLiteral("agenda/emptyDays"), on);
        m_agendaModel->setShowEmptyDays(on);
    });
    auto *hint = new QLabel(tr("Open tasks with a !due date, by day"), agendaPage);
    hint->setEnabled(false);
    agendaBar->addWidget(m_emptyDays);
    agendaBar->addStretch();
    agendaBar->addWidget(hint);
    agendaLayout->addLayout(agendaBar);
    agendaLayout->addWidget(m_agenda.view);

    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    m_tabs->addTab(m_list.view, tr("Tasks"));
    m_tabs->addTab(agendaPage, tr("Agenda"));
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] { current().view->setFocus(); });

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->addWidget(m_sidebar);
    m_splitter->addWidget(m_tabs);
    m_splitter->setCollapsible(0, true);
    m_splitter->setCollapsible(1, false);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({300, 700});
    connect(m_splitter, &QSplitter::splitterMoved, this, [this] {
        // Dragged shut counts as hidden.
        const bool shown = m_splitter->sizes().value(0) > 0;
        if (shown != m_toggleSidebar->isChecked()) {
            m_toggleSidebar->setChecked(shown);
            QSettings().setValue(QStringLiteral("view/sidebar"), shown);
        }
    });
    setCentralWidget(m_splitter);

    connect(m_sidebar, &Sidebar::filterRequested, this, &MainWindow::setFilterText);
    connect(m_sidebar, &Sidebar::toggleRequested, this, &MainWindow::toggleTask);
    connect(m_sidebar, &Sidebar::addRequested, this, [this](const QString &text, const QDate &day, Task *after) {
        if (after) {
            insertTask(text, after, false, day, false);
            return;
        }
        const QString path = defaultTaskFile();
        if (path.isEmpty()) {
            statusBar()->showMessage(tr("Add a directory first (Ctrl+O)"), 3000);
            return;
        }
        appendTask(text, path, day, false);
    });
    connect(m_sidebar, &Sidebar::taskActivated, this, [this](Task *t) {
        m_tabs->setCurrentIndex(0);
        const QModelIndex i = m_listModel->reveal(t);
        if (!i.isValid()) {
            statusBar()->showMessage(tr("That task is hidden by the current filter"), 4000);
            return;
        }
        restoreView(m_list, {{{t->file->path, t->line}}, LineKey{t->file->path, t->line}, 0}, true);
        m_list.view->setFocus();
    });

    m_status = new QLabel(this);
    statusBar()->addPermanentWidget(m_status);
    resize(1000, 750);
}

MainWindow::Pane &MainWindow::current()
{
    return m_tabs && m_tabs->currentIndex() == 1 ? m_agenda : m_list;
}

const MainWindow::Pane &MainWindow::current() const
{
    return m_tabs && m_tabs->currentIndex() == 1 ? m_agenda : m_list;
}

void MainWindow::showTab(int index)
{
    m_tabs->setCurrentIndex(index);
}

// ---------------------------------------------------------------- directories

void MainWindow::setDirectories(const QStringList &dirs, bool persist)
{
    QStringList normalized;
    for (const QString &d : dirs) {
        const QString n = TaskStore::normalizeRoot(d);
        if (!normalized.contains(n))
            normalized << n;
    }
    if (persist)
        QSettings().setValue(kDirsKey, normalized);
    m_store->setRoots(normalized);
}

void MainWindow::addDirectory(const QString &dir)
{
    QStringList dirs = m_store->roots();
    dirs << dir;
    setDirectories(dirs);
}

void MainWindow::removeDirectory(const QString &dir)
{
    QStringList dirs = m_store->roots();
    dirs.removeAll(dir);
    setDirectories(dirs);
}

void MainWindow::chooseDirectory()
{
    const QString start = m_store->roots().isEmpty() ? QDir::homePath() : m_store->roots().constLast();
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Add task directory"), start);
    if (!dir.isEmpty())
        addDirectory(dir);
}

// ---------------------------------------------------------------- filtering

void MainWindow::forEachFilter(const std::function<void(TaskFilter &)> &fn)
{
    fn(m_listModel->filter());
    fn(m_agendaModel->filter());
    applyFilter();
}

void MainWindow::setFilterText(const QString &text)
{
    m_filter->setText(text);
    m_filterTimer.stop();
    forEachFilter([&](TaskFilter &f) { f.setText(text); });
}

void MainWindow::applyFilter()
{
    const SavedView list = saveView(m_list), agenda = saveView(m_agenda);
    m_listModel->refilter();
    m_agendaModel->refilter();
    m_sidebar->setActiveFilter(m_filter->text());
    restoreView(m_list, list, true);
    restoreView(m_agenda, agenda, true);
}

// ---------------------------------------------------------------- selection

std::vector<Task *> MainWindow::selectedTasks(bool withSubtasks) const
{
    const Pane &p = current();
    QModelIndexList rows = p.view->selectionModel()->selectedRows(0);
    if (rows.isEmpty() && p.view->currentIndex().isValid())
        rows << p.view->currentIndex().siblingAtColumn(0);
    std::sort(rows.begin(), rows.end()); // visual order, not click order

    std::vector<Task *> out;
    QSet<const Task *> seen;
    std::function<void(Task *)> add = [&](Task *t) {
        if (seen.contains(t))
            return;
        seen.insert(t);
        out.push_back(t);
        if (withSubtasks) {
            for (Task *c : t->children)
                add(c);
        }
    };
    for (const QModelIndex &pi : rows) {
        if (Task *t = p.model->taskAt(pi))
            add(t);
    }
    return out;
}

void MainWindow::copyAsList()
{
    // Selected tasks with their subtasks, minus subtasks the filter hides
    // (e.g. done ones), so what's copied matches what's on screen.
    const TaskFilter &filter = static_cast<const FlatTaskModel *>(current().model)->filter();
    std::function<bool(const Task *)> shown = [&](const Task *t) {
        return filter.matches(*t) || std::any_of(t->children.begin(), t->children.end(), shown);
    };
    const auto selected = selectedTasks(false);
    const QSet<const Task *> picked(selected.begin(), selected.end());
    std::vector<Task *> tasks;
    for (Task *t : selectedTasks(true)) {
        if (picked.contains(t) || shown(t))
            tasks.push_back(t);
    }
    if (tasks.empty()) {
        statusBar()->showMessage(tr("Select tasks to copy"), 3000);
        return;
    }
    QApplication::clipboard()->setText(tasksAsBulletList(tasks));
    statusBar()->showMessage(tr("Copied %n task(s)", nullptr, int(tasks.size())), 3000);
}

TaskFile *MainWindow::selectedFile() const
{
    const Pane &p = current();
    return p.model->fileAt(p.view->currentIndex());
}

MainWindow::SavedView MainWindow::saveView(const Pane &p) const
{
    SavedView s;
    if (!p.view)
        return s;
    for (const QModelIndex &pi : p.view->selectionModel()->selectedRows(0)) {
        if (const Task *t = p.model->taskAt(pi))
            s.selection.push_back({t->file->path, t->line});
    }
    if (const Task *t = p.model->taskAt(p.view->currentIndex()))
        s.current = LineKey{t->file->path, t->line};
    s.scroll = p.view->verticalScrollBar()->value();
    return s;
}

void MainWindow::restoreView(Pane &p, const SavedView &s, bool scrollToCurrent)
{
    auto viewIndex = [&](const LineKey &k) {
        const Task *t = m_store->taskAt(k.path, k.line);
        return t ? p.model->indexOf(t) : QModelIndex();
    };
    QItemSelection sel;
    for (const LineKey &k : s.selection) {
        const QModelIndex i = viewIndex(k);
        if (i.isValid())
            sel.select(i, i);
    }
    QItemSelectionModel *sm = p.view->selectionModel();
    const QModelIndex cur = s.current ? viewIndex(*s.current) : QModelIndex();
    if (cur.isValid())
        sm->setCurrentIndex(cur, QItemSelectionModel::NoUpdate);
    sm->select(sel, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    if (scrollToCurrent && cur.isValid())
        p.view->scrollTo(cur);
    else
        p.view->verticalScrollBar()->setValue(s.scroll);
}

void MainWindow::runEdits(const std::vector<Edit> &edits, const QString &label,
                          std::optional<std::vector<LineKey>> select)
{
    if (edits.empty()) {
        statusBar()->showMessage(tr("%1: nothing to change").arg(label), 3000);
        return;
    }
    m_forceSelect = std::move(select);
    const int n = m_store->apply(edits, label);
    m_forceSelect.reset();
    if (n > 0 && n == int(edits.size()))
        statusBar()->showMessage(tr("%1: %n line(s) changed", nullptr, n).arg(label), 4000);
}

// ---------------------------------------------------------------- actions
// Bulk actions always include the subtasks of every selected task.

void MainWindow::setState(TaskState state)
{
    static const char *labels[] = {"Mark todo", "Mark in progress", "Mark done", "Mark cancelled"};
    const QDate today = QDate::currentDate();
    std::vector<Edit> edits;
    for (const Task *t : selectedTasks(true)) {
        if (t->state == state)
            continue;
        const QString raw = t->raw();
        edits.push_back({t->file->path, t->line, {raw}, {parser::setState(raw, state, today)}});
    }
    runEdits(edits, tr(labels[int(state)]));
}

void MainWindow::toggleDone()
{
    const auto tasks = selectedTasks(false);
    const bool allDone = !tasks.empty() && std::all_of(tasks.begin(), tasks.end(), [](const Task *t) {
        return t->state == TaskState::Done;
    });
    setState(allDone ? TaskState::Todo : TaskState::Done);
}

void MainWindow::toggleTask(Task *task)
{
    // Checkbox click: done <-> todo for this task and all its subtasks.
    const TaskState state = task->state == TaskState::Done ? TaskState::Todo : TaskState::Done;
    const QDate today = QDate::currentDate();
    std::vector<Edit> edits;
    std::function<void(Task *)> add = [&](Task *t) {
        if (t->state != state) {
            const QString raw = t->raw();
            edits.push_back({t->file->path, t->line, {raw}, {parser::setState(raw, state, today)}});
        }
        for (Task *c : t->children)
            add(c);
    };
    add(task);
    runEdits(edits, state == TaskState::Done ? tr("Mark done") : tr("Mark todo"));
}

void MainWindow::setDue()
{
    const auto tasks = selectedTasks(true);
    if (tasks.empty())
        return;
    DueDateDialog dlg(tasks.front()->due, int(tasks.size()), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    std::vector<Edit> edits;
    for (const Task *t : tasks) {
        if (t->due == dlg.date())
            continue;
        const QString raw = t->raw();
        edits.push_back({t->file->path, t->line, {raw}, {parser::setDue(raw, dlg.date())}});
    }
    runEdits(edits, tr("Set due date"));
}

void MainWindow::clearDue()
{
    std::vector<Edit> edits;
    for (const Task *t : selectedTasks(true)) {
        if (!t->due.isValid())
            continue;
        const QString raw = t->raw();
        edits.push_back({t->file->path, t->line, {raw}, {parser::setDue(raw, {})}});
    }
    runEdits(edits, tr("Clear due date"));
}

void MainWindow::setEta()
{
    const auto tasks = selectedTasks(true);
    if (tasks.empty())
        return;
    EtaDialog dlg(tasks.front()->etaText, int(tasks.size()), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QString eta = dlg.eta();
    std::vector<Edit> edits;
    for (const Task *t : tasks) {
        if (t->etaText == eta)
            continue;
        const QString raw = t->raw();
        edits.push_back({t->file->path, t->line, {raw}, {parser::setEta(raw, eta)}});
    }
    runEdits(edits, eta.isEmpty() ? tr("Clear ETA") : tr("Set ETA"));
}

void MainWindow::editText()
{
    const auto tasks = selectedTasks(false);
    if (tasks.size() > 1) {
        replaceText(); // bulk edit
        return;
    }
    QTreeView *v = current().view;
    if (v->currentIndex().isValid())
        v->edit(v->currentIndex().siblingAtColumn(0));
}

void MainWindow::replaceText()
{
    const auto tasks = selectedTasks(true);
    if (tasks.empty())
        return;
    ReplaceDialog dlg(int(tasks.size()), this);
    if (dlg.exec() != QDialog::Accepted || (dlg.find().isEmpty() && !dlg.regex()))
        return;
    const Qt::CaseSensitivity cs = dlg.caseSensitive() ? Qt::CaseSensitive : Qt::CaseInsensitive;
    QRegularExpression re;
    if (dlg.regex()) {
        re.setPattern(dlg.find());
        if (!dlg.caseSensitive())
            re.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
        if (!re.isValid()) {
            QMessageBox::warning(this, tr("Replace"), tr("Invalid regular expression: %1").arg(re.errorString()));
            return;
        }
    }
    QString repl = dlg.replacement();
    repl.replace(u'\n', u' ');
    std::vector<Edit> edits;
    for (const Task *t : tasks) {
        QString content = t->content;
        if (dlg.regex())
            content.replace(re, repl);
        else
            content.replace(dlg.find(), repl, cs);
        if (content == t->content)
            continue;
        const QString raw = t->raw();
        edits.push_back({t->file->path, t->line, {raw}, {parser::setContent(raw, content)}});
    }
    runEdits(edits, tr("Replace"));
}

QString MainWindow::defaultTaskFile() const
{
    // todo.md in the first directory that has one, else in the first directory.
    const QStringList roots = m_store->roots();
    for (const QString &r : roots) {
        if (QFileInfo::exists(r + QStringLiteral("/todo.md")))
            return r + QStringLiteral("/todo.md");
    }
    return roots.isEmpty() ? QString() : roots.first() + QStringLiteral("/todo.md");
}

void MainWindow::addTask(bool subtask)
{
    const auto tasks = selectedTasks(false);
    Task *anchorTask = tasks.empty() ? nullptr : tasks.back();
    TaskFile *file = anchorTask ? anchorTask->file : selectedFile();
    const QString path = file ? file->path : defaultTaskFile();
    if (path.isEmpty()) {
        statusBar()->showMessage(tr("Add a directory first (Ctrl+O)"), 3000);
        return;
    }
    QString prompt = tr("New task in %1:").arg(file ? file->relPath : QStringLiteral("todo.md"));
    if (anchorTask)
        prompt = subtask ? tr("New subtask of \"%1\":").arg(anchorTask->text)
                         : tr("New task after \"%1\":").arg(anchorTask->text);
    bool ok = false;
    const QString text = QInputDialog::getText(this, subtask ? tr("Add subtask") : tr("Add task"), prompt,
                                               QLineEdit::Normal, QString(), &ok);
    if (!ok || text.trimmed().isEmpty())
        return;
    if (anchorTask)
        insertTask(text, anchorTask, subtask, {}, true);
    else
        appendTask(text, path, {}, true);
}

QString MainWindow::newTaskLine(QString text, const QString &lead, const QDate &due)
{
    text.replace(u'\n', u' ');
    QString line = lead + text;
    Task typed;
    typed.content = text;
    parser::parseMeta(typed);
    if (due.isValid() && !typed.due.isValid()) // a !date typed by hand wins
        line = parser::setDue(line, due);
    return parser::setCreated(line, QDate::currentDate());
}

void MainWindow::appendTask(const QString &text, const QString &path, const QDate &due, bool select)
{
    Edit e;
    e.path = path;
    e.line = -1; // append to the end of the file (creating it if needed)
    e.after = {newTaskLine(text, QStringLiteral("- [ ] "), due)};
    if (!select) {
        runEdits({e}, tr("Add task"));
        return;
    }
    runEdits({e}, tr("Add task"), std::vector<LineKey>{});
    if (TaskFile *f = m_store->fileByPath(path); f && !f->tasks.empty()) {
        const LineKey k{path, f->tasks.back().line};
        restoreView(current(), {{k}, k, 0}, true);
    }
}

void MainWindow::insertTask(const QString &text, Task *anchorTask, bool subtask, const QDate &due, bool select)
{
    TaskFile *file = anchorTask->file;
    const QString lead = parser::leadingWhitespace(anchorTask->prefix);
    const QChar bullet = anchorTask->prefix[lead.size()];
    QString indent = lead;
    if (subtask)
        indent = anchorTask->children.empty() ? lead + indentUnit(file)
                                              : parser::leadingWhitespace(anchorTask->children.front()->prefix);
    // After the anchor's last descendant, so it lands as a sibling or last child.
    const Task *after = lastDescendant(anchorTask);
    Edit e;
    e.path = file->path;
    e.line = after->line;
    e.before = {after->raw()};
    e.after = {after->raw(), newTaskLine(text, indent + bullet + QStringLiteral(" [ ] "), due)};
    const QString label = subtask ? tr("Add subtask") : tr("Add task");
    if (select)
        runEdits({e}, label, std::vector<LineKey>{{file->path, after->line + 1}});
    else
        runEdits({e}, label);
}

void MainWindow::deleteTasks()
{
    const auto tasks = selectedTasks(true);
    if (tasks.empty())
        return;
    const auto answer = QMessageBox::question(
        this, tr("Delete tasks"),
        tr("Delete %n task line(s) (including subtasks)? This can be undone with Ctrl+Z.", nullptr, int(tasks.size())));
    if (answer != QMessageBox::Yes)
        return;
    std::vector<Edit> edits;
    for (const Task *t : tasks)
        edits.push_back({t->file->path, t->line, {t->raw()}, {}});
    runEdits(edits, tr("Delete"), std::vector<LineKey>{});
}

void MainWindow::openInEditor()
{
    const auto tasks = selectedTasks(false);
    TaskFile *file = tasks.empty() ? selectedFile() : tasks.front()->file;
    if (!file)
        return;
    const int line = tasks.empty() ? 1 : tasks.front()->line + 1;
    const QString cmd = editor::editorFromEnvironment();
    if (cmd.isEmpty()) {
        statusBar()->showMessage(tr("$EDITOR is not set; opening with the default application"), 5000);
        QDesktopServices::openUrl(QUrl::fromLocalFile(file->path));
        return;
    }
    const editor::Launch l = editor::build(cmd, editor::findTerminal(), file->path, line);
    if (!l.isValid()) {
        statusBar()->showMessage(tr("%1 needs a terminal, but none was found (set $TERMINAL)").arg(cmd), 6000);
        return;
    }
    if (!QProcess::startDetached(l.program, l.args))
        statusBar()->showMessage(tr("Could not start %1").arg(l.program), 5000);
}

// ---------------------------------------------------------------- view state

void MainWindow::applyTheme(const QString &name)
{
    QSettings().setValue(QStringLiteral("view/theme"), name);
    theme::apply(theme::fromString(name));
    m_listModel->refreshColors();
    m_agendaModel->refreshColors();
    m_sidebar->refreshColors();
    m_list.view->viewport()->update();
    m_agenda.view->viewport()->update();
}

void MainWindow::updateTitle()
{
    QStringList names;
    for (const QString &r : m_store->roots())
        names << QDir::toNativeSeparators(r);
    setWindowTitle(names.isEmpty() ? QStringLiteral("todo.md")
                                   : QStringLiteral("todo.md — %1").arg(names.join(QLatin1String(", "))));
}

void MainWindow::updateStatus()
{
    if (m_store->isLoading())
        return;
    if (m_store->roots().isEmpty()) {
        m_status->setText(tr("No directories — File › Add directory… (Ctrl+O)"));
        return;
    }
    int counts[4] = {};
    for (const auto &f : m_store->files()) {
        for (const Task &t : f->tasks)
            ++counts[int(t.state)];
    }
    QString s = tr("%1 todo · %2 in progress · %3 done · %4 cancelled · %5 files")
                    .arg(counts[0]).arg(counts[1]).arg(counts[2]).arg(counts[3])
                    .arg(m_store->files().size());
    if (m_lastLoadMs >= 0)
        s += tr(" · loaded in %1 ms").arg(m_lastLoadMs);
    m_status->setText(s);
}

void MainWindow::updateUndoActions()
{
    m_undo->setEnabled(m_store->canUndo());
    m_redo->setEnabled(m_store->canRedo());
    m_undo->setText(m_store->canUndo() ? tr("&Undo %1").arg(m_store->undoLabel()) : tr("&Undo"));
    m_redo->setText(m_store->canRedo() ? tr("&Redo %1").arg(m_store->redoLabel()) : tr("&Redo"));
}

void MainWindow::showContextMenu(const QPoint &pos)
{
    QTreeView *v = current().view;
    QMenu menu(this);
    menu.addActions({m_toggle, m_todo, m_doing, m_done, m_cancel});
    menu.addSeparator();
    menu.addActions({m_due, m_clearDue, m_eta});
    menu.addSeparator();
    menu.addActions({m_edit, m_replace, m_add, m_addSub, m_delete});
    menu.addSeparator();
    menu.addActions({m_copyList, m_openEditor});
    menu.exec(v->viewport()->mapToGlobal(pos));
}

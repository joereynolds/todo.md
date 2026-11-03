#include "mainwindow.h"

#include "dialogs.h"
#include "editor.h"
#include "models.h"
#include "parser.h"
#include "sidebar.h"
#include "store.h"
#include "tagcompleter.h"
#include "templates.h"
#include "templatespage.h"
#include "taskview.h"
#include "theme.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QCursor>
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
#include <QListWidget>
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
    m_list.store = m_agenda.store = store;

    // Templates are task files too, in a store of their own (rooted at the
    // hidden .templates directory) so they never mix with real tasks.
    m_templateStore = new TaskStore(this);
    connect(m_templateStore, &TaskStore::aboutToChange, this, [this] { m_saved[2] = saveView(m_templates); });
    m_templateModel = new TaskListModel(m_templateStore, this);
    m_templateModel->setTemplateHeaders(true);
    m_templates.model = m_templateModel;
    m_templates.store = m_templateStore;
    m_tagIndex = new TagIndex(store, this);
    m_sidebar = new Sidebar(store, m_tagIndex, this);
    new TagCompleter(m_tagIndex, m_sidebar->addBox());

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

    connect(m_templateStore, &TaskStore::changed, this, [this] {
        const bool forced = m_forceSelect.has_value();
        if (forced) {
            m_saved[2].selection = *m_forceSelect;
            m_saved[2].current = m_forceSelect->empty() ? std::nullopt : std::optional<LineKey>(m_forceSelect->front());
            m_forceSelect.reset();
        }
        restoreView(m_templates, m_saved[2], forced);
        // A template just created with New…: start editing its first task.
        if (!m_editWhenLoaded.isEmpty()) {
            if (TaskFile *f = m_templateStore->fileByPath(m_editWhenLoaded); f && !f->tasks.empty()) {
                m_editWhenLoaded.clear();
                const QModelIndex i = m_templateModel->indexOf(&f->tasks.front());
                m_templates.view->setCurrentIndex(i);
                m_templates.view->scrollTo(i);
                if (onTemplatesTab())
                    m_templates.view->edit(i);
            }
        }
        updateTemplatesPage();
    });
    connect(m_templateModel, &TaskModelBase::contentEdited, this,
            [this](const QString &path, int line, const QString &before, const QString &after) {
                runEdits({{path, line, {before}, {after}}}, tr("Edit template"));
            }, Qt::QueuedConnection);
    connect(m_templateStore, &TaskStore::undoStateChanged, this, &MainWindow::updateUndoActions);
    connect(m_templateStore, &TaskStore::message, this, [this](const QString &m) { statusBar()->showMessage(m, 6000); });
    // The templates directory follows the roots.
    connect(store, &TaskStore::loadStarted, this, &MainWindow::syncTemplateRoots);
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
    m_reload = make(tr("&Reload"), QKeySequence(Qt::Key_F5), [this] {
        m_store->reload();
        if (!m_templateStore->roots().isEmpty())
            m_templateStore->reload();
    });
    m_quit = make(tr("&Quit"), QKeySequence::Quit, [this] { close(); });

    // Undo/redo follow the tab: the templates have their own history.
    m_undo = make(tr("&Undo"), QKeySequence::Undo, [this] { current().store->undo(); });
    m_redo = make(tr("&Redo"), QKeySequence::Redo, [this] { current().store->redo(); });

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
    m_addTag = make(tr("Add tag…"), QKeySequence(Qt::Key_NumberSign), [this] {
        // Pop the tag menu up at the current row.
        QTreeView *v = current().view;
        const QRect r = v->visualRect(v->currentIndex());
        const QPoint at = r.isValid() ? v->viewport()->mapToGlobal(r.bottomLeft()) : QCursor::pos();
        m_tagMenu->exec(at);
    });
    // '#' is Shift+3 on US layouts and unshifted on UK ones; accept both forms.
    m_addTag->setShortcuts({QKeySequence(Qt::Key_NumberSign), QKeySequence(Qt::SHIFT | Qt::Key_NumberSign)});
    m_edit = make(tr("&Edit text"), QKeySequence(Qt::Key_F2), [this] { editText(); });
    m_replace = make(tr("&Replace in selection…"), QKeySequence(Qt::CTRL | Qt::Key_H), [this] { replaceText(); });
    m_add = make(tr("&Add task below…"), QKeySequence(Qt::Key_N), [this] { addTask(false); });
    m_addSub = make(tr("Add &subtask…"), QKeySequence(Qt::SHIFT | Qt::Key_N), [this] { addTask(true); });
    m_delete = make(tr("De&lete…"), QKeySequence::Delete, [this] { deleteTasks(); });
    m_openEditor = make(tr("Open in edi&tor ($EDITOR)"), QKeySequence(Qt::Key_E), [this] { openInEditor(); });
    m_saveTemplate = make(tr("Save as te&mplate…"), QKeySequence(), [this] { saveTemplate(); });
    m_saveTemplate->setToolTip(tr("Save the selected tasks (with subtasks) as a reusable list template"));
    m_insertTemplate = make(tr("Insert template…"), QKeySequence(Qt::CTRL | Qt::Key_T), [this] {
        // Pop the template menu up at the current row.
        QTreeView *v = current().view;
        const QRect r = v->visualRect(v->currentIndex());
        const QPoint at = r.isValid() ? v->viewport()->mapToGlobal(r.bottomLeft()) : QCursor::pos();
        m_templateMenu->exec(at);
    });
    m_tagMenu = new QMenu(tr("Add ta&g"), this);
    connect(m_tagMenu, &QMenu::aboutToShow, this, &MainWindow::populateTagMenu);
    m_templateMenu = new QMenu(tr("&Insert template"), this);
    connect(m_templateMenu, &QMenu::aboutToShow, this, &MainWindow::populateTemplateMenu);
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
    m_templatesTab = make(tr("Te&mplates"), QKeySequence(Qt::CTRL | Qt::Key_3), [this] { m_tabs->setCurrentIndex(2); });
    // Task actions work on the Templates tab too (it's a task list); these don't.
    m_tasksOnlyActions = {m_saveTemplate, m_insertTemplate};

    m_themeGroup = new QActionGroup(this);
    const QString current = s.value(QStringLiteral("view/theme"), QStringLiteral("light")).toString();
    for (const auto &[name, label] : {std::pair{QStringLiteral("light"), tr("&Light")},
                                      std::pair{QStringLiteral("soft"), tr("S&oft (grey light)")},
                                      std::pair{QStringLiteral("dark"), tr("&Dark")},
                                      std::pair{QStringLiteral("system"), tr("&System")},
                                      std::pair{QStringLiteral("clean"), tr("&Clean")},
                                      std::pair{QStringLiteral("midnight"), tr("&Midnight")},
                                      std::pair{QStringLiteral("paper"), tr("&Paper")}}) {
        QAction *a = m_themeGroup->addAction(label);
        a->setCheckable(true);
        a->setChecked(name == current);
        connect(a, &QAction::triggered, this, [this, n = name] { applyTheme(n); });
    }
}

QTreeView *MainWindow::makeView(bool tree)
{
    auto *v = new TaskView(tree, this);
    static_cast<TaskDelegate *>(v->itemDelegate())->setEditorHook([this](QLineEdit *edit) {
        new TagCompleter(m_tagIndex, edit);
    });
    connect(v, &TaskView::checkClicked, this, [this, v](const QModelIndex &index) {
        const Pane &p = v == m_agenda.view ? m_agenda : m_list;
        if (Task *t = p.model->taskAt(index))
            toggleTask(t);
    });
    connect(v, &TaskView::linkActivated, this, [](const QUrl &url) { QDesktopServices::openUrl(url); });
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
    edit->addMenu(m_tagMenu);
    edit->addSeparator();
    edit->addActions({m_edit, m_replace, m_add, m_addSub, m_delete});
    edit->addSeparator();
    edit->addActions({m_copyList, m_openEditor});
    edit->addSeparator();
    edit->addMenu(m_templateMenu);
    edit->addAction(m_saveTemplate);
    edit->addAction(m_insertTemplate);
    edit->addAction(m_addTag);

    QMenu *view = menuBar()->addMenu(tr("&View"));
    view->addActions({m_toggleSidebar, m_tasksTab, m_agendaTab, m_templatesTab});
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
    new TagCompleter(m_tagIndex, m_filter);
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
    m_tabs->setDocumentMode(!theme::cardLayout());
    m_tabs->addTab(m_list.view, tr("Tasks"));
    m_tabs->addTab(agendaPage, tr("Agenda"));
    m_templates.view = makeView(true);
    m_templates.view->setModel(m_templateModel);
    h = m_templates.view->header();
    h->setStretchLastSection(false);
    h->setSectionResizeMode(TaskListModel::TextColumn, QHeaderView::Stretch);
    h->setSectionResizeMode(TaskListModel::TagsColumn, QHeaderView::Interactive);
    h->resizeSection(TaskListModel::TagsColumn, tagsWidth);
    h->setSectionResizeMode(TaskListModel::EtaColumn, QHeaderView::Fixed);
    h->resizeSection(TaskListModel::EtaColumn, etaWidth);
    h->hideSection(TaskListModel::DueColumn);
    m_templatesPage = new TemplatesPage(m_templates.view, this);
    m_tabs->addTab(m_templatesPage, tr("Templates"));
    connect(m_templatesPage, &TemplatesPage::newRequested, this, &MainWindow::newTemplate);
    connect(m_templatesPage, &TemplatesPage::renameRequested, this, &MainWindow::renameTemplate);
    connect(m_templatesPage, &TemplatesPage::deleteRequested, this, &MainWindow::deleteTemplate);
    connect(m_templatesPage, &TemplatesPage::openRequested, this, [this] {
        const auto tasks = selectedTasks(false);
        if (const QString name = currentTemplate(); !name.isEmpty())
            openPathInEditor(templates::pathFor(templatesDir(), name), tasks.empty() ? 1 : tasks.front()->line + 1);
    });
    connect(m_templatesPage, &TemplatesPage::insertRequested, this, [this] {
        const QString name = currentTemplate();
        if (name.isEmpty())
            return;
        m_tabs->setCurrentIndex(0); // goes after the task selected there, as Ctrl+T would
        insertTemplate(name, false);
    });
    connect(m_templates.view->selectionModel(), &QItemSelectionModel::currentChanged, this,
            &MainWindow::updateTemplatesPage);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] {
        const bool templatesTab = onTemplatesTab();
        for (QAction *a : m_tasksOnlyActions)
            a->setEnabled(!templatesTab);
        updateUndoActions();
        updateTemplatesPage();
        current().view->setFocus();
    });

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
    return const_cast<Pane &>(std::as_const(*this).current());
}

const MainWindow::Pane &MainWindow::current() const
{
    const int i = m_tabs ? m_tabs->currentIndex() : 0;
    return i == 1 ? m_agenda : i == 2 ? m_templates : m_list;
}

bool MainWindow::onTemplatesTab() const
{
    return m_tabs && m_tabs->currentIndex() == 2;
}

TaskStore *MainWindow::storeForPath(const QString &path) const
{
    for (const QString &r : m_templateStore->roots()) {
        if (path.startsWith(r + u'/'))
            return m_templateStore;
    }
    return m_store;
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

// ---------------------------------------------------------------- templates

QString MainWindow::templatesDir() const
{
    return templates::dirFor(m_store->roots());
}

void MainWindow::populateTemplateMenu()
{
    m_templateMenu->clear();
    const QString dir = templatesDir();
    const QStringList names = templates::names(dir);
    if (names.isEmpty()) {
        m_templateMenu->addAction(tr("No templates yet: select tasks, then Save as template…"))->setEnabled(false);
    }
    for (const QString &name : names)
        m_templateMenu->addAction(name, this, [this, name] { insertTemplate(name, false); });
    m_templateMenu->addSeparator();
    if (!names.isEmpty()) {
        QMenu *sub = m_templateMenu->addMenu(tr("As &subtasks of the selected task"));
        sub->setEnabled(!selectedTasks(false).empty());
        for (const QString &name : names)
            sub->addAction(name, this, [this, name] { insertTemplate(name, true); });
        QMenu *edit = m_templateMenu->addMenu(tr("&Edit"));
        for (const QString &name : names)
            edit->addAction(name, this, [this, dir, name] { openPathInEditor(templates::pathFor(dir, name), 1); });
    }
    m_templateMenu->addAction(tr("&Manage templates…"), this, [this] { m_tabs->setCurrentIndex(2); });
    m_templateMenu->addAction(tr("Open templates &folder"), this, [this] {
        const QString dir = templatesDir();
        if (dir.isEmpty()) {
            statusBar()->showMessage(tr("Add a directory first (Ctrl+O)"), 3000);
            return;
        }
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
}

void MainWindow::syncTemplateRoots()
{
    const QString dir = templatesDir();
    const QStringList roots = QFileInfo(dir).isDir() ? QStringList{TaskStore::normalizeRoot(dir)} : QStringList{};
    if (roots != m_templateStore->roots())
        m_templateStore->setRoots(roots);
    updateTemplatesPage();
}

QString MainWindow::currentTemplate() const
{
    const TaskFile *f = m_templateModel->fileAt(m_templates.view->currentIndex());
    return f ? QFileInfo(f->path).completeBaseName() : QString();
}

void MainWindow::updateTemplatesPage()
{
    if (!m_templatesPage)
        return;
    m_templatesPage->setCurrentTemplate(currentTemplate());
    m_templatesPage->setEmpty(m_templateModel->rowCount() == 0 && !m_templateStore->isLoading(),
                              !templatesDir().isEmpty());
}

void MainWindow::newTemplate()
{
    const QString dir = templatesDir();
    if (dir.isEmpty())
        return;
    for (QString name;;) {
        bool ok = false;
        name = QInputDialog::getText(this, tr("New template"), tr("Template name:"), QLineEdit::Normal, name, &ok)
                   .trimmed();
        if (!ok || name.isEmpty())
            return;
        if (!templates::isValidName(name)) {
            QMessageBox::warning(this, tr("New template"), tr("A template name can't contain / or \\ or start with a dot."));
            continue;
        }
        const QString path = templates::pathFor(dir, name);
        if (QFileInfo::exists(path)) {
            QMessageBox::warning(this, tr("New template"), tr("There's already a template called \"%1\".").arg(name));
            continue;
        }
        // A template needs a task to be listed; it's opened for editing as soon as it's loaded.
        if (!templates::save(path, {QStringLiteral("- [ ] New task")})) {
            statusBar()->showMessage(tr("Could not write %1").arg(path), 5000);
            return;
        }
        m_editWhenLoaded = path;
        syncTemplateRoots(); // otherwise the directory watcher picks the file up
        return;
    }
}

void MainWindow::renameTemplate()
{
    const QString old = currentTemplate();
    const QString dir = templatesDir();
    if (old.isEmpty())
        return;
    for (QString name = old;;) {
        bool ok = false;
        name = QInputDialog::getText(this, tr("Rename template"), tr("New name for \"%1\":").arg(old), QLineEdit::Normal,
                                     name, &ok)
                   .trimmed();
        if (!ok || name.isEmpty() || name == old)
            return;
        if (!templates::isValidName(name)) {
            QMessageBox::warning(this, tr("Rename template"), tr("A template name can't contain / or \\ or start with a dot."));
            continue;
        }
        // A case-only rename is allowed (the "existing" file is this one on some file systems).
        if (QFileInfo::exists(templates::pathFor(dir, name)) && name.compare(old, Qt::CaseInsensitive) != 0) {
            QMessageBox::warning(this, tr("Rename template"), tr("There's already a template called \"%1\".").arg(name));
            continue;
        }
        if (!QFile::rename(templates::pathFor(dir, old), templates::pathFor(dir, name)))
            statusBar()->showMessage(tr("Could not rename %1").arg(old), 5000);
        return; // the directory watcher updates the list
    }
}

void MainWindow::deleteTemplate()
{
    const QString name = currentTemplate();
    if (name.isEmpty())
        return;
    if (QMessageBox::question(this, tr("Delete template"), tr("Delete the template \"%1\"?").arg(name))
        != QMessageBox::Yes)
        return;
    if (!QFile::remove(templates::pathFor(templatesDir(), name)))
        statusBar()->showMessage(tr("Could not delete %1").arg(name), 5000);
}

void MainWindow::saveTemplate()
{
    const auto tasks = selectedTasks(true);
    const QString dir = templatesDir();
    if (tasks.empty() || dir.isEmpty()) {
        statusBar()->showMessage(tr("Select the tasks to save as a template"), 3000);
        return;
    }
    // Suggest a name from the first task: "Seeing GF" -> "seeing-gf".
    QString suggestion = tasks.front()->text.toLower().simplified();
    suggestion.replace(QRegularExpression(QStringLiteral("[^\\w]+")), QStringLiteral("-"));
    suggestion = suggestion.left(40);
    while (suggestion.endsWith(u'-'))
        suggestion.chop(1);
    QString name;
    for (;;) {
        bool ok = false;
        name = QInputDialog::getText(this, tr("Save as template"),
                                     tr("Template name (%n task(s)):", nullptr, int(tasks.size())), QLineEdit::Normal,
                                     name.isEmpty() ? suggestion : name, &ok)
                   .trimmed();
        if (!ok || name.isEmpty())
            return;
        if (!templates::isValidName(name)) {
            QMessageBox::warning(this, tr("Save as template"), tr("A template name can't contain / or \\ or start with a dot."));
            continue;
        }
        break;
    }
    const QString path = templates::pathFor(dir, name);
    if (QFileInfo::exists(path)
        && QMessageBox::question(this, tr("Save as template"), tr("Replace the existing template \"%1\"?").arg(name))
               != QMessageBox::Yes)
        return;
    if (!templates::save(path, templates::fromTasks(tasks))) {
        statusBar()->showMessage(tr("Could not write %1").arg(path), 5000);
        return;
    }
    statusBar()->showMessage(tr("Saved template \"%1\" (%n task(s))", nullptr, int(tasks.size())).arg(name), 4000);
    syncTemplateRoots(); // the directory may have just been created
}

void MainWindow::insertTemplate(const QString &name, bool subtask)
{
    QFile in(templates::pathFor(templatesDir(), name));
    if (!in.open(QIODevice::ReadOnly)) {
        statusBar()->showMessage(tr("Could not read template \"%1\"").arg(name), 4000);
        return;
    }
    const QByteArray data = in.readAll();
    const auto tasks = selectedTasks(false);
    Task *anchor = tasks.empty() ? nullptr : tasks.back();
    if (subtask && !anchor)
        return;
    TaskFile *file = anchor ? anchor->file : selectedFile();
    const QString path = file ? file->path : defaultTaskFile();
    if (path.isEmpty()) {
        statusBar()->showMessage(tr("Add a directory first (Ctrl+O)"), 3000);
        return;
    }
    const QString unit = file ? indentUnit(file) : QStringLiteral("  ");
    QString lead;
    if (anchor) {
        lead = parser::leadingWhitespace(anchor->prefix);
        if (subtask)
            lead = anchor->children.empty() ? lead + unit : parser::leadingWhitespace(anchor->children.front()->prefix);
    }
    const QStringList lines = templates::instantiate(data, lead, unit, QDate::currentDate());
    if (lines.isEmpty()) {
        statusBar()->showMessage(tr("Template \"%1\" has no tasks").arg(name), 4000);
        return;
    }
    const QString label = tr("Insert template %1").arg(name);
    // The inserted tasks end up selected, ready for e.g. a due date (D).
    if (anchor) {
        const Task *after = lastDescendant(anchor);
        Edit e{path, after->line, {after->raw()}, QStringList{after->raw()} + lines};
        std::vector<LineKey> keys;
        for (int i = 0; i < lines.size(); ++i)
            keys.push_back({path, after->line + 1 + i});
        runEdits({e}, label, keys);
        return;
    }
    runEdits({Edit{path, -1, {}, lines}}, label, std::vector<LineKey>{});
    if (TaskFile *f = storeForPath(path)->fileByPath(path); f && f->tasks.size() >= size_t(lines.size())) {
        SavedView sel;
        for (size_t i = f->tasks.size() - size_t(lines.size()); i < f->tasks.size(); ++i)
            sel.selection.push_back({path, f->tasks[i].line});
        sel.current = sel.selection.front();
        restoreView(current(), sel, true);
    }
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
        const Task *t = p.store->taskAt(k.path, k.line);
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
    const int n = storeForPath(edits.front().path)->apply(edits, label);
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

void MainWindow::populateTagMenu()
{
    // Tags already in use (from the tasks store), alphabetical. A tag every selected
    // task already has is shown checked and disabled.
    m_tagMenu->clear();
    const auto tasks = selectedTasks(false);
    const auto &entries = m_tagIndex->entries();
    if (entries.empty()) {
        m_tagMenu->addAction(tr("No tags yet: type #tag in a task"))->setEnabled(false);
        return;
    }
    for (const TagIndex::Entry &e : entries) {
        const bool all = !tasks.empty() && std::all_of(tasks.cbegin(), tasks.cend(), [&](const Task *t) {
            return std::any_of(t->tags.cbegin(), t->tags.cend(),
                               [&](const QString &x) { return x.compare(e.tag, Qt::CaseInsensitive) == 0; });
        });
        QString label = u'#' + e.tag;
        label.replace(u'&', QLatin1String("&&"));
        QAction *a = m_tagMenu->addAction(label, this, [this, tag = e.tag] { addTag(tag); });
        a->setCheckable(all);
        a->setChecked(all);
        a->setEnabled(!tasks.empty() && !all);
    }
}

void MainWindow::addTag(const QString &tag)
{
    // Only the selected tasks: subtasks already inherit their ancestors' tags
    // for filtering, so tagging each of them would just clutter the files.
    std::vector<Edit> edits;
    for (const Task *t : selectedTasks(false)) {
        const QString raw = t->raw();
        const QString line = parser::addTag(raw, tag);
        if (line != raw)
            edits.push_back({t->file->path, t->line, {raw}, {line}});
    }
    runEdits(edits, tr("Add tag #%1").arg(tag));
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
    if (!file && onTemplatesTab()) {
        statusBar()->showMessage(tr("Select a template to add to (or create one with New template…)"), 4000);
        return;
    }
    const QString path = file ? file->path : defaultTaskFile();
    if (path.isEmpty()) {
        statusBar()->showMessage(tr("Add a directory first (Ctrl+O)"), 3000);
        return;
    }
    QString prompt = tr("New task in %1:").arg(file ? (onTemplatesTab() ? QFileInfo(file->path).completeBaseName() : file->relPath)
                                                    : QStringLiteral("todo.md"));
    if (anchorTask)
        prompt = subtask ? tr("New subtask of \"%1\":").arg(anchorTask->text)
                         : tr("New task after \"%1\":").arg(anchorTask->text);
    AddTaskDialog dlg(subtask ? tr("Add subtask") : tr("Add task"), prompt, this);
    new TagCompleter(m_tagIndex, dlg.lineEdit());
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QString text = dlg.text();
    if (text.trimmed().isEmpty())
        return;
    if (anchorTask)
        insertTask(text, anchorTask, subtask, dlg.due(), true);
    else
        appendTask(text, path, dlg.due(), true);
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
    if (TaskFile *f = storeForPath(path)->fileByPath(path); f && !f->tasks.empty()) {
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
    openPathInEditor(file->path, tasks.empty() ? 1 : tasks.front()->line + 1);
}

void MainWindow::openPathInEditor(const QString &path, int line)
{
    const QString cmd = editor::editorFromEnvironment();
    if (cmd.isEmpty()) {
        statusBar()->showMessage(tr("$EDITOR is not set; opening with the default application"), 5000);
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        return;
    }
    const editor::Launch l = editor::build(cmd, editor::findTerminal(), path, line);
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
    m_tabs->setDocumentMode(!theme::cardLayout());
    // Row heights depend on the theme.
    for (QTreeView *v : {m_list.view, m_agenda.view, m_templates.view})
        v->doItemsLayout();
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
    const TaskStore *st = current().store;
    m_undo->setEnabled(st->canUndo());
    m_redo->setEnabled(st->canRedo());
    m_undo->setText(st->canUndo() ? tr("&Undo %1").arg(st->undoLabel()) : tr("&Undo"));
    m_redo->setText(st->canRedo() ? tr("&Redo %1").arg(st->redoLabel()) : tr("&Redo"));
}

void MainWindow::showContextMenu(const QPoint &pos)
{
    QTreeView *v = current().view;
    QMenu menu(this);
    // Links first: the one under the cursor, else those of the task under it.
    auto copy = [this](const QString &url) {
        QApplication::clipboard()->setText(url);
        statusBar()->showMessage(tr("Copied %1").arg(url), 3000);
    };
    if (const QString url = static_cast<TaskView *>(v)->linkAt(pos); !url.isEmpty()) {
        menu.addAction(tr("Open &link"), this, [url] { QDesktopServices::openUrl(QUrl(url, QUrl::TolerantMode)); });
        menu.addAction(tr("Copy link add&ress"), this, [copy, url] { copy(url); });
        menu.addSeparator();
    } else if (const Task *t = current().model->taskAt(v->indexAt(pos)); t && !t->links.empty()) {
        if (t->links.size() == 1) {
            menu.addAction(tr("Copy link add&ress"), this, [copy, url = t->links.front().url] { copy(url); });
        } else {
            QMenu *sub = menu.addMenu(tr("Copy link add&ress"));
            for (const TaskLink &l : t->links) {
                const QString shown = t->text.mid(l.start, l.length);
                QString label = shown == l.url ? l.url : QStringLiteral("%1 — %2").arg(shown, l.url);
                label.replace(u'&', QLatin1String("&&"));
                sub->addAction(label, this, [copy, url = l.url] { copy(url); });
            }
        }
        menu.addSeparator();
    }
    menu.addActions({m_toggle, m_todo, m_doing, m_done, m_cancel});
    menu.addSeparator();
    menu.addActions({m_due, m_clearDue, m_eta});
    menu.addMenu(m_tagMenu);
    menu.addSeparator();
    menu.addActions({m_edit, m_replace, m_add, m_addSub, m_delete});
    menu.addSeparator();
    menu.addActions({m_copyList, m_openEditor});
    menu.addSeparator();
    menu.addMenu(m_templateMenu);
    menu.addAction(m_saveTemplate);
    menu.exec(v->viewport()->mapToGlobal(pos));
}

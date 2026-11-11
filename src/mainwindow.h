#pragma once

#include "edits.h"
#include "task.h"

#include <QMainWindow>
#include <QTimer>

#include <functional>
#include <optional>
#include <vector>

class AgendaModel;
class QSplitter;
class Sidebar;
class TagIndex;
class ViewList;
class ViewBar;
class QueryBuilder;
class QFrame;
class QToolButton;
class TemplatesPage;
class QAction;
class QActionGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QMenu;
class QTabWidget;
class QTreeView;
class TaskFilter;
class TaskListModel;
class TaskModelBase;
class TaskStore;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(TaskStore *store, QWidget *parent = nullptr);
    ~MainWindow() override;

    // Directories are remembered in QSettings unless persist is false.
    void setDirectories(const QStringList &dirs, bool persist = true);
    void addDirectory(const QString &dir);
    void removeDirectory(const QString &dir);
    void chooseDirectory();
    void showTab(int index);

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Pane {
        QTreeView *view = nullptr;
        TaskModelBase *model = nullptr;
        TaskStore *store = nullptr; // the tasks store, or the templates store
    };
    struct LineKey {
        QString path;
        int line;
    };
    struct SavedView {
        std::vector<LineKey> selection;
        std::optional<LineKey> current;
        int scroll = 0;
    };

    void buildActions();
    void buildUi();
    QTreeView *makeView(bool tree);
    Pane &current();
    const Pane &current() const;
    bool onTemplatesTab() const;
    TaskStore *storeForPath(const QString &path) const; // template files go to the templates store

    std::vector<Task *> selectedTasks(bool withSubtasks) const;
    TaskFile *selectedFile() const;
    SavedView saveView(const Pane &p) const;
    void restoreView(Pane &p, const SavedView &s, bool scrollToCurrent);
    void runEdits(const std::vector<Edit> &edits, const QString &label,
                  std::optional<std::vector<LineKey>> select = std::nullopt);

    void setState(TaskState state);
    void toggleDone();
    void keepVisible(const std::vector<const Task *> &tasks);
    void toggleTask(Task *task);
    void setDue();
    void clearDue();
    void setEta();
    void addTag(const QString &tag);
    void populateTagMenu();
    void editText();
    void replaceText();
    void addTask(bool subtask);
    void copyAsList();
    QString templatesDir() const;
    void syncTemplateRoots(); // points the templates store at templatesDir() (if it exists)
    QString currentTemplate() const; // name of the template under the current row in the Templates tab
    void newTemplate();
    void renameTemplate();
    void deleteTemplate();
    void updateTemplatesPage();
    void populateTemplateMenu();
    void saveTemplate();
    void insertTemplate(const QString &name, bool subtask);
    void openPathInEditor(const QString &path, int line);
    QString defaultTaskFile() const; // todo.md in the first directory (may not exist yet)
    static QString newTaskLine(QString text, const QString &lead, const QDate &due);
    void appendTask(const QString &text, const QString &path, const QDate &due, bool select);
    void insertTask(const QString &text, Task *anchor, bool subtask, const QDate &due, bool select);
    void deleteTasks();
    void openInEditor();

    void applyFilter();
    void setFilterText(const QString &text); // sets the box and applies at once
    void forEachFilter(const std::function<void(TaskFilter &)> &fn);
    void applyTheme(const QString &name);
    void updateStatus();
    void updateTitle();
    void updateUndoActions();
    void showContextMenu(const QPoint &pos);

    // Saved views (ViewList): applying one sets the filter box to its query.
    int activeView() const; // the view whose query is in the filter box, -1 if none
    void applyView(int index); // toggles: applying the active view clears the filter
    void newView();
    void editView(int index);
    void deleteView(int index);
    void populateViewsMenu();
    void showFilterBuilder();
    void fitViewBar(); // the Choose ▾ popup: chips that edit the filter box live
    void showViewMenu(int index, const QPoint &globalPos);

    TaskStore *m_store;
    ViewList *m_viewList = nullptr;
    QMenu *m_viewsMenu = nullptr;
    ViewBar *m_viewBar = nullptr;
    QFrame *m_builderPopup = nullptr;
    QueryBuilder *m_builder = nullptr;
    QToolButton *m_chooseButton = nullptr;
    QAction *m_chooseFilter = nullptr;
    QAction *m_newView = nullptr, *m_editView = nullptr, *m_deleteView = nullptr;
    std::vector<QAction *> m_viewShortcuts; // Ctrl+Alt+1..9
    TaskListModel *m_listModel;
    AgendaModel *m_agendaModel;
    TaskStore *m_templateStore;
    TaskListModel *m_templateModel;
    Pane m_list, m_agenda, m_templates;
    SavedView m_saved[3];
    QString m_editWhenLoaded; // template file whose first task to edit once it's loaded
    std::optional<std::vector<LineKey>> m_forceSelect;

    QTabWidget *m_tabs = nullptr;
    QSplitter *m_splitter = nullptr;
    Sidebar *m_sidebar = nullptr;
    QLineEdit *m_filter = nullptr;
    QComboBox *m_etaFilter = nullptr;
    QLabel *m_status = nullptr;
    QCheckBox *m_emptyDays = nullptr;
    QMenu *m_removeDirMenu = nullptr;
    QTimer m_filterTimer;
    QTimer m_clock;

    QAction *m_addDir, *m_reload, *m_quit;
    QAction *m_undo, *m_redo;
    QAction *m_todo, *m_doing, *m_done, *m_cancel, *m_toggle;
    QAction *m_due, *m_clearDue, *m_eta, *m_addTag, *m_edit, *m_replace, *m_add, *m_addSub, *m_delete;
    QAction *m_openEditor;
    QAction *m_copyList;
    QAction *m_saveTemplate;
    QAction *m_insertTemplate;
    QMenu *m_templateMenu = nullptr;
    QMenu *m_tagMenu = nullptr;
    QAction *m_templatesTab;
    std::vector<QAction *> m_tasksOnlyActions; // off on the Templates tab
    TemplatesPage *m_templatesPage = nullptr;
    TagIndex *m_tagIndex = nullptr;
    QAction *m_showState[4];
    QAction *m_focusFilter, *m_focusFilter2, *m_tasksTab, *m_agendaTab, *m_expandAll, *m_collapseAll, *m_toggleSidebar;
    QActionGroup *m_themeGroup;

    qint64 m_lastLoadMs = -1;
};

#pragma once

#include "edits.h"
#include "task.h"

#include <QMainWindow>
#include <QTimer>

#include <functional>
#include <optional>
#include <vector>

class AgendaFilterProxy;
class AgendaModel;
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

private:
    struct Pane {
        QTreeView *view = nullptr;
        TaskModelBase *model = nullptr;
        AgendaFilterProxy *proxy = nullptr; // agenda only; the task list filters itself
        QModelIndex toSource(const QModelIndex &i) const;
        QModelIndex fromSource(const QModelIndex &i) const;
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
    QTreeView *makeView();
    Pane &current();
    const Pane &current() const;

    std::vector<Task *> selectedTasks(bool withSubtasks) const;
    TaskFile *selectedFile() const;
    SavedView saveView(const Pane &p) const;
    void restoreView(Pane &p, const SavedView &s, bool scrollToCurrent);
    void runEdits(const std::vector<Edit> &edits, const QString &label,
                  std::optional<std::vector<LineKey>> select = std::nullopt);

    void setState(TaskState state);
    void toggleDone();
    void setDue();
    void clearDue();
    void setEta();
    void editText();
    void replaceText();
    void addTask(bool subtask);
    void deleteTasks();
    void openInEditor();

    void applyFilter();
    void forEachFilter(const std::function<void(TaskFilter &)> &fn);
    void applyTheme(const QString &name);
    void refreshAgendaView();
    void updateStatus();
    void updateTitle();
    void updateUndoActions();
    void showContextMenu(const QPoint &pos);

    TaskStore *m_store;
    TaskListModel *m_listModel;
    AgendaModel *m_agendaModel;
    Pane m_list, m_agenda;
    SavedView m_saved[2];
    std::optional<std::vector<LineKey>> m_forceSelect;

    QTabWidget *m_tabs = nullptr;
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
    QAction *m_due, *m_clearDue, *m_eta, *m_edit, *m_replace, *m_add, *m_addSub, *m_delete;
    QAction *m_openEditor;
    QAction *m_showState[4];
    QAction *m_focusFilter, *m_focusFilter2, *m_tasksTab, *m_agendaTab;
    QActionGroup *m_themeGroup;

    qint64 m_lastLoadMs = -1;
};

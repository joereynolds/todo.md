# todo.md

A desktop front-end for markdown tasks. Add one or more directories; every `*.md` /
`*.markdown` file under them is found recursively and its tasks are shown. The
user's requests are kept in `docs/prompts/` (0.md, 1.md, … in order); the task
syntax is in `docs/task-syntax.md`.

Stack: **C++20 + Qt 6 Widgets**, CMake + Ninja. Chosen for startup and scroll
performance. Linux is the target, but nothing is platform specific: no POSIX
calls, `std::filesystem` for the scan, `QSaveFile` for writes.

## Build / run / test

```sh
make                                  # configure if needed, build, run (ARGS="~/notes" to add dirs)
make test                             # build + ctest; also `make build`, `make clean`

cmake -S . -B build -G Ninja          # Release by default
cmake --build build
./build/todomd                        # remembered directories
./build/todomd ~/notes ~/work         # also adds these to the remembered list
cd build && ctest --output-on-failure # offscreen; 60 s timeout each (a test that waits on a
                                      # modal dialog that never appears would otherwise hang)

# Headless checks (only the given dirs; nothing is persisted):
QT_QPA_PLATFORM=offscreen ./build/todomd --bench DIR...
QT_QPA_PLATFORM=offscreen ./build/todomd --screenshot out.png [--tab agenda|templates] [--theme soft] DIR...
# Screenshots read the real settings (filters, collapse state). Isolate them with
# XDG_CONFIG_HOME=/some/dir, and put a todomd/todomd.conf there to set filters.
```

CI: `.github/workflows/tests.yml` builds and runs ctest on ubuntu-24.04 with Qt 6.8 LTS
from `jurplel/install-qt-action` (Ubuntu's packaged Qt 6.4 is below the 6.5 minimum).
Development happens on Qt 6.11, so avoid APIs newer than 6.8, or guard them with
`QT_VERSION_CHECK`.

Packaging and releases:
- `packaging/todomd.desktop` + `packaging/todomd.svg`, installed by `cmake --install` (Linux).
- Nix: `package.nix` (the derivation; version read from `project()` in CMakeLists.txt,
  tests run in `checkPhase` offscreen), `flake.nix` (packages, `overlays.default`,
  devShell; x86_64/aarch64 Linux), `default.nix` for non-flake users. New source dirs
  must be added to the fileset in `package.nix`, or the nix build won't see them.
  `.github/workflows/nix.yml` runs `nix flake check`, `nix build`, a smoke test and
  the non-flake build on every push.
- Releases: `make release [BUMP=patch|minor|major|x.y.z]` (`release.sh`; `-y` skips the
  prompt) checks master is clean and in sync, runs the tests, bumps `project(todomd VERSION …)`,
  commits, tags `vx.y.z` and pushes both. `.github/workflows/release.yml` refuses a tag that doesn't match,
  then builds on ubuntu-22.04 (older glibc) with Qt 6.8, runs ctest, bundles Qt with
  linuxdeploy + its Qt plugin, packs with appimagetool (icon: a 256 px PNG rendered from the SVG,
  since appimagetool rejects SVG for `.DirIcon`), and attaches it to a GitHub release.

Perf baseline (200k tasks / 5,000 files, generated corpus, every row visible):
scan+parse ≈ 50 ms, first paint ≈ 120 ms total (the list, the agenda with subtasks, and
the sidebar all build up front). A filter change takes 10–70 ms (the
free-text `contains` is the slowest). Re-run `--bench` after touching the load or list paths.

## Layout

| File | Role |
|---|---|
| `src/task.h` | `Task` (incl. `tags`), `TaskFile`, `TaskState`. Both derive from `Node{isFile}`. `Task::listRow` is owned by `TaskListModel`. |
| `src/parser.*` | Byte-level line parser and pure line transforms (`setState`, `setDue`, `setEta`, `addTag`, `setContent`, `parseEta`). |
| `src/edits.*` | `Edit` (range of lines: expected `before`, replacement `after`), `LineBuffer` (preserves CRLF / trailing newline), `applyEdits()`, which also returns the inverse edits for undo. |
| `src/templates.*` | List templates (pure, in `todocore`): `.templates/` dir lookup, listing, `fromTasks` (save) and `instantiate` (insert). |
| `src/editor.*` | Builds the command for "Open in editor" from `$EDITOR` (pure `editor::build`, unit tested). |
| `src/store.*` | `TaskStore`: several roots, async load (thread pool), `QFileSystemWatcher` sync, the **only** writer to disk, undo/redo. |
| `src/models.*` | `TaskFilter` (state / words / ETA / `due:` / `done:` / `is:` / `#tag`), `TagIndex`, `TaskColors`, `FlatTaskModel` (tree as a flat list: groups, filtering, collapse) with `TaskListModel` (group = file) and `AgendaModel` (group = day; single-day mode for the sidebar). |
| `src/dialogs.*` | Due-date picker, add task (`AddTaskDialog`: text + due choice), find/replace, ETA picker (number + unit + presets). |
| `src/theme.*` | Light (default) / Soft (grey light) / Dark / System: Fusion + palette. Clean / Midnight / Paper (the "modern" looks): Fusion + palette + an app style sheet generated from a `Look` (tokens: colours, radius, tab style, card, row padding); `theme::accent()` recolours in-progress, `rowPadding()` sets row height, `cardLayout()` turns tab `documentMode` off for Paper's card. Combo arrow is `src/icons/chevron-down.png` (Qt resource). |
| `src/taskview.*` | `TaskDelegate` (draws checkbox, chevron, guides, header band, progress pill) and `TaskView` (chevron/checkbox/tag-chip clicks, Left/Right keys, optional header spans). |
| `src/sidebar.*` | `Sidebar`: stat tiles (click = filter), `TaskCalendar` (dots on due days), single-day `AgendaModel` list, add box, `TagCloud`. |
| `src/templatespage.*` | `TemplatesPage`: the Templates tab's frame (buttons + empty-state hint around a `TaskView`). |
| `src/tagcompleter.*` | `TagCompleter`: completes the `#word` under the cursor in any `QLineEdit` from `TagIndex`. |
| `src/mainwindow.*` | Toolbar, tabs, actions and shortcuts, bulk ops, selection/scroll preservation. |
| `tests/` | `test_core` (parser, edits, ETA, editor command, templates), `test_store` (store + models under `QAbstractItemModelTester`, real temp dirs and watcher, multiple roots), `test_window` (drives shortcuts on the real window). |

CMake targets: `todocore` (parser, edits, editor, templates; Qt Core only) → `todoapp` (store,
models, window) → `todomd` (just `main.cpp`). The tests link the libraries.

## Parsing rules (decisions not spelled out in the syntax doc)

- A task is `<ws>[-*+] [c] ` where c is one of ` /xX-`. The space after `]` is required.
- Date tokens are whitespace-separated words of exactly `K YYYY-MM-DD` with K in `! + x -`
  and a valid date.
- ETA tokens are `~<1-9 digits><m|h|d|w|y>`, stored in minutes (y = 365 d). `Task::etaText`
  keeps the token as written for display.
- Tag tokens are `#` + letters/digits/`_`/`-`/`/`, not all digits (`parser::tagOf`); trailing
  punctuation is ignored (`#work,`). `Task::tags` holds them without `#`, deduplicated
  case-insensitively. Nested tags use `/`.
- Date and ETA tokens are stripped from the displayed text (`Task::text`), but stay in
  `Task::content`, which is what the free-text filter, inline editing and find/replace use.
  Tags are stripped only when they trail (no plain word after them): "Do it #office" shows
  "Do it", but "Call #mum about it" keeps `#mum` so the sentence still reads.
- Nesting comes from indentation (tab = next multiple of 4). Any non-blank line pops
  every open task indented at or deeper than it, so a heading or paragraph ends nesting.
  Blank lines don't. `Task::depth` is 0 for top-level tasks.
- Tasks inside fenced code blocks (``` or ~~~) are ignored. This is why
  `docs/task-syntax.md` itself shows no tasks.
- `(done/total)` counts **all descendants**. Cancelled tasks, and anything under them,
  are excluded from both numbers.
- Hidden directories (`.*`) and `node_modules` are skipped. With several roots, a file
  under more than one root is held once. Its display path is relative to the innermost
  root, prefixed with that root's folder name (no prefix when there's only one root).

## Writing (invariants worth keeping)

- All writes go through `TaskStore::apply(edits, label)`. A missing file is created only when
  every edit to it is an append (`line = -1`); it's added to `m_known` so the dir watcher
  doesn't load it twice. Undo leaves it on disk, empty. Each `Edit` carries the exact
  line(s) it expects. If the file changed on disk the edit is skipped and reported,
  never forced. The file is rewritten with `QSaveFile` (atomic) and re-parsed at once.
- Undo stores the inverse edits computed by `applyEdits`. At the same line number,
  line-consuming edits apply before insertions, and insertions keep list order. That
  rule is what makes undo of multi-line deletes and inserts exact (see `test_core`).
- New tasks (add task / subtask) get `+<today>` via `parser::setCreated`, unless the
  typed text already has a `+date`. All adds go through `MainWindow::newTaskLine` +
  `insertTask` (after an anchor's last descendant) or `appendTask` (end of file).
- N / Shift+N use `AddTaskDialog`: the text plus a due choice (None (default) · Today ·
  Tomorrow · Next week = +7 days · Next month = +1 month · Pick… = `DueDateDialog`), the
  resolved date shown under the buttons. A `!date` typed in the text wins (`newTaskLine`).
- A task with nowhere to go (N with nothing selected, or the sidebar add box with no day-list
  selection) is appended to `todo.md` in the first root that has one, else the first root
  (`defaultTaskFile`); the file is created if missing.
- Template files are written directly (not through a store) only when created: Save as
  template and New template use `templates::save`; rename/delete are plain file ops. Edits
  inside a template go through `m_templateStore` (undoable); inserting one is a normal edit
  of the tasks store.
- `setState` adds `x<today>` / `-<today>` when marking done/cancelled and removes
  both when moving back to todo/doing. It leaves the line untouched if the state
  doesn't change, so an existing completion date is kept.
- Inline edits from the models are emitted as `contentEdited` and applied through a
  **queued** connection, never inside the delegate's commit.

## Models and change protocol

`TaskStore` wraps every mutation in `aboutToChange()` … `changed()`. Both models
`beginResetModel` on the first and rebuild + `endResetModel` on the second, since they
hold raw `Task*`, which a change invalidates. The window connects to `aboutToChange`
**before** creating the models, so it can record selection, current row and scroll as
(path, line) while the rows are still valid. It restores them after `changed()`.
`runEdits(..., select)` overrides the restored selection (new task selected, nothing
after a delete). `test_store` runs `QAbstractItemModelTester` in fatal mode.

### Task list and agenda: trees drawn as flat lists (`FlatTaskModel`)

History: a QTreeView + QSortFilterProxyModel tree cost ~140 ms for `expandAll` at 200k
rows, and starting collapsed looked "sporadic" to the user. Round 2 asked for subtasks
always shown; round 3 brought the tree back with collapsible subtasks *and* files;
round 4 asked for the same in the agenda. Both views now share `FlatTaskModel`:

- A subclass supplies groups (`collectGroups`): a key, a header, and root tasks. The
  list makes one group per file (roots = top-level tasks). The agenda makes one group
  per day (roots = open tasks due that day, minus any whose ancestor is already a root
  that day). Headers come from `groupData`, task cells from `taskRowData`.
- Rows are a header (`depth 0`) followed by a depth-first walk of the roots and all
  subtasks. A task is visible if it matches the filter or has a visible descendant,
  so ancestors of matches stay as context. When the filter shows everything, a fast
  path skips the matching.
- A node's descendants are always a contiguous run of rows below it, so collapse is
  one `beginRemoveRows`, and expand regenerates that group's rows and inserts the run.
  No reset, so selection and scroll survive.
- Everything is expanded by default. Collapse keys are the group key (file path /
  `day:YYYY-MM-DD`) or path + `\x1f` + task text. They survive store changes and are
  saved to `view/collapsed` / `view/agendaCollapsed`. While the filter narrows, a
  temporary set is used so matches inside collapsed nodes still show.
- Row lookup: the list uses `Task::listRow`, which is fast at 200k. The agenda uses a
  hash, because a task can appear under more than one day.
- `TaskView` handles chevron/checkbox presses before QTreeView sees them, so the
  selection isn't touched. Left/Right collapse, expand or move to parent/child, and
  double-clicking a header toggles it. `TaskDelegate::layoutFor` is the single source
  of hit-test and paint geometry. The agenda and sidebar views span header rows across
  all columns (`setSpanHeaders`, which walks the rows on each reset, so not for the list).

### Sidebar

`QSplitter` [sidebar | tabs]. It can be hidden with Ctrl+B / the toolbar button, or
dragged shut (which unchecks the action). The state is saved (`view/sidebar`,
`view/splitter`). From the top (order requested by the user):

- **Calendar:** dots under days with open due tasks (one per task, max 3), red if
  overdue and amber today. Weekends are neutral and the navigation bar is restyled,
  because Qt's red weekends and blue bar clashed with the colour meanings.
- **Day list** (stretches): a single-day `AgendaModel` for the selected date. The
  checkbox toggles the task; double-click reveals it in the Tasks tab
  (`TaskListModel::reveal` expands its file and ancestors). It follows the toolbar's
  state toggles but not the filter text.
- **Add box** under the day list: Enter adds a task due on the selected day (`!date`
  unless the text has its own), after the selected day-list task in its file, else in
  `todo.md`. The placeholder says which.
- **Filters:** compact, deliberately neutral (the user asked for less colour) one-line
  tiles: overdue, due today, next 7 days, in progress, quick (≤ 2m), done today. Each
  sets the filter box to its term (done today = `done:today`), and clicking again clears it. The
  active one is outlined. A tile at 0 isn't clickable (nothing to show), unless it's the
  active filter, so it can still be cleared.
- **Tags:** a `TagCloud` of chips (`#tag count`, open tasks only, alphabetical; nested tags
  also count for their parents) in a scroll area capped at ~5 rows, hidden when there are
  none. Click = filter `#tag`, again = clear; the active one is outlined. Counts come from
  `TagIndex`, rebuilt on every store change.

Day header wording (`AgendaModel::groupData`): today / tomorrow / "in N days" /
"N days overdue" only within ±14 days, otherwise just the date. A past day with no open
tasks says "N days ago" rather than "overdue". This was a reported bug: an empty past
day in the sidebar read "174 days overdue".

## UI behaviour

- Task rows show a drawn checkbox instead of `[ ]`: empty = todo, blue inner square =
  in progress, green tick = done, grey × = cancelled. Clicking it toggles done ↔ todo
  for that task and its subtasks. Parents and file headers show `done/total` in a small
  pill (green when complete). Header rows (files, agenda days) have a tinted band.
  Colours come from `TaskColors::fromPalette`, so they follow the theme.
- **Bulk actions always include subtasks** (the user asked for no toggle).
  Marking a parent done/cancelled/etc. applies to all descendants, as do due date,
  ETA, replace and delete. Toggle-done decides done vs todo from the selected rows only.
- Agenda: open (todo / in progress) tasks with `!due`, one header row per day. Overdue in
  red, today in orange. "Show empty days" fills today → last due date (min 2 weeks, max 1 year).
- Filter box: space-separated terms, all must match. Plain words match the raw content
  or file path. Terms starting with `~` are ETA conditions:
  `~<=15m ~<1h ~>2h ~>=1d ~30m ~none ~any`. `due:overdue|today|week|none|any` and
  `is:todo|doing|done|cancelled|open` filter by date and state. `#tag` matches the tag
  exactly (case-insensitive) or nested under it (`#work` matches `#work/meeting`, not `#workshop`).
  Subtasks inherit their ancestors' tags for filtering (the Tags column shows only a task's own).
  `done:today|yesterday|week|any` matches done tasks by their `x` date (week = last 7 days).
  Explicit `is:` and `done:` terms override the toolbar state toggles (otherwise `done:today`
  would show nothing with Done hidden, the default).
- Add tag ▸ (context/Edit menu; `#` pops it at the current row): the tags already in use
  (`TagIndex`, main store), no free text. A tag every selected task has is checked and
  disabled. `parser::addTag` appends it unless the task has it (case-insensitive). It applies
  to the selected tasks only, **not** their subtasks — the one exception to the
  bulk-includes-subtasks rule, since subtasks inherit tags for filtering. New tags are
  still typed by hand (inline edit / add dialog).
- Tags column (list and agenda, hidden in the sidebar): chips drawn by `TaskDelegate`
  (`tagRects` is the shared paint/hit-test geometry; overflow shows `+N`, the tooltip lists
  all). Clicking a chip sets the filter to `#tag`; clicking it again when the filter is
  exactly that clears it.
- The toolbar ETA dropdown adds a preset on top of the filter text: ≤2m (GTD "do it now"),
  ≤15m, ≤1h, 1h–1d, >1d, has/no ETA. ETAs ≤ 2m are shown in green.
- Tag autocomplete (`TagCompleter`, fed by the shared `TagIndex`) on the filter box, the
  sidebar add box, the inline editor and the N / Shift+N dialog: it completes only the
  `#word` under the cursor. Enter/Tab on a highlighted suggestion only completes; QCompleter
  would otherwise also pass Enter to the line edit and submit the half-typed text.
- Selection is `ExtendedSelection` + `SelectRows`: click-drag, shift and ctrl all work.
- Shortcuts: Space toggle done · X done · P in progress · T todo · C cancelled ·
  D due date · Shift+D clear due · ~ set ETA (number + unit dialog) · # add tag (menu) · Ctrl+B sidebar · F2/double-click edit (multi-select →
  replace dialog) · Ctrl+H find/replace · N add task below · Shift+N add subtask · Del delete ·
  E open in `$EDITOR` · Ctrl+C copy as list · Ctrl+T insert template · Ctrl+3 templates tab · Ctrl+F or / filter (Esc clears, ↓ back to list) ·
  Alt+1..4 show todo/doing/done/cancelled · Ctrl+1/2/3 tabs · ←/→ collapse/expand ·
  Ctrl+. expand all · Ctrl+, collapse all groups (current tab) · Ctrl+Z / Ctrl+Shift+Z undo/redo ·
  Ctrl+O add directory · F5 reload.
  Single-key shortcuts are window-wide. `QLineEdit` eats printable keys via
  ShortcutOverride, so typing in the filter or the inline editor is safe. `~` and `#` are
  bound with and without Shift, because layouts differ.
- List templates: markdown files in `.templates/` of the first root that has one, else the
  first root (`templates::dirFor`). Hidden, so the scanner never shows them as tasks.
  "Save as template…" (context/Edit menu) saves the selected tasks + subtasks: reset to
  todo, all dates removed, ETAs/tags kept, name suggested from the first task. "Insert
  template ▸" (context/Edit menu, Ctrl+T pops it at the current row) lists them, plus "As
  subtasks of the selected task", "Edit ▸" (opens in `$EDITOR`) and "Open templates folder".
  Inserting goes where N would (after the selected task's subtree at its level, else the
  selected file, else `todo.md`), re-indented with the file's indent unit, every task todo
  and `+<today>` (the file's other dates dropped, except a hand-written `!due`). The inserted
  rows end up selected so D / ~ apply to all of them.
- Templates tab (third tab, Ctrl+3; "Manage templates…" in the template menu) is a task list
  like the Tasks tab: a second `TaskStore` (`m_templateStore`, rooted at the `.templates`
  dir via `syncTemplateRoots`, re-pointed on every load of the main store) with its own
  `TaskListModel` (`setTemplateHeaders`: header = name without `.md`, no progress pill) and
  pane (`m_templates`, Due column hidden). Every task action works there (N, Shift+N, F2,
  Del, X, ~, tags…) and edits are undoable: `Pane::store` picks the store for undo/redo and
  `storeForPath` routes `runEdits` by path, so a sidebar add while on that tab still goes to
  the tasks store. Only Save as template / Insert template (`m_tasksOnlyActions`) are off
  there. `TemplatesPage` is just the frame: New template… (writes the file with
  "- [ ] New task", then `m_editWhenLoaded` opens the inline editor on it once loaded),
  Rename…, Delete template (confirm), Open in $EDITOR, Insert into tasks (switches to Tasks
  and inserts like Ctrl+T) — all on the template of the current row; renames and deletes
  reach the list through the directory watcher. Template tasks never reach the sidebar,
  tag index or status counts (those use the main store only).
  `--screenshot --tab templates` shows it.
- Copy as list (Ctrl+C, context menu): selected tasks + subtasks as `- text` lines, indented
  per selected ancestor (`tasksAsBulletList`). Subtasks the filter hides are left out; tasks
  you explicitly selected never are.
- Open in editor: `$EDITOR`, then `$VISUAL`, else `QDesktopServices`. Terminal editors
  (vim/nvim/nano/hx/…) run inside `$TERMINAL`, or the first installed of kitty, alacritty,
  wezterm, foot, ghostty, konsole, gnome-terminal, xfce4-terminal, … The user is on i3 with
  `EDITOR=nvim` and has xfce4-terminal (`-x` to execute). If the app is started from a
  launcher that doesn't inherit the shell environment, `$EDITOR` may be missing.
- Settings live in `QSettings("todomd", "todomd")` (`~/.config/todomd/todomd.conf`):
  `directories` (list; migrated from the old `lastDirectory`), geometry, tab, state
  filters (done and cancelled hidden by default), `view/theme` (default `light`; also `soft`, `dark`, `system`,
  `clean`, `midnight`, `paper`; the user has `system` saved), `view/collapsed`, `view/agendaCollapsed`, `view/sidebar`,
  `view/splitter`, `agenda/emptyDays`.

## File watching

Files and directories are watched with `QFileSystemWatcher`. Changes are debounced
(120 ms) and re-parsed per file, and unchanged content is skipped by hash. After a load,
watches are added in slices of 256 per event-loop tick, because adding ~5k inotify
watches synchronously cost ~100 ms. Atomic saves drop the inotify watch, so changed
paths are re-added after each flush. F5 does a full rescan.

## Not done yet / ideas

- Reordering tasks (move up/down, drag between files).
- Priorities, recurring tasks (not in the syntax yet).
- ETA roll-up (sum of open subtasks' ETAs on the parent).
- Weekly or calendar layout for the agenda.
- Undo history is per session (in memory) only.

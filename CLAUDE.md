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
QT_QPA_PLATFORM=offscreen ./build/todomd --screenshot out.png [--tab agenda] [--theme soft] DIR...
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
- Releases: bump `project(todomd VERSION x.y.z)`, then `git tag vx.y.z && git push
  origin vx.y.z`. `.github/workflows/release.yml` refuses a tag that doesn't match,
  then builds on ubuntu-22.04 (older glibc) with Qt 6.8, runs ctest, packs an AppImage
  with linuxdeploy + its Qt plugin, and attaches it to a GitHub release.

Perf baseline (200k tasks / 5,000 files, generated corpus, every row visible):
scan+parse ≈ 50 ms, first paint ≈ 120 ms total (the list, the agenda with subtasks, and
the sidebar all build up front). A filter change takes 10–70 ms (the
free-text `contains` is the slowest). Re-run `--bench` after touching the load or list paths.

## Layout

| File | Role |
|---|---|
| `src/task.h` | `Task` (incl. `tags`), `TaskFile`, `TaskState`. Both derive from `Node{isFile}`. `Task::listRow` is owned by `TaskListModel`. |
| `src/parser.*` | Byte-level line parser and pure line transforms (`setState`, `setDue`, `setEta`, `setContent`, `parseEta`). |
| `src/edits.*` | `Edit` (range of lines: expected `before`, replacement `after`), `LineBuffer` (preserves CRLF / trailing newline), `applyEdits()`, which also returns the inverse edits for undo. |
| `src/editor.*` | Builds the command for "Open in editor" from `$EDITOR` (pure `editor::build`, unit tested). |
| `src/store.*` | `TaskStore`: several roots, async load (thread pool), `QFileSystemWatcher` sync, the **only** writer to disk, undo/redo. |
| `src/models.*` | `TaskFilter` (state / words / ETA / `due:` / `is:`), `TaskColors`, `FlatTaskModel` (tree as a flat list: groups, filtering, collapse) with `TaskListModel` (group = file) and `AgendaModel` (group = day; single-day mode for the sidebar). |
| `src/dialogs.*` | Due-date picker, find/replace, ETA picker (number + unit + presets). |
| `src/theme.*` | Light (default) / Soft (grey light) / Dark / System. Fusion style + palette. |
| `src/taskview.*` | `TaskDelegate` (draws checkbox, chevron, guides, header band, progress pill) and `TaskView` (chevron/checkbox/tag-chip clicks, Left/Right keys, optional header spans). |
| `src/sidebar.*` | `Sidebar`: stat tiles (click = filter), `TaskCalendar` (dots on due days), single-day `AgendaModel` list. |
| `src/mainwindow.*` | Toolbar, tabs, actions and shortcuts, bulk ops, selection/scroll preservation. |
| `tests/` | `test_core` (parser, edits, ETA, editor command), `test_store` (store + models under `QAbstractItemModelTester`, real temp dirs and watcher, multiple roots), `test_window` (drives shortcuts on the real window). |

CMake targets: `todocore` (parser, edits, editor; Qt Core only) → `todoapp` (store,
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
- A task with nowhere to go (N with nothing selected, or the sidebar add box with no day-list
  selection) is appended to `todo.md` in the first root that has one, else the first root
  (`defaultTaskFile`); the file is created if missing.
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
  except done-today sets the filter box to its term, and clicking again clears it. The
  active one is outlined. A tile at 0 isn't clickable (nothing to show), unless it's the
  active filter, so it can still be cleared.

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
- Tags column (list and agenda, hidden in the sidebar): chips drawn by `TaskDelegate`
  (`tagRects` is the shared paint/hit-test geometry; overflow shows `+N`, the tooltip lists
  all). Clicking a chip sets the filter to `#tag`; clicking it again when the filter is
  exactly that clears it. The
  toolbar ETA dropdown adds a preset on top: ≤2m (GTD "do it now"), ≤15m, ≤1h, 1h–1d,
  >1d, has/no ETA. ETAs ≤ 2m are shown in green.
- Selection is `ExtendedSelection` + `SelectRows`: click-drag, shift and ctrl all work.
- Shortcuts: Space toggle done · X done · P in progress · T todo · C cancelled ·
  D due date · Shift+D clear due · ~ set ETA (number + unit dialog) · Ctrl+B sidebar · F2/double-click edit (multi-select →
  replace dialog) · Ctrl+H find/replace · N add task below · Shift+N add subtask · Del delete ·
  E open in `$EDITOR` · Ctrl+C copy as list · Ctrl+F or / filter (Esc clears, ↓ back to list) ·
  Alt+1..4 show todo/doing/done/cancelled · Ctrl+1/2 tabs · ←/→ collapse/expand ·
  Ctrl+. expand all · Ctrl+, collapse all groups (current tab) · Ctrl+Z / Ctrl+Shift+Z undo/redo ·
  Ctrl+O add directory · F5 reload.
  Single-key shortcuts are window-wide. `QLineEdit` eats printable keys via
  ShortcutOverride, so typing in the filter or the inline editor is safe. `~` is bound
  with and without Shift, because layouts differ.
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
  filters (done and cancelled hidden by default), `view/theme` (default `light`; the user
  has `system` saved), `view/collapsed`, `view/agendaCollapsed`, `view/sidebar`,
  `view/splitter`, `agenda/emptyDays`.

## File watching

Files and directories are watched with `QFileSystemWatcher`. Changes are debounced
(120 ms) and re-parsed per file, and unchanged content is skipped by hash. After a load,
watches are added in slices of 256 per event-loop tick, because adding ~5k inotify
watches synchronously cost ~100 ms. Atomic saves drop the inotify watch, so changed
paths are re-added after each flush. F5 does a full rescan.

## Not done yet / ideas

- Reordering tasks (move up/down, drag between files).
- Priorities, recurring tasks (not in the syntax yet). Tag autocompletion, a tag list in the sidebar.
- ETA roll-up (sum of open subtasks' ETAs on the parent).
- Weekly or calendar layout for the agenda.
- Undo history is per session (in memory) only.

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
cmake -S . -B build -G Ninja          # Release by default
cmake --build build
./build/todomd                        # remembered directories
./build/todomd ~/notes ~/work         # also adds these to the remembered list
cd build && ctest --output-on-failure # tests run with QT_QPA_PLATFORM=offscreen

# Headless checks (only the given dirs; nothing is persisted):
QT_QPA_PLATFORM=offscreen ./build/todomd --bench DIR...
QT_QPA_PLATFORM=offscreen ./build/todomd --screenshot out.png [--tab agenda] DIR...
```

Perf baseline (200k tasks / 5,000 files, generated corpus, every row visible):
scan+parse ≈ 50 ms, first paint ≈ 85 ms total. A filter change takes 10–70 ms (the
free-text `contains` is the slowest). Re-run `--bench` after touching the load or list paths.

## Layout

| File | Role |
|---|---|
| `src/task.h` | `Task`, `TaskFile`, `TaskState`. Both derive from `Node{isFile}`. `Task::listRow` is owned by `TaskListModel`. |
| `src/parser.*` | Byte-level line parser and pure line transforms (`setState`, `setDue`, `setEta`, `setContent`, `parseEta`). |
| `src/edits.*` | `Edit` (range of lines: expected `before`, replacement `after`), `LineBuffer` (preserves CRLF / trailing newline), `applyEdits()`, which also returns the inverse edits for undo. |
| `src/editor.*` | Builds the command for "Open in editor" from `$EDITOR` (pure `editor::build`, unit tested). |
| `src/store.*` | `TaskStore`: several roots, async load (thread pool), `QFileSystemWatcher` sync, the **only** writer to disk, undo/redo. |
| `src/models.*` | `TaskFilter` (state / text / ETA), `TaskListModel` (flat list with built-in filtering), `AgendaModel` + `AgendaFilterProxy`. |
| `src/theme.*` | Dark (default) / Light / System. Fusion style + palette. |
| `src/mainwindow.*` | Toolbar, tabs, actions and shortcuts, bulk ops, selection/scroll preservation, `IndentDelegate`. |
| `src/dialogs.*` | Due-date picker, find/replace. |
| `tests/` | `test_core` (parser, edits, ETA, editor command), `test_store` (store + models under `QAbstractItemModelTester`, real temp dirs and watcher, multiple roots), `test_window` (drives shortcuts on the real window). |

CMake targets: `todocore` (parser, edits, editor; Qt Core only) → `todoapp` (store,
models, window) → `todomd` (just `main.cpp`). The tests link the libraries.

## Parsing rules (decisions not spelled out in the syntax doc)

- A task is `<ws>[-*+] [c] ` where c is one of ` /xX-`. The space after `]` is required.
- Date tokens are whitespace-separated words of exactly `K YYYY-MM-DD` with K in `! + x -`
  and a valid date.
- ETA tokens are `~<1-9 digits><m|h|d|w|y>`, stored in minutes (y = 365 d). `Task::etaText`
  keeps the token as written for display.
- Date and ETA tokens are stripped from the displayed text (`Task::text`), but stay in
  `Task::content`, which is what the free-text filter, inline editing and find/replace use.
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

- All writes go through `TaskStore::apply(edits, label)`. Each `Edit` carries the exact
  line(s) it expects. If the file changed on disk the edit is skipped and reported,
  never forced. The file is rewritten with `QSaveFile` (atomic) and re-parsed at once.
- Undo stores the inverse edits computed by `applyEdits`. At the same line number,
  line-consuming edits apply before insertions, and insertions keep list order. That
  rule is what makes undo of multi-line deletes and inserts exact (see `test_core`).
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

Why a flat list and not a tree: the user wants subtasks always visible, never
collapsible. A tree proxy plus `expandAll` cost ~140 ms at 200k rows, and the
collapse-by-default design looked "sporadic". `TaskListModel::rebuild` does one
reverse pass per file, so a matching task keeps its ancestors visible. It emits a
header row per file, then visible tasks with `DepthRole = depth + 1`. `IndentDelegate`
indents column 0 by depth and draws a faint guide line under each ancestor.

## UI behaviour

- **Bulk actions always include subtasks** (the user asked for no toggle).
  Marking a parent done/cancelled/etc. applies to all descendants, as do due date,
  ETA, replace and delete. Toggle-done decides done vs todo from the selected rows only.
- Agenda: open (todo / in progress) tasks with `!due`, one header row per day. Overdue in
  red, today in orange. "Show empty days" fills today → last due date (min 2 weeks, max 1 year).
- Filter box: space-separated terms, all must match the raw content or file path. Terms
  starting with `~` are ETA conditions: `~<=15m ~<1h ~>2h ~>=1d ~30m ~none ~any`. The
  toolbar ETA dropdown adds a preset on top: ≤2m (GTD "do it now"), ≤15m, ≤1h, 1h–1d,
  >1d, has/no ETA. ETAs ≤ 2m are shown in green.
- Selection is `ExtendedSelection` + `SelectRows`: click-drag, shift and ctrl all work.
- Shortcuts: Space toggle done · X done · P in progress · T todo · C cancelled ·
  D due date · Shift+D clear due · ~ set ETA · F2/double-click edit (multi-select →
  replace dialog) · Ctrl+H find/replace · N add task below · Shift+N add subtask · Del delete ·
  E open in `$EDITOR` · Ctrl+F or / filter (Esc clears, ↓ back to list) ·
  Alt+1..4 show todo/doing/done/cancelled · Ctrl+1/2 tabs · Ctrl+Z / Ctrl+Shift+Z undo/redo ·
  Ctrl+O add directory · F5 reload.
  Single-key shortcuts are window-wide. `QLineEdit` eats printable keys via
  ShortcutOverride, so typing in the filter or the inline editor is safe. `~` is bound
  with and without Shift, because layouts differ.
- Open in editor: `$EDITOR`, then `$VISUAL`, else `QDesktopServices`. Terminal editors
  (vim/nvim/nano/hx/…) run inside `$TERMINAL`, or the first installed of kitty, alacritty,
  wezterm, foot, ghostty, konsole, gnome-terminal, xfce4-terminal, … The user is on i3 with
  `EDITOR=nvim` and has xfce4-terminal (`-x` to execute). If the app is started from a
  launcher that doesn't inherit the shell environment, `$EDITOR` may be missing.
- Settings live in `QSettings("todomd", "todomd")` (`~/.config/todomd/todomd.conf`):
  `directories` (list; migrated from the old `lastDirectory`), geometry, tab, state
  filters (done and cancelled hidden by default), `view/theme`, `agenda/emptyDays`.

## File watching

Files and directories are watched with `QFileSystemWatcher`. Changes are debounced
(120 ms) and re-parsed per file, and unchanged content is skipped by hash. After a load,
watches are added in slices of 256 per event-loop tick, because adding ~5k inotify
watches synchronously cost ~100 ms. Atomic saves drop the inotify watch, so changed
paths are re-added after each flush. F5 does a full rescan.

## Not done yet / ideas

- Reordering tasks (move up/down, drag between files).
- Priorities, tags, recurring tasks (not in the syntax yet).
- ETA roll-up (sum of open subtasks' ETAs on the parent).
- Weekly or calendar layout for the agenda.
- Undo history is per session (in memory) only.

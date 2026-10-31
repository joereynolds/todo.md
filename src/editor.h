#pragma once

#include <QString>
#include <QStringList>

// Opening a task's file in the user's editor ($EDITOR, then $VISUAL).
// Terminal editors (vim, nvim, nano, ...) are run inside a terminal emulator:
// $TERMINAL if set, otherwise the first known one installed.
namespace editor {

struct Launch {
    QString program;
    QStringList args;
    bool isValid() const { return !program.isEmpty(); }
};

// Pure: `editorCmd` as in $EDITOR (may contain arguments), `terminal` the
// terminal program (empty if none found).
Launch build(const QString &editorCmd, const QString &terminal, const QString &file, int line);

bool isTerminalEditor(const QString &program);
QString findTerminal(); // $TERMINAL or the first known terminal on PATH
QString editorFromEnvironment(); // $EDITOR, else $VISUAL, else empty

} // namespace editor

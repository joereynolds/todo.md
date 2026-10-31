#include "editor.h"

#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>

namespace editor {

namespace {

QString baseName(const QString &program)
{
    return QFileInfo(program).fileName();
}

// Arguments that make `term` run a command (the command follows them).
QStringList execArgs(const QString &term)
{
    const QString t = baseName(term);
    if (t == QLatin1String("kitty") || t == QLatin1String("foot"))
        return {};
    if (t == QLatin1String("gnome-terminal") || t == QLatin1String("kgx"))
        return {QStringLiteral("--")};
    if (t == QLatin1String("wezterm"))
        return {QStringLiteral("start"), QStringLiteral("--")};
    if (t == QLatin1String("xfce4-terminal") || t == QLatin1String("terminator"))
        return {QStringLiteral("-x")};
    return {QStringLiteral("-e")}; // alacritty, konsole, xterm, urxvt, st, ghostty, ...
}

} // namespace

bool isTerminalEditor(const QString &program)
{
    static const QStringList known = {
        QStringLiteral("vi"),    QStringLiteral("vim"),   QStringLiteral("nvim"), QStringLiteral("nano"),
        QStringLiteral("micro"), QStringLiteral("hx"),    QStringLiteral("helix"), QStringLiteral("kak"),
        QStringLiteral("emacs"), QStringLiteral("ne"),    QStringLiteral("joe"),  QStringLiteral("mg"),
        QStringLiteral("ed"),    QStringLiteral("nvi"),   QStringLiteral("elvis"),
    };
    return known.contains(baseName(program));
}

Launch build(const QString &editorCmd, const QString &terminal, const QString &file, int line)
{
    QStringList cmd = QProcess::splitCommand(editorCmd);
    if (cmd.isEmpty())
        return {};
    const QString name = baseName(cmd.first());

    // Go to the line where the editor supports it.
    if (name == QLatin1String("hx") || name == QLatin1String("helix"))
        cmd << QStringLiteral("%1:%2").arg(file).arg(line);
    else if (isTerminalEditor(name) || name == QLatin1String("gvim") || name == QLatin1String("nvim-qt"))
        cmd << QStringLiteral("+%1").arg(line) << file;
    else if (name == QLatin1String("code") || name == QLatin1String("codium"))
        cmd << QStringLiteral("-g") << QStringLiteral("%1:%2").arg(file).arg(line);
    else
        cmd << file;

    if (isTerminalEditor(name)) {
        if (terminal.isEmpty())
            return {};
        Launch l{terminal, execArgs(terminal)};
        l.args += cmd;
        return l;
    }
    Launch l{cmd.takeFirst(), cmd};
    return l;
}

QString findTerminal()
{
    const QString env = qEnvironmentVariable("TERMINAL");
    if (!env.isEmpty())
        return env;
    static const char *candidates[] = {"kitty",     "alacritty",      "wezterm",        "foot",
                                       "ghostty",   "konsole",        "gnome-terminal", "xfce4-terminal",
                                       "terminator", "urxvt",         "st",             "xterm"};
    for (const char *c : candidates) {
        const QString path = QStandardPaths::findExecutable(QString::fromLatin1(c));
        if (!path.isEmpty())
            return path;
    }
    return {};
}

QString editorFromEnvironment()
{
    QString e = qEnvironmentVariable("EDITOR").trimmed();
    if (e.isEmpty())
        e = qEnvironmentVariable("VISUAL").trimmed();
    return e;
}

} // namespace editor

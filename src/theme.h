#pragma once

#include <QColor>
#include <QString>

// Application-wide colour theme. Light, Soft and Dark use the Fusion style so
// they look the same on every platform; System restores whatever was active
// at startup. Soft is a light theme with grey surfaces instead of white.
//
// Clean, Midnight and Paper are the "modern" looks: Fusion plus a palette and
// an application style sheet (flat chrome, rounded inputs, styled tabs, thin
// scroll bars), an accent colour and roomier rows.
namespace theme {

enum class Theme { System, Light, Soft, Dark, Clean, Midnight, Paper };

void init(); // call once, before any apply(), to remember the system look
void apply(Theme t);
Theme fromString(const QString &s); // unknown -> Light
QString toString(Theme t);

// Of the theme last applied: the accent (invalid for the classic themes, which
// keep the built-in blue) and the extra height added to each task row.
QColor accent();
int rowPadding();
bool cardLayout(); // tab pages drawn as a rounded card (needs QTabWidget::documentMode off)

} // namespace theme

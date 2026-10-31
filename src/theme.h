#pragma once

#include <QString>

// Application-wide colour theme. Light and Dark use the Fusion style so they
// look the same on every platform; System restores whatever was active at
// startup.
namespace theme {

enum class Theme { System, Light, Dark };

void init(); // call once, before any apply(), to remember the system look
void apply(Theme t);
Theme fromString(const QString &s); // unknown -> Dark
QString toString(Theme t);

} // namespace theme

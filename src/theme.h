#pragma once

#include <QString>

// Application-wide colour theme. Light, Soft and Dark use the Fusion style so
// they look the same on every platform; System restores whatever was active
// at startup. Soft is a light theme with grey surfaces instead of white.
namespace theme {

enum class Theme { System, Light, Soft, Dark };

void init(); // call once, before any apply(), to remember the system look
void apply(Theme t);
Theme fromString(const QString &s); // unknown -> Light
QString toString(Theme t);

} // namespace theme

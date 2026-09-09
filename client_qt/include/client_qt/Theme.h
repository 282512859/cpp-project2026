#pragma once

#include <QString>

namespace client_qt {

// Returns the full application stylesheet for the requested color theme.
// The two themes share the same widget structure and objectName conventions;
// only the color tokens differ. Prefer objectName / dynamic "state" properties
// over inline setStyleSheet calls so switching themes repaints everything.
QString themeStyleSheet(bool dark);

// Label shown on the theme toggle button: the theme the user will switch TO.
QString themeToggleLabel(bool dark);

} // namespace client_qt

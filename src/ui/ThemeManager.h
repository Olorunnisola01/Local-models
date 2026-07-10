#pragma once

#include <QApplication>

namespace ui {

enum class Theme { Light, Dark };

// Applies Fusion style + light/dark palette. Persists choice in QSettings.
class ThemeManager {
public:
    static Theme currentTheme();
    static void apply(Theme theme);
    static void applySaved();
    static void toggle();
};

} // namespace ui
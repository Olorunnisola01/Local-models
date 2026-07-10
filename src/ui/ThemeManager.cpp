#include "ThemeManager.h"

#include <QColor>
#include <QPalette>
#include <QStyle>
#include <QSettings>
#include <QStyleFactory>

namespace ui {

namespace {

constexpr const char* kOrg = "EdgeTTS-Studio";
constexpr const char* kApp = "EdgeTTS-Studio";
constexpr const char* kThemeKey = "theme";

QPalette darkPalette() {
    QPalette p;
    p.setColor(QPalette::Window, QColor(45, 45, 48));
    p.setColor(QPalette::WindowText, QColor(220, 220, 220));
    p.setColor(QPalette::Base, QColor(30, 30, 32));
    p.setColor(QPalette::AlternateBase, QColor(45, 45, 48));
    p.setColor(QPalette::ToolTipBase, QColor(50, 50, 54));
    p.setColor(QPalette::ToolTipText, QColor(220, 220, 220));
    p.setColor(QPalette::Text, QColor(220, 220, 220));
    p.setColor(QPalette::Button, QColor(55, 55, 58));
    p.setColor(QPalette::ButtonText, QColor(220, 220, 220));
    p.setColor(QPalette::BrightText, Qt::red);
    p.setColor(QPalette::Link, QColor(100, 160, 255));
    p.setColor(QPalette::Highlight, QColor(70, 120, 200));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(120, 120, 120));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(120, 120, 120));
    return p;
}

} // namespace

Theme ThemeManager::currentTheme() {
    QSettings settings(kOrg, kApp);
    return settings.value(kThemeKey, "dark").toString() == "light" ? Theme::Light : Theme::Dark;
}

void ThemeManager::apply(Theme theme) {
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    if (theme == Theme::Dark) {
        qApp->setPalette(darkPalette());
    } else {
        qApp->setPalette(QApplication::style()->standardPalette());
    }
    QSettings settings(kOrg, kApp);
    settings.setValue(kThemeKey, theme == Theme::Dark ? "dark" : "light");
}

void ThemeManager::applySaved() {
    apply(currentTheme());
}

void ThemeManager::toggle() {
    apply(currentTheme() == Theme::Dark ? Theme::Light : Theme::Dark);
}

} // namespace ui
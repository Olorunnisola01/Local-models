#include <QApplication>

#include "ui/MainWindow.h"
#include "ui/ThemeManager.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    ui::ThemeManager::applySaved();
    MainWindow window;
    window.show();
    return app.exec();
}
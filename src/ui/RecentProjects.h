#pragma once

#include <QString>
#include <QStringList>

class QMenu;

namespace ui {

// MRU list persisted in QSettings; populates a File submenu.
class RecentProjects {
public:
    static constexpr int kMaxEntries = 10;

    static QStringList list();
    static void add(const QString& path);
    static void remove(const QString& path);
    static void populateMenu(QMenu* menu, QObject* receiver, const char* slot);
};

} // namespace ui
#include "RecentProjects.h"

#include <QFileInfo>
#include <QMenu>
#include <QSettings>

namespace ui {

namespace {

constexpr const char* kOrg = "EdgeTTS-Studio";
constexpr const char* kApp = "EdgeTTS-Studio";
constexpr const char* kKey = "recentProjects";

QStringList readList() {
    QSettings settings(kOrg, kApp);
    return settings.value(kKey).toStringList();
}

void writeList(const QStringList& paths) {
    QSettings settings(kOrg, kApp);
    settings.setValue(kKey, paths);
}

} // namespace

QStringList RecentProjects::list() {
    QStringList out;
    for (const QString& p : readList()) {
        if (QFileInfo::exists(p)) {
            out.push_back(p);
        }
    }
    if (out.size() != readList().size()) {
        writeList(out);
    }
    return out;
}

void RecentProjects::add(const QString& path) {
    if (path.isEmpty()) {
        return;
    }
    QStringList items = readList();
    items.removeAll(path);
    items.prepend(path);
    while (items.size() > kMaxEntries) {
        items.removeLast();
    }
    writeList(items);
}

void RecentProjects::remove(const QString& path) {
    QStringList items = readList();
    items.removeAll(path);
    writeList(items);
}

void RecentProjects::populateMenu(QMenu* /*menu*/, QObject* /*receiver*/, const char* /*slot*/) {}

} // namespace ui
// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorZones/ScrollingTemplateStore.h>

#include <PhosphorZones/LayoutRegistry.h>

#include "zoneslogging.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <utility>

namespace PhosphorZones {

ScrollingTemplateStore::ScrollingTemplateStore(QObject* parent)
    : QObject(parent)
{
}

QString ScrollingTemplateStore::templateSubdirectory()
{
    return QStringLiteral("plasmazones/scrolling-templates");
}

void ScrollingTemplateStore::loadTemplates()
{
    // locateAll answers user first, system last; reverse so system loads
    // first and a user file sharing an id SHADOWS the bundled one — the
    // same precedence the layout loader uses.
    QStringList allDirs = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, templateSubdirectory(),
                                                    QStandardPaths::LocateDirectory);
    std::reverse(allDirs.begin(), allDirs.end());

    // Canonicalize the user directory ONCE, outside the loop. An empty
    // canonical path means the directory does not exist yet (no user template
    // has ever been saved); comparing against it would make every system
    // directory whose canonicalPath is also empty look like the user one.
    const QString userCanonical = QDir(userTemplateDirectory()).canonicalPath();
    QHash<QUuid, ScrollingTemplate> loaded;
    for (const QString& dirPath : allDirs) {
        const QDir dir(dirPath);
        const bool isUserDir = !userCanonical.isEmpty() && dir.canonicalPath() == userCanonical;
        const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
        for (const QString& fileName : files) {
            QFile file(dir.absoluteFilePath(fileName));
            if (!file.open(QIODevice::ReadOnly)) {
                qCWarning(lcZonesLib) << "ScrollingTemplateStore: cannot read" << file.fileName();
                continue;
            }
            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
            if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
                qCWarning(lcZonesLib) << "ScrollingTemplateStore: malformed template" << file.fileName() << ":"
                                      << parseError.errorString();
                continue;
            }
            ScrollingTemplate templ = ScrollingTemplate::fromJson(doc.object());
            if (!templ.isValid()) {
                qCWarning(lcZonesLib) << "ScrollingTemplateStore: refusing invalid template" << file.fileName();
                continue;
            }
            templ.isSystem = !isUserDir;
            templ.sourcePath = dir.absoluteFilePath(fileName);
            // Later directories override earlier ones by id (user shadows
            // system). Within one directory the name-sorted file order makes
            // any same-id collision deterministic.
            loaded.insert(templ.id, templ);
        }
    }

    if (loaded == m_templates) {
        return;
    }
    m_templates = std::move(loaded);
    qCInfo(lcZonesLib) << "ScrollingTemplateStore: loaded" << m_templates.size() << "templates";
    Q_EMIT templatesChanged();
}

QList<ScrollingTemplate> ScrollingTemplateStore::templates() const
{
    QList<ScrollingTemplate> out = m_templates.values();
    std::sort(out.begin(), out.end(), [](const ScrollingTemplate& a, const ScrollingTemplate& b) {
        const int byName = QString::compare(a.name, b.name, Qt::CaseInsensitive);
        if (byName != 0) {
            return byName < 0;
        }
        // Name ties break on id so the order is total and stable across
        // processes (both pickers render this list).
        return a.id < b.id;
    });
    return out;
}

ScrollingTemplate ScrollingTemplateStore::templateById(const QUuid& id) const
{
    return m_templates.value(id);
}

QUuid ScrollingTemplateStore::saveTemplate(ScrollingTemplate templ)
{
    if (templ.id.isNull()) {
        templ.id = QUuid::createUuid();
    }
    if (!templ.normalize()) {
        qCWarning(lcZonesLib) << "ScrollingTemplateStore: refusing to save invalid template" << templ.id;
        return QUuid();
    }
    // A save always produces a USER file, whatever the source of the
    // in-memory entry was — editing a bundled template shadows it.
    templ.isSystem = false;
    // A user template hand-placed under a name other than <id>.json would
    // otherwise DUPLICATE on save (the id-named file appears beside the
    // original, and the next rescan resolves the collision by name order).
    // Capture the old user-owned path so it can be retired after the write.
    // Canonicalize the user directory ONCE and require it to be non-empty, the
    // same guard removeTemplate uses: an empty canonical path means the user
    // directory does not exist yet, and comparing against it would match any
    // path whose own canonicalPath is also empty.
    const QString userCanonical = QDir(userTemplateDirectory()).canonicalPath();
    QString stalePath;
    const auto existing = m_templates.constFind(templ.id);
    if (existing != m_templates.constEnd() && !existing->isSystem && !existing->sourcePath.isEmpty()
        && !userCanonical.isEmpty() && existing->sourcePath != userTemplateFilePath(templ.id)
        && QFileInfo(existing->sourcePath).canonicalPath() == userCanonical) {
        stalePath = existing->sourcePath;
    }
    templ.sourcePath = userTemplateFilePath(templ.id);
    // Only emit when the value actually changed. The settings pages re-save on
    // every field commit, and an unchanged save otherwise rewrote the file and
    // fanned templatesChanged out to every picker and layout source.
    if (existing != m_templates.constEnd() && *existing == templ) {
        return templ.id;
    }
    if (!writeTemplateFile(templ)) {
        return QUuid();
    }
    if (!stalePath.isEmpty()) {
        // The pre-write compare was raw string inequality, which a symlink or a
        // non-normalized spelling can make wrong in the dangerous direction:
        // removing the file just written. Both paths exist now that the write
        // committed, so canonicalFilePath is well defined for each — retire the
        // old file only when it really is a different file.
        const QString staleCanonical = QFileInfo(stalePath).canonicalFilePath();
        const QString writtenCanonical = QFileInfo(templ.sourcePath).canonicalFilePath();
        if (!staleCanonical.isEmpty() && staleCanonical != writtenCanonical && !QFile::remove(stalePath)) {
            qCWarning(lcZonesLib) << "ScrollingTemplateStore: failed to retire stale file" << stalePath;
        }
    }
    m_templates.insert(templ.id, templ);
    Q_EMIT templatesChanged();
    return templ.id;
}

bool ScrollingTemplateStore::removeTemplate(const QUuid& id)
{
    const auto it = m_templates.constFind(id);
    if (it == m_templates.constEnd()) {
        return false;
    }
    if (it->isSystem) {
        // Pure system template: nothing user-owned to delete. The UI disables
        // delete for these; refusing here keeps the contract even for direct
        // D-Bus callers. Branching on the entry's own origin flag rather than
        // the user file's existence keeps the refusal aimed at bundled
        // templates only — a user template whose file vanished underneath us
        // is a missing file, not a bundled one.
        qCWarning(lcZonesLib) << "ScrollingTemplateStore: refusing to delete bundled template" << id;
        return false;
    }
    // Delete EVERY user file that can carry this id, not just one of them.
    // loadTemplates stamps sourcePath from the real filename and a hand-placed
    // user file need not be named <id>.json, so the entry's own path and the
    // id-derived path can be two different files both holding this id (the
    // name-sorted load order picks one; the other would survive the delete and
    // resurface on the next rescan). The canonical-directory compare keeps the
    // delete inside the user directory: a sourcePath pointing anywhere else is
    // never a target. An empty canonical user directory means the directory
    // does not exist at all, so nothing there can be user-owned.
    const QString userCanonical = QDir(userTemplateDirectory()).canonicalPath();
    const QString idFile = userTemplateFilePath(id);
    QStringList candidates;
    if (!it->sourcePath.isEmpty() && !userCanonical.isEmpty()
        && QFileInfo(it->sourcePath).canonicalPath() == userCanonical) {
        candidates.append(it->sourcePath);
    }
    if (!candidates.contains(idFile)) {
        // <id>.json is a guess at where this id lives, not a fact: file names
        // are free-form and loadTemplates keys each entry by the id INSIDE the
        // file, so <id-A>.json can perfectly well hold template B while A came
        // from foo.json. Deleting the guess blindly would take B's file with
        // it. Skip the append when a live entry other than this one owns that
        // file. A non-existent idFile canonicalizes to an empty string, which
        // matches no entry, so the ordinary case still appends.
        const QString idCanonical = QFileInfo(idFile).canonicalFilePath();
        bool ownedByOther = false;
        if (!idCanonical.isEmpty()) {
            for (auto other = m_templates.constBegin(); other != m_templates.constEnd(); ++other) {
                if (other->id == id || other->sourcePath.isEmpty()) {
                    continue;
                }
                if (QFileInfo(other->sourcePath).canonicalFilePath() == idCanonical) {
                    ownedByOther = true;
                    break;
                }
            }
        }
        if (!ownedByOther) {
            candidates.append(idFile);
        }
    }
    for (const QString& path : std::as_const(candidates)) {
        if (QFile::exists(path) && !QFile::remove(path)) {
            qCWarning(lcZonesLib) << "ScrollingTemplateStore: failed to delete" << path;
        }
    }
    // Rescan rather than erase: deleting a user file that shadowed a
    // bundled template must resurface the bundled original, and only the
    // directories know.
    loadTemplates();
    // Success is measured on disk, not on the remove calls: a file that was
    // already gone before the call (deleted out from under the process) leaves
    // the requested state holding, and refusing there would refuse forever on
    // a state refusing cannot repair. A file that survives a failed remove is
    // a real failure, and the warning above names it.
    for (const QString& path : std::as_const(candidates)) {
        if (QFile::exists(path)) {
            return false;
        }
    }
    return true;
}

QUuid ScrollingTemplateStore::duplicateTemplate(const QUuid& id, const QString& newName)
{
    ScrollingTemplate copy = templateById(id);
    if (!copy.isValid()) {
        return QUuid();
    }
    copy.id = QUuid::createUuid();
    // The layout duplicate flow's suffix, taken from its one authority so the
    // two flows cannot drift in the UI.
    copy.name = newName.isEmpty() ? copy.name + LayoutRegistry::duplicateNameSuffix() : newName;
    return saveTemplate(copy);
}

QString ScrollingTemplateStore::userTemplateDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QLatin1Char('/')
        + templateSubdirectory();
}

QString ScrollingTemplateStore::userTemplateFilePath(const QUuid& id) const
{
    // WithoutBraces: filesystem paths drop the braces by convention.
    return userTemplateDirectory() + QLatin1Char('/') + id.toString(QUuid::WithoutBraces) + QLatin1String(".json");
}

bool ScrollingTemplateStore::writeTemplateFile(const ScrollingTemplate& templ)
{
    const QString dirPath = userTemplateDirectory();
    if (!QDir().mkpath(dirPath)) {
        qCWarning(lcZonesLib) << "ScrollingTemplateStore: cannot create" << dirPath;
        return false;
    }
    QSaveFile file(userTemplateFilePath(templ.id));
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(lcZonesLib) << "ScrollingTemplateStore: cannot write" << file.fileName();
        return false;
    }
    file.write(QJsonDocument(templ.toJson()).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        qCWarning(lcZonesLib) << "ScrollingTemplateStore: write failed for" << file.fileName();
        return false;
    }
    return true;
}

} // namespace PhosphorZones

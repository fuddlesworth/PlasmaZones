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

namespace {

/// Every *.json file directly under @p dirPath whose stored template id is
/// @p id, excluding the file whose canonical path is @p keepCanonical.
///
/// loadTemplates keys entries by the id INSIDE the file and keeps exactly one
/// entry per id, so a second file declaring the same id is invisible to the
/// store: it holds no entry, no sourcePath records it, and the only thing it
/// can do is win the next rescan's name-ordered collision and revert whatever
/// the surviving file says. Finding those needs the ids off disk, because the
/// in-memory map cannot name a file it dropped.
/// An EMPTY @p keepCanonical answers an empty list rather than sweeping
/// everything: the kept file is written before this runs, so an empty canonical
/// path means the file cannot be identified, and without it the sweep would
/// delete the file it is meant to protect.
QStringList duplicateIdFiles(const QString& dirPath, const QUuid& id, const QString& keepCanonical)
{
    QStringList out;
    if (keepCanonical.isEmpty()) {
        return out;
    }
    const QDir dir(dirPath);
    const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QString& fileName : files) {
        const QString path = dir.absoluteFilePath(fileName);
        if (QFileInfo(path).canonicalFilePath() == keepCanonical) {
            continue;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isObject()) {
            continue;
        }
        if (QUuid::fromString(doc.object().value(TemplateJsonKeys::Id).toString()) == id) {
            out.append(path);
        }
    }
    return out;
}

} // namespace

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
    const auto existing = m_templates.constFind(templ.id);
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
    // Retire every OTHER user file carrying this id. A user template
    // hand-placed under a name other than <id>.json would otherwise DUPLICATE
    // on save (the id-named file appears beside the original, and the next
    // rescan resolves the collision by name order, which can silently revert
    // the edit that was just saved). The sweep is by file CONTENT rather than
    // by the entry's recorded sourcePath, because the store keeps one entry per
    // id and therefore cannot name a second file holding the same id. It runs
    // AFTER the write commits, so the kept file exists and canonicalFilePath is
    // well defined for it — comparing canonical paths keeps a symlink or a
    // non-normalized spelling from making the removal wrong in the dangerous
    // direction. Scoped to the user directory: a system file is never removed,
    // and a shadowed bundled template still resurfaces on delete.
    const QString writtenCanonical = QFileInfo(templ.sourcePath).canonicalFilePath();
    for (const QString& stalePath : duplicateIdFiles(userTemplateDirectory(), templ.id, writtenCanonical)) {
        if (!QFile::remove(stalePath)) {
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
    if (candidates.isEmpty()) {
        // No user file this delete could target: the entry's own path lies
        // outside the user directory (or was never recorded) and <id>.json is
        // owned by a different live entry. Nothing was deleted, so reporting
        // success would tell the caller a template it can still see is gone.
        // The rescan below is deliberately skipped too — the directory was
        // not touched, so re-reading it could only churn.
        qCWarning(lcZonesLib) << "ScrollingTemplateStore: no user file to delete for template" << id;
        return false;
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

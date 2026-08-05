// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorZones/ScrollingTemplateStore.h>

#include "zoneslogging.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace PhosphorZones {

namespace {
QLatin1String templateSubdirectory()
{
    return QLatin1String("plasmazones/scrolling-templates");
}
} // namespace

ScrollingTemplateStore::ScrollingTemplateStore(QObject* parent)
    : QObject(parent)
{
}

void ScrollingTemplateStore::loadTemplates()
{
    // locateAll answers user first, system last; reverse so system loads
    // first and a user file sharing an id SHADOWS the bundled one — the
    // same precedence the layout loader uses.
    QStringList allDirs = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, templateSubdirectory(),
                                                    QStandardPaths::LocateDirectory);
    std::reverse(allDirs.begin(), allDirs.end());

    const QString userDir = userTemplateDirectory();
    QHash<QUuid, ScrollingTemplate> loaded;
    for (const QString& dirPath : allDirs) {
        const QDir dir(dirPath);
        const bool isUserDir = (QDir(dirPath).canonicalPath() == QDir(userDir).canonicalPath());
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
    if (!writeTemplateFile(templ)) {
        return QUuid();
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
    const QString userFile = userTemplateFilePath(id);
    if (!QFile::exists(userFile)) {
        // Pure system template: nothing user-owned to delete. The UI
        // disables delete for these; refusing here keeps the contract even
        // for direct D-Bus callers.
        qCWarning(lcZonesLib) << "ScrollingTemplateStore: refusing to delete bundled template" << id;
        return false;
    }
    if (!QFile::remove(userFile)) {
        qCWarning(lcZonesLib) << "ScrollingTemplateStore: failed to delete" << userFile;
        return false;
    }
    // Rescan rather than erase: deleting a user file that shadowed a
    // bundled template must resurface the bundled original, and only the
    // directories know. loadTemplates emits templatesChanged (the set
    // necessarily differs — either the entry vanished or its isSystem
    // origin flipped back).
    loadTemplates();
    return true;
}

QUuid ScrollingTemplateStore::duplicateTemplate(const QUuid& id, const QString& newName)
{
    ScrollingTemplate copy = templateById(id);
    if (!copy.isValid()) {
        return QUuid();
    }
    copy.id = QUuid::createUuid();
    // Same literal suffix as LayoutRegistry::duplicateNameSuffix so the two
    // duplicate flows read identically in the UI.
    copy.name = newName.isEmpty() ? copy.name + QStringLiteral(" (Copy)") : newName;
    return saveTemplate(copy);
}

QString ScrollingTemplateStore::userTemplateDirectory() const
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

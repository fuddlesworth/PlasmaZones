// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Layout file I/O: loading, saving, importing, exporting.
// Part of LayoutManager — split from layoutmanager.cpp for SRP.

#include "../layoutmanager.h"
#include "../constants.h"
#include "../logging.h"
#include "../utils.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <KConfigGroup>
#include <KSharedConfig>
#include <algorithm>

namespace PlasmaZones {

void LayoutManager::loadLayouts()
{
    ensureLayoutDirectory();

    // Load from ALL data locations (system directories first, then user)
    // locateAll() returns paths in priority order: user first, system last
    // We reverse to load system first, so user layouts can override
    QStringList allDirs = QStandardPaths::locateAll(
        QStandardPaths::GenericDataLocation, QStringLiteral("plasmazones/layouts"), QStandardPaths::LocateDirectory);

    // Reverse order: load system layouts first, user layouts last (override)
    std::reverse(allDirs.begin(), allDirs.end());

    for (const QString& dir : allDirs) {
        int beforeCount = m_layouts.size();
        loadLayoutsFromDirectory(dir);
        qCInfo(lcLayout) << "Loaded layouts=" << (m_layouts.size() - beforeCount) << "from=" << dir;
    }

    qCInfo(lcLayout) << "Total layouts=" << m_layouts.size();

    // Sort by defaultOrder (from layout JSON) so the preferred default is first when defaultLayoutId is empty
    std::stable_sort(m_layouts.begin(), m_layouts.end(), [](Layout* a, Layout* b) {
        return (a ? a->defaultOrder() : 999) < (b ? b->defaultOrder() : 999);
    });

    // Set initial active layout if none set: use defaultLayout() (settings-based fallback)
    if (!m_activeLayout && !m_layouts.isEmpty()) {
        Layout* initial = defaultLayout();
        if (initial) {
            qCInfo(lcLayout) << "Active layout name=" << initial->name() << "id=" << initial->id().toString()
                             << "zones=" << initial->zoneCount();
        }
        setActiveLayout(initial);
    }

    Q_EMIT layoutsLoaded();
    Q_EMIT layoutsChanged();
}

void LayoutManager::loadLayoutsFromDirectory(const QString& directory)
{
    QDir dir(directory);
    if (!dir.exists()) {
        qCWarning(lcLayout) << "Layout directory does not exist:" << directory;
        return;
    }

    if (!dir.isReadable()) {
        qCWarning(lcLayout) << "Layout directory is not readable:" << directory;
        return;
    }

    const auto entries = dir.entryList({QStringLiteral("*.json")}, QDir::Files);

    for (const auto& entry : entries) {
        if (entry == QStringLiteral("assignments.json") || entry == QStringLiteral("autotile-overrides.json")) {
            continue; // Skip non-layout files
        }

        const QString filePath = dir.absoluteFilePath(entry);
        QFile file(filePath);

        if (!file.exists()) {
            qCWarning(lcLayout) << "Layout file does not exist:" << filePath;
            continue;
        }

        if (!file.open(QIODevice::ReadOnly)) {
            qCWarning(lcLayout) << "Failed to open layout file:" << filePath << "Error:" << file.errorString();
            continue;
        }

        const QByteArray data = file.readAll();
        if (data.isEmpty()) {
            qCWarning(lcLayout) << "Layout file is empty:" << filePath;
            continue;
        }

        QJsonParseError parseError;
        const auto doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            qCWarning(lcLayout) << "Failed to parse layout file:" << filePath << "Error:" << parseError.errorString()
                                << "at offset" << parseError.offset;
            continue;
        }

        auto layout = Layout::fromJson(doc.object(), this);
        if (!layout) {
            qCWarning(lcLayout) << "Failed to create layout from JSON:" << filePath;
            continue;
        }

        // Set source path - this determines if layout is system or user layout
        // isSystemLayout() checks if sourcePath is outside user's writable directory
        layout->setSourcePath(filePath);

        if (!layout->name().isEmpty() && layout->zoneCount() > 0) {
            // Check for duplicate IDs
            Layout* existing = layoutById(layout->id());
            if (!existing) {
                // No duplicate - add the layout
                m_layouts.append(layout);
                connect(layout, &Layout::layoutModified, this, [this, layout]() {
                    saveLayout(layout);
                });
                qCInfo(lcLayout) << "Loaded layout name=" << layout->name() << "zones=" << layout->zoneCount()
                                 << "source=" << (layout->isSystemLayout() ? "system" : "user") << "from=" << filePath;
            } else {
                // Duplicate ID found - user layouts (from .local) should override system layouts
                if (!layout->isSystemLayout() && existing->isSystemLayout()) {
                    // User layout overrides system layout - replace the existing one
                    if (layout->systemSourcePath().isEmpty()) {
                        layout->setSystemSourcePath(existing->sourcePath());
                    }
                    // Preserve defaultOrder from system layout when user copy doesn't have one
                    if (layout->defaultOrder() == 999 && existing->defaultOrder() != 999) {
                        layout->setDefaultOrder(existing->defaultOrder());
                    }
                    int index = m_layouts.indexOf(existing);
                    disconnect(existing, &Layout::layoutModified, this, nullptr);
                    m_layouts.replace(index, layout);
                    connect(layout, &Layout::layoutModified, this, [this, layout]() {
                        saveLayout(layout);
                    });
                    delete existing;
                    qCInfo(lcLayout) << "User layout overrides system layout name=" << layout->name()
                                     << "from=" << filePath;
                } else {
                    // Same source type or system trying to override user - skip
                    qCInfo(lcLayout) << "Skipping duplicate layout name=" << layout->name() << "id=" << layout->id();
                    delete layout;
                }
            }
        } else {
            qCWarning(lcLayout) << "Skipping invalid layout entry=" << entry << "reason=empty name or no zones";
            // Clean up orphaned file from user directory (don't delete system layouts)
            if (!layout->isSystemLayout()) {
                QFile::remove(filePath);
                qCInfo(lcLayout) << "Removed orphaned layout file:" << filePath;
            }
            delete layout;
        }
    }
}

void LayoutManager::saveLayout(Layout* layout)
{
    if (!layout || !layout->isDirty()) {
        return;
    }

    // Don't persist invalid layouts (empty name or no zones) — they would be
    // skipped on reload anyway, creating orphaned files on disk.
    if (layout->name().isEmpty() || layout->zoneCount() == 0) {
        qCDebug(lcLayout) << "saveLayout: skipping invalid layout (name empty or no zones)" << layout->id();
        return;
    }

    ensureLayoutDirectory();

    // If this is a system layout being saved to user dir for the first time,
    // capture the system origin path before toJson/sourcePath changes
    if (layout->isSystemLayout() && !layout->hasSystemOrigin()) {
        layout->setSystemSourcePath(layout->sourcePath());
        qCInfo(lcLayout) << "Captured system origin for" << layout->name() << "from=" << layout->sourcePath();
    }

    const QString filePath = layoutFilePath(layout->id());
    QFile file(filePath);

    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(lcLayout) << "Failed to open layout file for writing:" << filePath << "Error:" << file.errorString();
        return;
    }

    // toJson() includes systemSourcePath so it persists across daemon restarts
    QJsonDocument doc(layout->toJson());
    const QByteArray data = doc.toJson(QJsonDocument::Indented);

    if (file.write(data) != data.size()) {
        qCWarning(lcLayout) << "Failed to write layout file:" << filePath << "Error:" << file.errorString();
        return;
    }

    if (!file.flush()) {
        qCWarning(lcLayout) << "Failed to flush layout file:" << filePath << "Error:" << file.errorString();
        return;
    }

    // Update sourcePath so isSystemLayout() returns correctly after saving
    layout->setSourcePath(filePath);
    layout->clearDirty();
}

void LayoutManager::saveLayouts()
{
    for (auto* layout : m_layouts) {
        saveLayout(layout);
    }

    // Not emitting layoutsChanged() — disk persistence doesn't change the layout list
    Q_EMIT layoutsSaved();
}

void LayoutManager::loadAssignments()
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    config->reparseConfiguration();
    const QStringList allGroups = config->groupList();

    // ── Primary path: read from [Assignment:*] KConfig groups ──────────────
    const QLatin1String assignmentPrefix("Assignment:");
    bool foundKConfigGroups = false;

    for (const QString& groupName : allGroups) {
        if (!groupName.startsWith(assignmentPrefix))
            continue;

        foundKConfigGroups = true;
        // Parse group name: Assignment:screenId[:Desktop:N][:Activity:id]
        QString remainder = groupName.mid(assignmentPrefix.size());
        if (remainder.isEmpty())
            continue;

        QString screenId;
        int virtualDesktop = 0;
        QString activity;

        // Extract :Activity:id suffix first (activity IDs contain colons in UUIDs)
        int actIdx = remainder.indexOf(QLatin1String(":Activity:"));
        if (actIdx >= 0) {
            activity = remainder.mid(actIdx + 10); // len(":Activity:") = 10
            remainder = remainder.left(actIdx);
        }

        // Extract :Desktop:N suffix
        int deskIdx = remainder.indexOf(QLatin1String(":Desktop:"));
        if (deskIdx >= 0) {
            bool ok = false;
            virtualDesktop = remainder.mid(deskIdx + 9).toInt(&ok); // len(":Desktop:") = 9
            if (!ok)
                virtualDesktop = 0;
            remainder = remainder.left(deskIdx);
        }

        screenId = remainder;
        if (screenId.isEmpty())
            continue;

        KConfigGroup grp = config->group(groupName);
        AssignmentEntry entry;
        int modeInt = grp.readEntry(QStringLiteral("Mode"), 0);
        entry.mode = (modeInt == AssignmentEntry::Autotile) ? AssignmentEntry::Autotile : AssignmentEntry::Snapping;
        entry.snappingLayout = grp.readEntry(QStringLiteral("SnappingLayout"), QString());
        entry.tilingAlgorithm = grp.readEntry(QStringLiteral("TilingAlgorithm"), QString());

        if (entry.isValid()) {
            LayoutAssignmentKey key{screenId, virtualDesktop, activity};
            m_assignments[key] = entry;
        }
    }

    // ── Quick layout shortcuts from [QuickLayouts] group ───────────────────
    {
        KConfigGroup quickGroup = config->group(QStringLiteral("QuickLayouts"));
        if (quickGroup.exists()) {
            const QStringList keys = quickGroup.keyList();
            for (const QString& key : keys) {
                bool ok = false;
                int number = key.toInt(&ok);
                if (!ok)
                    continue;
                QString layoutId = quickGroup.readEntry(key, QString());
                if (!layoutId.isEmpty())
                    m_quickLayoutShortcuts[number] = layoutId;
            }
        }
    }

    // ── Migration fallback: read from assignments.json (one-time) ──────────
    if (!foundKConfigGroups) {
        const QString filePath = m_layoutDirectory + QStringLiteral("/assignments.json");
        QFile file(filePath);

        if (file.exists() && file.open(QIODevice::ReadOnly)) {
            const QByteArray data = file.readAll();
            if (!data.isEmpty()) {
                QJsonParseError parseError;
                const auto doc = QJsonDocument::fromJson(data, &parseError);
                if (parseError.error == QJsonParseError::NoError) {
                    const auto root = doc.object();
                    qCInfo(lcLayout) << "Migrating assignments from JSON to KConfig";

                    // Load shadowed assignments from old format
                    QHash<LayoutAssignmentKey, QString> oldShadows;
                    const auto shadowArray = root[QStringLiteral("shadowedAssignments")].toArray();
                    for (const auto& value : shadowArray) {
                        if (!value.isObject())
                            continue;
                        const auto obj = value.toObject();
                        QString sid = obj[JsonKeys::ScreenId].toString();
                        if (sid.isEmpty())
                            sid = obj[JsonKeys::Screen].toString();
                        LayoutAssignmentKey key{sid, obj[JsonKeys::Desktop].toInt(),
                                                obj[JsonKeys::Activity].toString()};
                        const QString layoutIdStr = obj[JsonKeys::LayoutId].toString();
                        if (!layoutIdStr.isEmpty())
                            oldShadows[key] = layoutIdStr;
                    }

                    // Load assignments
                    const auto assignmentsArray = root[JsonKeys::Assignments].toArray();
                    for (const auto& value : assignmentsArray) {
                        if (!value.isObject())
                            continue;
                        const auto obj = value.toObject();
                        QString sid = obj[JsonKeys::ScreenId].toString();
                        if (sid.isEmpty())
                            sid = obj[JsonKeys::Screen].toString();
                        LayoutAssignmentKey key{sid, obj[JsonKeys::Desktop].toInt(),
                                                obj[JsonKeys::Activity].toString()};

                        AssignmentEntry entry;
                        if (obj.contains(QStringLiteral("mode"))) {
                            // Already in new explicit format
                            int modeInt = obj[QStringLiteral("mode")].toInt();
                            entry.mode = (modeInt == AssignmentEntry::Autotile) ? AssignmentEntry::Autotile
                                                                                : AssignmentEntry::Snapping;
                            entry.snappingLayout = obj[QStringLiteral("snappingLayout")].toString();
                            entry.tilingAlgorithm = obj[QStringLiteral("tilingAlgorithm")].toString();
                        } else {
                            // Old single layoutId format
                            const QString layoutIdStr = obj[JsonKeys::LayoutId].toString();
                            QString normalizedId = layoutIdStr;
                            if (!LayoutId::isAutotile(layoutIdStr)) {
                                const QUuid uuid = QUuid::fromString(layoutIdStr);
                                if (uuid.isNull())
                                    continue;
                                normalizedId = uuid.toString();
                            }
                            entry = AssignmentEntry::fromLayoutId(normalizedId);

                            // Merge shadow into opposite field
                            auto shadowIt = oldShadows.constFind(key);
                            if (shadowIt != oldShadows.constEnd()) {
                                if (entry.mode == AssignmentEntry::Autotile
                                    && !LayoutId::isAutotile(shadowIt.value())) {
                                    entry.snappingLayout = shadowIt.value();
                                } else if (entry.mode == AssignmentEntry::Snapping
                                           && LayoutId::isAutotile(shadowIt.value())) {
                                    entry.tilingAlgorithm = LayoutId::extractAlgorithmId(shadowIt.value());
                                }
                            }
                        }

                        if (entry.isValid())
                            m_assignments[key] = entry;
                    }

                    // Load quick shortcuts from JSON (migration)
                    if (m_quickLayoutShortcuts.isEmpty()) {
                        const auto shortcutsObj = root[JsonKeys::QuickShortcuts].toObject();
                        for (auto it = shortcutsObj.begin(); it != shortcutsObj.end(); ++it) {
                            bool ok = false;
                            const int number = it.key().toInt(&ok);
                            if (!ok)
                                continue;
                            const QString layoutIdStr = it.value().toString();
                            if (LayoutId::isAutotile(layoutIdStr)) {
                                m_quickLayoutShortcuts[number] = layoutIdStr;
                            } else {
                                const QUuid uuid = QUuid::fromString(layoutIdStr);
                                if (!uuid.isNull())
                                    m_quickLayoutShortcuts[number] = uuid.toString();
                            }
                        }
                    }

                    // Migrate activity-keyed per-desktop entries
                    {
                        QList<LayoutAssignmentKey> toRemove;
                        QHash<LayoutAssignmentKey, AssignmentEntry> toAdd;
                        for (auto it = m_assignments.constBegin(); it != m_assignments.constEnd(); ++it) {
                            const LayoutAssignmentKey& key = it.key();
                            if (key.virtualDesktop > 0 && !key.activity.isEmpty()) {
                                LayoutAssignmentKey emptyActivityKey{key.screenId, key.virtualDesktop, QString()};
                                if (!m_assignments.contains(emptyActivityKey) && !toAdd.contains(emptyActivityKey)) {
                                    toAdd[emptyActivityKey] = it.value();
                                }
                                toRemove.append(key);
                            }
                        }
                        for (const auto& key : toRemove)
                            m_assignments.remove(key);
                        for (auto it = toAdd.constBegin(); it != toAdd.constEnd(); ++it)
                            m_assignments[it.key()] = it.value();
                        if (!toRemove.isEmpty()) {
                            qCInfo(lcLayout) << "Migrated" << toRemove.size()
                                             << "activity-keyed per-desktop assignments to empty-activity";
                        }
                    }

                    // Write migrated data to KConfig immediately
                    saveAssignments();
                    qCInfo(lcLayout) << "Migration from assignments.json complete";

                    // Rename old file so it isn't re-read on next startup
                    file.close();
                    QFile::rename(filePath, filePath + QStringLiteral(".migrated"));
                }
            }
        }
    }

    // ── Migrate [ModeTracking] into base screen entries ──────────────────────
    // Old ModeTracker stored a global LastTilingMode, LastManualLayoutId, and
    // LastAutotileAlgorithm. If base screen entries exist but lack the opposite
    // field (e.g. mode=Autotile but snappingLayout is empty), fill from legacy.
    {
        KConfigGroup modeGroup = config->group(QStringLiteral("ModeTracking"));
        if (modeGroup.exists()) {
            const QString lastManualId = modeGroup.readEntry(QStringLiteral("LastManualLayoutId"), QString());
            const QString lastAlgorithm = modeGroup.readEntry(QStringLiteral("LastAutotileAlgorithm"), QString());
            bool migrated = false;

            for (auto it = m_assignments.begin(); it != m_assignments.end(); ++it) {
                if (it.key().virtualDesktop != 0 || !it.key().activity.isEmpty())
                    continue; // Only base screen entries
                AssignmentEntry& entry = it.value();
                if (entry.snappingLayout.isEmpty() && !lastManualId.isEmpty()) {
                    entry.snappingLayout = lastManualId;
                    migrated = true;
                }
                if (entry.tilingAlgorithm.isEmpty() && !lastAlgorithm.isEmpty()) {
                    entry.tilingAlgorithm = lastAlgorithm;
                    migrated = true;
                }
            }

            // Delete the old group
            modeGroup.deleteGroup();
            config->sync();

            if (migrated) {
                saveAssignments();
                qCInfo(lcLayout) << "Migrated [ModeTracking] into base screen entries";
            } else {
                qCInfo(lcLayout) << "Cleaned up obsolete [ModeTracking] group";
            }
        }
    }

    // ── Clean up other obsolete KConfig groups ──────────────────────────────
    {
        bool cleaned = false;
        const QStringList currentGroups = config->groupList();
        for (const QString& groupName : currentGroups) {
            if (groupName.startsWith(QLatin1String("TilingScreen:"))
                || groupName.startsWith(QLatin1String("TilingActivity:"))
                || groupName.startsWith(QLatin1String("TilingDesktop:"))) {
                config->deleteGroup(groupName);
                cleaned = true;
            }
        }
        if (cleaned) {
            config->sync();
            qCInfo(lcLayout) << "Cleaned up obsolete TilingScreen/TilingActivity/TilingDesktop KConfig groups";
        }
    }

    qCInfo(lcLayout) << "Loaded assignments=" << m_assignments.size()
                     << "quickShortcuts=" << m_quickLayoutShortcuts.size();
    for (auto it = m_assignments.constBegin(); it != m_assignments.constEnd(); ++it) {
        const AssignmentEntry& entry = it.value();
        qCDebug(lcLayout) << "Assignment screenId=" << it.key().screenId << "desktop=" << it.key().virtualDesktop
                          << "activity=" << (it.key().activity.isEmpty() ? QStringLiteral("(all)") : it.key().activity)
                          << "mode=" << static_cast<int>(entry.mode) << "snapping=" << entry.snappingLayout
                          << "tiling=" << entry.tilingAlgorithm;
    }
}

void LayoutManager::saveAssignments()
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));

    // Delete old Assignment:* groups
    const QStringList allGroups = config->groupList();
    for (const QString& groupName : allGroups) {
        if (groupName.startsWith(QLatin1String("Assignment:"))) {
            config->deleteGroup(groupName);
        }
    }

    // Write [Assignment:*] groups
    for (auto it = m_assignments.constBegin(); it != m_assignments.constEnd(); ++it) {
        const LayoutAssignmentKey& key = it.key();
        const AssignmentEntry& entry = it.value();

        // Build group name: Assignment:screenId[:Desktop:N][:Activity:id]
        QString groupName = QStringLiteral("Assignment:") + key.screenId;
        if (key.virtualDesktop > 0) {
            groupName += QStringLiteral(":Desktop:") + QString::number(key.virtualDesktop);
        }
        if (!key.activity.isEmpty()) {
            groupName += QStringLiteral(":Activity:") + key.activity;
        }

        KConfigGroup group = config->group(groupName);
        group.writeEntry(QStringLiteral("Mode"), static_cast<int>(entry.mode));
        group.writeEntry(QStringLiteral("SnappingLayout"), entry.snappingLayout);
        group.writeEntry(QStringLiteral("TilingAlgorithm"), entry.tilingAlgorithm);
    }

    // Write [QuickLayouts] group
    {
        KConfigGroup quickGroup = config->group(QStringLiteral("QuickLayouts"));
        quickGroup.deleteGroup();
        for (auto it = m_quickLayoutShortcuts.constBegin(); it != m_quickLayoutShortcuts.constEnd(); ++it) {
            quickGroup.writeEntry(QString::number(it.key()), it.value());
        }
    }

    config->sync();
    qCInfo(lcLayout) << "Saved assignments=" << m_assignments.size()
                     << "quickShortcuts=" << m_quickLayoutShortcuts.size() << "to KConfig";
}

void LayoutManager::importLayout(const QString& filePath)
{
    if (filePath.isEmpty()) {
        qCWarning(lcLayout) << "Cannot import layout: file path is empty";
        return;
    }

    QFile file(filePath);
    if (!file.exists()) {
        qCWarning(lcLayout) << "Cannot import layout: file does not exist:" << filePath;
        return;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcLayout) << "Failed to open layout file for import:" << filePath << "Error:" << file.errorString();
        return;
    }

    const QByteArray data = file.readAll();
    if (data.isEmpty()) {
        qCWarning(lcLayout) << "Cannot import layout: file is empty:" << filePath;
        return;
    }

    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(lcLayout) << "Failed to parse layout file for import:" << filePath
                            << "Error:" << parseError.errorString() << "at offset" << parseError.offset;
        return;
    }

    auto* parsed = Layout::fromJson(doc.object(), this);
    if (!parsed) {
        qCWarning(lcLayout) << "Failed to create layout from imported JSON:" << filePath;
        return;
    }

    // Regenerate IDs if UUID collides with an existing layout
    Layout* layout = parsed;
    if (layoutById(parsed->id())) {
        qCInfo(lcLayout) << "importLayout: UUID collision, regenerating IDs";
        layout = new Layout(*parsed);
        delete parsed;
    }

    // Reset visibility restrictions since screen/desktop/activity names are machine-specific
    layout->setHiddenFromSelector(false);
    layout->setAllowedScreens({});
    layout->setAllowedDesktops({});
    layout->setAllowedActivities({});

    addLayout(layout);

    qCInfo(lcLayout) << "Imported layout:" << layout->name() << "from" << filePath;
}

void LayoutManager::exportLayout(Layout* layout, const QString& filePath)
{
    if (!layout) {
        qCWarning(lcLayout) << "Cannot export layout: layout is null";
        return;
    }

    if (filePath.isEmpty()) {
        qCWarning(lcLayout) << "Cannot export layout: file path is empty";
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(lcLayout) << "Failed to open file for layout export:" << filePath << "Error:" << file.errorString();
        return;
    }

    QJsonDocument doc(layout->toJson());
    const QByteArray data = doc.toJson(QJsonDocument::Indented);

    if (file.write(data) != data.size()) {
        qCWarning(lcLayout) << "Failed to write layout to file:" << filePath << "Error:" << file.errorString();
        return;
    }

    if (!file.flush()) {
        qCWarning(lcLayout) << "Failed to flush layout export file:" << filePath << "Error:" << file.errorString();
    }

    qCInfo(lcLayout) << "Exported layout:" << layout->name() << "to" << filePath;
}

Layout* LayoutManager::restoreSystemLayout(const QUuid& id, const QString& systemPath)
{
    if (systemPath.isEmpty() || layoutById(id)) {
        return nullptr;
    }

    QFile file(systemPath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        qCWarning(lcLayout) << "System layout file missing:" << systemPath;
        return nullptr;
    }

    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(lcLayout) << "System layout parse error:" << systemPath << parseError.errorString();
        return nullptr;
    }

    auto* layout = Layout::fromJson(doc.object(), this);
    if (!layout || layout->id() != id) {
        delete layout;
        return nullptr;
    }

    layout->setSourcePath(systemPath);
    m_layouts.append(layout);
    connect(layout, &Layout::layoutModified, this, [this, layout]() {
        saveLayout(layout);
    });
    Q_EMIT layoutAdded(layout);
    qCInfo(lcLayout) << "Restored system layout name=" << layout->name() << "from=" << systemPath;
    return layout;
}

void LayoutManager::ensureLayoutDirectory()
{
    QDir dir(m_layoutDirectory);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
}

QString LayoutManager::layoutFilePath(const QUuid& id) const
{
    // Strip braces only for filesystem path (avoid { } in filenames). Everywhere else we use default (with braces).
    return m_layoutDirectory + QStringLiteral("/") + id.toString(QUuid::WithoutBraces) + QStringLiteral(".json");
}

} // namespace PlasmaZones

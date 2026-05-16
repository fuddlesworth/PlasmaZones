// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Layout file I/O + assignment persistence.
// Part of LayoutRegistry — split from layoutregistry.cpp for SRP.

#include <PhosphorZones/LayoutRegistry.h>

#include "zoneslogging.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <algorithm>

namespace PhosphorZones {

namespace {

// Wire-format constants for the config backend group/key names.
// Owned by this lib because they ARE the LayoutRegistry's serialization
// format — any consumer of the lib shares them automatically.
constexpr QLatin1String AssignmentGroupPrefix{"Assignment:"};
constexpr QLatin1String QuickLayoutsGroup{"QuickLayouts"};

} // namespace

void LayoutRegistry::loadLayouts()
{
    ensureLayoutDirectory();

    // Load from ALL data locations (system directories first, then user)
    // locateAll() returns paths in priority order: user first, system last
    // We reverse to load system first, so user layouts can override
    QStringList allDirs = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, m_layoutSubdirectory,
                                                    QStandardPaths::LocateDirectory);

    // Reverse order: load system layouts first, user layouts last (override)
    std::reverse(allDirs.begin(), allDirs.end());

    for (const QString& dir : allDirs) {
        int beforeCount = m_layouts.size();
        loadLayoutsFromDirectory(dir);
        qCInfo(lcZonesLib) << "Loaded layouts=" << (m_layouts.size() - beforeCount) << "from=" << dir;
    }

    qCInfo(lcZonesLib) << "Total layouts=" << m_layouts.size();

    // Sort by defaultOrder (from layout JSON) so the preferred default is first when defaultLayoutId is empty
    std::stable_sort(m_layouts.begin(), m_layouts.end(), [](PhosphorZones::Layout* a, PhosphorZones::Layout* b) {
        return (a ? a->defaultOrder() : 999) < (b ? b->defaultOrder() : 999);
    });

    // Set initial active layout if none set: use defaultLayout() (settings-based fallback)
    if (!m_activeLayout && !m_layouts.isEmpty()) {
        PhosphorZones::Layout* initial = defaultLayout();
        if (initial) {
            qCInfo(lcZonesLib) << "Active layout name=" << initial->name() << "id=" << initial->id().toString()
                               << "zones=" << initial->zoneCount();
        }
        setActiveLayout(initial);
    }

    Q_EMIT layoutsLoaded();
    Q_EMIT layoutsChanged();
}

void LayoutRegistry::loadLayoutsFromDirectory(const QString& directory)
{
    QDir dir(directory);
    if (!dir.exists()) {
        qCWarning(lcZonesLib) << "Layout directory does not exist:" << directory;
        return;
    }

    if (!dir.isReadable()) {
        qCWarning(lcZonesLib) << "Layout directory is not readable:" << directory;
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
            qCWarning(lcZonesLib) << "Layout file does not exist:" << filePath;
            continue;
        }

        if (!file.open(QIODevice::ReadOnly)) {
            qCWarning(lcZonesLib) << "Failed to open layout file:" << filePath << "Error:" << file.errorString();
            continue;
        }

        const QByteArray data = file.readAll();
        if (data.isEmpty()) {
            qCWarning(lcZonesLib) << "Layout file is empty:" << filePath;
            continue;
        }

        QJsonParseError parseError;
        const auto doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            qCWarning(lcZonesLib) << "Failed to parse layout file:" << filePath << "Error:" << parseError.errorString()
                                  << "at offset" << parseError.offset;
            continue;
        }

        auto layout = PhosphorZones::Layout::fromJson(doc.object(), this);
        if (!layout) {
            qCWarning(lcZonesLib) << "Failed to create layout from JSON:" << filePath;
            continue;
        }

        // Set source path - this determines if layout is system or user layout
        // isSystemLayout() checks if sourcePath is outside user's writable directory
        layout->setSourcePath(filePath);

        if (!layout->name().isEmpty() && layout->zoneCount() > 0) {
            // Check for duplicate IDs
            PhosphorZones::Layout* existing = layoutById(layout->id());
            if (!existing) {
                // No duplicate - add the layout
                m_layouts.append(layout);
                connect(layout, &PhosphorZones::Layout::layoutModified, this, [this, layout]() {
                    saveLayout(layout);
                });
                qCInfo(lcZonesLib) << "Loaded layout name=" << layout->name() << "zones=" << layout->zoneCount()
                                   << "source=" << (layout->isSystemLayout() ? "system" : "user")
                                   << "from=" << filePath;
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
                    disconnect(existing, &PhosphorZones::Layout::layoutModified, this, nullptr);
                    m_layouts.replace(index, layout);
                    connect(layout, &PhosphorZones::Layout::layoutModified, this, [this, layout]() {
                        saveLayout(layout);
                    });
                    delete existing;
                    qCInfo(lcZonesLib) << "User layout overrides system layout name=" << layout->name()
                                       << "from=" << filePath;
                } else {
                    // Same source type or system trying to override user - skip
                    qCInfo(lcZonesLib) << "Skipping duplicate layout name=" << layout->name() << "id=" << layout->id();
                    delete layout;
                }
            }
        } else {
            qCWarning(lcZonesLib) << "Skipping invalid layout entry=" << entry << "reason=empty name or no zones";
            // Clean up orphaned file from user directory (don't delete system layouts)
            if (!layout->isSystemLayout()) {
                QFile::remove(filePath);
                qCInfo(lcZonesLib) << "Removed orphaned layout file:" << filePath;
            }
            delete layout;
        }
    }
}

void LayoutRegistry::saveLayout(PhosphorZones::Layout* layout)
{
    if (!layout || !layout->isDirty()) {
        return;
    }

    // Don't persist invalid layouts (empty name or no zones) — they would be
    // skipped on reload anyway, creating orphaned files on disk.
    if (layout->name().isEmpty() || layout->zoneCount() == 0) {
        qCDebug(lcZonesLib) << "saveLayout: skipping invalid layout (name empty or no zones)" << layout->id();
        return;
    }

    ensureLayoutDirectory();

    // If this is a system layout being saved to user dir for the first time,
    // capture the system origin path before toJson/sourcePath changes
    if (layout->isSystemLayout() && !layout->hasSystemOrigin()) {
        layout->setSystemSourcePath(layout->sourcePath());
        qCInfo(lcZonesLib) << "Captured system origin for" << layout->name() << "from=" << layout->sourcePath();
    }

    const QString filePath = layoutFilePath(layout->id());
    QFile file(filePath);

    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(lcZonesLib) << "Failed to open layout file for writing:" << filePath
                              << "Error:" << file.errorString();
        return;
    }

    // toJson() includes systemSourcePath so it persists across daemon restarts
    QJsonDocument doc(layout->toJson());
    const QByteArray data = doc.toJson(QJsonDocument::Indented);

    if (file.write(data) != data.size()) {
        qCWarning(lcZonesLib) << "Failed to write layout file:" << filePath << "Error:" << file.errorString();
        return;
    }

    if (!file.flush()) {
        qCWarning(lcZonesLib) << "Failed to flush layout file:" << filePath << "Error:" << file.errorString();
        return;
    }

    // Update sourcePath so isSystemLayout() returns correctly after saving
    layout->setSourcePath(filePath);
    layout->clearDirty();
}

void LayoutRegistry::saveLayouts()
{
    for (auto* layout : m_layouts) {
        saveLayout(layout);
    }

    // Not emitting layoutsChanged() — disk persistence doesn't change the layout list
    Q_EMIT layoutsSaved();
}

void LayoutRegistry::readAssignmentGroups(PhosphorConfig::IBackend* backend)
{
    const QStringList allGroups = backend->groupList();
    const QString& assignmentPrefix = AssignmentGroupPrefix;

    for (const QString& groupName : allGroups) {
        if (!groupName.startsWith(assignmentPrefix))
            continue;

        const auto key = LayoutAssignmentKey::fromGroupName(groupName, assignmentPrefix);
        if (key.screenId.isEmpty())
            continue;

        auto grp = backend->group(groupName);
        AssignmentEntry entry;
        int modeInt = grp->readInt(QLatin1String("Mode"), 0);
        entry.mode = (modeInt == AssignmentEntry::Autotile) ? AssignmentEntry::Autotile : AssignmentEntry::Snapping;
        entry.snappingLayout = grp->readString(QLatin1String("SnappingLayout"));
        entry.tilingAlgorithm = grp->readString(QLatin1String("TilingAlgorithm"));

        m_assignments[key] = entry;
    }
}

void LayoutRegistry::readQuickLayouts(PhosphorConfig::IBackend* backend)
{
    auto quickGroup = backend->group(QuickLayoutsGroup);
    for (int i = 1; i <= 9; ++i) {
        QString key = QString::number(i);
        if (quickGroup->hasKey(key)) {
            QString layoutId = quickGroup->readString(key);
            if (!layoutId.isEmpty())
                m_quickLayoutShortcuts[i] = layoutId;
        }
    }
}

void LayoutRegistry::loadAssignments()
{
    m_configBackend->reparseConfiguration();
    readAssignmentGroups(m_configBackend);
    readQuickLayouts(m_configBackend);

    qCInfo(lcZonesLib) << "Loaded assignments=" << m_assignments.size()
                       << "quickShortcuts=" << m_quickLayoutShortcuts.size();
    for (auto it = m_assignments.constBegin(); it != m_assignments.constEnd(); ++it) {
        const AssignmentEntry& entry = it.value();
        qCDebug(lcZonesLib) << "Assignment screenId=" << it.key().screenId << "desktop=" << it.key().virtualDesktop
                            << "activity="
                            << (it.key().activity.isEmpty() ? QStringLiteral("(all)") : it.key().activity)
                            << "mode=" << static_cast<int>(entry.mode) << "snapping=" << entry.snappingLayout
                            << "tiling=" << entry.tilingAlgorithm;
    }
}

void LayoutRegistry::saveAssignments()
{
    // Delete old <prefix>* groups
    const QStringList allGroups = m_configBackend->groupList();
    const QString& prefix = AssignmentGroupPrefix;
    for (const QString& groupName : allGroups) {
        if (groupName.startsWith(prefix)) {
            m_configBackend->deleteGroup(groupName);
        }
    }

    // Write [<prefix>*] groups
    for (auto it = m_assignments.constBegin(); it != m_assignments.constEnd(); ++it) {
        const LayoutAssignmentKey& key = it.key();
        const AssignmentEntry& entry = it.value();

        // Build group name: <prefix>screenId[:Desktop:N][:Activity:id]
        QString groupName = prefix + key.screenId;
        if (key.virtualDesktop > 0) {
            groupName += QStringLiteral(":Desktop:") + QString::number(key.virtualDesktop);
        }
        if (!key.activity.isEmpty()) {
            groupName += QStringLiteral(":Activity:") + key.activity;
        }

        auto group = m_configBackend->group(groupName);
        group->writeInt(QLatin1String("Mode"), static_cast<int>(entry.mode));
        group->writeString(QLatin1String("SnappingLayout"), entry.snappingLayout);
        group->writeString(QLatin1String("TilingAlgorithm"), entry.tilingAlgorithm);
    }

    // Write [QuickLayouts] group
    {
        m_configBackend->deleteGroup(QuickLayoutsGroup);
        auto quickGroup = m_configBackend->group(QuickLayoutsGroup);
        for (auto it = m_quickLayoutShortcuts.constBegin(); it != m_quickLayoutShortcuts.constEnd(); ++it) {
            quickGroup->writeString(QString::number(it.key()), it.value());
        }
    }

    m_configBackend->sync();
    qCInfo(lcZonesLib) << "Saved assignments=" << m_assignments.size()
                       << "quickShortcuts=" << m_quickLayoutShortcuts.size();
}

void LayoutRegistry::importLayout(const QString& filePath)
{
    if (filePath.isEmpty()) {
        qCWarning(lcZonesLib) << "Cannot import layout: file path is empty";
        return;
    }

    QFile file(filePath);
    if (!file.exists()) {
        qCWarning(lcZonesLib) << "Cannot import layout: file does not exist:" << filePath;
        return;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcZonesLib) << "Failed to open layout file for import:" << filePath << "Error:" << file.errorString();
        return;
    }

    const QByteArray data = file.readAll();
    if (data.isEmpty()) {
        qCWarning(lcZonesLib) << "Cannot import layout: file is empty:" << filePath;
        return;
    }

    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(lcZonesLib) << "Failed to parse layout file for import:" << filePath
                              << "Error:" << parseError.errorString() << "at offset" << parseError.offset;
        return;
    }

    auto* parsed = PhosphorZones::Layout::fromJson(doc.object(), this);
    if (!parsed) {
        qCWarning(lcZonesLib) << "Failed to create layout from imported JSON:" << filePath;
        return;
    }

    // Regenerate IDs if UUID collides with an existing layout
    PhosphorZones::Layout* layout = parsed;
    if (layoutById(parsed->id())) {
        qCInfo(lcZonesLib) << "importLayout: UUID collision, regenerating IDs";
        layout = new PhosphorZones::Layout(*parsed);
        delete parsed;
    }

    // Reset visibility restrictions since screen/desktop/activity names are machine-specific
    layout->setHiddenFromSelector(false);
    layout->setAllowedScreens({});
    layout->setAllowedDesktops({});
    layout->setAllowedActivities({});

    addLayout(layout);

    qCInfo(lcZonesLib) << "Imported layout:" << layout->name() << "from" << filePath;
}

void LayoutRegistry::exportLayout(PhosphorZones::Layout* layout, const QString& filePath)
{
    if (!layout) {
        qCWarning(lcZonesLib) << "Cannot export layout: layout is null";
        return;
    }

    if (filePath.isEmpty()) {
        qCWarning(lcZonesLib) << "Cannot export layout: file path is empty";
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(lcZonesLib) << "Failed to open file for layout export:" << filePath << "Error:" << file.errorString();
        return;
    }

    QJsonDocument doc(layout->toJson());
    const QByteArray data = doc.toJson(QJsonDocument::Indented);

    if (file.write(data) != data.size()) {
        qCWarning(lcZonesLib) << "Failed to write layout to file:" << filePath << "Error:" << file.errorString();
        return;
    }

    if (!file.flush()) {
        qCWarning(lcZonesLib) << "Failed to flush layout export file:" << filePath << "Error:" << file.errorString();
    }

    qCInfo(lcZonesLib) << "Exported layout:" << layout->name() << "to" << filePath;
}

PhosphorZones::Layout* LayoutRegistry::restoreSystemLayout(const QUuid& id, const QString& systemPath)
{
    if (systemPath.isEmpty() || layoutById(id)) {
        return nullptr;
    }

    QFile file(systemPath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        qCWarning(lcZonesLib) << "System layout file missing:" << systemPath;
        return nullptr;
    }

    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(lcZonesLib) << "System layout parse error:" << systemPath << parseError.errorString();
        return nullptr;
    }

    auto* layout = PhosphorZones::Layout::fromJson(doc.object(), this);
    if (!layout || layout->id() != id) {
        delete layout;
        return nullptr;
    }

    layout->setSourcePath(systemPath);
    m_layouts.append(layout);
    connect(layout, &PhosphorZones::Layout::layoutModified, this, [this, layout]() {
        saveLayout(layout);
    });
    Q_EMIT layoutAdded(layout);
    qCInfo(lcZonesLib) << "Restored system layout name=" << layout->name() << "from=" << systemPath;
    return layout;
}

void LayoutRegistry::ensureLayoutDirectory()
{
    QDir dir(m_layoutDirectory);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
}

QString LayoutRegistry::layoutFilePath(const QUuid& id) const
{
    // Strip braces only for filesystem path (avoid { } in filenames). Everywhere else we use default (with braces).
    return m_layoutDirectory + QStringLiteral("/") + id.toString(QUuid::WithoutBraces) + QStringLiteral(".json");
}

} // namespace PhosphorZones

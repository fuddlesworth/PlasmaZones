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
        qCInfo(lcLayout) << "Loaded layouts= " << (m_layouts.size() - beforeCount) << " from= " << dir;
    }

    qCInfo(lcLayout) << "Total layouts= " << m_layouts.size();

    // Sort by defaultOrder (from layout JSON) so the preferred default is first when defaultLayoutId is empty
    std::stable_sort(m_layouts.begin(), m_layouts.end(), [](Layout* a, Layout* b) {
        return (a ? a->defaultOrder() : 999) < (b ? b->defaultOrder() : 999);
    });

    // Set initial active layout if none set: use defaultLayout() (settings-based fallback)
    if (!m_activeLayout && !m_layouts.isEmpty()) {
        Layout* initial = defaultLayout();
        if (initial) {
            qCInfo(lcLayout) << "Active layout name= " << initial->name() << " id= " << initial->id().toString()
                             << " zones= " << initial->zoneCount();
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
                qCInfo(lcLayout) << "  Loaded layout name= " << layout->name() << " zones= " << layout->zoneCount()
                                 << " source= " << (layout->isSystemLayout() ? "system" : "user")
                                 << " from= " << filePath;
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
                    qCInfo(lcLayout) << "  User layout overrides system layout name= " << layout->name()
                                     << " from= " << filePath;
                } else {
                    // Same source type or system trying to override user - skip
                    qCInfo(lcLayout) << "  Skipping duplicate layout name= " << layout->name()
                                     << " id= " << layout->id();
                    delete layout;
                }
            }
        } else {
            qCWarning(lcLayout) << "Skipping invalid layout entry= " << entry << " reason= empty name or no zones";
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
    const QString filePath = m_layoutDirectory + QStringLiteral("/assignments.json");
    QFile file(filePath);

    if (!file.exists()) {
        qCInfo(lcLayout) << "Assignments file does not exist, using defaults:" << filePath;
        return;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcLayout) << "Failed to open assignments file:" << filePath << "Error:" << file.errorString();
        return;
    }

    const QByteArray data = file.readAll();
    if (data.isEmpty()) {
        qCWarning(lcLayout) << "Assignments file is empty:" << filePath;
        return;
    }

    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(lcLayout) << "Failed to parse assignments file:" << filePath << "Error:" << parseError.errorString()
                            << "at offset" << parseError.offset;
        return;
    }

    const auto root = doc.object();

    // Load assignments with error handling
    const auto assignmentsArray = root[JsonKeys::Assignments].toArray();
    for (const auto& value : assignmentsArray) {
        if (!value.isObject()) {
            qCWarning(lcLayout) << "Invalid assignment entry (not an object), skipping";
            continue;
        }

        const auto obj = value.toObject();
        // Prefer screenId (EDID-based); fall back to screen (connector name) for legacy configs
        QString sid = obj[JsonKeys::ScreenId].toString();
        if (sid.isEmpty()) {
            sid = obj[JsonKeys::Screen].toString();
        }
        LayoutAssignmentKey key{sid, obj[JsonKeys::Desktop].toInt(), obj[JsonKeys::Activity].toString()};

        const QString layoutIdStr = obj[JsonKeys::LayoutId].toString();

        if (LayoutId::isAutotile(layoutIdStr)) {
            // Autotile IDs are stored as-is
            m_assignments[key] = layoutIdStr;
        } else {
            // Validate UUID format
            const QUuid uuid = QUuid::fromString(layoutIdStr);
            if (uuid.isNull()) {
                qCWarning(lcLayout) << "Invalid layout ID in assignment:" << layoutIdStr << "skipping";
                continue;
            }
            // Normalize to braced UUID format for consistent comparison
            m_assignments[key] = uuid.toString();
        }
    }

    // Load quick shortcuts with error handling
    const auto shortcutsObj = root[JsonKeys::QuickShortcuts].toObject();
    for (auto it = shortcutsObj.begin(); it != shortcutsObj.end(); ++it) {
        bool ok = false;
        const int number = it.key().toInt(&ok);
        if (!ok) {
            qCWarning(lcLayout) << "Invalid shortcut number:" << it.key() << "skipping";
            continue;
        }

        const QString layoutIdStr = it.value().toString();

        if (LayoutId::isAutotile(layoutIdStr)) {
            m_quickLayoutShortcuts[number] = layoutIdStr;
        } else {
            const QUuid uuid = QUuid::fromString(layoutIdStr);
            if (uuid.isNull()) {
                qCWarning(lcLayout) << "Invalid layout ID in shortcut:" << layoutIdStr << "skipping";
                continue;
            }
            m_quickLayoutShortcuts[number] = uuid.toString();
        }
    }

    qCInfo(lcLayout) << "Loaded assignments= " << m_assignments.size()
                     << " quickShortcuts= " << m_quickLayoutShortcuts.size();
    for (auto it = m_assignments.constBegin(); it != m_assignments.constEnd(); ++it) {
        Layout* layout = LayoutId::isAutotile(it.value()) ? nullptr : layoutById(QUuid::fromString(it.value()));
        QString layoutName =
            layout ? layout->name() : (LayoutId::isAutotile(it.value()) ? it.value() : QStringLiteral("(unknown)"));
        qCDebug(lcLayout) << "  Assignment screenId= " << it.key().screenId << " desktop= " << it.key().virtualDesktop
                          << " activity= "
                          << (it.key().activity.isEmpty() ? QStringLiteral("(all)") : it.key().activity)
                          << " layout= " << layoutName;
    }
}

void LayoutManager::saveAssignments()
{
    ensureLayoutDirectory();

    QJsonObject root;

    // Save assignments (write both screenId and screen for backward compat)
    QJsonArray assignmentsArray;
    for (auto it = m_assignments.begin(); it != m_assignments.end(); ++it) {
        QJsonObject obj;
        obj[JsonKeys::ScreenId] = it.key().screenId;
        // Write connector name for backward compat and debugging
        QString connectorName = Utils::screenNameForId(it.key().screenId);
        obj[JsonKeys::Screen] = connectorName.isEmpty() ? it.key().screenId : connectorName;
        obj[JsonKeys::Desktop] = it.key().virtualDesktop;
        obj[JsonKeys::Activity] = it.key().activity;
        obj[JsonKeys::LayoutId] = it.value();
        assignmentsArray.append(obj);
    }
    root[JsonKeys::Assignments] = assignmentsArray;

    // Save quick shortcuts
    QJsonObject shortcutsObj;
    for (auto it = m_quickLayoutShortcuts.begin(); it != m_quickLayoutShortcuts.end(); ++it) {
        shortcutsObj[QString::number(it.key())] = it.value();
    }
    root[JsonKeys::QuickShortcuts] = shortcutsObj;

    const QString filePath = m_layoutDirectory + QStringLiteral("/assignments.json");
    QFile file(filePath);

    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(lcLayout) << "Failed to open assignments file for writing:" << filePath
                            << "Error:" << file.errorString();
        return;
    }

    QJsonDocument doc(root);
    const QByteArray data = doc.toJson(QJsonDocument::Indented);

    if (file.write(data) != data.size()) {
        qCWarning(lcLayout) << "Failed to write assignments file:" << filePath << "Error:" << file.errorString();
        return;
    }

    if (!file.flush()) {
        qCWarning(lcLayout) << "Failed to flush assignments file:" << filePath << "Error:" << file.errorString();
    }

    qCInfo(lcLayout) << "Saved assignments to:" << filePath;
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
        qCInfo(lcLayout) << "Imported layout UUID collides with existing layout — regenerating IDs";
        layout = new Layout(*parsed);
        delete parsed;
    }

    // Reset visibility restrictions since screen/desktop/activity names are machine-specific
    layout->setHiddenFromSelector(false);
    layout->setAllowedScreens({});
    layout->setAllowedDesktops({});
    layout->setAllowedActivities({});

    addLayout(layout);

    qCInfo(lcLayout) << "Successfully imported layout:" << layout->name() << "from" << filePath;
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

    qCInfo(lcLayout) << "Successfully exported layout:" << layout->name() << "to" << filePath;
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

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Layout file I/O + assignment persistence.
// Part of LayoutRegistry — split from layoutregistry.cpp for SRP.

#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/LayoutSettingsStore.h>
#include <PhosphorZones/ZoneJsonKeys.h>

#include "zoneslogging.h"

#include <PhosphorFsLoader/SchemaValidator.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>

namespace PhosphorZones {

namespace {

// Process-wide layout schema validator, compiled once on first use from the
// RCC-embedded schema (see qt6_add_resources in CMakeLists). Always available,
// never depends on an installed data path. validate() is const and only reads
// the compiled schema, so reusing this single instance across the load loop
// (and across threads) is safe.
const PhosphorFsLoader::SchemaValidator& layoutSchemaValidator()
{
    static const PhosphorFsLoader::SchemaValidator validator = PhosphorFsLoader::SchemaValidator::fromResource(
        QStringLiteral(":/phosphorzones/schemas/layout.schema.json"), lcZonesLib());
    return validator;
}

} // namespace

void LayoutRegistry::loadLayouts()
{
    ensureLayoutDirectory();

    // Load the per-layout settings sidecar once up front so each layout's
    // settings can be merged back onto its structural JSON as it loads.
    m_layoutSettings.loadFromFile(layoutSettingsFilePath());

    // Fold the retired standalone autotile-overrides.json into the unified
    // sidecar (one-time; self-deletes the legacy file). Done after the sidecar
    // load so the store is populated, and before layouts merge so autotile
    // entries are queryable for the rest of this load.
    migrateLegacyAutotileOverrides();

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
        // Skip sibling sidecar files that share this directory but aren't
        // layouts. "autotile-overrides.json" is a retired format (folded into
        // layout-settings.json by migrateLegacyAutotileOverrides); it's kept in
        // the skip-list as a one-release safety net in case a stale copy lingers
        // in a system data dir the migration didn't delete.
        if (entry == QStringLiteral("assignments.json") || entry == QStringLiteral("autotile-overrides.json")
            || entry == QStringLiteral("rules.json") || entry == QStringLiteral("quicklayouts.json")
            || entry == QStringLiteral("layout-settings.json")) {
            continue;
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

        const QJsonObject structural = doc.object();

        // Validate the on-disk structural document against the layout schema
        // before merging sidecar settings. Catches malformed user-authored or
        // third-party layout files (missing zones, out-of-range relative
        // geometry, wrong types) up front with a precise diagnostic, rather
        // than letting them fall through to fromJson and produce broken zones.
        if (const auto errors = layoutSchemaValidator().validate(structural)) {
            qCWarning(lcZonesLib) << "Skipping layout file failing schema validation:" << filePath;
            PhosphorFsLoader::logSchemaErrors(lcZonesLib(), *errors);
            continue;
        }

        // Merge the layout's settings (from the sidecar, keyed by layout UUID)
        // back onto the structural JSON before constructing the Layout. A
        // not-yet-split (full-format) file with no sidecar entry round-trips
        // unchanged — mergeSettings preserves keys the file already carries.
        const QString layoutId = structural.value(::PhosphorZones::ZoneJsonKeys::Id).toString();
        const QJsonObject merged =
            LayoutSettingsStore::mergeSettings(structural, m_layoutSettings.settingsFor(layoutId));

        auto layout = PhosphorZones::Layout::fromJson(merged, this);
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
                    // deleteLater, not delete (project rule: no manual delete).
                    // The replaced object is inert from here — removed from
                    // m_layouts and disconnected above — and the daemon spins
                    // its event loop after loadLayouts(), so the deferred
                    // deletion runs. No layoutRemoved is emitted: bulk load is
                    // deliberately signal-silent per layout, loadLayouts()
                    // emits a single layoutsChanged at the end.
                    existing->deleteLater();
                    qCInfo(lcZonesLib) << "User layout overrides system layout name=" << layout->name()
                                       << "from=" << filePath;
                } else {
                    // Same source type or system trying to override user - skip
                    qCInfo(lcZonesLib) << "Skipping duplicate layout name=" << layout->name() << "id=" << layout->id();
                    // deleteLater, not delete — see the override branch above.
                    layout->deleteLater();
                }
            }
        } else {
            qCWarning(lcZonesLib) << "Skipping invalid layout entry=" << entry << "reason=empty name or no zones";
            // Clean up orphaned file from user directory (don't delete system layouts)
            if (!layout->isSystemLayout()) {
                QFile::remove(filePath);
                qCInfo(lcZonesLib) << "Removed orphaned layout file:" << filePath;
            }
            // deleteLater, not delete — see the override branch above.
            layout->deleteLater();
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
    // QSaveFile gives atomic temp-write + rename — a crash mid-write never
    // leaves a truncated layout file on disk.
    QSaveFile file(filePath);

    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(lcZonesLib) << "Failed to open layout file for writing:" << filePath
                              << "Error:" << file.errorString();
        return;
    }

    // Split the full layout JSON: the per-layout SETTINGS go to the sidecar
    // (keyed by layout UUID), and only the structural definition is written to
    // the layout file. toJson() includes systemSourcePath so it persists across
    // daemon restarts.
    const QJsonObject full = layout->toJson();
    m_layoutSettings.setSettingsFor(layout->id().toString(), LayoutSettingsStore::extractSettings(full));
    // Invariant: never strip settings out of the layout file unless the
    // sidecar copy actually landed. On sidecar failure, write the FULL
    // object inline for this cycle so the settings survive somewhere on
    // disk; a full-format layout file remains loadable (the load path's
    // mergeSettings preserves keys the file already carries, see
    // loadLayoutsFromDirectory), and a later successful save re-splits it.
    //
    // Write ORDER (sidecar first, layout file second) is safe: if the
    // layout-file commit below fails after the sidecar landed, the old layout
    // file survives untouched (atomic QSaveFile), leaving a fresher sidecar
    // against an older file. That is harmless because mergeSettings gives the
    // SIDECAR precedence on key conflict at load (QJsonObject::insert
    // overwrites the file's inline value, and sidecar zone appearance likewise
    // replaces inline appearance), so stale inline settings in an old
    // full-format file cannot shadow the fresh sidecar. The order also lets us
    // know sidecarSaved before choosing stripped-vs-full content above.
    const bool sidecarSaved = m_layoutSettings.saveToFile(layoutSettingsFilePath());
    if (!sidecarSaved) {
        qCWarning(lcZonesLib) << "Failed to persist layout settings sidecar for" << layout->id().toString()
                              << "- keeping settings inline in the layout file";
    }

    QJsonDocument doc(sidecarSaved ? LayoutSettingsStore::stripSettings(full) : full);
    const QByteArray data = doc.toJson(QJsonDocument::Indented);

    if (file.write(data) != data.size()) {
        qCWarning(lcZonesLib) << "Failed to write layout file:" << filePath << "Error:" << file.errorString();
        file.cancelWriting();
        return;
    }

    if (!file.commit()) {
        qCWarning(lcZonesLib) << "Failed to commit layout file:" << filePath << "Error:" << file.errorString();
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

QString LayoutRegistry::quickLayoutsFilePath() const
{
    // Quick-layout slots are NOT rules — they persist to a sibling
    // JSON file next to the RuleStore file (so the location is stable
    // and independent of any later setLayoutDirectory() call).
    return QFileInfo(m_ruleStore->filePath()).absolutePath() + QStringLiteral("/quicklayouts.json");
}

QString LayoutRegistry::layoutSettingsFilePath() const
{
    // Per-layout settings persist to a sibling JSON file next to the
    // RuleStore file — same stable, layout-dir-independent location as
    // the quick-layout sidecar.
    return QFileInfo(m_ruleStore->filePath()).absolutePath() + QStringLiteral("/layout-settings.json");
}

void LayoutRegistry::readQuickLayouts()
{
    m_quickLayoutSlots[0].clear();
    m_quickLayoutSlots[1].clear();
    QFile file(quickLayoutsFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return; // a missing file is not an error
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return;
    }
    const QJsonObject root = doc.object();

    // Reader for one mode's nested slot object ({ "1": id, ... }).
    const auto readModeSlots = [](const QJsonObject& obj, QHash<int, QString>& out) {
        for (int i = 1; i <= 9; ++i) {
            const QString key = QString::number(i);
            if (obj.contains(key)) {
                const QString layoutId = obj.value(key).toString();
                if (!layoutId.isEmpty()) {
                    out[i] = layoutId;
                }
            }
        }
    };

    // Slots are nested by mode ({ "snapping": {...}, "autotile": {...} }). This
    // is the single on-disk format: the writer below and the v3→v4 migration
    // both emit it. A pre-mode (flat) file has neither key, so both modes stay
    // empty — no ad-hoc legacy read, matching the config policy that a
    // restructured store drops old values rather than carrying a second format.
    readModeSlots(root.value(QuickSlotsSnappingKey).toObject(), m_quickLayoutSlots[0]);
    readModeSlots(root.value(QuickSlotsAutotileKey).toObject(), m_quickLayoutSlots[1]);
}

void LayoutRegistry::writeQuickLayouts()
{
    QDir().mkpath(QFileInfo(quickLayoutsFilePath()).absolutePath());
    const auto modeSlotsToJson = [](const QHash<int, QString>& slots) {
        QJsonObject obj;
        for (auto it = slots.constBegin(); it != slots.constEnd(); ++it) {
            obj.insert(QString::number(it.key()), it.value());
        }
        return obj;
    };
    QJsonObject obj;
    obj.insert(QuickSlotsSnappingKey, modeSlotsToJson(m_quickLayoutSlots[0]));
    obj.insert(QuickSlotsAutotileKey, modeSlotsToJson(m_quickLayoutSlots[1]));
    // QSaveFile gives atomic temp-write + rename — a crash mid-write never
    // leaves a truncated quicklayouts.json behind.
    QSaveFile file(quickLayoutsFilePath());
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(lcZonesLib) << "Failed to save quick layouts:" << file.errorString();
        return;
    }
    const QByteArray payload = QJsonDocument(obj).toJson();
    if (file.write(payload) != payload.size()) {
        qCWarning(lcZonesLib) << "Failed to write quick layouts:" << file.errorString();
        file.cancelWriting();
        return;
    }
    if (!file.commit()) {
        qCWarning(lcZonesLib) << "Failed to commit quick layouts:" << file.errorString();
        return;
    }
    qCInfo(lcZonesLib) << "Saved quickShortcuts: snapping=" << m_quickLayoutSlots[0].size()
                       << "autotile=" << m_quickLayoutSlots[1].size();
}

void LayoutRegistry::loadAssignments()
{
    // Assignments live in the unified RuleStore — (re)load it from disk
    // so cross-process deltas surface, then read the quick-layout sidecar.
    m_ruleStore->load();
    readQuickLayouts();

    qCInfo(lcZonesLib) << "Loaded rules=" << m_ruleStore->count()
                       << "quickShortcuts: snapping=" << m_quickLayoutSlots[0].size()
                       << "autotile=" << m_quickLayoutSlots[1].size();
}

void LayoutRegistry::saveAssignments()
{
    // The RuleStore persists on every mutation — no separate flush is
    // needed for the rule set. Only the quick-layout sidecar is written here.
    writeQuickLayouts();
}

PhosphorZones::Layout* LayoutRegistry::importLayout(const QString& filePath)
{
    if (filePath.isEmpty()) {
        qCWarning(lcZonesLib) << "Cannot import layout: file path is empty";
        return nullptr;
    }

    QFile file(filePath);
    if (!file.exists()) {
        qCWarning(lcZonesLib) << "Cannot import layout: file does not exist:" << filePath;
        return nullptr;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcZonesLib) << "Failed to open layout file for import:" << filePath << "Error:" << file.errorString();
        return nullptr;
    }

    const QByteArray data = file.readAll();
    if (data.isEmpty()) {
        qCWarning(lcZonesLib) << "Cannot import layout: file is empty:" << filePath;
        return nullptr;
    }

    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(lcZonesLib) << "Failed to parse layout file for import:" << filePath
                              << "Error:" << parseError.errorString() << "at offset" << parseError.offset;
        return nullptr;
    }

    // Import is an untrusted-input load path (a user-picked file), so gate it on
    // the same schema as the directory scan rather than letting a malformed file
    // reach fromJson and produce broken zones.
    if (const auto errors = layoutSchemaValidator().validate(doc.object())) {
        qCWarning(lcZonesLib) << "Cannot import layout: file failing schema validation:" << filePath;
        PhosphorFsLoader::logSchemaErrors(lcZonesLib(), *errors);
        return nullptr;
    }

    auto* parsed = PhosphorZones::Layout::fromJson(doc.object(), this);
    if (!parsed) {
        qCWarning(lcZonesLib) << "Failed to create layout from imported JSON:" << filePath;
        return nullptr;
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
    return layout;
}

bool LayoutRegistry::exportLayout(PhosphorZones::Layout* layout, const QString& filePath)
{
    if (!layout) {
        qCWarning(lcZonesLib) << "Cannot export layout: layout is null";
        return false;
    }

    if (filePath.isEmpty()) {
        qCWarning(lcZonesLib) << "Cannot export layout: file path is empty";
        return false;
    }

    // QSaveFile for the same reason saveLayout uses it, and one more: the
    // destination is a file the user picked, which may already hold something
    // they want. A plain QFile opened WriteOnly truncates it the moment it
    // opens, so a write that then failed left them with neither their old file
    // nor an export. The temp-write plus rename only replaces the destination
    // once the whole document is down.
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(lcZonesLib) << "Failed to open file for layout export:" << filePath << "Error:" << file.errorString();
        return false;
    }

    QJsonDocument doc(layout->toJson());
    const QByteArray data = doc.toJson(QJsonDocument::Indented);

    if (file.write(data) != data.size()) {
        qCWarning(lcZonesLib) << "Failed to write layout to file:" << filePath << "Error:" << file.errorString();
        return false;
    }

    // commit() flushes, closes and renames. It is the only point at which the
    // export is known to have landed: a buffered write can still fail here, and
    // the previous form reported success after logging a failed flush.
    if (!file.commit()) {
        qCWarning(lcZonesLib) << "Failed to commit layout export file:" << filePath << "Error:" << file.errorString();
        return false;
    }

    qCInfo(lcZonesLib) << "Exported layout:" << layout->name() << "to" << filePath;
    return true;
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

    // Gate on the same schema as the scan/import paths so a corrupt system file
    // is refused rather than restored as broken zones.
    if (const auto errors = layoutSchemaValidator().validate(doc.object())) {
        qCWarning(lcZonesLib) << "System layout failing schema validation:" << systemPath;
        PhosphorFsLoader::logSchemaErrors(lcZonesLib(), *errors);
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

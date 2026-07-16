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

/// Open @p file and write @p doc into its staging file WITHOUT committing, so
/// the caller can decide later whether the rename happens. Cancels the staging
/// file and returns false when the open or the write fails; on success the
/// caller owns the commit (or a cancelWriting). @p path names the destination
/// in log output only.
bool stageJson(QSaveFile& file, const QJsonDocument& doc, const QString& path)
{
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(lcZonesLib) << "Failed to open file for writing:" << path << "Error:" << file.errorString();
        return false;
    }
    const QByteArray payload = doc.toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        qCWarning(lcZonesLib) << "Failed to write file:" << path << "Error:" << file.errorString();
        file.cancelWriting();
        return false;
    }
    return true;
}

} // namespace

bool LayoutRegistry::isLayoutJsonValid(const QJsonObject& json, const QString& context)
{
    const auto errors = layoutSchemaValidator().validate(json);
    if (!errors) {
        return true;
    }
    qCWarning(lcZonesLib) << "Rejecting layout failing schema validation:" << context;
    PhosphorFsLoader::logSchemaErrors(lcZonesLib(), *errors);
    return false;
}

void LayoutRegistry::loadLayouts()
{
    ensureLayoutDirectory();

    // Retire the previous in-memory set before the scan. This is a RELOAD, not
    // an append: callers invoke it precisely to pick up on-disk changes (the
    // editor and the settings app both re-call it on every daemon layout
    // signal), and without the clear every entry would hit the duplicate-id
    // branch below and be skipped — the reload would silently keep serving the
    // stale objects.
    //
    // The retired objects are parented to this registry, so: drop the
    // active/previous pointers (they are re-seated by id after the scan, since
    // a layout with the same UUID comes back as a NEW object), disconnect the
    // save hook so a retired layout can't write back, and deleteLater rather
    // than delete (project rule; the pointers are also inert from here — out
    // of m_layouts and disconnected).
    //
    // Consumers DO cache a Layout* across this boundary, so deleteLater alone
    // is not what keeps them safe. The invariant every holder must satisfy is:
    // hold the layout as a QPointer, or connect to its destroyed signal and
    // null the cached pointer there. destroyed fires when the deferred delete
    // runs, i.e. before the memory goes away, so both forms self-null.
    // Current holders: OverlayService::m_layout / m_observedLayouts and
    // LayoutComputeService::m_trackedLayouts are QPointers;
    // ZoneDetector::m_layout is a raw pointer with a destroyed guard
    // (see ZoneDetector::setLayout). A new raw-pointer holder without one of
    // those two is a use-after-free — this function does not protect it.
    //
    // Self-nulling only gets a holder to null, not to the replacement object,
    // which is why the activeLayoutChanged emit at the end of this function is
    // load-bearing: it is what re-seats them onto the freshly-built instance
    // (the daemon re-seats ZoneDetector from it).
    const QUuid activeIdBefore = m_activeLayout ? m_activeLayout->id() : QUuid();
    const QUuid previousIdBefore = m_previousLayout ? m_previousLayout->id() : QUuid();
    const QVector<PhosphorZones::Layout*> retired = m_layouts;
    m_layouts.clear();
    m_activeLayout = nullptr;
    m_previousLayout = nullptr;
    for (PhosphorZones::Layout* layout : retired) {
        if (!layout) {
            continue;
        }
        disconnect(layout, &PhosphorZones::Layout::layoutModified, this, nullptr);
        layout->deleteLater();
    }

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
        return (a ? a->defaultOrder() : PhosphorZones::Layout::DefaultOrderUnset)
            < (b ? b->defaultOrder() : PhosphorZones::Layout::DefaultOrderUnset);
    });

    // Re-seat the active / previous pointers onto the freshly-built objects.
    // Same UUID means the same layout to every consumer, but it is a different
    // instance now and the old one is queued for deletion. A layout that is
    // gone from disk re-seats to null.
    const bool hadActive = !activeIdBefore.isNull();
    m_activeLayout = hadActive ? layoutById(activeIdBefore) : nullptr;
    m_previousLayout = previousIdBefore.isNull() ? nullptr : layoutById(previousIdBefore);

    // Set initial active layout if none set: use defaultLayout() (settings-based fallback).
    // Also covers a reload whose active layout no longer exists on disk.
    if (!m_activeLayout && !m_layouts.isEmpty()) {
        PhosphorZones::Layout* initial = defaultLayout();
        if (initial) {
            qCInfo(lcZonesLib) << "Active layout name=" << initial->name() << "id=" << initial->id().toString()
                               << "zones=" << initial->zoneCount();
        }
        setActiveLayout(initial); // emits activeLayoutChanged itself
    } else if (m_activeLayout || hadActive) {
        // Either re-seated to a NEW object behind the same UUID, or the active
        // layout is gone from disk and there is nothing to fall back to. Both
        // are real pointer changes, not spurious emits: subscribers cache the
        // pointer and the instance they hold is deleteLater'd a tick from now,
        // so they have to be told to re-read it.
        Q_EMIT activeLayoutChanged(m_activeLayout);
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
        if (!isLayoutJsonValid(structural, filePath)) {
            continue;
        }

        // Merge the layout's settings (from the sidecar, keyed by layout UUID)
        // back onto the structural JSON before constructing the Layout. A
        // not-yet-split (full-format) file with no sidecar entry round-trips
        // unchanged — mergeSettings preserves keys the file already carries.
        const QString layoutId = structural.value(::PhosphorZones::ZoneJsonKeys::Id).toString();
        const QJsonObject merged =
            LayoutSettingsStore::mergeSettings(structural, m_layoutSettings.settingsFor(layoutId));

        // Re-validate after the merge. The sidecar is a separate document that
        // reaches fromJson through this path, and while mergeSettings restricts
        // itself to the settings key set (so it can no longer overwrite the id
        // or any other structural key), the VALUES it inserts under those keys
        // are still whatever a corrupt or hand-edited sidecar entry holds. The
        // merged shape is just the full (pre-split) layout format, which is
        // what the schema describes and what importLayout already validates,
        // so this is the same gate rather than a second one. The structural
        // check stays: it runs first so a fault in the layout file is reported
        // against the layout file.
        if (!isLayoutJsonValid(merged, filePath + QStringLiteral(" (merged with layout-settings sidecar)"))) {
            continue;
        }

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
                    if (layout->defaultOrder() == PhosphorZones::Layout::DefaultOrderUnset
                        && existing->defaultOrder() != PhosphorZones::Layout::DefaultOrderUnset) {
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
                    //
                    // No active/previous re-seat here: `existing` was appended
                    // by this same scan, and loadLayouts() nulls both pointers
                    // before the scan and re-seats them by id afterwards, so
                    // neither can be pointing at it.
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

    // Split the full layout JSON: the per-layout SETTINGS go to the sidecar
    // (keyed by layout UUID), and only the structural definition is written to
    // the layout file. toJson() includes systemSourcePath so it persists across
    // daemon restarts.
    const QJsonObject full = layout->toJson();

    // The sidecar entry lands on a CANDIDATE copy of the store, not on the
    // shared one. m_layoutSettings is whole-file state: every saveLayout /
    // removeLayout writes all of it, so mutating it before the write that can
    // reject it would let a later successful save for a DIFFERENT layout flush
    // this layout's new settings while this layout's file still held the old
    // structure. The candidate is committed into the shared store only once
    // both files are down.
    LayoutSettingsStore candidateSettings = m_layoutSettings;
    candidateSettings.setSettingsFor(layout->id().toString(), LayoutSettingsStore::extractSettings(full));

    // The sidecar and the layout file are ONE unit, so both are STAGED through
    // QSaveFile (temp-write, no rename) before either is committed. Everything
    // that can realistically fail — open, write, disk-full — happens during
    // staging, and any failure there cancels both stagings and leaves the
    // last-good pair from the previous save untouched. What is left is the two
    // renames: the layout file commits first, and only its success unlocks the
    // sidecar's commit.
    //
    // That leaves exactly one non-atomic window: the sidecar's rename failing
    // after the layout file's rename succeeded, which lands new structure
    // against old settings. Closing it would need a journal; instead, that path
    // leaves the layout dirty and the shared store un-advanced (so memory still
    // matches the sidecar on disk), and saveLayouts / the layoutModified
    // connection retry the whole pair.
    //
    // Writing the settings inline to the layout file as a fallback does NOT
    // work: mergeSettings gives the SIDECAR precedence on key conflict at load
    // (QJsonObject::insert overwrites the file's inline value), so a stale
    // sidecar would shadow the fresh inline value and the newer setting would
    // sit on disk unreachable.
    const QString filePath = layoutFilePath(layout->id());
    const QString settingsPath = layoutSettingsFilePath();
    QSaveFile layoutFile(filePath);
    QSaveFile sidecarFile(settingsPath);

    if (!stageJson(layoutFile, QJsonDocument(LayoutSettingsStore::stripSettings(full)), filePath)) {
        return;
    }
    if (!stageJson(sidecarFile, QJsonDocument(candidateSettings.toJson()), settingsPath)) {
        qCWarning(lcZonesLib) << "Abandoning save of layout" << layout->id().toString()
                              << "- layout stays dirty for retry";
        layoutFile.cancelWriting();
        return;
    }

    if (!layoutFile.commit()) {
        qCWarning(lcZonesLib) << "Failed to commit layout file:" << filePath << "Error:" << layoutFile.errorString();
        sidecarFile.cancelWriting();
        return;
    }
    if (!sidecarFile.commit()) {
        qCWarning(lcZonesLib) << "Failed to commit layout settings sidecar:" << settingsPath
                              << "Error:" << sidecarFile.errorString()
                              << "- the layout file already landed; layout stays dirty so the pair is rewritten";
        return;
    }

    // Both files are down — the candidate is now what disk holds.
    m_layoutSettings = std::move(candidateSettings);

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
    if (!isLayoutJsonValid(doc.object(), filePath)) {
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
        // deleteLater, not delete — see the override branch in loadLayouts().
        parsed->deleteLater();
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
    if (!isLayoutJsonValid(doc.object(), systemPath)) {
        return nullptr;
    }

    auto* layout = PhosphorZones::Layout::fromJson(doc.object(), this);
    if (!layout || layout->id() != id) {
        if (layout) {
            // deleteLater, not delete — see the override branch in loadLayouts().
            layout->deleteLater();
        }
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

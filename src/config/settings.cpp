// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settings.h"
#include "colorimporter.h"
#include "configdefaults.h"
#include "configbackends.h"
#include "configmigration.h"
#include "perscreenresolver.h"
#include "settings/settings_detail.h"
#include "settingsschema.h"

#include "core/types/animationshadersupportedpaths.h"
#include "core/types/constants.h"
#include "core/platform/logging.h"
#include "core/utils/utils.h"

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorConfig/JsonBackend.h>
#include <PhosphorRules/ContextRuleBridge.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorSurface/DecorationProfileTree.h>

#include <QEvent>
#include <QGuiApplication>
#include <QScopedValueRollback>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QPalette>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QUuid>

#include <optional>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTileEngine/AutotileConfig.h>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

using namespace settings_detail;

// ── Constructor ──────────────────────────────────────────────────────────────

namespace {
std::unique_ptr<PhosphorConfig::IBackend> migrateAndCreateOwnedBackend()
{
    // Defensive: ensure INI→JSON + schema-version migration has run before
    // we open the backend. Production entry points (daemon/main,
    // settings/main, editor controller) already call this; routing it
    // through the owning ctor makes tests and one-off tools safe too.
    // ensureJsonConfig is idempotent via a process-level atomic.
    ConfigMigration::ensureJsonConfig();
    return createDefaultConfigBackend();
}
} // namespace

Settings::Settings(PhosphorConfig::IBackend* backend, PhosphorAnimation::CurveRegistry* curveRegistry,
                   PhosphorRules::RuleStore* ruleStore, QObject* parent)
    : ISettings(parent)
    , m_configBackend(backend)
    , m_store(std::make_unique<PhosphorConfig::Store>(backend, buildSettingsSchema(), this))
    , m_ownedRuleStore(ruleStore ? nullptr
                                 : std::make_unique<PhosphorRules::RuleStore>(ConfigDefaults::rulesFilePath(), this))
    , m_ruleStore(ruleStore ? ruleStore : m_ownedRuleStore.get())
    , m_curveRegistry(curveRegistry)
{
    // Contract: @p backend MUST already be pointing at a migrated config
    // file. Production entry points (daemon/main, settings/main, editor
    // controller) call ConfigMigration::ensureJsonConfig() exactly once at
    // startup, before instantiating the backend. The non-owning ctor is
    // used when callers share one backend across Settings + PhosphorZones::LayoutRegistry
    // + other components, so the migration responsibility lives with them.
    //
    // The curve registry is taken at construction (before load()) so the
    // initial property snapshot parses Profile blobs through the caller's
    // registry and preserves `shared_ptr<const Curve>` identity end-to-end.
    // There is deliberately no post-construction setter — a late-set
    // registry would leave already-parsed Profile state referencing
    // curves built through the fallback registry, defeating the shared-
    // curve cache without any mechanism for the cached state to notice.
    //
    // (The owning ctor below routes through migrateAndCreateOwnedBackend
    // which performs the migration itself for standalone tools and tests.)
    load();
    connectRuleStoreGapReactivity();
    trackSystemPaletteChanges();
}

Settings::Settings(QObject* parent)
    : ISettings(parent)
    , m_ownedBackend(migrateAndCreateOwnedBackend())
    , m_configBackend(m_ownedBackend.get())
    , m_store(std::make_unique<PhosphorConfig::Store>(m_configBackend, buildSettingsSchema(), this))
    , m_ownedRuleStore(std::make_unique<PhosphorRules::RuleStore>(ConfigDefaults::rulesFilePath(), this))
    , m_ruleStore(m_ownedRuleStore.get())
{
    // m_curveRegistry is left null; `animationProfile()` /
    // `setAnimationEasingCurve()` fall back to `fallbackCurveRegistry()`.
    // That fallback is a process-static CurveRegistry — NOT the daemon's
    // injected instance — so `shared_ptr<const Curve>` identity is not
    // preserved across the Settings ↔ daemon boundary when this ctor is
    // used. The standalone settings app and tests both communicate with
    // the daemon via disk / D-Bus config round-trips where identity was
    // never going to survive anyway, so this is the expected path.
    // qCDebug rather than qCWarning to avoid log-spam on every test
    // fixture and standalone-settings launch.
    qCDebug(lcConfig) << "Settings constructed without explicit CurveRegistry — using process-static fallback.";
    load();
    connectRuleStoreGapReactivity();
    trackSystemPaletteChanges();
}

Settings::Settings(PhosphorRules::RuleStore* ruleStore, QObject* parent)
    : ISettings(parent)
    , m_ownedBackend(migrateAndCreateOwnedBackend())
    , m_configBackend(m_ownedBackend.get())
    , m_store(std::make_unique<PhosphorConfig::Store>(m_configBackend, buildSettingsSchema(), this))
    // Borrow the caller's store when provided; degrade to owning one when null
    // (mirrors the backend-injecting ctor's defensive borrow-or-own so a stray
    // null still yields a usable store rather than a null deref).
    , m_ownedRuleStore(ruleStore ? nullptr
                                 : std::make_unique<PhosphorRules::RuleStore>(ConfigDefaults::rulesFilePath(), this))
    , m_ruleStore(ruleStore ? ruleStore : m_ownedRuleStore.get())
{
    // Standalone backend + process-static curve fallback (m_curveRegistry left
    // null), exactly like Settings(QObject*); the only difference is the
    // borrowed window-rule store. See that ctor's note on why identity is not
    // preserved across the Settings ↔ daemon boundary in this configuration.
    qCDebug(lcConfig) << "Settings constructed without explicit CurveRegistry — using process-static fallback.";
    load();
    connectRuleStoreGapReactivity();
    trackSystemPaletteChanges();
}

// ── Helper Methods ───────────────────────────────────────────────────────────

QString Settings::normalizeUuidString(const QString& uuidStr)
{
    if (uuidStr.isEmpty()) {
        return QString();
    }
    QUuid uuid = QUuid::fromString(uuidStr);
    if (uuid.isNull()) {
        qCWarning(lcConfig) << "Invalid UUID string in config, ignoring:" << uuidStr;
        return QString();
    }
    return uuid.toString();
}

// ── load() dispatcher ────────────────────────────────────────────────────────

void Settings::load()
{
    // Snapshot every Q_PROPERTY declared on Settings (skipping inherited
    // QObject properties like objectName) that has a NOTIFY signal, so we
    // can re-emit the matching NOTIFY signal after load() refreshes
    // backing state. Without these emits QML bindings bound to, say,
    // `settings.borderWidth` would not update when the discard path
    // reloads the on-disk values.
    //
    // CRITICAL ORDERING: snapshot MUST be taken BEFORE
    // reparseConfiguration() and before per-screen / virtual-screen
    // loaders mutate members. Store-backed properties read on demand
    // through m_store, so once reparseConfiguration() overwrites the
    // backend's in-memory state, a post-reparse snapshot would see the
    // already-reloaded value and the comparison loop below would never
    // detect a change — silently breaking the discard-changes UX flow.
    //
    // INVARIANT: every Q_PROPERTY on Settings must hold a type whose
    // QVariant comparison is meaningful (Qt builtins, POD enums,
    // QVariantMap, QStringList). Custom Q_GADGETs without a registered
    // operator== will silently miscompare here.
    const QVector<QVariant> propSnapshot = snapshotNotifyProperties();

    // Per-mode disable lists are NOT Q_PROPERTYs (their getters take a Mode
    // argument, which Q_PROPERTY can't express). Snapshot them explicitly
    // so the post-reparse re-emission below can fire the per-mode signals
    // when an external write — daemon shortcut, D-Bus call, discard path —
    // changes a list. Without this, QML consumers bound through the bridge
    // never see cross-process disable-state changes until the page is
    // re-rendered.
    // Per-mode disable-list snapshots — captured per Mode so the post-reload
    // re-emission below can fire one signal per mode that actually changed.
    // Iterating PhosphorZones::allModes() keeps the snapshot in lockstep
    // with the enum: adding a future mode automatically gets snapshotted +
    // re-emitted without touching this block.
    //
    // Snapshot the CANONICAL form, and compare canonically below. The getters
    // return entries in rule order, and setAllRules rewrites the whole rule
    // list, so a reload can hand back the same disable set in a different order
    // for reasons that have nothing to do with these lists. A raw compare would
    // read that as a change and fire disabled*Changed + settingsChanged, which
    // on the daemon side means a retile nobody asked for.
    using Mode = PhosphorZones::AssignmentEntry::Mode;
    QHash<Mode, QStringList> monitorsBefore;
    QHash<Mode, QStringList> desktopsBefore;
    QHash<Mode, QStringList> activitiesBefore;
    for (const Mode mode : PhosphorZones::allModes()) {
        monitorsBefore.insert(mode, canonicalDisableEntries(DisableAxis::Monitor, disabledMonitors(mode)));
        desktopsBefore.insert(mode, canonicalDisableEntries(DisableAxis::Desktop, disabledDesktops(mode)));
        activitiesBefore.insert(mode, canonicalDisableEntries(DisableAxis::Activity, disabledActivities(mode)));
    }

    m_configBackend->reparseConfiguration();

    // Per-mode disable lists live in rules.json, a separate file the
    // config backend's reparseConfiguration() does not touch. Reload the
    // rule store explicitly so a cross-process write (daemon shortcut, KCM
    // D-Bus call) surfaces — the per-mode signal re-emission below compares
    // against the pre-reload snapshot taken above.
    //
    // Only reload when WE own the store. When the store is borrowed (the
    // daemon shares its single store with us, or the settings app's
    // SettingsController owns it and lends it here), the OWNER is the writer
    // and a load() here would clobber unflushed in-memory edits — exactly the
    // dual-store race this borrow pattern exists to avoid. The borrowed-store
    // path relies on the owner to drive reloads (the daemon via its on-startup
    // load + RuleAdaptor; the settings app via
    // SettingsController::reloadLocalRuleStore on the daemon's rulesChanged
    // D-Bus signal and on Discard, plus a RuleStoreWatcher that reloads
    // on external file writes when no daemon is running).
    if (m_ownedRuleStore) {
        m_ownedRuleStore->load();
    }

    // Every schema-declared, store-backed group (the full set buildSettingsSchema
    // registers, e.g. Windows, Gaps, Decorations, Shaders, Animations, ...) needs no
    // explicit load call — their getters read through m_store on demand. Enumerating the
    // groups here just drifts as new ones are added, so it is left to the schema.
    // Per-screen override maps are not Q_PROPERTYs, so the snapshot loop above
    // doesn't cover them. Capture them around the reload so settingsChanged()
    // can fire when a reload (e.g. the daemon's reloadSettings after a Save)
    // brings in new per-screen gap / selector overrides. Without this the
    // daemon reloads the maps but its settingsChanged-driven retile never runs,
    // so per-monitor gaps never take effect on save (discussion #661).
    const QHash<QString, QVariantMap> perScreenZoneSelectorBefore = m_perScreenZoneSelectorSettings;
    const QHash<QString, QVariantMap> perScreenAutotileBefore = m_perScreenAutotileSettings;

    loadPerScreenOverrides(m_configBackend);
    loadVirtualScreenConfigs(m_configBackend);

    const bool perScreenZoneSelectorChanged = perScreenZoneSelectorBefore != m_perScreenZoneSelectorSettings;
    const bool perScreenAutotileChanged = perScreenAutotileBefore != m_perScreenAutotileSettings;
    // Per-monitor gaps are config-backed and live in the per-screen autotile store.
    // A gap change must resnap already-snapped windows, the same way a global gap
    // change or a context gap rule does; the daemon binds that resnap to
    // perScreenSnappingSettingsChanged. Fire it only when the gap-dimension subset
    // changed, so an algorithm/split-only per-screen edit doesn't trigger a spurious
    // gap resnap.
    const bool perScreenGapChanged =
        Settings::perScreenGapDimensionsDiffer(perScreenAutotileBefore, m_perScreenAutotileSettings);
    const bool perScreenChanged = perScreenZoneSelectorChanged || perScreenAutotileChanged;

    if (useSystemColors()) {
        // The derive routes through the public color setters. Squelch their
        // per-setter NOTIFY + settingsChanged emissions for this call only:
        // emitChangedNotifyProperties() below compares the post-derive values
        // against the pre-reparse snapshot and fires each changed NOTIFY
        // exactly once, and the aggregate settingsChanged fires once at the
        // end of load(). eventFilter()'s palette re-derive batches with the
        // same flag; only setUseSystemColors relies on the setters emitting
        // normally.
        QScopedValueRollback<bool> squelch(m_suppressDerivedColorEmissions, true);
        applySystemColorScheme();
    }

    qCInfo(lcConfig) << "Settings loaded";

    // Emit NOTIFY signals for every Q_PROPERTY whose value changed. load()
    // sets members directly (not via setters), so without this loop QML
    // bindings would never see reloaded values after discard / reset.
    const bool anyChanged = emitChangedNotifyProperties(propSnapshot);

    // Per-mode disable lists: emit one signal per Mode whose list changed.
    // Mirrors the Q_PROPERTY loop above but keyed by (signal, mode) instead
    // of by property index. Iterates allModes so future enum extensions
    // automatically participate without touching this block.
    bool anyDisableChanged = false;
    for (const Mode mode : PhosphorZones::allModes()) {
        if (canonicalDisableEntries(DisableAxis::Monitor, disabledMonitors(mode)) != monitorsBefore.value(mode)) {
            Q_EMIT disabledMonitorsChanged(mode);
            anyDisableChanged = true;
        }
        if (canonicalDisableEntries(DisableAxis::Desktop, disabledDesktops(mode)) != desktopsBefore.value(mode)) {
            Q_EMIT disabledDesktopsChanged(mode);
            anyDisableChanged = true;
        }
        if (canonicalDisableEntries(DisableAxis::Activity, disabledActivities(mode)) != activitiesBefore.value(mode)) {
            Q_EMIT disabledActivitiesChanged(mode);
            anyDisableChanged = true;
        }
    }

    // Aggregate signal — fires when ANY property OR ANY per-mode
    // disable list changed. Without `anyDisableChanged` in the
    // predicate, a cross-process write (daemon shortcut, KCM D-Bus
    // call) that updates only a disable list would emit the per-mode
    // signals but NOT the aggregate — inconsistent with the direct
    // setter path (`writeDisableEntries` emits `settingsChanged()`
    // whenever persistence succeeds).
    // Per-screen override maps are not Q_PROPERTYs, so the NOTIFY loop above
    // doesn't cover them. Emit their change signals here, gated on the
    // before/after comparison, so QML per-screen helpers refresh on Discard /
    // Reset and the daemon resnaps on a per-screen gap change — without firing
    // on reloads that left the maps untouched.
    if (perScreenZoneSelectorChanged)
        Q_EMIT perScreenZoneSelectorSettingsChanged();
    if (perScreenAutotileChanged)
        Q_EMIT perScreenAutotileSettingsChanged();
    if (perScreenGapChanged)
        Q_EMIT perScreenSnappingSettingsChanged();

    if (anyChanged || anyDisableChanged || perScreenChanged)
        Q_EMIT settingsChanged();

    // The store now mirrors disk — refresh the committed baseline that
    // per-page Discard reverts to and isKeyModified() compares against.
    captureBaseline();
}

// ── save() dispatcher ────────────────────────────────────────────────────────

// Groups that reset() deletes exhaustively. NOT used by save() — save()
// iterates the schema and lets purgeStaleKeys() handle cleanup. The legacy
// "Updates" group is deliberately absent: nothing writes it anymore (the
// dismissed-update-version moved to the settings app's QSettings), so it is
// swept by reset() as a stale husk rather than managed here.
QStringList Settings::managedGroupNames()
{
    return {
        ConfigDefaults::generalGroup(), // "General"
        ConfigDefaults::snappingGroup(), // "Snapping"
        ConfigDefaults::tilingGroup(), // "Tiling"
        ConfigDefaults::displayGroup(), // "Display" — dead in v4 (per-mode disable lists moved to rules.json);
                                        // listed so reset() drops any partial-v3 migration husk left in this group
        ConfigDefaults::exclusionsGroup(), // "Exclusions"
        ConfigDefaults::performanceGroup(), // "Performance"
        ConfigDefaults::renderingGroup(), // "Rendering"
        ConfigDefaults::shadersGroup(), // "Shaders"
        ConfigDefaults::shortcutsGroup(), // "Shortcuts" — covers Shortcuts.Global + Shortcuts.Tiling
        ConfigDefaults::animationsGroup(), // "Animations"
        ConfigDefaults::animationsWindowFilteringGroup(), // "Animations.WindowFiltering"
        ConfigDefaults::editorGroup(), // "Editor" — covers Editor.Shortcuts + Editor.Snapping + Editor.FillOnDrop
        ConfigDefaults::orderingGroup(), // "Ordering"
        ConfigDefaults::windowsAppearanceGroup(), // "Windows" — window border + title bar decoration
        ConfigDefaults::decorationsWindowFilteringGroup(), // "Decorations.WindowFiltering" — border-pass window filter
        ConfigDefaults::decorationsPerformanceGroup(), // "Decorations.Performance" — idle/focused-only animation gating
        ConfigDefaults::gapsGroup(), // "Gaps" — shared inner/outer gap model
        ConfigDefaults::decorationsGroup(), // "Decorations" — per-surface decoration tree (DecorationProfileTree blob)
                                            // + WindowFiltering + Performance sub-groups
    };
}

void Settings::deletePerScreenGroups(PhosphorConfig::IBackend* backend)
{
    const QStringList allGroups = backend->groupList();
    for (const QString& groupName : allGroups) {
        if (PerScreenPathResolver::isPerScreenPrefix(groupName)
            || groupName.startsWith(ConfigDefaults::virtualScreenGroupPrefix())) {
            backend->deleteGroup(groupName);
        }
    }
    // Explicitly remove the resolver's reserved container (e.g. the "PerScreen"
    // top-level object). groupList() hides this key entirely (it's filtered as
    // a reserved root), so the loop above never sees it — but when every
    // colon-form descendant has been individually deleted, an empty {} husk
    // can survive at the container path. Delete it directly via the raw JSON
    // path so it doesn't linger on disk after reset().
    backend->deleteGroup(PerScreenPathResolver::perScreenKey());
}

void Settings::purgeStaleKeys()
{
    // Root-level groups that must survive a save() cycle — written
    // independently of Settings::save(). Assignment rules live in
    // rules.json (rule-store sidecar) and QuickLayout slots live
    // in quicklayouts.json (separate sidecar) — neither is visible to
    // this purge after the v4 migration retired assignments.json.
    //
    // The backend's root container (where writeRootString / readRootString put
    // ungrouped keys) needs no entry here: IBackend::groupList()'s
    // reserved-name contract requires every backend to keep it out of the
    // enumeration, so Pass 2 cannot reach it. This holds against the interface
    // and not just the JsonBackend we happen to construct today.
    //
    // Root-level KEYS holding stash data that must survive Pass 2's
    // blanket-delete loop. (The legacy "Updates" group used to be carved out
    // here too; nothing writes or reads it since the dismissed-update-version
    // moved to the settings app's QSettings, so Pass 2 now sweeps any stale
    // husk of it like every other unmanaged group.) `_v4DisableStash`,
    // `_v4ExclusionStash` and `_v4AnimationExclusionStash` are JSON
    // OBJECTS and survive Pass 2 via `preservedGroups.contains(topLevel)`
    // membership in the loop body below (without that short-circuit Pass
    // 2's group iteration WOULD delete them); the `_v4AnimationRulesStash`
    // is a JSON ARRAY and is additionally preserved by Pass 2 NOT
    // enumerating non-object root values (jsonbackend's `groupList()`
    // filter), so the listing here is defence-in-depth for that case
    // against future Pass 2 restructuring. All four stashes feed the v4
    // chain-stall retry path in configmigration.cpp::finalizeV4Conversion.
    const QStringList preservedGroups = {
        ConfigKeys::Legacy::v4DisableStashKey(),
        ConfigKeys::Legacy::v4AnimationRulesStashKey(),
        ConfigKeys::Legacy::v4ExclusionStashKey(),
        ConfigKeys::Legacy::v4AnimationExclusionStashKey(),
    };

    // Compute the set of paths the Store claims. These must not be
    // blanket-deleted because Store::write has already persisted authoritative
    // values and no subsequent save*Config call will rewrite them. Their
    // ancestor paths ("Snapping" for "Snapping.Zones.Colors") must also
    // survive as intermediate JSON nodes.
    //
    // `storeKeyPathPrefixes` covers schema keys that hold OBJECT values
    // (e.g. AnimationProfile / ShaderProfileTree under Animations).
    // JsonBackend's groupList() reports any nested object as a dot-path
    // group, so without this set pass 2 would treat the declared object
    // value as a stale group and wipe it on save.
    const auto& schema = m_store->schema();
    QSet<QString> storeGroups;
    QSet<QString> storeAncestors;
    QStringList storeKeyPathPrefixes;
    for (auto it = schema.groups.constBegin(); it != schema.groups.constEnd(); ++it) {
        const QString& groupName = it.key();
        storeGroups.insert(groupName);
        const QStringList segments = groupName.split(QLatin1Char('.'), Qt::SkipEmptyParts);
        QString ancestor;
        for (int i = 0; i + 1 < segments.size(); ++i) {
            ancestor = ancestor.isEmpty() ? segments[i] : (ancestor + QLatin1Char('.') + segments[i]);
            storeAncestors.insert(ancestor);
        }
        for (const auto& def : it.value()) {
            storeKeyPathPrefixes.append(groupName + QLatin1Char('.') + def.key);
        }
    }

    auto isStoreClaimed = [&](const QString& group) {
        if (storeGroups.contains(group) || storeAncestors.contains(group)) {
            return true;
        }
        for (const QString& prefix : storeKeyPathPrefixes) {
            if (group == prefix || group.startsWith(prefix + QLatin1Char('.'))) {
                return true;
            }
        }
        return false;
    };

    // Pass 1: per-key scalar purging inside Store-claimed paths.
    //
    // Store-declared groups: keep the declared key set, delete everything else
    // (evicts stale leftovers from renamed / removed keys).
    //
    // Store ancestor groups (e.g. "Snapping" when the schema declares
    // "Snapping.Zones.Colors"): declared set is empty, so every scalar
    // leaf key gets deleted. save*Config will rewrite valid scalars a moment
    // later; sub-objects (the actual Store-claimed descendants) are preserved
    // because we only touch non-object children.
    auto purgeScalarsIn = [&](const QString& groupName, const QSet<QString>& declared) {
        auto g = m_configBackend->group(groupName);
        // IGroup::keyList returns scalar-leaf keys only; nested sub-groups are
        // filtered out by the backend so deleting undeclared keys here never
        // touches a Store-claimed descendant.
        const QStringList existingKeys = g->keyList();
        for (const QString& key : existingKeys) {
            if (!declared.contains(key)) {
                g->deleteKey(key);
            }
        }
    };

    for (auto it = schema.groups.constBegin(); it != schema.groups.constEnd(); ++it) {
        QSet<QString> declared;
        for (const auto& def : it.value()) {
            declared.insert(def.key);
        }
        purgeScalarsIn(it.key(), declared);
    }
    for (const QString& ancestor : std::as_const(storeAncestors)) {
        // Skip ancestors that are themselves Store-declared groups (e.g.
        // "Snapping.Behavior" carries its own declared keys AND hosts
        // declared descendants). The schema-loop pass above already
        // purged them with the right declared set; wiping with an empty
        // set here would delete those declared scalars.
        if (storeGroups.contains(ancestor)) {
            continue;
        }
        purgeScalarsIn(ancestor, {});
    }

    // Pass 2: anything else in the backend that isn't preserved, isn't
    // claimed by the Store, and isn't a per-screen / virtual-screen group
    // gets blanket-deleted. save*Config runs next and rewrites unmigrated
    // managed groups from their cached members.
    QSet<QString> deletedTopLevels;
    QSet<QString> deletedSubPaths;
    for (const QString& groupName : m_configBackend->groupList()) {
        if (PerScreenPathResolver::isPerScreenPrefix(groupName)) {
            continue;
        }
        if (groupName.startsWith(ConfigDefaults::virtualScreenGroupPrefix())) {
            continue;
        }
        if (isStoreClaimed(groupName)) {
            continue;
        }
        const int dotIdx = groupName.indexOf(QLatin1Char('.'));
        const QString topLevel = (dotIdx >= 0) ? groupName.left(dotIdx) : groupName;
        if (deletedTopLevels.contains(topLevel) || preservedGroups.contains(topLevel)) {
            continue;
        }
        // When the top-level itself is a Store ancestor, we can't delete
        // the whole thing — descend and delete only the non-claimed child.
        if (isStoreClaimed(topLevel)) {
            if (deletedSubPaths.contains(groupName)) {
                continue;
            }
            m_configBackend->deleteGroup(groupName);
            deletedSubPaths.insert(groupName);
        } else {
            m_configBackend->deleteGroup(topLevel);
            deletedTopLevels.insert(topLevel);
        }
    }
}

void Settings::save()
{
    purgeStaleKeys();

    // Flush every Store-declared key so the on-disk file always carries
    // the complete declared set after save, not just the keys a user
    // happened to mutate. Keys that haven't been written yet fall back to
    // the schema default via Store::readVariant, and the write path runs
    // the validator so clamped/normalized values land as the canonical form.
    //
    // INVARIANT: this loop iterates schema-declared keys only. Any key
    // present in the backing store but NOT in the schema is ignored here
    // and purged separately by @c purgeStaleKeys. @c Store::write rejects
    // writes to undeclared keys with a warning, so looping over
    // @c groupList() keys instead would fail closed but noisily.
    const auto& schema = m_store->schema();
    for (auto it = schema.groups.constBegin(); it != schema.groups.constEnd(); ++it) {
        for (const auto& def : it.value()) {
            m_store->write(it.key(), def.key, m_store->readVariant(it.key(), def.key));
        }
    }

    // Store-backed groups persist through setters on every write; the flush
    // loop above rewrites every declared key so clamp/canonicalization takes
    // effect during save(). Only the non-Store groups need explicit save calls.
    saveAllPerScreenOverrides(m_configBackend);
    saveVirtualScreenConfigs(m_configBackend);

    // commit(), not sync(): the baseline moves below on a true return, so this
    // has to be a "the bytes landed" answer. sync() is allowed to return true
    // for a write it merely scheduled (JsonBackend's Deferred policy debounces
    // it), which would advance the baseline against a write that has not
    // happened and may still fail.
    if (!m_configBackend->commit()) {
        // Nothing reached disk. The backend stays dirty and retries on the next
        // sync, so the values are not lost — but the baseline must NOT move:
        // it is the "what disk holds" reference per-page Discard reverts to and
        // isKeyModified() compares against. Advancing it here would tell the
        // user their unsaved edits were saved and take away their ability to
        // discard them.
        qCWarning(lcConfig) << "save: failed to write the configuration to disk — keeping the previous baseline so "
                               "the unsaved values stay discardable; the next save retries";
        return;
    }

    // Disk now holds the flushed store — the just-saved values become the new
    // committed baseline for per-page Discard / dirty checks.
    captureBaseline();
}

bool Settings::exportTo(const QString& filePath)
{
    auto* json = dynamic_cast<PhosphorConfig::JsonBackend*>(m_configBackend);
    if (!json) {
        qCWarning(lcConfig) << "exportTo: backend is not JSON-backed, cannot export";
        return false;
    }

    // Store-backed groups persist through their setters on every write, so the
    // backend's in-memory root already carries whatever the user currently has,
    // saved or not. The non-Store groups (per-screen overrides, virtual screen
    // configs) only reach it during save(), so write them in as well. The
    // helpers can only stage into the LIVE root, and anything left there would
    // be committed by a later sync() or the backend's flush-on-destruction.
    // So: snapshot the root and dirty flag first, stage, take the export
    // snapshot, then restore both — the staged groups reach the export
    // document only, never the live config.
    const QJsonObject liveRoot = json->jsonRootSnapshot();
    const bool wasDirty = json->isDirty();

    saveAllPerScreenOverrides(m_configBackend);
    saveVirtualScreenConfigs(m_configBackend);
    QJsonObject exportRoot = json->jsonRootSnapshot();
    // sync() stamps the schema version on its way to disk, so a store that has
    // never synced carries no `_version` in memory. Apply the same stamp here
    // or the export reads back as version-less and re-runs the entire migration
    // chain when someone imports it.
    json->applyVersionStamp(exportRoot);

    json->replaceRoot(liveRoot);
    if (!wasDirty) {
        // replaceRoot marks dirty as a precaution; the root we put back is
        // exactly the one that matched disk, so restore the clean state.
        json->clearDirty();
    }

    // Deliberately no sync() and no captureBaseline(). Exporting must not
    // commit pending edits to the live config, and must not move the baseline
    // that per-page Discard reverts to.
    return PhosphorConfig::JsonBackend::writeJsonAtomically(filePath, exportRoot);
}

// ── Settings-profiles support ────────────────────────────────────────────────

QJsonObject Settings::exportConfigToJson() const
{
    return m_store->exportToJson();
}

QJsonObject Settings::defaultConfigJson() const
{
    // Store::defaultsToJson mirrors exportToJson's shape and version stamp;
    // delegating keeps the two snapshots field-for-field comparable, which the
    // profiles delta engine depends on. A local re-implementation here could
    // drift from exportToJson's serialization without anything noticing.
    return m_store->defaultsToJson();
}

bool Settings::applyConfigOverlayStaged(const QJsonObject& fullConfigBlob)
{
    // Same snapshot / re-emit contract as load(), minus the disk round-trip:
    // snapshot the NOTIFY-able properties BEFORE mutating the store, overwrite
    // the declared keys in memory (no commit/sync), then fire the NOTIFY for
    // every property whose value actually changed. Deliberately NO
    // captureBaseline() — the store must diverge from the committed baseline so
    // the settings app reports the staged keys as unsaved edits.
    //
    // Also deliberately no applySystemColorScheme() re-derivation: a profile
    // captures the zone colours it was saved with, and activation stages those
    // captured values even when UseSystemColors is on. The live palette is
    // re-derived on the next load() or palette-change event (app restart, a
    // Discard-triggered reload, or a theme switch) — save() itself only
    // commits, it does not re-run the palette derivation.
    const QVector<QVariant> propSnapshot = snapshotNotifyProperties();

    // importFromJson is additive/overwriting over declared keys; a
    // fully-resolved profile blob carries every declared key (defaults included)
    // so keys the profile leaves at default are written back to default too.
    // It REFUSES a blob stamped with a different schema version, writing
    // nothing — surface that instead of silently staging a no-op.
    if (!m_store->importFromJson(fullConfigBlob)) {
        qCWarning(lcConfig) << "applyConfigOverlayStaged: store rejected the blob (schema version mismatch?)"
                            << "— nothing was staged";
        return false;
    }

    const bool anyChanged = emitChangedNotifyProperties(propSnapshot);
    if (anyChanged)
        Q_EMIT settingsChanged();
    return true;
}

// ── Per-page reset / discard support ────────────────────────────────────────

QVector<QVariant> Settings::snapshotNotifyProperties() const
{
    // Index-aligned to the metaobject property table so the paired
    // emitChangedNotifyProperties() can compare position-for-position.
    // Inherited QObject properties (objectName) are skipped — only Settings'
    // own NOTIFY-able readable properties participate.
    const QMetaObject* mo = metaObject();
    const int propCount = mo->propertyCount();
    const int firstOwnProp = QObject::staticMetaObject.propertyCount();
    QVector<QVariant> snapshot;
    snapshot.resize(propCount);
    for (int i = firstOwnProp; i < propCount; ++i) {
        const QMetaProperty prop = mo->property(i);
        if (prop.hasNotifySignal() && prop.isReadable())
            snapshot[i] = prop.read(this);
    }
    return snapshot;
}

bool Settings::emitChangedNotifyProperties(const QVector<QVariant>& before)
{
    const QMetaObject* mo = metaObject();
    const int propCount = mo->propertyCount();
    const int firstOwnProp = QObject::staticMetaObject.propertyCount();
    bool anyChanged = false;
    for (int i = firstOwnProp; i < propCount; ++i) {
        const QMetaProperty prop = mo->property(i);
        if (!prop.hasNotifySignal() || !prop.isReadable())
            continue;
        const QVariant newValue = prop.read(this);
        if (newValue != before.value(i)) {
            anyChanged = true;
            const QMetaMethod notify = prop.notifySignal();
            notify.invoke(this, Qt::DirectConnection);
        }
    }
    return anyChanged;
}

void Settings::captureBaseline()
{
    m_baseline.clear();
    const auto& schema = m_store->schema();
    for (auto it = schema.groups.constBegin(); it != schema.groups.constEnd(); ++it) {
        QVariantMap groupMap;
        for (const auto& def : it.value()) {
            // readVariant returns the schema default for never-written keys, so
            // the baseline always carries a concrete value per declared key.
            groupMap.insert(def.key, m_store->readVariant(it.key(), def.key));
        }
        m_baseline.insert(it.key(), groupMap);
    }
}

void Settings::rebaselineDerivedColorKeys()
{
    // System-colors mode owns the four zone-color keys: applySystemColorScheme()
    // DERIVES them from the application palette, so a runtime palette change
    // writes them through m_store AFTER the settings app captured its baseline
    // — without this refresh, isKeyModified() reports a phantom unsaved edit
    // and Discard reverts to the stale pre-switch colors.
    //
    // Callable ONLY from the ApplicationPaletteChange path (eventFilter) —
    // and even there only when the useSystemColors toggle is COMMITTED
    // (!isKeyModified on the useSystem key). If the toggle is a pending
    // unsaved edit, the toggle and the colors it derived are ONE user edit
    // and must stay discardable together; rebaselining mid-edit would let
    // Discard revert the toggle to off while pinning the palette-derived
    // colors as the new baseline.
    //
    // applySystemColorScheme() is not Q_INVOKABLE (no QML caller), so its
    // call sites are exactly three; the other two must not rebaseline:
    // - setUseSystemColors(true) is a USER edit; the toggle and the colors it
    //   derives must stay discardable together. Rebaselining there would let
    //   Discard revert the toggle to off while pinning the system-derived
    //   colors as the new baseline.
    // - load() derives BEFORE captureBaseline(), so the full baseline already
    //   carries the derived values.
    const ConfigKeyList derivedKeys{
        {ConfigDefaults::snappingZonesColorsGroup(), ConfigDefaults::highlightKey()},
        {ConfigDefaults::snappingZonesColorsGroup(), ConfigDefaults::inactiveKey()},
        {ConfigDefaults::snappingZonesColorsGroup(), ConfigDefaults::borderKey()},
        {ConfigDefaults::snappingZonesLabelsGroup(), ConfigDefaults::fontColorKey()},
    };
    for (const ConfigKey& gk : derivedKeys) {
        m_baseline[gk.first].insert(gk.second, m_store->readVariant(gk.first, gk.second));
    }
}

bool Settings::isKeyModified(const QString& group, const QString& key) const
{
    return m_store->readVariant(group, key) != m_baseline.value(group).value(key);
}

void Settings::discardKeys(const ConfigKeyList& keys)
{
    const QVector<QVariant> before = snapshotNotifyProperties();
    for (const ConfigKey& gk : keys) {
        // Only keys captured in the committed baseline (i.e. schema-declared) can
        // be reverted; a mistyped manifest entry would otherwise write a default-
        // constructed QVariant() over a live value. Skip it instead.
        const auto groupIt = m_baseline.constFind(gk.first);
        if (groupIt == m_baseline.constEnd())
            continue;
        const auto keyIt = groupIt->constFind(gk.second);
        if (keyIt == groupIt->constEnd())
            continue;
        // Revert to the committed baseline, but only when it actually differs, so
        // an unmodified key in the list is not needlessly re-written. write() runs
        // the schema validator, so the reverted value lands in its canonical form
        // (matching what a fresh load() would produce).
        if (m_store->readVariant(gk.first, gk.second) != *keyIt)
            m_store->write(gk.first, gk.second, *keyIt);
    }
    if (emitChangedNotifyProperties(before))
        Q_EMIT settingsChanged();
}

void Settings::resetKeys(const ConfigKeyList& keys)
{
    const QVector<QVariant> before = snapshotNotifyProperties();
    for (const ConfigKey& gk : keys) {
        m_store->reset(gk.first, gk.second);
    }
    if (emitChangedNotifyProperties(before))
        Q_EMIT settingsChanged();
}

void Settings::connectRuleStoreGapReactivity()
{
    if (m_ruleStore == nullptr) {
        return;
    }
    // The global and per-screen gaps are config-backed now, so nothing here
    // seeds them. Only the context (per-mode / per-desktop / per-activity) gap
    // rules remain rule-backed and feed the geometry cascade — seed their
    // fingerprint so the first rulesChanged emit compares against a real snapshot.
    m_cachedGapFingerprint = gapRulesFingerprint();
    connect(m_ruleStore, &PhosphorRules::RuleStore::rulesChanged, this, [this](bool /*persisted*/) {
        onRuleStoreChanged();
    });
}

void Settings::onRuleStoreChanged()
{
    // Global inner/outer gaps are config-backed; their NOTIFY is owned by the
    // gap setters, not this handler.
    //
    // A rule edit can also change a context (per-mode / per-desktop / per-
    // activity) gap rule, which feeds the geometry cascade (mergeConfigPerScreen-
    // Gaps folds config per-monitor gaps under these rule overrides). Re-sync the
    // geometry consumers — the daemon's perScreenSnappingSettingsChanged handler
    // reschedules the gap resnap, and its settingsChanged handler re-runs the
    // per-screen autotile config (updateAutotileScreens) plus
    // refreshConfigFromSettings — but ONLY when a gap action somewhere in the rule
    // set actually changed. Emitting on every rulesChanged made a mode/assignment
    // toggle (also a rule write) fire settingsChanged, which drove the daemon to
    // re-resolve the default assignment and immediately revert the toggle. The
    // fingerprint covers every gap action across all rules, so non-gap rule writes
    // no longer trigger a gap re-sync.
    const QString gapFingerprint = gapRulesFingerprint();
    if (gapFingerprint != m_cachedGapFingerprint) {
        m_cachedGapFingerprint = gapFingerprint;
        Q_EMIT perScreenAutotileSettingsChanged();
        Q_EMIT perScreenSnappingSettingsChanged();
        Q_EMIT settingsChanged();
    }
}

// Fingerprint of every gap action across the whole rule set (the context per-
// mode / per-desktop / per-activity gap rules), used to gate the gap re-sync in
// onRuleStoreChanged so only real gap edits fire it — never a mode/assignment/
// border rule write.
QString Settings::gapRulesFingerprint() const
{
    if (m_ruleStore == nullptr) {
        return QString();
    }
    static const QSet<QString> gapTypes = {
        QString(PhosphorRules::ActionType::SetInnerGap),           QString(PhosphorRules::ActionType::SetOuterGap),
        QString(PhosphorRules::ActionType::SetUsePerSideOuterGap), QString(PhosphorRules::ActionType::SetOuterGapTop),
        QString(PhosphorRules::ActionType::SetOuterGapBottom),     QString(PhosphorRules::ActionType::SetOuterGapLeft),
        QString(PhosphorRules::ActionType::SetOuterGapRight)};
    QString fingerprint;
    const QString valueKey = QString(PhosphorRules::ActionParam::Value);
    for (const PhosphorRules::Rule& rule : m_ruleStore->ruleSet().rules()) {
        for (const PhosphorRules::RuleAction& action : rule.actions) {
            if (gapTypes.contains(action.type)) {
                fingerprint += rule.id.toString();
                fingerprint += QLatin1Char('|');
                fingerprint += action.type;
                fingerprint += QLatin1Char('=');
                fingerprint += action.params.value(valueKey).toVariant().toString();
                fingerprint += QLatin1Char(';');
            }
        }
    }
    return fingerprint;
}

P_STORE_GET(int, adjacentThreshold, snappingGapsGroup, adjacentThresholdKey, int)
P_STORE_SET_INT(setAdjacentThreshold, snappingGapsGroup, adjacentThresholdKey, adjacentThresholdChanged)

// ── Display (PhosphorConfig::Store-backed) ──────────────────────────────────
// Display.* keys live in snappingBehaviorDisplayGroup; OSD + Effects keys
// live in snappingEffectsGroup.
// QStringList keys go over the wire as comma-joined strings; the getters
// parse back via parseCommaList (settings_detail::parseCommaList in
// settings/settings_detail.h).

P_STORE_GET(bool, showZonesOnAllMonitors, snappingBehaviorDisplayGroup, showOnAllMonitorsKey, bool)
P_STORE_SET_BOOL(setShowZonesOnAllMonitors, snappingBehaviorDisplayGroup, showOnAllMonitorsKey,
                 showZonesOnAllMonitorsChanged)

P_STORE_GET(bool, showZoneNumbers, snappingEffectsGroup, showNumbersKey, bool)
P_STORE_SET_BOOL(setShowZoneNumbers, snappingEffectsGroup, showNumbersKey, showZoneNumbersChanged)
P_STORE_GET(bool, flashZonesOnSwitch, snappingEffectsGroup, flashOnSwitchKey, bool)
P_STORE_SET_BOOL(setFlashZonesOnSwitch, snappingEffectsGroup, flashOnSwitchKey, flashZonesOnSwitchChanged)
P_STORE_GET(bool, showOsdOnLayoutSwitch, snappingEffectsGroup, osdOnLayoutSwitchKey, bool)
P_STORE_SET_BOOL(setShowOsdOnLayoutSwitch, snappingEffectsGroup, osdOnLayoutSwitchKey, showOsdOnLayoutSwitchChanged)
P_STORE_GET(bool, showOsdOnDesktopSwitch, snappingEffectsGroup, osdOnDesktopSwitchKey, bool)
P_STORE_SET_BOOL(setShowOsdOnDesktopSwitch, snappingEffectsGroup, osdOnDesktopSwitchKey, showOsdOnDesktopSwitchChanged)
P_STORE_GET(bool, showNavigationOsd, snappingEffectsGroup, navigationOsdKey, bool)
P_STORE_SET_BOOL(setShowNavigationOsd, snappingEffectsGroup, navigationOsdKey, showNavigationOsdChanged)

// ── reset / color helpers ────────────────────────────────────────────────────

void Settings::reset()
{
    // Delete all managed groups (reset nukes everything). The explicit
    // "Updates" sweep scrubs the retired legacy group from configs written by
    // older builds — nothing writes or reads it anymore (dismissed-update-
    // version lives in the settings app's QSettings).
    for (const QString& groupName : managedGroupNames()) {
        m_configBackend->deleteGroup(groupName);
    }
    m_configBackend->deleteGroup(ConfigDefaults::updatesGroup());
    deletePerScreenGroups(m_configBackend);
    // commit(), not sync(), for the same reason save() uses it: sync() is
    // allowed to return true for a write it merely scheduled (JsonBackend's
    // Deferred policy debounces it), and the result gates whether the other two
    // stores are touched at all.
    //
    // config.json goes FIRST, and a failure returns before the session file and
    // the rule store are touched. reset() spans three files with no journal, so
    // the only ordering that cannot half-apply is: do the one that can fail
    // cleanly (nothing has been destroyed yet) before the two that are
    // irreversible. Returning here is safe precisely because deleteGroup only
    // mutated the backend's in-memory root, and the load() below reparses from
    // disk, discarding those deletions and restoring the live state.
    if (!m_configBackend->commit()) {
        qCWarning(lcConfig) << "reset: failed to write the cleared configuration to disk — the deletions were "
                               "dropped and the previous values remain";
        // Reparse from disk so the in-memory store stops reporting the dropped
        // deletions, and re-baseline onto what disk actually holds. The session
        // file and the rule store were never touched, so there is nothing else
        // to undo.
        load();
        return;
    }
    if (!QFile::remove(ConfigDefaults::sessionFilePath()) && QFile::exists(ConfigDefaults::sessionFilePath())) {
        qCWarning(lcConfig) << "Failed to remove session file:" << ConfigDefaults::sessionFilePath();
    }

    // Per-mode disable lists live in rules.json as DisableEngine
    // context rules — drop every such rule from the store (assignment /
    // animation / exclude rules are untouched: reset() only owns the
    // settings surface). The store persists on setAllRules, so the final
    // load() below re-reads the now-cleared file.
    //
    // load()'s own per-mode change-detection snapshots the six disable lists
    // from the *live* store; clearing the store before load() runs would
    // leave that snapshot empty, so the per-mode disabled*Changed signals
    // would never fire even when reset() actually removed disable rules.
    // Snapshot the six lists here — before the clear — and re-emit the
    // per-mode signals ourselves after load(), comparing against this
    // pre-clear state.
    // See the matching block in load(): snapshot the six pre-clear disable
    // lists keyed by Mode so the per-mode re-emit loop after load() can fire
    // exactly one signal per Mode whose list actually changed. Iterating
    // PhosphorZones::allModes() keeps this lockstep with the enum.
    using Mode = PhosphorZones::AssignmentEntry::Mode;
    QHash<Mode, QStringList> resetMonitorsBefore;
    QHash<Mode, QStringList> resetDesktopsBefore;
    QHash<Mode, QStringList> resetActivitiesBefore;
    // Snapshot in canonical form — the same canonicalDisableEntries mechanism
    // load() uses — so entries that canonicalize away (unresolvable connector
    // names, formatting variants) don't produce spurious change signals when
    // the raw pre-clear list differs only cosmetically from the post-load one.
    for (const Mode mode : PhosphorZones::allModes()) {
        resetMonitorsBefore.insert(mode, canonicalDisableEntries(DisableAxis::Monitor, disabledMonitors(mode)));
        resetDesktopsBefore.insert(mode, canonicalDisableEntries(DisableAxis::Desktop, disabledDesktops(mode)));
        resetActivitiesBefore.insert(mode, canonicalDisableEntries(DisableAxis::Activity, disabledActivities(mode)));
    }
    {
        namespace CRB = PhosphorRules::ContextRuleBridge;
        QList<PhosphorRules::Rule> kept;
        for (const PhosphorRules::Rule& rule : m_ruleStore->ruleSet().rules()) {
            if (!CRB::disableRuleMode(rule)) {
                kept.append(rule); // not a DisableEngine rule — preserve
            }
        }
        if (kept.size() != m_ruleStore->count()) {
            if (!m_ruleStore->setAllRules(kept)) {
                // Persistence failed — the in-memory store advanced but the
                // on-disk file is stale. Roll back the in-memory state by
                // reloading from disk so the per-mode re-emit loop below
                // doesn't fire `disabled*Changed` for changes that didn't
                // land. The owned-store path falls through to load()
                // (which reloads its own copy); the borrowed-store path
                // (daemon) skips reloading the rules in load() and would
                // otherwise leave the in-memory store advanced — explicitly
                // reload via the store's own load() to roll back. The
                // settings load() below also re-snapshots disable rules
                // from the post-rollback store, so a follow-up flag was
                // unnecessary.
                qCWarning(lcConfig)
                    << "reset: failed to persist window-rule store — rolling back in-memory state; disable "
                       "rules will reappear on next launch";
                m_ruleStore->load();
            }
        }
    }

    load();

    // No baseline fix-up here: load() reparses config.json from disk and closes
    // with captureBaseline(), so the baseline already describes exactly what
    // disk holds.

    // Re-emit per-mode disable signals against the pre-clear snapshot taken
    // above. load()'s internal snapshot saw the already-cleared store, so it
    // could not detect that reset() removed disable rules — this loop covers
    // the gap. All six lists are empty post-clear, so any non-empty pre-clear
    // list fires exactly once. Track aggregate change so the same
    // `settingsChanged()` invariant load() enforces (any disable change → fire
    // aggregate) also holds for the reset path; otherwise a reset that only
    // cleared disable rules would fire the per-mode signals but not the
    // aggregate.
    bool anyDisableChanged = false;
    for (const Mode mode : PhosphorZones::allModes()) {
        if (canonicalDisableEntries(DisableAxis::Monitor, disabledMonitors(mode)) != resetMonitorsBefore.value(mode)) {
            Q_EMIT disabledMonitorsChanged(mode);
            anyDisableChanged = true;
        }
        if (canonicalDisableEntries(DisableAxis::Desktop, disabledDesktops(mode)) != resetDesktopsBefore.value(mode)) {
            Q_EMIT disabledDesktopsChanged(mode);
            anyDisableChanged = true;
        }
        if (canonicalDisableEntries(DisableAxis::Activity, disabledActivities(mode))
            != resetActivitiesBefore.value(mode)) {
            Q_EMIT disabledActivitiesChanged(mode);
            anyDisableChanged = true;
        }
    }
    if (anyDisableChanged) {
        Q_EMIT settingsChanged();
    }

    qCInfo(lcConfig) << "Settings reset to defaults";
}

} // namespace PlasmaZones

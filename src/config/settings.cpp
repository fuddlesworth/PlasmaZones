// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settings.h"
#include "colorimporter.h"
#include "configdefaults.h"
#include "configbackends.h"
#include "configmigration.h"
#include "perscreenresolver.h"
#include "settingsschema.h"

#include "../core/animationshadersupportedpaths.h"
#include "../core/constants.h"
#include "../core/logging.h"
#include "../core/utils.h"

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

// ── Per-mode disable-list helpers ────────────────────────────────────────────
// Declared up here rather than beside the disable-list section further down
// because load()'s change detection is the first user of them. See that
// section for what the (axis, mode) rule families are.

// Disable-axis enum mirrors the persisted (axis, mode) family layout. Distinct
// from `ContextRuleBridge::ContextAxis` only because the bridge enum also
// carries `CatchAll` and `Combined`, neither of which is a managed disable
// family — see `axisOf` for why `Combined` is excluded.
enum class DisableAxis {
    Monitor,
    Desktop,
    Activity
};

// Resolve a connector name ("DP-2") to its stable screen id
// ("Manuf:Model:Serial"), or return @p screen unchanged.
//
// The ONE resolution rule for the disable lists, shared by the getter
// (disabledMonitors, which resolves on every read) and by
// canonicalDisableEntries. The two must agree exactly: if the canonical form
// resolved an entry differently from the getter, re-saving an unchanged list
// would look like a change and misfire disabled*Changed.
//
// An unresolvable name falls through to the name itself, which is what we want:
// canonicalizing an entry down to an empty screen segment would erase which
// screen it names. That fall-through is idForName's own contract — when no live
// screen carries the connector (an unplugged monitor, a name from another
// machine) it returns the NAME UNCHANGED, never empty. So there is nothing to
// guard against here: for a connector name the result is either the resolved id
// or the name back, and a non-connector name (which includes the empty string,
// isConnectorName rejects it) is already canonical. Mirrors the fall-through in
// ScreenIdentity::variantsFor.
QString resolveScreenId(const QString& screen)
{
    if (PhosphorScreens::ScreenIdentity::isConnectorName(screen)) {
        return PhosphorScreens::ScreenIdentity::idForName(screen);
    }
    return screen;
}

// The canonical form of a disable list for @p axis: entries trimmed, their
// screen segment resolved through resolveScreenId, malformed entries dropped,
// duplicates collapsed, and the result sorted.
//
// This is what "the same disable list" means. Two lists are the same set of
// disable rules iff their canonical forms are equal — the store returns
// entries in rule order, so a raw list compare would report a change for a
// mere reordering (setAllRules rewrites the whole list, so rule order churns
// for reasons that have nothing to do with this axis).
//
// For Desktop/Activity the entry is a composite (`screenId/desktop`,
// `screenId/activity`); split on the LAST '/' so a screen id that legitimately
// contains one (the disambiguated `Manuf:Model:Serial/CONNECTOR` shape) isn't
// truncated. Entries the write path's parse loop would reject (missing or edge
// '/', and for Desktop a non-positive or non-numeric desktop segment) are
// dropped here as well: the canonical form has to be the EFFECTIVE set, or a
// write whose every kept entry already matches the current rules would look
// like a change.
QStringList canonicalDisableEntries(DisableAxis axis, const QStringList& list)
{
    QStringList c;
    for (const QString& raw : list) {
        QString value = raw.trimmed();
        if (axis == DisableAxis::Monitor) {
            value = resolveScreenId(value);
        } else {
            const int slash = value.lastIndexOf(QLatin1Char('/'));
            if (slash <= 0 || slash == value.size() - 1) {
                continue;
            }
            const QString screen = resolveScreenId(value.left(slash));
            if (axis == DisableAxis::Desktop) {
                bool ok = false;
                const int desktop = value.mid(slash + 1).toInt(&ok);
                if (!ok || desktop <= 0) {
                    continue;
                }
                // Rebuild the desktop segment via QString::number so the
                // canonical form matches the getter's serialization
                // (disableEntriesFor) — otherwise numeric aliases like "+3" or
                // "03" survive as distinct entries, defeat the write path's
                // no-op guard, and produce a second disable rule with the same
                // deterministic UUID.
                value = screen + QLatin1Char('/') + QString::number(desktop);
            } else {
                value = screen + QLatin1Char('/') + value.mid(slash + 1);
            }
        }
        if (!value.isEmpty() && !c.contains(value)) {
            c.append(value);
        }
    }
    c.sort();
    return c;
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
        // end of load(). Outside load() (eventFilter's palette re-derive,
        // setUseSystemColors) the setters must keep emitting normally.
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
// iterates the schema and lets purgeStaleKeys() handle cleanup. Does NOT
// include unmanaged groups (Updates) which are written independently and
// must survive a forced default-restore.
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
    // Mixed list of (a) root-level GROUP names that must survive Pass 2's
    // blanket-delete loop ("Updates")
    // and (b) root-level KEYS holding stash data — `_v4DisableStash`,
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
        ConfigDefaults::updatesGroup(),
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

// ── Shaders (PhosphorConfig::Store-backed) ──────────────────────────────────

int Settings::shaderFrameRate() const
{
    return m_store->read<int>(ConfigDefaults::shadersGroup(), ConfigDefaults::frameRateKey());
}

void Settings::setShaderFrameRate(int fps)
{
    // Schema validator clamps on write, so a read-back after the write gives
    // the canonical value even if the caller passed something out of range.
    const int before = shaderFrameRate();
    m_store->write(ConfigDefaults::shadersGroup(), ConfigDefaults::frameRateKey(), fps);
    if (shaderFrameRate() == before) {
        return;
    }
    Q_EMIT shaderFrameRateChanged();
    Q_EMIT settingsChanged();
}

bool Settings::enableAudioVisualizer() const
{
    return m_store->read<bool>(ConfigDefaults::shadersAudioGroup(), ConfigDefaults::enabledKey());
}

void Settings::setEnableAudioVisualizer(bool enable)
{
    if (enableAudioVisualizer() == enable) {
        return;
    }
    m_store->write(ConfigDefaults::shadersAudioGroup(), ConfigDefaults::enabledKey(), enable);
    Q_EMIT enableAudioVisualizerChanged();
    Q_EMIT settingsChanged();
}

int Settings::audioSpectrumBarCount() const
{
    return m_store->read<int>(ConfigDefaults::shadersAudioGroup(), ConfigDefaults::barsKey());
}

void Settings::setAudioSpectrumBarCount(int count)
{
    const int before = audioSpectrumBarCount();
    m_store->write(ConfigDefaults::shadersAudioGroup(), ConfigDefaults::barsKey(), count);
    if (audioSpectrumBarCount() == before) {
        return;
    }
    Q_EMIT audioSpectrumBarCountChanged();
    Q_EMIT settingsChanged();
}

// ── Store-backed getter/setter macros ───────────────────────────────────────
// Shared by every group migrated to PhosphorConfig::Store. Each macro
// expands to a mechanical "read through m_store / write through m_store,
// then check change before NOTIFY" implementation. Clamp / normalize
// behaviour lives in the schema validator, so the read-back after write
// gives the canonical value.
//
// These are local to settings.cpp and #undef'd at the bottom of the file.

#define P_STORE_GET(retType, fn, group, key, readType)                                                                 \
    retType Settings::fn() const                                                                                       \
    {                                                                                                                  \
        return m_store->read<readType>(ConfigDefaults::group(), ConfigDefaults::key());                                \
    }

#define P_STORE_SET_BOOL(fn, group, key, signal)                                                                       \
    void Settings::fn(bool value)                                                                                      \
    {                                                                                                                  \
        const bool before = m_store->read<bool>(ConfigDefaults::group(), ConfigDefaults::key());                       \
        m_store->write(ConfigDefaults::group(), ConfigDefaults::key(), value);                                         \
        const bool after = m_store->read<bool>(ConfigDefaults::group(), ConfigDefaults::key());                        \
        if (after == before) {                                                                                         \
            return;                                                                                                    \
        }                                                                                                              \
        Q_EMIT signal();                                                                                               \
        Q_EMIT settingsChanged();                                                                                      \
    }

#define P_STORE_SET_INT(fn, group, key, signal)                                                                        \
    void Settings::fn(int value)                                                                                       \
    {                                                                                                                  \
        const int before = m_store->read<int>(ConfigDefaults::group(), ConfigDefaults::key());                         \
        m_store->write(ConfigDefaults::group(), ConfigDefaults::key(), value);                                         \
        const int after = m_store->read<int>(ConfigDefaults::group(), ConfigDefaults::key());                          \
        if (after == before) {                                                                                         \
            return;                                                                                                    \
        }                                                                                                              \
        Q_EMIT signal();                                                                                               \
        Q_EMIT settingsChanged();                                                                                      \
    }

#define P_STORE_SET_DOUBLE(fn, group, key, signal)                                                                     \
    void Settings::fn(qreal value)                                                                                     \
    {                                                                                                                  \
        const qreal before = m_store->read<double>(ConfigDefaults::group(), ConfigDefaults::key());                    \
        m_store->write(ConfigDefaults::group(), ConfigDefaults::key(), value);                                         \
        const qreal after = m_store->read<double>(ConfigDefaults::group(), ConfigDefaults::key());                     \
        if (qFuzzyCompare(1.0 + before, 1.0 + after)) {                                                                \
            return;                                                                                                    \
        }                                                                                                              \
        Q_EMIT signal();                                                                                               \
        Q_EMIT settingsChanged();                                                                                      \
    }

#define P_STORE_SET_COLOR(fn, group, key, signal)                                                                      \
    void Settings::fn(const QColor& value)                                                                             \
    {                                                                                                                  \
        const QColor before = m_store->read<QColor>(ConfigDefaults::group(), ConfigDefaults::key());                   \
        m_store->write(ConfigDefaults::group(), ConfigDefaults::key(), value);                                         \
        const QColor after = m_store->read<QColor>(ConfigDefaults::group(), ConfigDefaults::key());                    \
        if (after == before) {                                                                                         \
            return;                                                                                                    \
        }                                                                                                              \
        /* load()'s applySystemColorScheme() derive: the value is persisted                                            \
           above, but load() announces once via emitChangedNotifyProperties +                                          \
           its single settingsChanged — a setter-level emission here would                                           \
           duplicate both. */                                                                                          \
        if (m_suppressDerivedColorEmissions) {                                                                         \
            return;                                                                                                    \
        }                                                                                                              \
        Q_EMIT signal();                                                                                               \
        Q_EMIT settingsChanged();                                                                                      \
    }

#define P_STORE_SET_STRING(fn, group, key, signal)                                                                     \
    void Settings::fn(const QString& value)                                                                            \
    {                                                                                                                  \
        const QString before = m_store->read<QString>(ConfigDefaults::group(), ConfigDefaults::key());                 \
        m_store->write(ConfigDefaults::group(), ConfigDefaults::key(), value);                                         \
        const QString after = m_store->read<QString>(ConfigDefaults::group(), ConfigDefaults::key());                  \
        if (after == before) {                                                                                         \
            return;                                                                                                    \
        }                                                                                                              \
        Q_EMIT signal();                                                                                               \
        Q_EMIT settingsChanged();                                                                                      \
    }

// ── Appearance (PhosphorConfig::Store-backed) ───────────────────────────────
// Colors group
P_STORE_GET(bool, useSystemColors, snappingZonesColorsGroup, useSystemKey, bool)
void Settings::setUseSystemColors(bool use)
{
    if (useSystemColors() == use) {
        return;
    }
    m_store->write(ConfigDefaults::snappingZonesColorsGroup(), ConfigDefaults::useSystemKey(), use);
    if (use) {
        applySystemColorScheme();
    }
    Q_EMIT useSystemColorsChanged();
    Q_EMIT settingsChanged();
}
P_STORE_GET(QColor, highlightColor, snappingZonesColorsGroup, highlightKey, QColor)
P_STORE_SET_COLOR(setHighlightColor, snappingZonesColorsGroup, highlightKey, highlightColorChanged)
P_STORE_GET(QColor, inactiveColor, snappingZonesColorsGroup, inactiveKey, QColor)
P_STORE_SET_COLOR(setInactiveColor, snappingZonesColorsGroup, inactiveKey, inactiveColorChanged)
P_STORE_GET(QColor, borderColor, snappingZonesColorsGroup, borderKey, QColor)
P_STORE_SET_COLOR(setBorderColor, snappingZonesColorsGroup, borderKey, borderColorChanged)

// Labels group
P_STORE_GET(QColor, labelFontColor, snappingZonesLabelsGroup, fontColorKey, QColor)
P_STORE_SET_COLOR(setLabelFontColor, snappingZonesLabelsGroup, fontColorKey, labelFontColorChanged)
P_STORE_GET(QString, labelFontFamily, snappingZonesLabelsGroup, fontFamilyKey, QString)
P_STORE_SET_STRING(setLabelFontFamily, snappingZonesLabelsGroup, fontFamilyKey, labelFontFamilyChanged)
P_STORE_GET(qreal, labelFontSizeScale, snappingZonesLabelsGroup, fontSizeScaleKey, double)
P_STORE_SET_DOUBLE(setLabelFontSizeScale, snappingZonesLabelsGroup, fontSizeScaleKey, labelFontSizeScaleChanged)
P_STORE_GET(int, labelFontWeight, snappingZonesLabelsGroup, fontWeightKey, int)
P_STORE_SET_INT(setLabelFontWeight, snappingZonesLabelsGroup, fontWeightKey, labelFontWeightChanged)
P_STORE_GET(bool, labelFontItalic, snappingZonesLabelsGroup, fontItalicKey, bool)
P_STORE_SET_BOOL(setLabelFontItalic, snappingZonesLabelsGroup, fontItalicKey, labelFontItalicChanged)
P_STORE_GET(bool, labelFontUnderline, snappingZonesLabelsGroup, fontUnderlineKey, bool)
P_STORE_SET_BOOL(setLabelFontUnderline, snappingZonesLabelsGroup, fontUnderlineKey, labelFontUnderlineChanged)
P_STORE_GET(bool, labelFontStrikeout, snappingZonesLabelsGroup, fontStrikeoutKey, bool)
P_STORE_SET_BOOL(setLabelFontStrikeout, snappingZonesLabelsGroup, fontStrikeoutKey, labelFontStrikeoutChanged)

// Opacity group
P_STORE_GET(qreal, activeOpacity, snappingZonesOpacityGroup, activeKey, double)
P_STORE_SET_DOUBLE(setActiveOpacity, snappingZonesOpacityGroup, activeKey, activeOpacityChanged)
P_STORE_GET(qreal, inactiveOpacity, snappingZonesOpacityGroup, inactiveKey, double)
P_STORE_SET_DOUBLE(setInactiveOpacity, snappingZonesOpacityGroup, inactiveKey, inactiveOpacityChanged)

// Border group
P_STORE_GET(int, borderWidth, snappingZonesBorderGroup, widthKey, int)
P_STORE_SET_INT(setBorderWidth, snappingZonesBorderGroup, widthKey, borderWidthChanged)
P_STORE_GET(int, borderRadius, snappingZonesBorderGroup, radiusKey, int)
P_STORE_SET_INT(setBorderRadius, snappingZonesBorderGroup, radiusKey, borderRadiusChanged)

// Effects group (blur lives here for historical reasons)
P_STORE_GET(bool, enableBlur, snappingEffectsGroup, blurKey, bool)
P_STORE_SET_BOOL(setEnableBlur, snappingEffectsGroup, blurKey, enableBlurChanged)

// ── Ordering (PhosphorConfig::Store-backed) ─────────────────────────────────
// On disk: comma-joined QString. In API: QStringList. The schema validator
// normalizes the canonical format (trim/dedup), so the round-trip through
// the store always produces the same string for any equivalent input. The
// parser below is still defensive (trim + skip-empty) in case a caller
// reads a string written before the validator was installed.

namespace {
QStringList parseCommaList(const QString& raw)
{
    if (raw.isEmpty()) {
        return {};
    }
    QStringList parts = raw.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (auto& s : parts) {
        s = s.trimmed();
    }
    return parts;
}
} // namespace

QStringList Settings::snappingLayoutOrder() const
{
    return parseCommaList(
        m_store->read<QString>(ConfigDefaults::orderingGroup(), ConfigDefaults::snappingLayoutOrderKey()));
}

void Settings::setSnappingLayoutOrder(const QStringList& order)
{
    // Read the canonical stored form before AND after writing so the
    // canonicalCommaList validator gets to pick the comparison points.
    // Comparing the user's (possibly non-canonical) input to the stored
    // canonical value would emit a spurious `changed` signal every time a
    // caller passed e.g. " a , b " while disk already holds "a,b".
    const QString before =
        m_store->read<QString>(ConfigDefaults::orderingGroup(), ConfigDefaults::snappingLayoutOrderKey());
    m_store->write(ConfigDefaults::orderingGroup(), ConfigDefaults::snappingLayoutOrderKey(),
                   order.join(QLatin1Char(',')));
    const QString after =
        m_store->read<QString>(ConfigDefaults::orderingGroup(), ConfigDefaults::snappingLayoutOrderKey());
    if (before == after) {
        return;
    }
    Q_EMIT snappingLayoutOrderChanged();
    Q_EMIT settingsChanged();
}

QStringList Settings::tilingAlgorithmOrder() const
{
    return parseCommaList(
        m_store->read<QString>(ConfigDefaults::orderingGroup(), ConfigDefaults::tilingAlgorithmOrderKey()));
}

void Settings::setTilingAlgorithmOrder(const QStringList& order)
{
    // See setSnappingLayoutOrder — post-write compare against the canonical
    // form avoids spurious change signals for equivalent non-canonical input.
    const QString before =
        m_store->read<QString>(ConfigDefaults::orderingGroup(), ConfigDefaults::tilingAlgorithmOrderKey());
    m_store->write(ConfigDefaults::orderingGroup(), ConfigDefaults::tilingAlgorithmOrderKey(),
                   order.join(QLatin1Char(',')));
    const QString after =
        m_store->read<QString>(ConfigDefaults::orderingGroup(), ConfigDefaults::tilingAlgorithmOrderKey());
    if (before == after) {
        return;
    }
    Q_EMIT tilingAlgorithmOrderChanged();
    Q_EMIT settingsChanged();
}

// ── Animations (PhosphorConfig::Store-backed) ───────────────────────────────
// Snapping + autotile geometry-change transitions. Phase 4 sub-commit 6
// migrated the on-disk format from five per-field keys to a single
// Profile JSON blob (decision S — no backwards compat for the old
// layout). Each per-field accessor now decomposes / composes the blob,
// preserving the Q_PROPERTY + ISettings interface for QML and the
// daemon's existing consumers.
//
// `animationsEnabled` stays as a standalone bool — it's an orthogonal
// on/off toggle rather than part of the Profile concept.

P_STORE_GET(bool, animationsEnabled, animationsGroup, enabledKey, bool)
P_STORE_SET_BOOL(setAnimationsEnabled, animationsGroup, enabledKey, animationsEnabledChanged)

// Process-wide fallback registry for standalone Settings instances
// constructed without an injected CurveRegistry (tests, settings-app
// tools that don't share a registry with the daemon). Consolidated
// here as a single function-local static so every fallback path
// resolves curve wire-format strings through the same registry —
// avoids the "two identical spring:14,0.6 strings resolve to different
// shared_ptr<const Curve> instances" surprise that three separate
// per-method statics introduced.
static PhosphorAnimation::CurveRegistry& fallbackCurveRegistry()
{
    static PhosphorAnimation::CurveRegistry s_registry;
    return s_registry;
}

namespace {
/// Read the Profile blob as a mutable QJsonObject. Stored as a nested
/// QVariantMap; an absent / malformed entry returns an empty object so
/// callers that then write back produce a minimal blob containing only
/// the patched field.
QJsonObject readProfileObject(const PhosphorConfig::Store& store)
{
    const QVariantMap map =
        store.read<QVariantMap>(ConfigDefaults::animationsGroup(), ConfigDefaults::animationProfileKey());
    return QJsonObject::fromVariantMap(map);
}

/// Write @p obj as the Profile blob (stored as a nested QVariantMap).
void writeProfileObject(PhosphorConfig::Store& store, const QJsonObject& obj)
{
    store.write(ConfigDefaults::animationsGroup(), ConfigDefaults::animationProfileKey(), obj.toVariantMap());
}
} // namespace

PhosphorAnimation::Profile Settings::animationProfile() const
{
    // An absent / malformed blob returns an empty object, which yields a
    // default-constructed Profile (unset-optional fields). Profile::effective*
    // methods then substitute library defaults — "garbage in disk → sensible
    // defaults".
    const QJsonObject obj = readProfileObject(*m_store);
    if (obj.isEmpty()) {
        return PhosphorAnimation::Profile{};
    }
    const PhosphorAnimation::CurveRegistry& reg = m_curveRegistry ? *m_curveRegistry : fallbackCurveRegistry();
    return PhosphorAnimation::Profile::fromJson(obj, reg);
}

void Settings::setAnimationProfile(const PhosphorAnimation::Profile& profile)
{
    // Change detection compares the merged QJsonObject against the
    // current stored blob via QJsonObject::operator==, NOT through a
    // semantic `Profile::operator==` check. The semantic comparison hits
    // two traps in practice:
    //
    //   1. `Profile::fromJson` resolves the curve field through the
    //      runtime `CurveRegistry`. Plugin-registered curves that aren't
    //      loaded yet produce a null curve pointer, so two blobs that
    //      reference different unresolved curves still compare equal
    //      (both have null curves) — silently data-losing a user's
    //      curve choice when the plugin arrives later.
    //   2. Inversely, when the blob round-trips cleanly the new Profile
    //      may differ slightly (e.g. `effective*` defaults vs unset
    //      optionals) and the semantic `operator==` returns `false` even
    //      though the stored blob would be unchanged — firing a signal
    //      storm every call.
    //
    // QJsonObject equality at the blob level sidesteps both: the only
    // thing that fires signals is a real change to what gets written
    // to disk.
    //
    // Per-field signal emission still goes through the parsed prev /
    // new Profiles because each `xxxChanged` signal describes an
    // observable semantic field, not a blob identity. A blob-identity
    // change with identical effective fields (e.g. curve precision
    // canonicalisation) correctly fires `animationProfileChanged`
    // without fanning out to the per-field signals.

    // Merge: start from the stored blob and overlay every field the
    // incoming Profile emitted. `Profile::toJson` only emits set-optional
    // fields and a non-null curve — unset optionals are absent from the
    // output. Those absent fields leave the corresponding on-disk key
    // untouched, matching the partial-update semantics of the per-field
    // setters: a caller that wants to "clear" a field sets it explicitly
    // via a per-field setter or by constructing a Profile with the
    // intended value, not by leaving it unset.
    //
    // The loop is additive only (no removes) so unknown on-disk keys —
    // future schema extensions, third-party plugin fields — survive
    // intact. Asymmetric removal would wipe them on the first aggregate
    // setter call, and contradict the per-field setters' merge semantics.
    const QJsonObject current = readProfileObject(*m_store);
    QJsonObject merged = current;
    const QJsonObject incoming = profile.toJson();
    for (auto it = incoming.constBegin(); it != incoming.constEnd(); ++it) {
        merged.insert(it.key(), it.value());
    }

    if (merged == current) {
        // Nothing to write — skip both the store write and every signal.
        // Guards the slider-drag-at-30-Hz signal-storm case: a merge that
        // produces a structurally identical object must not wake observers.
        return;
    }

    // Snapshot per-field effective values BEFORE the write so we can
    // emit per-field signals only for fields whose semantic observable
    // actually changed. Parses the PREVIOUS disk bytes through the same
    // registry the runtime uses, matching the invariant that per-field
    // `xxxChanged` signals describe what `animationDuration()` (etc.)
    // will return before vs. after the write.
    const PhosphorAnimation::Profile prev = animationProfile();
    const int prevDuration = qRound(prev.effectiveDuration());
    // Read the pre-write curve directly off the on-disk blob via the
    // same path the live `animationEasingCurve()` getter takes. Going
    // through `prev.curve->toString()` would null-fall-back to the
    // ConfigDefaults default whenever `CurveRegistry::tryCreate`
    // failed to resolve the stored spec — comparing that fallback
    // against the unchanged on-disk value below would always flag a
    // difference and fire a spurious `animationEasingCurveChanged`.
    const QString prevCurveWire = animationEasingCurve();
    const int prevMinDistance = prev.effectiveMinDistance();
    const int prevSequenceMode = static_cast<int>(prev.effectiveSequenceMode());
    const int prevStaggerInterval = prev.effectiveStaggerInterval();

    writeProfileObject(*m_store, merged);

    // Per-field: only emit when the post-write OBSERVABLE differs from
    // the pre-write observable. Compare against `animationProfile()`
    // re-read post-write — NOT against `profile.effective*()` (the
    // incoming arg), because `merged` preserves prev's values for any
    // field the caller left unset. With the incoming `profile` having
    // all-unset fields, `profile.effective*()` returns library
    // defaults; comparing those against `prev*` would fire spurious
    // signals even though the on-disk value is unchanged. Cache the
    // post-write Profile in one read instead of paying a JSON parse +
    // CurveRegistry resolve per field.
    //
    // Per-field signals are emitted BEFORE the aggregate, matching
    // `patchProfileField`'s ordering so QML consumers binding to both
    // a per-field setter and the aggregate observe a consistent
    // emission order regardless of which code path mutated the Profile.
    const auto next = animationProfile();
    if (qRound(next.effectiveDuration()) != prevDuration) {
        Q_EMIT animationDurationChanged();
    }
    // `animationEasingCurve()` reads the curve directly off the merged
    // JSON blob (preserving any unresolved raw spec) — distinct from
    // `next.curve->toString()` which could lose detail when the curve
    // failed to resolve through `CurveRegistry::tryCreate`. The wire
    // form is what disk-level equality tracks.
    const QString newCurveWire = animationEasingCurve();
    if (newCurveWire != prevCurveWire) {
        Q_EMIT animationEasingCurveChanged();
    }
    if (next.effectiveMinDistance() != prevMinDistance) {
        Q_EMIT animationMinDistanceChanged();
    }
    if (static_cast<int>(next.effectiveSequenceMode()) != prevSequenceMode) {
        Q_EMIT animationSequenceModeChanged();
    }
    if (next.effectiveStaggerInterval() != prevStaggerInterval) {
        Q_EMIT animationStaggerIntervalChanged();
    }

    // Aggregate last — consumers that want to observe the Profile
    // atomically (the daemon's fan-out hook) get one signal per call,
    // after every per-field signal has fired.
    Q_EMIT animationProfileChanged();
    Q_EMIT settingsChanged();
}

// ─── Per-field projections over the Profile blob ───────────────────────────
// Each setter patches ONE field in the stored JSON blob directly rather
// than round-tripping through Profile::toJson. Round-tripping would drop
// any unresolved raw curve spec preserved by `setAnimationEasingCurve` —
// `animationProfile()` goes through `CurveRegistry::tryCreate`, which nulls
// the curve pointer on resolution failure, and `Profile::toJson` then omits
// the null field. Patching the raw JSON blob leaves every field the setter
// isn't touching exactly where it was.
//
// Getters route through `animationProfile()` for anything that needs the
// `effective*` defaulting, and read the blob directly for the curve field
// (so unresolved specs round-trip through the getter/setter pair).
//
// Hot path: settings-UI slider drag ~30 Hz. Per call: one read + one JSON
// parse + one field-insert + one serialise + one store write. Acceptable.
//
// The shared merge primitive `patchProfileField` (declared in settings.h)
// owns the read → guard → insert → write → emit-trio sequence so each
// per-field setter is a one-liner over its type-specific pre-processing
// (clamp for numerics, registry resolution for the curve string). The
// helper is defined in this TU because it is consumed exclusively here;
// keeping it private to settings.cpp keeps the .h surface compact.
template<typename T>
void Settings::patchProfileField(const char* jsonFieldName, const T& currentValue, const T& newValue,
                                 void (Settings::*fieldChangedSignal)())
{
    if (currentValue == newValue) {
        // No-op guard — the setter contract requires that signals only
        // fire when an observable changes. A slider drag at constant
        // value wakes no observers.
        return;
    }
    QJsonObject obj = readProfileObject(*m_store);
    obj.insert(QLatin1String(jsonFieldName), newValue);
    writeProfileObject(*m_store, obj);
    Q_EMIT(this->*fieldChangedSignal)();
    Q_EMIT animationProfileChanged();
    Q_EMIT settingsChanged();
}

int Settings::animationDuration() const
{
    return qRound(animationProfile().effectiveDuration());
}

void Settings::setAnimationDuration(int duration)
{
    const int clamped =
        qBound(ConfigDefaults::animationDurationMin(), duration, ConfigDefaults::animationDurationMax());
    patchProfileField<int>(PhosphorAnimation::Profile::JsonFieldDuration, animationDuration(), clamped,
                           &Settings::animationDurationChanged);
}

QString Settings::animationEasingCurve() const
{
    // Read the curve field directly from the Profile JSON blob so any
    // wire-format string round-trips through the setter/getter — even
    // unresolved specs (user plugin curve name not yet registered,
    // hand-edited config with a typo). `animationProfile()` routes the
    // curve through `CurveRegistry::tryCreate` which nulls the pointer
    // on resolution failure; the raw string would then be lost on
    // read-back. The runtime still gracefully falls back to the library
    // default at animation time — persisting the raw string here just
    // preserves the user's intent across restarts.
    const QJsonObject obj = readProfileObject(*m_store);
    const QString spec = obj.value(QLatin1String(PhosphorAnimation::Profile::JsonFieldCurve)).toString();
    return spec.isEmpty() ? ConfigDefaults::animationEasingCurve() : spec;
}

void Settings::setAnimationEasingCurve(const QString& curve)
{
    // NOT routed through patchProfileField — the easing setter has a pre-
    // resolution no-op-guard contract that the generic helper cannot
    // express. Specifically: the original behaviour compares the raw
    // CALLER string against the current stored string before resolution,
    // so two consecutive `setAnimationEasingCurve("alias")` calls with
    // the same alias short-circuit the second call regardless of
    // canonicalisation. Routing through the helper would compare the
    // post-resolution `toStore` against the current stored string — a
    // strictly different no-op gate that would, in the alias-with-already-
    // canonicalised-disk case, suppress signals the original would have
    // fired. Preserving observable behaviour > sharing the merge code.
    //
    // The merge sequence (read → insert → write → emit-trio) IS
    // duplicated relative to patchProfileField; that is intentional and
    // documented here so a future "tidy this up" pass does not re-route
    // through the helper without revisiting the no-op semantics first.

    // Compare against the raw-stored wire string (same shape the getter
    // returns) so no-op assignments short-circuit before any write.
    if (animationEasingCurve() == curve) {
        return;
    }

    // Resolution through the registry is informational only — it tunes
    // the warning below and produces a canonical wire-form when the spec
    // resolves. Either way, the blob keeps the caller's string intact so
    // the user's edit round-trips cleanly through the Q_PROPERTY.
    PhosphorAnimation::CurveRegistry& reg = m_curveRegistry ? *m_curveRegistry : fallbackCurveRegistry();
    auto resolved = reg.tryCreate(curve);
    QString toStore;
    if (resolved) {
        // Known curve — store the canonical wire-format so a later
        // read-back matches what the runtime sees (prevents spurious
        // config rewrites when a user-supplied alias like "0.25,1.0,..."
        // resolves to a slightly-different-precision canonical form).
        toStore = resolved->toString();
    } else {
        // DEBUG, and the spec is NOT echoed. `curve` is the caller's raw string, and
        // animationEasingCurve is a registered STRING setting — so any session-bus peer
        // reaches this with content of its own choosing, unbounded in length and rate. That
        // is the same log-injection vector the per-screen writers were demoted for, one call
        // frame deeper still. "It did not resolve" is the signal; the failing text is not
        // worth an attacker-writable line in the daemon's log.
        qCDebug(lcConfig) << "setAnimationEasingCurve: curve spec did not resolve — persisting raw "
                             "(the library default applies at animation time)";
        toStore = curve;
    }

    QJsonObject obj = readProfileObject(*m_store);
    obj.insert(QLatin1String(PhosphorAnimation::Profile::JsonFieldCurve), toStore);
    writeProfileObject(*m_store, obj);

    Q_EMIT animationEasingCurveChanged();
    Q_EMIT animationProfileChanged();
    Q_EMIT settingsChanged();
}

int Settings::animationMinDistance() const
{
    return animationProfile().effectiveMinDistance();
}

void Settings::setAnimationMinDistance(int distance)
{
    const int clamped =
        qBound(ConfigDefaults::animationMinDistanceMin(), distance, ConfigDefaults::animationMinDistanceMax());
    patchProfileField<int>(PhosphorAnimation::Profile::JsonFieldMinDistance, animationMinDistance(), clamped,
                           &Settings::animationMinDistanceChanged);
}

int Settings::animationSequenceMode() const
{
    return static_cast<int>(animationProfile().effectiveSequenceMode());
}

void Settings::setAnimationSequenceMode(int mode)
{
    const int clamped =
        qBound(ConfigDefaults::animationSequenceModeMin(), mode, ConfigDefaults::animationSequenceModeMax());
    patchProfileField<int>(PhosphorAnimation::Profile::JsonFieldSequenceMode, animationSequenceMode(), clamped,
                           &Settings::animationSequenceModeChanged);
}

int Settings::animationStaggerInterval() const
{
    return animationProfile().effectiveStaggerInterval();
}

void Settings::setAnimationStaggerInterval(int ms)
{
    const int clamped =
        qBound(ConfigDefaults::animationStaggerIntervalMin(), ms, ConfigDefaults::animationStaggerIntervalMax());
    patchProfileField<int>(PhosphorAnimation::Profile::JsonFieldStaggerInterval, animationStaggerInterval(), clamped,
                           &Settings::animationStaggerIntervalChanged);
}

PhosphorAnimationShaders::ShaderProfileTree Settings::shaderProfileTree() const
{
    const QVariantMap map =
        m_store->read<QVariantMap>(ConfigDefaults::animationsGroup(), ConfigDefaults::shaderProfileTreeKey());
    // Prune on read so a config that contains stale overrides on paths
    // the daemon's overlay service doesn't consume (left over from an
    // earlier UI revision that exposed the picker on every event row)
    // can never shadow a user-intended parent override at runtime. The
    // resolver walks deeper-leaf-wins, so an unsupported leaf entry
    // would otherwise silently beat the supported parent entry the user
    // can actually edit. See `src/core/animationshadersupportedpaths.h`
    // for the rationale + the full SSOT.
    return pruneShaderProfileTreeToSupportedPaths(
        PhosphorAnimationShaders::ShaderProfileTree::fromJson(QJsonObject::fromVariantMap(map)));
}

void Settings::setShaderProfileTree(const PhosphorAnimationShaders::ShaderProfileTree& tree)
{
    // Prune incoming tree at the persistence boundary — same rationale
    // as the read-side prune in shaderProfileTree(). Belt-and-braces:
    // the QML UI gates the picker via supportsShaderLeg(), but a
    // Q_INVOKABLE write coming from elsewhere (future scripting hooks,
    // tests) cannot stamp unsupported-path entries onto disk.
    const auto pruned = pruneShaderProfileTreeToSupportedPaths(tree);

    // Value-equality compare so a same-tree write doesn't fire a spurious
    // changed signal (e.g. discard-changes path that calls
    // setShaderProfileTree(currentTree)). Compare AFTER pruning so the
    // first save against a stale-on-disk config still produces a write
    // that drops the unsupported entries.
    const QVariantMap prevMap =
        m_store->read<QVariantMap>(ConfigDefaults::animationsGroup(), ConfigDefaults::shaderProfileTreeKey());
    PhosphorAnimationShaders::ShaderProfileTree prevTree;
    if (!prevMap.isEmpty())
        prevTree = PhosphorAnimationShaders::ShaderProfileTree::fromJson(QJsonObject::fromVariantMap(prevMap));
    const auto prevPruned = pruneShaderProfileTreeToSupportedPaths(prevTree);
    if (pruned == prevPruned)
        return;
    m_store->write(ConfigDefaults::animationsGroup(), ConfigDefaults::shaderProfileTreeKey(),
                   pruned.toJson().toVariantMap());
    Q_EMIT shaderProfileTreeChanged();
    Q_EMIT settingsChanged();
}

QString Settings::shaderProfileTreeJson() const
{
    return QString::fromUtf8(QJsonDocument(shaderProfileTree().toJson()).toJson(QJsonDocument::Compact));
}

void Settings::setShaderProfileTreeJson(const QString& json)
{
    if (json.isEmpty()) {
        setShaderProfileTree(PhosphorAnimationShaders::ShaderProfileTree{});
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) {
        qCWarning(lcConfig) << "setShaderProfileTreeJson: malformed JSON, ignoring";
        return;
    }
    setShaderProfileTree(PhosphorAnimationShaders::ShaderProfileTree::fromJson(doc.object()));
}

// ── Decorations tree (PhosphorConfig::Store-backed) ─────────────────────────
// Persisted as one nested JSON entry under Decorations/DecorationProfileTree,
// mirroring how the animation shaderProfileTree persists under
// Animations/ShaderProfileTree. The read-side falls back to the
// ConfigDefaults tree (EMPTY / neutral: no baseline chain, no overrides —
// border and titlebar visuals are owned by the window rules, see
// ConfigDefaults::decorationProfileTree) when the store holds no entry, so a
// fresh config starts with no shader-pack decoration.

PhosphorSurfaceShaders::DecorationProfileTree Settings::decorationProfileTree() const
{
    const QVariantMap map =
        m_store->read<QVariantMap>(ConfigDefaults::decorationsGroup(), ConfigDefaults::decorationProfileTreeKey());
    // The Decorations schema registers a NON-empty default for this key (the
    // serialized ConfigDefaults::decorationProfileTree), so a store built with
    // the schema never returns empty here. The guard covers a store constructed
    // WITHOUT the schema default (e.g. a bare test stub): fall back to the same
    // canonical default rather than to an empty tree.
    if (map.isEmpty())
        return ConfigDefaults::decorationProfileTree();
    return PhosphorSurfaceShaders::DecorationProfileTree::fromJson(QJsonObject::fromVariantMap(map));
}

void Settings::setDecorationProfileTree(const PhosphorSurfaceShaders::DecorationProfileTree& tree)
{
    // Prune the incoming tree at the persistence boundary — same
    // belt-and-braces rationale as setShaderProfileTree. fromJson is the
    // tree's canonical unsupported-path filter (setOverride itself does not
    // validate), so a toJson→fromJson round trip IS the prune: a Q_INVOKABLE
    // write from scripting/tests cannot stamp unsupported-path entries onto
    // disk. The read side (decorationProfileTree) passes through the same
    // filter, so the comparison below is pruned-vs-pruned.
    const auto pruned = PhosphorSurfaceShaders::DecorationProfileTree::fromJson(tree.toJson());
    // Value-equality compare so a same-tree write doesn't fire a spurious
    // changed signal. Compare against the effective current tree (the
    // ConfigDefaults default when the store is empty) so writing the default
    // back over an empty store is correctly a no-op.
    if (pruned == decorationProfileTree())
        return;
    m_store->write(ConfigDefaults::decorationsGroup(), ConfigDefaults::decorationProfileTreeKey(),
                   pruned.toJson().toVariantMap());
    Q_EMIT decorationProfileTreeChanged();
    Q_EMIT settingsChanged();
}

QString Settings::decorationProfileTreeJson() const
{
    return QString::fromUtf8(QJsonDocument(decorationProfileTree().toJson()).toJson(QJsonDocument::Compact));
}

void Settings::setDecorationProfileTreeJson(const QString& json)
{
    if (json.isEmpty()) {
        // Empty string = reset to the canonical default, exactly like the
        // animation shaderProfileTree facade. That default is the EMPTY /
        // neutral tree (ConfigDefaults::decorationProfileTree — border and
        // titlebar visuals are rule-owned, the tree carries only opt-in
        // shader-pack decoration), so clearing restores "no decoration".
        setDecorationProfileTree(ConfigDefaults::decorationProfileTree());
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) {
        qCWarning(lcConfig) << "setDecorationProfileTreeJson: malformed JSON, ignoring";
        return;
    }
    setDecorationProfileTree(PhosphorSurfaceShaders::DecorationProfileTree::fromJson(doc.object()));
}

// ── Decorations.Performance (PhosphorConfig::Store-backed) ──────────────────
// These bound WHEN the decoration chain animates, not how much work it does per
// frame. An animated pack repaints every window carrying it on every vsync, and
// that alone holds the GPU in its top performance state however cheap the frame
// is — so the only lever that returns the card to its idle clocks is to stop
// drawing when nothing needs to change.

P_STORE_GET(bool, decorationAnimateFocusedOnly, decorationsPerformanceGroup, animateFocusedOnlyKey, bool)
P_STORE_SET_BOOL(setDecorationAnimateFocusedOnly, decorationsPerformanceGroup, animateFocusedOnlyKey,
                 decorationAnimateFocusedOnlyChanged)

P_STORE_GET(bool, decorationPauseWhenIdle, decorationsPerformanceGroup, pauseWhenIdleKey, bool)
P_STORE_SET_BOOL(setDecorationPauseWhenIdle, decorationsPerformanceGroup, pauseWhenIdleKey,
                 decorationPauseWhenIdleChanged)

P_STORE_GET(int, decorationIdleTimeoutSec, decorationsPerformanceGroup, idleTimeoutSecKey, int)
P_STORE_SET_INT(setDecorationIdleTimeoutSec, decorationsPerformanceGroup, idleTimeoutSecKey,
                decorationIdleTimeoutSecChanged)

// ── Rendering (PhosphorConfig::Store-backed) ────────────────────────────────
// Validator (normalizeRenderingBackend in the schema) coerces unknown values
// to a known backend, so a hand-edited "Rendering.Backend = foobar" reads
// back as the default on next load.

P_STORE_GET(QString, renderingBackend, renderingGroup, backendKey, QString)
P_STORE_SET_STRING(setRenderingBackend, renderingGroup, backendKey, renderingBackendChanged)

// Shaders.Audio (ISettings) — the audio-spectrum analysis parameter set.
// enableAudioVisualizer / audioSpectrumBarCount live with the other
// handwritten shader accessors above; the rest are uniform store-backed
// scalars, so the macros apply.
P_STORE_GET(bool, audioAutosens, shadersAudioGroup, autosensKey, bool)
P_STORE_SET_BOOL(setAudioAutosens, shadersAudioGroup, autosensKey, audioAutosensChanged)
P_STORE_GET(int, audioSensitivity, shadersAudioGroup, sensitivityKey, int)
P_STORE_SET_INT(setAudioSensitivity, shadersAudioGroup, sensitivityKey, audioSensitivityChanged)
P_STORE_GET(int, audioNoiseReduction, shadersAudioGroup, noiseReductionKey, int)
P_STORE_SET_INT(setAudioNoiseReduction, shadersAudioGroup, noiseReductionKey, audioNoiseReductionChanged)
P_STORE_GET(int, audioLowerCutoffHz, shadersAudioGroup, lowerCutoffHzKey, int)
P_STORE_SET_INT(setAudioLowerCutoffHz, shadersAudioGroup, lowerCutoffHzKey, audioLowerCutoffHzChanged)
P_STORE_GET(int, audioHigherCutoffHz, shadersAudioGroup, higherCutoffHzKey, int)
P_STORE_SET_INT(setAudioHigherCutoffHz, shadersAudioGroup, higherCutoffHzKey, audioHigherCutoffHzChanged)
P_STORE_GET(bool, audioMonstercat, shadersAudioGroup, monstercatKey, bool)
P_STORE_SET_BOOL(setAudioMonstercat, shadersAudioGroup, monstercatKey, audioMonstercatChanged)
P_STORE_GET(bool, audioWaves, shadersAudioGroup, wavesKey, bool)
P_STORE_SET_BOOL(setAudioWaves, shadersAudioGroup, wavesKey, audioWavesChanged)
P_STORE_GET(QString, audioChannelMode, shadersAudioGroup, channelModeKey, QString)
P_STORE_SET_STRING(setAudioChannelMode, shadersAudioGroup, channelModeKey, audioChannelModeChanged)
P_STORE_GET(bool, audioReverse, shadersAudioGroup, reverseKey, bool)
P_STORE_SET_BOOL(setAudioReverse, shadersAudioGroup, reverseKey, audioReverseChanged)
P_STORE_GET(int, audioExtraSmoothing, shadersAudioGroup, extraSmoothingKey, int)
P_STORE_SET_INT(setAudioExtraSmoothing, shadersAudioGroup, extraSmoothingKey, audioExtraSmoothingChanged)
P_STORE_GET(QString, audioInputMethod, shadersAudioGroup, inputMethodKey, QString)
P_STORE_SET_STRING(setAudioInputMethod, shadersAudioGroup, inputMethodKey, audioInputMethodChanged)
P_STORE_GET(QString, audioInputSource, shadersAudioGroup, inputSourceKey, QString)
P_STORE_SET_STRING(setAudioInputSource, shadersAudioGroup, inputSourceKey, audioInputSourceChanged)

// ── Performance (PhosphorConfig::Store-backed) ──────────────────────────────

P_STORE_GET(int, pollIntervalMs, performanceGroup, pollIntervalMsKey, int)
P_STORE_SET_INT(setPollIntervalMs, performanceGroup, pollIntervalMsKey, pollIntervalMsChanged)
P_STORE_GET(int, minimumZoneSizePx, performanceGroup, minimumZoneSizePxKey, int)
P_STORE_SET_INT(setMinimumZoneSizePx, performanceGroup, minimumZoneSizePxKey, minimumZoneSizePxChanged)
P_STORE_GET(int, minimumZoneDisplaySizePx, performanceGroup, minimumZoneDisplaySizePxKey, int)
P_STORE_SET_INT(setMinimumZoneDisplaySizePx, performanceGroup, minimumZoneDisplaySizePxKey,
                minimumZoneDisplaySizePxChanged)

// ── PhosphorZones::Zone geometry (PhosphorConfig::Store-backed) ────────────────────────────
// Inner/outer gaps (uniform + per-side) plus adjacency threshold. Schema
// clampInt validators enforce the same ranges readValidatedInt used to.

// ── Shared inner/outer gaps (PhosphorConfig::Store-backed, "Gaps" group) ─────
// Read on demand through m_store (validator clamps to the declared gap range);
// setters route the write through the store and emit only on a real change.
// The autotile* gap forwarders in settings.h delegate to these getters, so
// tiling and snapping always resolve the same values.
P_STORE_GET(int, innerGap, gapsGroup, innerGapKey, int)
P_STORE_SET_INT(setInnerGap, gapsGroup, innerGapKey, innerGapChanged)
P_STORE_GET(int, outerGap, gapsGroup, outerGapKey, int)
P_STORE_SET_INT(setOuterGap, gapsGroup, outerGapKey, outerGapChanged)
P_STORE_GET(bool, usePerSideOuterGap, gapsGroup, usePerSideOuterGapKey, bool)
P_STORE_SET_BOOL(setUsePerSideOuterGap, gapsGroup, usePerSideOuterGapKey, usePerSideOuterGapChanged)
P_STORE_GET(int, outerGapTop, gapsGroup, outerGapTopKey, int)
P_STORE_SET_INT(setOuterGapTop, gapsGroup, outerGapTopKey, outerGapTopChanged)
P_STORE_GET(int, outerGapBottom, gapsGroup, outerGapBottomKey, int)
P_STORE_SET_INT(setOuterGapBottom, gapsGroup, outerGapBottomKey, outerGapBottomChanged)
P_STORE_GET(int, outerGapLeft, gapsGroup, outerGapLeftKey, int)
P_STORE_SET_INT(setOuterGapLeft, gapsGroup, outerGapLeftKey, outerGapLeftChanged)
P_STORE_GET(int, outerGapRight, gapsGroup, outerGapRightKey, int)
P_STORE_SET_INT(setOuterGapRight, gapsGroup, outerGapRightKey, outerGapRightChanged)

// ── Window decoration appearance (Store-backed, "Windows" group) ─────────────
P_STORE_GET(bool, showWindowBorder, windowsAppearanceGroup, showBorderKey, bool)
P_STORE_SET_BOOL(setShowWindowBorder, windowsAppearanceGroup, showBorderKey, showWindowBorderChanged)
P_STORE_GET(QString, windowBorderScope, windowsAppearanceGroup, borderScopeKey, QString)
P_STORE_SET_STRING(setWindowBorderScope, windowsAppearanceGroup, borderScopeKey, windowBorderScopeChanged)
P_STORE_GET(int, windowBorderWidth, windowsAppearanceGroup, widthKey, int)
P_STORE_SET_INT(setWindowBorderWidth, windowsAppearanceGroup, widthKey, windowBorderWidthChanged)
P_STORE_GET(int, windowBorderRadius, windowsAppearanceGroup, radiusKey, int)
P_STORE_SET_INT(setWindowBorderRadius, windowsAppearanceGroup, radiusKey, windowBorderRadiusChanged)
P_STORE_GET(QString, windowBorderColorActive, windowsAppearanceGroup, borderColorActiveKey, QString)
P_STORE_SET_STRING(setWindowBorderColorActive, windowsAppearanceGroup, borderColorActiveKey,
                   windowBorderColorActiveChanged)
P_STORE_GET(QString, windowBorderColorInactive, windowsAppearanceGroup, borderColorInactiveKey, QString)
P_STORE_SET_STRING(setWindowBorderColorInactive, windowsAppearanceGroup, borderColorInactiveKey,
                   windowBorderColorInactiveChanged)
P_STORE_GET(bool, hideWindowTitleBars, windowsAppearanceGroup, hideTitleBarsKey, bool)
P_STORE_SET_BOOL(setHideWindowTitleBars, windowsAppearanceGroup, hideTitleBarsKey, hideWindowTitleBarsChanged)
P_STORE_GET(QString, windowTitleBarScope, windowsAppearanceGroup, titleBarScopeKey, QString)
P_STORE_SET_STRING(setWindowTitleBarScope, windowsAppearanceGroup, titleBarScopeKey, windowTitleBarScopeChanged)
P_STORE_GET(int, focusFadeDuration, windowsAppearanceGroup, focusFadeDurationKey, int)
P_STORE_SET_INT(setFocusFadeDuration, windowsAppearanceGroup, focusFadeDurationKey, focusFadeDurationChanged)
// Plain opacity+tint layer (same "Windows" group).
P_STORE_GET(bool, showWindowOpacityTint, windowsAppearanceGroup, showOpacityTintKey, bool)
P_STORE_SET_BOOL(setShowWindowOpacityTint, windowsAppearanceGroup, showOpacityTintKey, showWindowOpacityTintChanged)
P_STORE_GET(QString, windowOpacityTintScope, windowsAppearanceGroup, opacityTintScopeKey, QString)
P_STORE_SET_STRING(setWindowOpacityTintScope, windowsAppearanceGroup, opacityTintScopeKey,
                   windowOpacityTintScopeChanged)
P_STORE_GET(double, windowOpacity, windowsAppearanceGroup, opacityKey, double)
P_STORE_SET_DOUBLE(setWindowOpacity, windowsAppearanceGroup, opacityKey, windowOpacityChanged)
P_STORE_GET(double, windowTintStrength, windowsAppearanceGroup, tintStrengthKey, double)
P_STORE_SET_DOUBLE(setWindowTintStrength, windowsAppearanceGroup, tintStrengthKey, windowTintStrengthChanged)
P_STORE_GET(QString, windowTintColor, windowsAppearanceGroup, tintColorKey, QString)
P_STORE_SET_STRING(setWindowTintColor, windowsAppearanceGroup, tintColorKey, windowTintColorChanged)

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
// share snappingEffectsGroup with the already-migrated Appearance.Blur.
// QStringList keys go over the wire as comma-joined strings; the getters
// parse back via parseCommaList (defined above in the Ordering section).

P_STORE_GET(bool, showZonesOnAllMonitors, snappingBehaviorDisplayGroup, showOnAllMonitorsKey, bool)
P_STORE_SET_BOOL(setShowZonesOnAllMonitors, snappingBehaviorDisplayGroup, showOnAllMonitorsKey,
                 showZonesOnAllMonitorsChanged)

// ── Per-mode disable lists — rule-backed (Phase 3b) ─────────────────────────
//
// Per-mode disable entries are `DisableEngine` context rules in the unified
// Rule store (rules.json), NOT config.json keys. A disable rule
// pins exactly the context dimensions of its axis:
//   - monitor  : ScreenId only
//   - desktop  : ScreenId + VirtualDesktop
//   - activity : ScreenId + Activity
// and carries one `DisableEngine` action whose `mode` param ("snapping" /
// "autotile") scopes which engine it gates. The deterministic shape lets the
// getters enumerate the store and the setters replace a whole (axis, mode)
// family without touching the other axes / the other mode.

namespace {

// Classify a disable rule's MATCH into a disable axis. Returns nullopt for a
// shape no disable axis can represent — those are not managed disable entries,
// so the getter must not report them and the write path's kept-walk must not
// rewrite them. Routed through `ContextRuleBridge::contextAxisFor` so the
// cascade-axis formula lives in one place, and so the three admission gates that
// formula folds in apply here identically to the assignment side:
//   - `isContextOnly()` — a rule carrying a window-property leaf is not a
//     context rule at all. Rebuilding it from the decoded dims would drop that
//     leaf, and `disableEntryFor` has nowhere to put it.
//   - `pinsNonDimensionContextField()` — a rule pinning Mode /
//     TiledWindowCount / ScreenOrientation / ActiveLayout is more specific than
//     the (screen, desktop, activity) shape the entry strings can key. Taking it
//     as a bare axis rule would BOTH over-report the gate (an
//     `All{ScreenId==A, ScreenOrientation=="portrait"}` disable would switch the
//     engine off on screen A in every orientation) and destroy the extra leaf on
//     the next rebuild of the family.
//   - the axis decode itself, below.
//
// STRICT, like the assignment side's `matchIsExactContextActivityStrict`: a
// Combined (screen+desktop+activity) tuple is NOT the Activity axis. The three
// disable-list keys are `screenId`, `screenId/desktop` and `screenId/activity`
// — none of them can carry all three dimensions, so projecting a Combined rule
// onto the Activity axis loses information both ways:
//   - disableEntriesFor() would serialize it as `screenId/activity`, dropping
//     the desktop pin, and the rebuild loop below would recreate it from that
//     entry with desktop=0 — silently widening a rule that gated ONE desktop
//     into one that gates ALL desktops (under a different disableRuleIdFor
//     UUID, so it is a different rule, not an edit).
//   - a Combined rule and a plain Activity rule for the same (screen,
//     activity) produce the SAME entry string, so canonicalDisableEntries'
//     de-duplication would collapse the pair and the rebuild would emit only
//     one — losing the other outright.
// Excluded here instead, a Combined disable rule is invisible to the disable
// lists (it cannot be keyed by them) and the rebuild's kept-walk preserves it
// untouched, exactly as it already does for a rule that pins a non-dimension
// context field. Nothing in-tree authors one — the v3→v4 migration emits
// desktop-only and activity-only disables — so this is only reachable for a
// rule hand-built in the rule editor, which is precisely the rule the disable
// lists must not rewrite.
std::optional<DisableAxis> axisOf(const PhosphorRules::MatchExpression& match)
{
    namespace CRB = PhosphorRules::ContextRuleBridge;
    switch (CRB::contextAxisFor(match)) {
    case CRB::ContextAxis::Monitor:
        return DisableAxis::Monitor;
    case CRB::ContextAxis::Desktop:
        return DisableAxis::Desktop;
    case CRB::ContextAxis::Activity:
        return DisableAxis::Activity;
    case CRB::ContextAxis::Combined:
    case CRB::ContextAxis::CatchAll:
        return std::nullopt;
    }
    return std::nullopt;
}

// Serialize a decoded (screen, desktop, activity) tuple into the disable-list
// entry string for @p axis. The ONE place the entry shape is spelled, shared by
// the getter (disableEntriesFor) and the write path's kept-walk — the two have
// to agree on what "the entry for this rule" is, or the kept-walk would fail to
// recognise the very entries the getter produced.
QString disableEntryFor(DisableAxis axis, const QString& screenId, int desktop, const QString& activity)
{
    switch (axis) {
    case DisableAxis::Monitor:
        return screenId;
    case DisableAxis::Desktop:
        return screenId + QLatin1Char('/') + QString::number(desktop);
    case DisableAxis::Activity:
        return screenId + QLatin1Char('/') + activity;
    }
    return QString();
}

} // namespace

QStringList Settings::disableEntriesFor(PhosphorZones::AssignmentEntry::Mode mode, int axisInt) const
{
    namespace CRB = PhosphorRules::ContextRuleBridge;
    const auto axis = static_cast<DisableAxis>(axisInt);
    const QString wantToken = PhosphorZones::modeToWireString(mode);
    QStringList out;
    for (const PhosphorRules::Rule& rule : m_ruleStore->ruleSet().rules()) {
        if (!rule.enabled) {
            // A disabled rule does not gate anything. The evaluator skips
            // !enabled before filling any slot, and these lists ARE the gate
            // (isMonitorDisabled reads them straight), so honouring a disabled
            // rule here would keep an engine switched off that the user had
            // explicitly toggled off in the rule editor. The rule itself is
            // preserved — see the kept-walk in writeDisableEntries, which must
            // not delete what this getter no longer reports.
            continue;
        }
        const auto ruleToken = CRB::disableRuleMode(rule);
        if (!ruleToken || *ruleToken != wantToken) {
            continue; // not a disable rule, or scoped to a different mode
        }
        // axisOf() is the single admission gate — see its comment for the three
        // shapes it refuses (window-property leaves, a pinned non-dimension
        // context field, an axis the entry strings cannot key).
        const auto ruleAxis = axisOf(rule.match);
        if (!ruleAxis || *ruleAxis != axis) {
            continue;
        }
        QString screenId;
        int desktop = 0;
        QString activity;
        CRB::contextDimsOf(rule.match, screenId, desktop, activity);
        out.append(disableEntryFor(axis, screenId, desktop, activity));
    }
    return out;
}

void Settings::writeDisableEntries(PhosphorZones::AssignmentEntry::Mode mode, int axisInt, const QStringList& entries,
                                   DisableModeSignalFn signalFn)
{
    namespace CRB = PhosphorRules::ContextRuleBridge;
    const auto axis = static_cast<DisableAxis>(axisInt);
    const QString modeToken = PhosphorZones::modeToWireString(mode);

    // Snapshot the current entries for this (axis, mode) so a no-op write does
    // not fire a spurious changed signal. Both sides go through
    // canonicalDisableEntries — see it for what the canonical form is and why
    // a raw compare is not enough.
    const QStringList before = canonicalDisableEntries(axis, disableEntriesFor(mode, axisInt));
    const QStringList after = canonicalDisableEntries(axis, entries);

    // No-op guard: when the canonical disable sets are equal there is nothing
    // to persist. Skip the kept-rebuild + setAllRules walk entirely — the
    // rebuild would produce a structurally identical rule list
    // (makeDisableRule is deterministic via disableRuleIdFor's createUuidV5
    // over (screenId, desktop, activity, modeToken), so the ids match
    // byte-for-byte and setAllRules would correctly detect "no change"),
    // but walking every rule in the store and allocating fresh Rule
    // copies just to discover that is wasted work. Pure micro-optimisation,
    // not a correctness guard against UUID churn.
    if (before == after) {
        return;
    }

    // Rebuild the rule list: keep every rule that is NOT a disable rule of
    // this exact (axis, mode) family, then append the new entries.
    QList<PhosphorRules::Rule> kept;
    for (const PhosphorRules::Rule& rule : m_ruleStore->ruleSet().rules()) {
        const auto ruleToken = CRB::disableRuleMode(rule);
        // axisOf() gates admission with exactly the shapes disableEntriesFor
        // reports, so this walk drops only rules the append loop below rebuilds.
        // Anything it refuses — a window-property leaf, a pinned non-dimension
        // context field (Mode / TiledWindowCount / ScreenOrientation /
        // ActiveLayout), a Combined tuple — is NOT the pure (screen, desktop,
        // activity) shape this (axis, mode) rewrite owns, so it falls through to
        // `kept` untouched. Dropping it would destroy the rule outright: the
        // append loop rebuilds only the bare tuple, so the extra leaf would be
        // gone and the rule's derived UUID with it.
        if (ruleToken && *ruleToken == modeToken) {
            const auto ruleAxis = axisOf(rule.match);
            if (ruleAxis && *ruleAxis == axis) {
                QString screenId;
                int desktop = 0;
                QString activity;
                CRB::contextDimsOf(rule.match, screenId, desktop, activity);
                // A DISABLED rule of this family is invisible to
                // disableEntriesFor, so it is in neither `before` nor `after` and
                // the append loop below will not recreate it. Dropping it here
                // would silently delete the user's rule on the next unrelated
                // write to this (axis, mode) family. Keep it — unless this write
                // re-asserts its exact entry, in which case the append loop
                // rebuilds the same tuple as an ENABLED rule with the same
                // deterministic id. The append loop runs after this walk, so
                // keeping the disabled one too would hand setAllRules the
                // disabled rule first, and RuleSet::setRules keeps the FIRST
                // entry for any id and drops later collisions — the rebuilt
                // ENABLED rule would be the one thrown away, leaving the
                // re-enable a silent no-op. Ticking a monitor back on in the UI
                // is precisely that case, and re-enabling the rule is what the
                // user asked for.
                const QStringList canonical =
                    canonicalDisableEntries(axis, {disableEntryFor(axis, screenId, desktop, activity)});
                const bool reasserted = !canonical.isEmpty() && after.contains(canonical.first());
                if (rule.enabled || reasserted) {
                    continue; // drop — replaced below
                }
            }
        }
        kept.append(rule);
    }

    for (const QString& canonicalEntry : after) {
        QString screenId;
        int desktop = 0;
        QString activity;
        switch (axis) {
        case DisableAxis::Monitor:
            screenId = canonicalEntry;
            break;
        case DisableAxis::Desktop: {
            // No shape check: `after` came out of canonicalDisableEntries, which
            // already dropped every entry this loop could reject and rebuilt the
            // survivors as `<non-empty screen>/<QString::number(desktop > 0)>`.
            // The screen segment is non-empty (resolveScreenId returns its input
            // unchanged when it doesn't resolve) and the desktop segment never
            // contains '/', so lastIndexOf always lands on the separator we put
            // there and the tail always parses back to the same positive int.
            const int slash = canonicalEntry.lastIndexOf(QLatin1Char('/'));
            desktop = canonicalEntry.mid(slash + 1).toInt();
            screenId = canonicalEntry.left(slash);
            break;
        }
        case DisableAxis::Activity: {
            // Use lastIndexOf so a disambiguated screen ID
            // (`Manuf:Model:Serial/CONNECTOR`, see
            // libs/phosphor-screens/include/PhosphorScreens/ScreenIdentity.h)
            // splits at the activity boundary, not at the connector
            // boundary inside the screen ID. Activity UUIDs are
            // canonical and never contain `/`, so the trailing segment
            // is unambiguous. Mirrors the Desktop axis above, including
            // the shape guarantee canonicalDisableEntries provides.
            const int slash = canonicalEntry.lastIndexOf(QLatin1Char('/'));
            screenId = canonicalEntry.left(slash);
            activity = canonicalEntry.mid(slash + 1);
            break;
        }
        }
        // Compose the rule name with the same axis suffix the v3→v4
        // migration uses (see `configmigration.cpp::disableRuleForDesktop`
        // / `disableRuleForActivity`). Without the suffix, a runtime-
        // authored desktop or activity disable rule shows up in the rule
        // editor as the bare monitor-prefix label (`"Snapping off · DP-1"`),
        // visually indistinguishable from a monitor-axis rule for the same
        // screen — even though the underlying `(screen, desktop, activity)`
        // tuple is what `disableRuleIdFor` keys the v5 UUID off. Two
        // different rules with the same display name confuses the editor's
        // dedup-on-name heuristic and obscures the actual scope.
        QString name = disableRulePrefixFor(mode) + screenId;
        if (axis == DisableAxis::Desktop) {
            name += disableRuleDesktopSuffix(desktop);
        } else if (axis == DisableAxis::Activity) {
            name += disableRuleActivitySuffix();
        }
        kept.append(CRB::makeDisableRule(name, screenId, desktop, activity, modeToken, CRB::kContextBandBase));
    }

    if (!m_ruleStore->setAllRules(kept)) {
        // Persistence failed — the in-memory rule set still advanced,
        // so consumers wired to `rulesChanged(persisted=false)` already
        // know. Surface it on the settings-side log too so users
        // grepping `lcConfig` see the failure. Then try to roll the
        // in-memory store back to its on-disk state (mirrors the
        // reset() rollback below) so subsequent reads through this
        // Settings instance return the same view as cross-process
        // consumers reading the unmodified file.
        //
        // The aggregate `settingsChanged()` emit is GATED on whether that
        // rollback actually restored the original set (see the
        // emit-on-change note below it), NOT skipped outright. In the
        // double-failure case the rollback leaves the value diverged and
        // the emit does fire, so a dirty-state tracker can re-read the
        // advanced in-memory value and mark itself clean while disk still
        // holds the old list. That is the lesser of the two divergences:
        // staying silent instead strands every disable-list UI on a set
        // the getters no longer report.
        //
        // BEST-EFFORT, not guaranteed: load() deliberately keeps the
        // in-memory set when it cannot read the file, and an unreadable
        // file is a plausible reason the persist failed in the first
        // place. In that double failure the in-memory state stays
        // diverged from disk until the next successful save or load.
        // Nothing better is available here — inventing a second recovery
        // path for a store we already know we cannot write would be
        // guessing.
        qCWarning(lcConfig) << "writeDisableEntries: failed to persist window-rule store for mode" << mode << "axis"
                            << axisInt;
        m_ruleStore->load();
        // Emit only if the rollback did NOT restore the original set — which is exactly the
        // emit-on-change rule, applied to the state we actually ended up in.
        //
        // The usual case is that load() puts the entries back where they started, so nothing
        // changed and nothing is announced; firing then would make every disable-list UI
        // re-read a set identical to the one it is already showing. But load() is best-effort
        // (it keeps the advanced in-memory set when it cannot read the file, and an unreadable
        // file is a plausible reason the write failed in the first place), so the rollback can
        // leave the value MOVED. Announcing nothing there would strand every UI on the old
        // list while the getters report the new one — which is the same silent divergence,
        // just from the other side.
        if (canonicalDisableEntries(axis, disableEntriesFor(mode, axisInt)) != before) {
            Q_EMIT(this->*signalFn)(mode);
            Q_EMIT settingsChanged();
        }
        return;
    }

    Q_EMIT(this->*signalFn)(mode);
    Q_EMIT settingsChanged();
}

QStringList Settings::disabledMonitors(PhosphorZones::AssignmentEntry::Mode mode) const
{
    // Resolve connector names → stable screen ids on every read, through the
    // same resolveScreenId helper canonicalDisableEntries uses — an
    // unresolvable name therefore stays the name here too, instead of
    // collapsing to an empty entry. Stored connector names stay human-readable;
    // consumers see canonical ids.
    // Desktop/activity getters intentionally skip this resolution because
    // their composite keys (`screenId/desktop`, `screenId/activity`) embed
    // the screen id in a non-isolatable way; the connector↔id matching is
    // done at lookup time inside isDesktopDisabled / isActivityDisabled.
    QStringList entries = disableEntriesFor(mode, static_cast<int>(DisableAxis::Monitor));
    for (auto& name : entries) {
        name = resolveScreenId(name);
    }
    return entries;
}

void Settings::setDisabledMonitors(PhosphorZones::AssignmentEntry::Mode mode, const QStringList& screenIdOrNames)
{
    writeDisableEntries(mode, static_cast<int>(DisableAxis::Monitor), screenIdOrNames,
                        &Settings::disabledMonitorsChanged);
}

bool Settings::isMonitorDisabled(PhosphorZones::AssignmentEntry::Mode mode, const QString& screenIdOrName) const
{
    const QStringList entries = disabledMonitors(mode);
    for (const QString& name : PhosphorScreens::ScreenIdentity::variantsFor(screenIdOrName)) {
        if (entries.contains(name)) {
            return true;
        }
    }
    return false;
}

// Composite keys (`screenId/desktop`) are returned verbatim — connector-name
// resolution is deferred to isDesktopDisabled, which knows how to split the
// composite and match either the connector or the resolved id segment.
QStringList Settings::disabledDesktops(PhosphorZones::AssignmentEntry::Mode mode) const
{
    return disableEntriesFor(mode, static_cast<int>(DisableAxis::Desktop));
}

void Settings::setDisabledDesktops(PhosphorZones::AssignmentEntry::Mode mode, const QStringList& entries)
{
    writeDisableEntries(mode, static_cast<int>(DisableAxis::Desktop), entries, &Settings::disabledDesktopsChanged);
}

bool Settings::isDesktopDisabled(PhosphorZones::AssignmentEntry::Mode mode, const QString& screenIdOrName,
                                 int desktop) const
{
    if (desktop <= 0) {
        return false;
    }
    const QStringList entries = disabledDesktops(mode);
    const QStringList namesToCheck = PhosphorScreens::ScreenIdentity::variantsFor(screenIdOrName);
    const QString desktopStr = QString::number(desktop);
    for (const QString& name : namesToCheck) {
        if (entries.contains(name + QLatin1Char('/') + desktopStr)) {
            return true;
        }
    }
    return false;
}

// Composite keys (`screenId/activityUuid`) are returned verbatim — see the
// comment on disabledDesktops for why connector-name resolution is deferred
// to isActivityDisabled rather than applied per-read here.
QStringList Settings::disabledActivities(PhosphorZones::AssignmentEntry::Mode mode) const
{
    return disableEntriesFor(mode, static_cast<int>(DisableAxis::Activity));
}

void Settings::setDisabledActivities(PhosphorZones::AssignmentEntry::Mode mode, const QStringList& entries)
{
    writeDisableEntries(mode, static_cast<int>(DisableAxis::Activity), entries, &Settings::disabledActivitiesChanged);
}

bool Settings::isActivityDisabled(PhosphorZones::AssignmentEntry::Mode mode, const QString& screenIdOrName,
                                  const QString& activityId) const
{
    if (activityId.isEmpty()) {
        return false;
    }
    const QStringList entries = disabledActivities(mode);
    const QStringList namesToCheck = PhosphorScreens::ScreenIdentity::variantsFor(screenIdOrName);
    for (const QString& name : namesToCheck) {
        if (entries.contains(name + QLatin1Char('/') + activityId)) {
            return true;
        }
    }
    return false;
}

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

// Enum setters: stored as int, exposed via the enum-typed getter/setter and
// also via the int adapters QML uses (osdStyleInt / overlayDisplayModeInt).

OsdStyle Settings::osdStyle() const
{
    return static_cast<OsdStyle>(
        m_store->read<int>(ConfigDefaults::snappingEffectsGroup(), ConfigDefaults::osdStyleKey()));
}
int Settings::osdStyleInt() const
{
    return static_cast<int>(osdStyle());
}
void Settings::setOsdStyle(OsdStyle style)
{
    const int before = m_store->read<int>(ConfigDefaults::snappingEffectsGroup(), ConfigDefaults::osdStyleKey());
    m_store->write(ConfigDefaults::snappingEffectsGroup(), ConfigDefaults::osdStyleKey(), static_cast<int>(style));
    const int after = m_store->read<int>(ConfigDefaults::snappingEffectsGroup(), ConfigDefaults::osdStyleKey());
    if (after == before) {
        return;
    }
    Q_EMIT osdStyleChanged();
    Q_EMIT settingsChanged();
}
void Settings::setOsdStyleInt(int style)
{
    if (style >= 0 && style <= static_cast<int>(OsdStyle::Preview)) {
        setOsdStyle(static_cast<OsdStyle>(style));
    }
}

OverlayDisplayMode Settings::overlayDisplayMode() const
{
    return static_cast<OverlayDisplayMode>(
        m_store->read<int>(ConfigDefaults::snappingEffectsGroup(), ConfigDefaults::overlayDisplayModeKey()));
}
int Settings::overlayDisplayModeInt() const
{
    return static_cast<int>(overlayDisplayMode());
}
void Settings::setOverlayDisplayMode(OverlayDisplayMode mode)
{
    const int before =
        m_store->read<int>(ConfigDefaults::snappingEffectsGroup(), ConfigDefaults::overlayDisplayModeKey());
    m_store->write(ConfigDefaults::snappingEffectsGroup(), ConfigDefaults::overlayDisplayModeKey(),
                   static_cast<int>(mode));
    const int after =
        m_store->read<int>(ConfigDefaults::snappingEffectsGroup(), ConfigDefaults::overlayDisplayModeKey());
    if (after == before) {
        return;
    }
    Q_EMIT overlayDisplayModeChanged();
    Q_EMIT settingsChanged();
}
void Settings::setOverlayDisplayModeInt(int mode)
{
    if (mode >= 0 && mode <= static_cast<int>(OverlayDisplayMode::LayoutPreview)) {
        setOverlayDisplayMode(static_cast<OverlayDisplayMode>(mode));
    }
}

// filterLayoutsByAspectRatio sits in snappingBehaviorDisplayGroup with the
// other display settings — NOTIFY signal is filterLayoutsByAspectRatioChanged.
P_STORE_GET(bool, filterLayoutsByAspectRatio, snappingBehaviorDisplayGroup, filterByAspectRatioKey, bool)
P_STORE_SET_BOOL(setFilterLayoutsByAspectRatio, snappingBehaviorDisplayGroup, filterByAspectRatioKey,
                 filterLayoutsByAspectRatioChanged)

// ── Global exclusion knobs (PhosphorConfig::Store-backed) ──────────────────
//
// Three global behavioural knobs survive in the `Exclusions` group:
// `excludeTransientWindows` (boolean), `minimumWindowWidth` (int px),
// `minimumWindowHeight` (int px). Per-app / per-class exclusion lists
// retired in v4 — the migration in src/config/configmigration.cpp drains
// the legacy lists into Application-subject Rules, and runtime
// evaluators in SnapEngine, the KWin effect, and the WTA
// pending-restore prune route through `PhosphorRules::ExclusionRules`
// over the unified store.
//
// The on-disk group name `"Exclusions"` is INTENTIONALLY kept after the
// UI moved these knobs into a card relabeled "Window filtering" on the
// General page (see src/settings/qml/GeneralPage.qml). Renaming the
// group would require a v4→v5 schema migration to remap every existing
// user config without losing the three preserved knobs — disproportionate
// for a label change. The accessor name `exclusionsGroup()` keeps the
// disk shape, and the runtime UI label is independent.

P_STORE_GET(bool, excludeTransientWindows, exclusionsGroup, transientWindowsKey, bool)
P_STORE_SET_BOOL(setExcludeTransientWindows, exclusionsGroup, transientWindowsKey, excludeTransientWindowsChanged)
P_STORE_GET(int, minimumWindowWidth, exclusionsGroup, minimumWindowWidthKey, int)
P_STORE_SET_INT(setMinimumWindowWidth, exclusionsGroup, minimumWindowWidthKey, minimumWindowWidthChanged)
P_STORE_GET(int, minimumWindowHeight, exclusionsGroup, minimumWindowHeightKey, int)
P_STORE_SET_INT(setMinimumWindowHeight, exclusionsGroup, minimumWindowHeightKey, minimumWindowHeightChanged)

// ── Animation Window Filtering (PhosphorConfig::Store-backed) ──────────────
//
// Four global animation-filtering knobs in `Animations.WindowFiltering`:
// `animationExcludeTransientWindows`, `animationExcludeNotificationsAndOsd`,
// `animationMinimumWindowWidth`, `animationMinimumWindowHeight`. Mirrors
// the Exclusions block above (plus a NotificationsAndOsd knob), stored
// independently so a user can disable animations for an app while still
// snapping it (or vice versa). Per-app / per-class animation exclusion
// lists retired in v4 — they fold into ExcludeAnimations Rules.

P_STORE_GET(bool, animationExcludeTransientWindows, animationsWindowFilteringGroup, transientWindowsKey, bool)
P_STORE_SET_BOOL(setAnimationExcludeTransientWindows, animationsWindowFilteringGroup, transientWindowsKey,
                 animationExcludeTransientWindowsChanged)
P_STORE_GET(bool, animationExcludeNotificationsAndOsd, animationsWindowFilteringGroup, notificationsAndOsdKey, bool)
P_STORE_SET_BOOL(setAnimationExcludeNotificationsAndOsd, animationsWindowFilteringGroup, notificationsAndOsdKey,
                 animationExcludeNotificationsAndOsdChanged)
P_STORE_GET(int, animationMinimumWindowWidth, animationsWindowFilteringGroup, minimumWindowWidthKey, int)
P_STORE_SET_INT(setAnimationMinimumWindowWidth, animationsWindowFilteringGroup, minimumWindowWidthKey,
                animationMinimumWindowWidthChanged)
P_STORE_GET(int, animationMinimumWindowHeight, animationsWindowFilteringGroup, minimumWindowHeightKey, int)
P_STORE_SET_INT(setAnimationMinimumWindowHeight, animationsWindowFilteringGroup, minimumWindowHeightKey,
                animationMinimumWindowHeightChanged)

// ── Decoration Window Filtering (PhosphorConfig::Store-backed) ─────────────
//
// Three global decoration-filtering knobs in `Decorations.WindowFiltering`:
// `decorationExcludeTransientWindows`, `decorationMinimumWindowWidth`,
// `decorationMinimumWindowHeight`. Mirrors the Exclusions block, stored
// independently so the KWin effect's border pass can be tuned separately
// from snapping and animation filtering. Reuses the shared leaf keys; only
// the group differs. Consumed effect-side via the generic settingsChanged
// D-Bus broadcast (see kwin-effect loadCachedSettings).

P_STORE_GET(bool, decorationExcludeTransientWindows, decorationsWindowFilteringGroup, transientWindowsKey, bool)
P_STORE_SET_BOOL(setDecorationExcludeTransientWindows, decorationsWindowFilteringGroup, transientWindowsKey,
                 decorationExcludeTransientWindowsChanged)
P_STORE_GET(int, decorationMinimumWindowWidth, decorationsWindowFilteringGroup, minimumWindowWidthKey, int)
P_STORE_SET_INT(setDecorationMinimumWindowWidth, decorationsWindowFilteringGroup, minimumWindowWidthKey,
                decorationMinimumWindowWidthChanged)
P_STORE_GET(int, decorationMinimumWindowHeight, decorationsWindowFilteringGroup, minimumWindowHeightKey, int)
P_STORE_SET_INT(setDecorationMinimumWindowHeight, decorationsWindowFilteringGroup, minimumWindowHeightKey,
                decorationMinimumWindowHeightChanged)

// animationExcludedApplications / animationExcludedWindowClasses (+ their
// add*/remove* convenience methods) retired in v4 — the v4 migration drains
// the Animations.WindowFiltering group's Applications / WindowClasses leaves
// into `ExcludeAnimations` Rules. The settings accessors had no
// consumers after the KWin effect rewired off the loadSettingAsync fetches.

// ── PhosphorZones::Zone Selector (PhosphorConfig::Store-backed) ────────────────────────────
// Three enum-ints exposed via both the typed setter and an Int adapter for
// QML binding. Stored as int, the schema clamps the range.

P_STORE_GET(bool, zoneSelectorEnabled, snappingZoneSelectorGroup, enabledKey, bool)
P_STORE_SET_BOOL(setZoneSelectorEnabled, snappingZoneSelectorGroup, enabledKey, zoneSelectorEnabledChanged)
P_STORE_GET(int, zoneSelectorTriggerDistance, snappingZoneSelectorGroup, triggerDistanceKey, int)
P_STORE_SET_INT(setZoneSelectorTriggerDistance, snappingZoneSelectorGroup, triggerDistanceKey,
                zoneSelectorTriggerDistanceChanged)

ZoneSelectorPosition Settings::zoneSelectorPosition() const
{
    return static_cast<ZoneSelectorPosition>(
        m_store->read<int>(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::positionKey()));
}
int Settings::zoneSelectorPositionInt() const
{
    return static_cast<int>(zoneSelectorPosition());
}
void Settings::setZoneSelectorPosition(ZoneSelectorPosition value)
{
    const int before = m_store->read<int>(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::positionKey());
    m_store->write(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::positionKey(), static_cast<int>(value));
    const int after = m_store->read<int>(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::positionKey());
    if (after == before) {
        return;
    }
    Q_EMIT zoneSelectorPositionChanged();
    Q_EMIT settingsChanged();
}
void Settings::setZoneSelectorPositionInt(int value)
{
    if (value >= 0 && value <= static_cast<int>(ZoneSelectorPosition::BottomRight)) {
        setZoneSelectorPosition(static_cast<ZoneSelectorPosition>(value));
    }
}

ZoneSelectorLayoutMode Settings::zoneSelectorLayoutMode() const
{
    return static_cast<ZoneSelectorLayoutMode>(
        m_store->read<int>(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::layoutModeKey()));
}
int Settings::zoneSelectorLayoutModeInt() const
{
    return static_cast<int>(zoneSelectorLayoutMode());
}
void Settings::setZoneSelectorLayoutMode(ZoneSelectorLayoutMode value)
{
    const int before = m_store->read<int>(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::layoutModeKey());
    m_store->write(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::layoutModeKey(),
                   static_cast<int>(value));
    const int after = m_store->read<int>(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::layoutModeKey());
    if (after == before) {
        return;
    }
    Q_EMIT zoneSelectorLayoutModeChanged();
    Q_EMIT settingsChanged();
}
void Settings::setZoneSelectorLayoutModeInt(int value)
{
    if (value >= 0 && value <= static_cast<int>(ZoneSelectorLayoutMode::Vertical)) {
        setZoneSelectorLayoutMode(static_cast<ZoneSelectorLayoutMode>(value));
    }
}

P_STORE_GET(int, zoneSelectorPreviewWidth, snappingZoneSelectorGroup, previewWidthKey, int)
P_STORE_SET_INT(setZoneSelectorPreviewWidth, snappingZoneSelectorGroup, previewWidthKey,
                zoneSelectorPreviewWidthChanged)
P_STORE_GET(int, zoneSelectorPreviewHeight, snappingZoneSelectorGroup, previewHeightKey, int)
P_STORE_SET_INT(setZoneSelectorPreviewHeight, snappingZoneSelectorGroup, previewHeightKey,
                zoneSelectorPreviewHeightChanged)
P_STORE_GET(bool, zoneSelectorPreviewLockAspect, snappingZoneSelectorGroup, previewLockAspectKey, bool)
P_STORE_SET_BOOL(setZoneSelectorPreviewLockAspect, snappingZoneSelectorGroup, previewLockAspectKey,
                 zoneSelectorPreviewLockAspectChanged)
P_STORE_GET(int, zoneSelectorGridColumns, snappingZoneSelectorGroup, gridColumnsKey, int)
P_STORE_SET_INT(setZoneSelectorGridColumns, snappingZoneSelectorGroup, gridColumnsKey, zoneSelectorGridColumnsChanged)

ZoneSelectorSizeMode Settings::zoneSelectorSizeMode() const
{
    return static_cast<ZoneSelectorSizeMode>(
        m_store->read<int>(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::sizeModeKey()));
}
int Settings::zoneSelectorSizeModeInt() const
{
    return static_cast<int>(zoneSelectorSizeMode());
}
void Settings::setZoneSelectorSizeMode(ZoneSelectorSizeMode value)
{
    const int before = m_store->read<int>(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::sizeModeKey());
    m_store->write(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::sizeModeKey(), static_cast<int>(value));
    const int after = m_store->read<int>(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::sizeModeKey());
    if (after == before) {
        return;
    }
    Q_EMIT zoneSelectorSizeModeChanged();
    Q_EMIT settingsChanged();
}
void Settings::setZoneSelectorSizeModeInt(int value)
{
    if (value >= 0 && value <= static_cast<int>(ZoneSelectorSizeMode::Manual)) {
        setZoneSelectorSizeMode(static_cast<ZoneSelectorSizeMode>(value));
    }
}

P_STORE_GET(int, zoneSelectorMaxRows, snappingZoneSelectorGroup, maxRowsKey, int)
P_STORE_SET_INT(setZoneSelectorMaxRows, snappingZoneSelectorGroup, maxRowsKey, zoneSelectorMaxRowsChanged)

// ── Activation + Behavior (PhosphorConfig::Store-backed) ────────────────────

// The trigger list schema declares @c QVariantList with the
// @c canonicalTriggerList validator that caps at @c MaxTriggersPerAction
// and coerces each entry to {modifier:int, mouseButton:int}. Settings
// reads via @c Store::readVariant(...).toList() and writes via
// @c Store::write — the Store routes structured values through
// @c IGroup::writeJson so they land on disk as native JSON arrays.
//
// Both Settings::MaxTriggersPerAction and the schema's trigger-list cap
// derive from ConfigDefaults::maxTriggersPerAction() — see that accessor
// for the single source of truth. This assert is defence-in-depth in
// case a future refactor accidentally detaches Settings from the shared
// constant.
static_assert(Settings::MaxTriggersPerAction == ConfigDefaults::maxTriggersPerAction(),
              "Settings::MaxTriggersPerAction must equal ConfigDefaults::maxTriggersPerAction — "
              "single source of truth lives in ConfigDefaults.");

P_STORE_GET(bool, snappingEnabled, snappingGroup, enabledKey, bool)
P_STORE_SET_BOOL(setSnappingEnabled, snappingGroup, enabledKey, snappingEnabledChanged)
P_STORE_GET(bool, toggleActivation, snappingBehaviorGroup, toggleActivationKey, bool)
P_STORE_SET_BOOL(setToggleActivation, snappingBehaviorGroup, toggleActivationKey, toggleActivationChanged)
P_STORE_GET(bool, zoneSpanToggleMode, snappingBehaviorZoneSpanGroup, toggleActivationKey, bool)
P_STORE_SET_BOOL(setZoneSpanToggleMode, snappingBehaviorZoneSpanGroup, toggleActivationKey, zoneSpanToggleModeChanged)

// Shared helper for the three "plain" trigger-list setters (activation,
// snap-assist, autotile-insert). Post-write compare — the schema's
// canonicalTriggerList validator drops non-map entries, strips unknown keys,
// and caps the list. A pre-write equality check against the stored canonical
// form would fire a spurious changed signal whenever the caller passed a
// list that canonicalises to the same value (e.g. extra keys, sub-cap
// padding). zoneSpan keeps its own setter due to the legacy-modifier sync.
void Settings::writeTriggerList(const QString& group, const QString& key, const QVariantList& triggers,
                                TriggerListSignalFn specificSignal)
{
    const QVariantList before = m_store->readVariant(group, key).toList();
    m_store->write(group, key, triggers.mid(0, MaxTriggersPerAction));
    const QVariantList after = m_store->readVariant(group, key).toList();
    if (before == after) {
        return;
    }
    Q_EMIT(this->*specificSignal)();
    Q_EMIT settingsChanged();
}

QVariantList Settings::dragActivationTriggers() const
{
    return m_store->readVariant(ConfigDefaults::snappingBehaviorGroup(), ConfigDefaults::triggersKey()).toList();
}
void Settings::setDragActivationTriggers(const QVariantList& triggers)
{
    writeTriggerList(ConfigDefaults::snappingBehaviorGroup(), ConfigDefaults::triggersKey(), triggers,
                     &Settings::dragActivationTriggersChanged);
}

P_STORE_GET(bool, zoneSpanEnabled, snappingBehaviorZoneSpanGroup, enabledKey, bool)
P_STORE_SET_BOOL(setZoneSpanEnabled, snappingBehaviorZoneSpanGroup, enabledKey, zoneSpanEnabledChanged)

DragModifier Settings::zoneSpanModifier() const
{
    return static_cast<DragModifier>(
        m_store->read<int>(ConfigDefaults::snappingBehaviorZoneSpanGroup(), ConfigDefaults::modifierKey()));
}
int Settings::zoneSpanModifierInt() const
{
    return static_cast<int>(zoneSpanModifier());
}
void Settings::setZoneSpanModifier(DragModifier modifier)
{
    // Write-then-compare so the schema's validIntOr validator gets the first
    // word on whether the request is valid. A pre-write equality check like
    // `before == static_cast<int>(modifier)` would let an invalid modifier
    // (e.g. 99) sneak past: the store would snap it to Disabled=0 and skip
    // the disk write, but we'd then stamp a trigger list with modifier=99
    // baked in because the synthesis below ran with the raw input.
    const int before =
        m_store->read<int>(ConfigDefaults::snappingBehaviorZoneSpanGroup(), ConfigDefaults::modifierKey());
    m_store->write(ConfigDefaults::snappingBehaviorZoneSpanGroup(), ConfigDefaults::modifierKey(),
                   static_cast<int>(modifier));
    const int after =
        m_store->read<int>(ConfigDefaults::snappingBehaviorZoneSpanGroup(), ConfigDefaults::modifierKey());
    if (after == before) {
        return;
    }

    // Modifier actually changed. Sync the trigger list's first entry — using
    // the validator-coerced `after` value, not the raw input, so an invalid
    // request that was snapped to the default doesn't smuggle a
    // {modifier: 99} phantom into the trigger list.
    const QVariantList beforeTriggers = zoneSpanTriggers();
    QVariantList triggers = beforeTriggers;
    if (!triggers.isEmpty()) {
        QVariantMap first = triggers.first().toMap();
        first[ConfigDefaults::triggerModifierField()] = after;
        triggers[0] = first;
    } else {
        QVariantMap trigger;
        trigger[ConfigDefaults::triggerModifierField()] = after;
        trigger[ConfigDefaults::triggerMouseButtonField()] = 0;
        triggers = {trigger};
    }
    m_store->write(ConfigDefaults::snappingBehaviorZoneSpanGroup(), ConfigDefaults::triggersKey(), triggers);
    const QVariantList afterTriggers = zoneSpanTriggers();

    Q_EMIT zoneSpanModifierChanged();
    if (afterTriggers != beforeTriggers) {
        Q_EMIT zoneSpanTriggersChanged();
    }
    Q_EMIT settingsChanged();
}
void Settings::setZoneSpanModifierInt(int modifier)
{
    if (modifier >= 0 && modifier <= static_cast<int>(DragModifier::CtrlAltMeta)) {
        setZoneSpanModifier(static_cast<DragModifier>(modifier));
    }
}

QVariantList Settings::zoneSpanTriggers() const
{
    // ZoneSpan uniquely carries BOTH a legacy scalar "Modifier" key and a
    // v2 "Triggers" list — historical result of the migration path where
    // the old setting was a single modifier. SnapAssist and DragActivation
    // only have the trigger list, so no equivalent synthesis applies there.
    //
    // When Triggers is absent on disk, synthesize a single-entry list from
    // the actual ZoneSpanModifier value instead of returning the schema's
    // static default. That keeps the two settings in sync when only one was
    // explicitly persisted — e.g. a hand-edited config that sets
    // ZoneSpanModifier but leaves Triggers untouched should report the
    // edited modifier through zoneSpanTriggers(), not the compiled default.
    //
    // hasKey and zoneSpanModifier() both open a JsonGroup, and
    // JsonBackend enforces a single active group at a time. Scope the
    // hasKey group in its own block so it's released before
    // zoneSpanModifier() opens the next one.
    bool hasTriggers = false;
    {
        auto g = m_configBackend->group(ConfigDefaults::snappingBehaviorZoneSpanGroup());
        hasTriggers = g->hasKey(ConfigDefaults::triggersKey());
    }
    if (!hasTriggers) {
        QVariantMap trigger;
        trigger[ConfigDefaults::triggerModifierField()] = static_cast<int>(zoneSpanModifier());
        trigger[ConfigDefaults::triggerMouseButtonField()] = 0;
        return {trigger};
    }
    return m_store->readVariant(ConfigDefaults::snappingBehaviorZoneSpanGroup(), ConfigDefaults::triggersKey())
        .toList();
}
void Settings::setZoneSpanTriggers(const QVariantList& triggers)
{
    // Post-write compare — see setDragActivationTriggers for the
    // canonicalisation rationale. Snapshot both triggers AND the legacy
    // modifier up front so we only emit the NOTIFY signals whose value
    // actually changed.
    const QVariantList beforeTriggers = zoneSpanTriggers();
    const int beforeModifier =
        m_store->read<int>(ConfigDefaults::snappingBehaviorZoneSpanGroup(), ConfigDefaults::modifierKey());

    m_store->write(ConfigDefaults::snappingBehaviorZoneSpanGroup(), ConfigDefaults::triggersKey(),
                   triggers.mid(0, MaxTriggersPerAction));

    // Sync legacy modifier member from first trigger with a non-zero modifier.
    // Derive from the validator-coerced post-write list so a
    // {modifier: 99} entry in the input doesn't leak past the clamp.
    const QVariantList afterTriggers = zoneSpanTriggers();
    DragModifier synced = DragModifier::Disabled;
    for (const auto& t : afterTriggers) {
        const int mod = t.toMap().value(ConfigDefaults::triggerModifierField(), 0).toInt();
        if (mod != 0) {
            synced = static_cast<DragModifier>(qBound(0, mod, static_cast<int>(DragModifier::CtrlAltMeta)));
            break;
        }
    }
    m_store->write(ConfigDefaults::snappingBehaviorZoneSpanGroup(), ConfigDefaults::modifierKey(),
                   static_cast<int>(synced));
    const int afterModifier =
        m_store->read<int>(ConfigDefaults::snappingBehaviorZoneSpanGroup(), ConfigDefaults::modifierKey());

    const bool triggersChanged = afterTriggers != beforeTriggers;
    const bool modifierChanged = afterModifier != beforeModifier;
    if (!triggersChanged && !modifierChanged) {
        return;
    }
    if (triggersChanged) {
        Q_EMIT zoneSpanTriggersChanged();
    }
    if (modifierChanged) {
        Q_EMIT zoneSpanModifierChanged();
    }
    Q_EMIT settingsChanged();
}

// Behavior: WindowHandling + SnapAssist.

P_STORE_GET(bool, keepWindowsInZonesOnResolutionChange, snappingBehaviorWindowHandlingGroup, keepOnResolutionChangeKey,
            bool)
P_STORE_SET_BOOL(setKeepWindowsInZonesOnResolutionChange, snappingBehaviorWindowHandlingGroup,
                 keepOnResolutionChangeKey, keepWindowsInZonesOnResolutionChangeChanged)
P_STORE_GET(bool, moveNewWindowsToLastZone, snappingBehaviorWindowHandlingGroup, moveNewToLastZoneKey, bool)
P_STORE_SET_BOOL(setMoveNewWindowsToLastZone, snappingBehaviorWindowHandlingGroup, moveNewToLastZoneKey,
                 moveNewWindowsToLastZoneChanged)
P_STORE_GET(bool, restoreOriginalSizeOnUnsnap, snappingBehaviorWindowHandlingGroup, restoreOnUnsnapKey, bool)
P_STORE_SET_BOOL(setRestoreOriginalSizeOnUnsnap, snappingBehaviorWindowHandlingGroup, restoreOnUnsnapKey,
                 restoreOriginalSizeOnUnsnapChanged)

StickyWindowHandling Settings::snappingStickyWindowHandling() const
{
    return static_cast<StickyWindowHandling>(m_store->read<int>(ConfigDefaults::snappingBehaviorWindowHandlingGroup(),
                                                                ConfigDefaults::stickyWindowHandlingKey()));
}
int Settings::snappingStickyWindowHandlingInt() const
{
    return static_cast<int>(snappingStickyWindowHandling());
}
void Settings::setSnappingStickyWindowHandling(StickyWindowHandling handling)
{
    const int before = m_store->read<int>(ConfigDefaults::snappingBehaviorWindowHandlingGroup(),
                                          ConfigDefaults::stickyWindowHandlingKey());
    m_store->write(ConfigDefaults::snappingBehaviorWindowHandlingGroup(), ConfigDefaults::stickyWindowHandlingKey(),
                   static_cast<int>(handling));
    const int after = m_store->read<int>(ConfigDefaults::snappingBehaviorWindowHandlingGroup(),
                                         ConfigDefaults::stickyWindowHandlingKey());
    if (after == before) {
        return;
    }
    Q_EMIT snappingStickyWindowHandlingChanged();
    Q_EMIT settingsChanged();
}
void Settings::setSnappingStickyWindowHandlingInt(int handling)
{
    if (handling >= static_cast<int>(StickyWindowHandling::TreatAsNormal)
        && handling <= static_cast<int>(StickyWindowHandling::IgnoreAll)) {
        setSnappingStickyWindowHandling(static_cast<StickyWindowHandling>(handling));
    }
}

P_STORE_GET(bool, restoreWindowsToZonesOnLogin, snappingBehaviorWindowHandlingGroup, restoreOnLoginKey, bool)
P_STORE_SET_BOOL(setRestoreWindowsToZonesOnLogin, snappingBehaviorWindowHandlingGroup, restoreOnLoginKey,
                 restoreWindowsToZonesOnLoginChanged)
P_STORE_GET(bool, snappingRestoreFloatedWindowsOnLogin, snappingBehaviorWindowHandlingGroup, restoreFloatedOnLoginKey,
            bool)
P_STORE_SET_BOOL(setSnappingRestoreFloatedWindowsOnLogin, snappingBehaviorWindowHandlingGroup, restoreFloatedOnLoginKey,
                 snappingRestoreFloatedWindowsOnLoginChanged)
P_STORE_GET(bool, autotileRestoreFloatedWindowsOnLogin, tilingBehaviorGroup, restoreFloatedOnLoginKey, bool)
P_STORE_SET_BOOL(setAutotileRestoreFloatedWindowsOnLogin, tilingBehaviorGroup, restoreFloatedOnLoginKey,
                 autotileRestoreFloatedWindowsOnLoginChanged)
P_STORE_GET(bool, snapUnfloatFallbackToZone, snappingBehaviorWindowHandlingGroup, unfloatFallbackToZoneKey, bool)
P_STORE_SET_BOOL(setSnapUnfloatFallbackToZone, snappingBehaviorWindowHandlingGroup, unfloatFallbackToZoneKey,
                 snapUnfloatFallbackToZoneChanged)
P_STORE_GET(bool, autoAssignAllLayouts, snappingBehaviorWindowHandlingGroup, autoAssignAllLayoutsKey, bool)
P_STORE_SET_BOOL(setAutoAssignAllLayouts, snappingBehaviorWindowHandlingGroup, autoAssignAllLayoutsKey,
                 autoAssignAllLayoutsChanged)
P_STORE_GET(bool, suppressDefaultLayoutAssignment, snappingBehaviorWindowHandlingGroup,
            suppressDefaultLayoutAssignmentKey, bool)
P_STORE_SET_BOOL(setSuppressDefaultLayoutAssignment, snappingBehaviorWindowHandlingGroup,
                 suppressDefaultLayoutAssignmentKey, suppressDefaultLayoutAssignmentChanged)

QString Settings::defaultLayoutId() const
{
    return m_store->read<QString>(ConfigDefaults::snappingBehaviorWindowHandlingGroup(),
                                  ConfigDefaults::defaultLayoutIdKey());
}
void Settings::setDefaultLayoutId(const QString& layoutId)
{
    const QString normalized = normalizeUuidString(layoutId);
    // A non-empty input that normalised to empty is a malformed UUID
    // (the normaliser logs a warning at that point). Treat as a no-op
    // rather than silently clearing a previously-valid stored value —
    // a typo in the KCM picker should not wipe the user's configured
    // default layout.
    if (normalized.isEmpty() && !layoutId.isEmpty()) {
        return;
    }
    if (defaultLayoutId() == normalized) {
        return;
    }
    m_store->write(ConfigDefaults::snappingBehaviorWindowHandlingGroup(), ConfigDefaults::defaultLayoutIdKey(),
                   normalized);
    Q_EMIT defaultLayoutIdChanged();
    Q_EMIT settingsChanged();
}

P_STORE_GET(bool, snapAssistFeatureEnabled, snappingBehaviorSnapAssistGroup, featureEnabledKey, bool)
P_STORE_SET_BOOL(setSnapAssistFeatureEnabled, snappingBehaviorSnapAssistGroup, featureEnabledKey,
                 snapAssistFeatureEnabledChanged)
P_STORE_GET(bool, snapAssistEnabled, snappingBehaviorSnapAssistGroup, enabledKey, bool)
P_STORE_SET_BOOL(setSnapAssistEnabled, snappingBehaviorSnapAssistGroup, enabledKey, snapAssistEnabledChanged)

QVariantList Settings::snapAssistTriggers() const
{
    return m_store->readVariant(ConfigDefaults::snappingBehaviorSnapAssistGroup(), ConfigDefaults::triggersKey())
        .toList();
}
void Settings::setSnapAssistTriggers(const QVariantList& triggers)
{
    writeTriggerList(ConfigDefaults::snappingBehaviorSnapAssistGroup(), ConfigDefaults::triggersKey(), triggers,
                     &Settings::snapAssistTriggersChanged);
}

// ── Autotiling (PhosphorConfig::Store-backed) ──────────────────────────────
// Largest group — seven sub-groups. defaultAutotileAlgorithm passes through
// PhosphorTiles::AlgorithmRegistry for validation; per-algorithm settings round-trip as a
// JSON string and sanitize via AutotileConfig::perAlgoFromVariantMap.

P_STORE_GET(bool, autotileEnabled, tilingGroup, enabledKey, bool)
P_STORE_SET_BOOL(setAutotileEnabled, tilingGroup, enabledKey, autotileEnabledChanged)

QString Settings::defaultAutotileAlgorithm() const
{
    return m_store->read<QString>(ConfigDefaults::tilingAlgorithmGroup(), ConfigDefaults::defaultKey());
}
void Settings::setDefaultAutotileAlgorithm(const QString& algorithm)
{
    // Settings does not hold a tile-algorithm registry — composition
    // roots own one each. The id is stored as-is and validated by the
    // engine (which has the registry) when the setting is consumed.
    // The previous in-place built-in allow-list went stale every time
    // PhosphorTiles added a new built-in (false-positive warnings); the
    // engine-side validation is authoritative, so the dual gate just
    // rotted.
    if (defaultAutotileAlgorithm() == algorithm) {
        return;
    }
    m_store->write(ConfigDefaults::tilingAlgorithmGroup(), ConfigDefaults::defaultKey(), algorithm);
    Q_EMIT defaultAutotileAlgorithmChanged();
    Q_EMIT settingsChanged();
}

P_STORE_GET(qreal, autotileSplitRatio, tilingAlgorithmGroup, splitRatioKey, double)
P_STORE_SET_DOUBLE(setAutotileSplitRatio, tilingAlgorithmGroup, splitRatioKey, autotileSplitRatioChanged)
P_STORE_GET(qreal, autotileSplitRatioStep, tilingAlgorithmGroup, splitRatioStepKey, double)
P_STORE_SET_DOUBLE(setAutotileSplitRatioStep, tilingAlgorithmGroup, splitRatioStepKey, autotileSplitRatioStepChanged)
P_STORE_GET(int, autotileMasterCount, tilingAlgorithmGroup, masterCountKey, int)
P_STORE_SET_INT(setAutotileMasterCount, tilingAlgorithmGroup, masterCountKey, autotileMasterCountChanged)
P_STORE_GET(int, autotileMaxWindows, tilingAlgorithmGroup, maxWindowsKey, int)
P_STORE_SET_INT(setAutotileMaxWindows, tilingAlgorithmGroup, maxWindowsKey, autotileMaxWindowsChanged)

QVariantMap Settings::autotilePerAlgorithmSettings() const
{
    // Schema declares this key as QVariantMap with sanitizePerAlgorithmSettings
    // as the validator, so read/write both pass through
    // AutotileConfig::perAlgo{From,To}VariantMap without duplicating the logic
    // here. The Store routes the map through IGroup::writeJson → native
    // JSON object on disk.
    return m_store->readVariant(ConfigDefaults::tilingAlgorithmGroup(), ConfigDefaults::perAlgorithmSettingsKey())
        .toMap();
}
void Settings::setAutotilePerAlgorithmSettings(const QVariantMap& value)
{
    // Pre-sanitize so the equality check compares against the canonicalised
    // form (the schema validator would canonicalise on both sides anyway,
    // but avoiding the redundant write keeps the settingsChanged signal
    // from firing when a caller writes a semantically equal unsorted map).
    const QVariantMap sanitized = PhosphorTileEngine::AutotileConfig::perAlgoToVariantMap(
        PhosphorTileEngine::AutotileConfig::perAlgoFromVariantMap(value));
    if (autotilePerAlgorithmSettings() == sanitized) {
        return;
    }
    m_store->write(ConfigDefaults::tilingAlgorithmGroup(), ConfigDefaults::perAlgorithmSettingsKey(), sanitized);
    Q_EMIT autotilePerAlgorithmSettingsChanged();
    Q_EMIT settingsChanged();
}

// Tiling.Gaps
// Autotile inner/outer gaps are unified with snapping — the IAutotileSettings
// gap getters forward to the shared innerGap()/outerGap*() accessors (see
// settings.h). Only the tiling-specific SmartGaps remains stored here.
P_STORE_GET(bool, autotileSmartGaps, tilingGapsGroup, smartGapsKey, bool)
P_STORE_SET_BOOL(setAutotileSmartGaps, tilingGapsGroup, smartGapsKey, autotileSmartGapsChanged)

// Tiling.Behavior
P_STORE_GET(bool, autotileFocusNewWindows, tilingBehaviorGroup, focusNewWindowsKey, bool)
P_STORE_SET_BOOL(setAutotileFocusNewWindows, tilingBehaviorGroup, focusNewWindowsKey, autotileFocusNewWindowsChanged)
P_STORE_GET(bool, autotileFocusFollowsMouse, tilingBehaviorGroup, focusFollowsMouseKey, bool)
P_STORE_SET_BOOL(setAutotileFocusFollowsMouse, tilingBehaviorGroup, focusFollowsMouseKey,
                 autotileFocusFollowsMouseChanged)
P_STORE_GET(bool, snappingFocusNewWindows, snappingBehaviorGroup, focusNewWindowsKey, bool)
P_STORE_SET_BOOL(setSnappingFocusNewWindows, snappingBehaviorGroup, focusNewWindowsKey, snappingFocusNewWindowsChanged)
P_STORE_GET(bool, snappingFocusFollowsMouse, snappingBehaviorGroup, focusFollowsMouseKey, bool)
P_STORE_SET_BOOL(setSnappingFocusFollowsMouse, snappingBehaviorGroup, focusFollowsMouseKey,
                 snappingFocusFollowsMouseChanged)

// ISnapSettings hook for the snap engine — same Snapping.Behavior value, surfaced
// under the interface name the engine consults on AutoRestored commits.
bool Settings::focusNewWindows() const
{
    return snappingFocusNewWindows();
}
bool Settings::unfloatFallbackToZone() const
{
    return snapUnfloatFallbackToZone();
}
P_STORE_GET(bool, autotileRespectMinimumSize, tilingBehaviorGroup, respectMinimumSizeKey, bool)
P_STORE_SET_BOOL(setAutotileRespectMinimumSize, tilingBehaviorGroup, respectMinimumSizeKey,
                 autotileRespectMinimumSizeChanged)

Settings::AutotileInsertPosition Settings::autotileInsertPosition() const
{
    return static_cast<AutotileInsertPosition>(
        m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::insertPositionKey()));
}
int Settings::autotileInsertPositionInt() const
{
    return static_cast<int>(autotileInsertPosition());
}
void Settings::setAutotileInsertPosition(AutotileInsertPosition position)
{
    const int before = m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::insertPositionKey());
    m_store->write(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::insertPositionKey(),
                   static_cast<int>(position));
    const int after = m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::insertPositionKey());
    if (after == before) {
        return;
    }
    Q_EMIT autotileInsertPositionChanged();
    Q_EMIT settingsChanged();
}
void Settings::setAutotileInsertPositionInt(int position)
{
    if (position >= ConfigDefaults::autotileInsertPositionMin()
        && position <= ConfigDefaults::autotileInsertPositionMax()) {
        setAutotileInsertPosition(static_cast<AutotileInsertPosition>(position));
    }
}

StickyWindowHandling Settings::autotileStickyWindowHandling() const
{
    return static_cast<StickyWindowHandling>(
        m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::stickyWindowHandlingKey()));
}
int Settings::autotileStickyWindowHandlingInt() const
{
    return static_cast<int>(autotileStickyWindowHandling());
}
void Settings::setAutotileStickyWindowHandling(StickyWindowHandling handling)
{
    const int before =
        m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::stickyWindowHandlingKey());
    m_store->write(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::stickyWindowHandlingKey(),
                   static_cast<int>(handling));
    const int after =
        m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::stickyWindowHandlingKey());
    if (after == before) {
        return;
    }
    Q_EMIT autotileStickyWindowHandlingChanged();
    Q_EMIT settingsChanged();
}
void Settings::setAutotileStickyWindowHandlingInt(int handling)
{
    if (handling >= static_cast<int>(StickyWindowHandling::TreatAsNormal)
        && handling <= static_cast<int>(StickyWindowHandling::IgnoreAll)) {
        setAutotileStickyWindowHandling(static_cast<StickyWindowHandling>(handling));
    }
}

AutotileDragBehavior Settings::autotileDragBehavior() const
{
    return static_cast<AutotileDragBehavior>(
        m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::dragBehaviorKey()));
}
int Settings::autotileDragBehaviorInt() const
{
    return static_cast<int>(autotileDragBehavior());
}
void Settings::setAutotileDragBehavior(AutotileDragBehavior behavior)
{
    const int before = m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::dragBehaviorKey());
    m_store->write(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::dragBehaviorKey(),
                   static_cast<int>(behavior));
    const int after = m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::dragBehaviorKey());
    if (after == before) {
        return;
    }
    Q_EMIT autotileDragBehaviorChanged();
    Q_EMIT settingsChanged();
}
void Settings::setAutotileDragBehaviorInt(int behavior)
{
    if (behavior == static_cast<int>(AutotileDragBehavior::Float)
        || behavior == static_cast<int>(AutotileDragBehavior::Reorder)) {
        setAutotileDragBehavior(static_cast<AutotileDragBehavior>(behavior));
    }
}

AutotileOverflowBehavior Settings::autotileOverflowBehavior() const
{
    return static_cast<AutotileOverflowBehavior>(
        m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::overflowBehaviorKey()));
}
int Settings::autotileOverflowBehaviorInt() const
{
    return static_cast<int>(autotileOverflowBehavior());
}
void Settings::setAutotileOverflowBehavior(AutotileOverflowBehavior behavior)
{
    const int before = m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::overflowBehaviorKey());
    m_store->write(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::overflowBehaviorKey(),
                   static_cast<int>(behavior));
    const int after = m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::overflowBehaviorKey());
    if (after == before) {
        return;
    }
    Q_EMIT autotileOverflowBehaviorChanged();
    Q_EMIT settingsChanged();
}
void Settings::setAutotileOverflowBehaviorInt(int behavior)
{
    if (behavior == static_cast<int>(AutotileOverflowBehavior::Float)
        || behavior == static_cast<int>(AutotileOverflowBehavior::Unlimited)) {
        setAutotileOverflowBehavior(static_cast<AutotileOverflowBehavior>(behavior));
    }
}

QStringList Settings::lockedScreens() const
{
    return parseCommaList(
        m_store->read<QString>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::lockedScreensKey()));
}
void Settings::setLockedScreens(const QStringList& screens)
{
    // Post-write compare — see writeDisableEntries for the canonicalisation
    // rationale.
    const QString before =
        m_store->read<QString>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::lockedScreensKey());
    m_store->write(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::lockedScreensKey(),
                   screens.join(QLatin1Char(',')));
    const QString after =
        m_store->read<QString>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::lockedScreensKey());
    if (before == after) {
        return;
    }
    Q_EMIT lockedScreensChanged();
    Q_EMIT settingsChanged();
}

bool Settings::isScreenLocked(const QString& screenIdOrName) const
{
    return isContextLocked(screenIdOrName, 0, QString());
}
void Settings::setScreenLocked(const QString& screenIdOrName, bool locked)
{
    setContextLocked(screenIdOrName, 0, QString(), locked);
}

bool Settings::isContextLocked(const QString& screenIdOrName, int virtualDesktop, const QString& activity) const
{
    const QStringList locked = lockedScreens();
    const QStringList namesToCheck = PhosphorScreens::ScreenIdentity::variantsFor(screenIdOrName);
    for (const QString& name : namesToCheck) {
        if (virtualDesktop > 0 && !activity.isEmpty()) {
            const QString k = name + QLatin1Char(':') + QString::number(virtualDesktop) + QLatin1Char(':') + activity;
            if (locked.contains(k)) {
                return true;
            }
        }
        if (virtualDesktop > 0) {
            const QString k = name + QLatin1Char(':') + QString::number(virtualDesktop);
            if (locked.contains(k)) {
                return true;
            }
        }
        if (locked.contains(name)) {
            return true;
        }
    }
    return false;
}

void Settings::setContextLocked(const QString& screenIdOrName, int virtualDesktop, const QString& activity, bool locked)
{
    // Composite-key format mirrors isContextLocked():
    //   name                          → per-screen lock
    //   name:<desktop>                → per-(screen,desktop) lock
    //   name:<desktop>:<activity>     → per-(screen,desktop,activity) lock
    //
    // (screen, 0, "activity") has no representable form in the current
    // schema. Silently composing just `name` would land the caller on a
    // whole-screen lock while their intent was activity-scoped — that's
    // data loss masquerading as success. Reject loudly at the boundary so
    // the caller can either supply a desktop or accept the screen scope
    // explicitly with an empty activity.
    if (virtualDesktop <= 0 && !activity.isEmpty()) {
        qCWarning(lcConfig) << "Settings::setContextLocked: activity supplied without a virtual desktop —"
                            << "(screen, 0, activity) cannot be expressed in the lockedScreens schema."
                            << "Refusing to write a whole-screen lock that would silently drop the activity.";
        return;
    }
    QString key = screenIdOrName;
    if (virtualDesktop > 0) {
        key += QLatin1Char(':') + QString::number(virtualDesktop);
        if (!activity.isEmpty()) {
            key += QLatin1Char(':') + activity;
        }
    }
    QStringList current = lockedScreens();
    if (locked && !current.contains(key)) {
        current.append(key);
        setLockedScreens(current);
    } else if (!locked && current.removeAll(key) > 0) {
        setLockedScreens(current);
    }
}

QVariantList Settings::autotileDragInsertTriggers() const
{
    return m_store->readVariant(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::triggersKey()).toList();
}
void Settings::setAutotileDragInsertTriggers(const QVariantList& triggers)
{
    writeTriggerList(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::triggersKey(), triggers,
                     &Settings::autotileDragInsertTriggersChanged);
}

P_STORE_GET(bool, autotileDragInsertToggle, tilingBehaviorGroup, toggleActivationKey, bool)
P_STORE_SET_BOOL(setAutotileDragInsertToggle, tilingBehaviorGroup, toggleActivationKey, autotileDragInsertToggleChanged)

// ── reset / color helpers ────────────────────────────────────────────────────

void Settings::reset()
{
    // Delete all managed groups plus unmanaged groups (reset nukes everything)
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
    for (const Mode mode : PhosphorZones::allModes()) {
        resetMonitorsBefore.insert(mode, disabledMonitors(mode));
        resetDesktopsBefore.insert(mode, disabledDesktops(mode));
        resetActivitiesBefore.insert(mode, disabledActivities(mode));
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
        if (disabledMonitors(mode) != resetMonitorsBefore.value(mode)) {
            Q_EMIT disabledMonitorsChanged(mode);
            anyDisableChanged = true;
        }
        if (disabledDesktops(mode) != resetDesktopsBefore.value(mode)) {
            Q_EMIT disabledDesktopsChanged(mode);
            anyDisableChanged = true;
        }
        if (disabledActivities(mode) != resetActivitiesBefore.value(mode)) {
            Q_EMIT disabledActivitiesChanged(mode);
            anyDisableChanged = true;
        }
    }
    if (anyDisableChanged) {
        Q_EMIT settingsChanged();
    }

    qCInfo(lcConfig) << "Settings reset to defaults";
}

// ── Shortcuts (PhosphorConfig::Store-backed) ────────────────────────────────
// Every shortcut is a flat string; schema registers them without validators.
// Change-detection goes through the P_STORE_SET_STRING macro.

// Global shortcuts — meta actions, zone navigation, snap-to-zone numbered,
// layout rotation, virtual-screen swap/rotate.
P_STORE_GET(QString, openEditorShortcut, shortcutsGlobalGroup, openEditorKey, QString)
P_STORE_SET_STRING(setOpenEditorShortcut, shortcutsGlobalGroup, openEditorKey, openEditorShortcutChanged)
P_STORE_GET(QString, openSettingsShortcut, shortcutsGlobalGroup, openSettingsKey, QString)
P_STORE_SET_STRING(setOpenSettingsShortcut, shortcutsGlobalGroup, openSettingsKey, openSettingsShortcutChanged)
P_STORE_GET(QString, previousLayoutShortcut, shortcutsGlobalGroup, previousLayoutKey, QString)
P_STORE_SET_STRING(setPreviousLayoutShortcut, shortcutsGlobalGroup, previousLayoutKey, previousLayoutShortcutChanged)
P_STORE_GET(QString, nextLayoutShortcut, shortcutsGlobalGroup, nextLayoutKey, QString)
P_STORE_SET_STRING(setNextLayoutShortcut, shortcutsGlobalGroup, nextLayoutKey, nextLayoutShortcutChanged)

// quickLayoutN and snapToZoneN arrays — dispatch to per-index key.
// Each wrapper reads/writes the same store using ConfigDefaults::quickLayoutKey(n).

#define P_QUICK_LAYOUT(N)                                                                                              \
    QString Settings::quickLayout##N##Shortcut() const                                                                 \
    {                                                                                                                  \
        return m_store->read<QString>(ConfigDefaults::shortcutsGlobalGroup(), ConfigDefaults::quickLayoutKey(N));      \
    }                                                                                                                  \
    void Settings::setQuickLayout##N##Shortcut(const QString& shortcut)                                                \
    {                                                                                                                  \
        setQuickLayoutShortcut(N - 1, shortcut);                                                                       \
    }

P_QUICK_LAYOUT(1)
P_QUICK_LAYOUT(2)
P_QUICK_LAYOUT(3)
P_QUICK_LAYOUT(4)
P_QUICK_LAYOUT(5)
P_QUICK_LAYOUT(6)
P_QUICK_LAYOUT(7)
P_QUICK_LAYOUT(8)
P_QUICK_LAYOUT(9)
#undef P_QUICK_LAYOUT

QString Settings::quickLayoutShortcut(int index) const
{
    if (index < 0 || index >= 9) {
        return {};
    }
    return m_store->read<QString>(ConfigDefaults::shortcutsGlobalGroup(), ConfigDefaults::quickLayoutKey(index + 1));
}

void Settings::setQuickLayoutShortcut(int index, const QString& shortcut)
{
    if (index < 0 || index >= 9) {
        return;
    }
    const QString key = ConfigDefaults::quickLayoutKey(index + 1);
    if (m_store->read<QString>(ConfigDefaults::shortcutsGlobalGroup(), key) == shortcut) {
        return;
    }
    m_store->write(ConfigDefaults::shortcutsGlobalGroup(), key, shortcut);

    static constexpr ShortcutSignalFn signals[9] = {
        &Settings::quickLayout1ShortcutChanged, &Settings::quickLayout2ShortcutChanged,
        &Settings::quickLayout3ShortcutChanged, &Settings::quickLayout4ShortcutChanged,
        &Settings::quickLayout5ShortcutChanged, &Settings::quickLayout6ShortcutChanged,
        &Settings::quickLayout7ShortcutChanged, &Settings::quickLayout8ShortcutChanged,
        &Settings::quickLayout9ShortcutChanged,
    };
    Q_EMIT(this->*signals[index])();
    Q_EMIT settingsChanged();
}

// Navigation shortcuts.
P_STORE_GET(QString, moveWindowLeftShortcut, shortcutsGlobalGroup, moveWindowLeftKey, QString)
P_STORE_SET_STRING(setMoveWindowLeftShortcut, shortcutsGlobalGroup, moveWindowLeftKey, moveWindowLeftShortcutChanged)
P_STORE_GET(QString, moveWindowRightShortcut, shortcutsGlobalGroup, moveWindowRightKey, QString)
P_STORE_SET_STRING(setMoveWindowRightShortcut, shortcutsGlobalGroup, moveWindowRightKey, moveWindowRightShortcutChanged)
P_STORE_GET(QString, moveWindowUpShortcut, shortcutsGlobalGroup, moveWindowUpKey, QString)
P_STORE_SET_STRING(setMoveWindowUpShortcut, shortcutsGlobalGroup, moveWindowUpKey, moveWindowUpShortcutChanged)
P_STORE_GET(QString, moveWindowDownShortcut, shortcutsGlobalGroup, moveWindowDownKey, QString)
P_STORE_SET_STRING(setMoveWindowDownShortcut, shortcutsGlobalGroup, moveWindowDownKey, moveWindowDownShortcutChanged)
P_STORE_GET(QString, focusZoneLeftShortcut, shortcutsGlobalGroup, focusZoneLeftKey, QString)
P_STORE_SET_STRING(setFocusZoneLeftShortcut, shortcutsGlobalGroup, focusZoneLeftKey, focusZoneLeftShortcutChanged)
P_STORE_GET(QString, focusZoneRightShortcut, shortcutsGlobalGroup, focusZoneRightKey, QString)
P_STORE_SET_STRING(setFocusZoneRightShortcut, shortcutsGlobalGroup, focusZoneRightKey, focusZoneRightShortcutChanged)
P_STORE_GET(QString, focusZoneUpShortcut, shortcutsGlobalGroup, focusZoneUpKey, QString)
P_STORE_SET_STRING(setFocusZoneUpShortcut, shortcutsGlobalGroup, focusZoneUpKey, focusZoneUpShortcutChanged)
P_STORE_GET(QString, focusZoneDownShortcut, shortcutsGlobalGroup, focusZoneDownKey, QString)
P_STORE_SET_STRING(setFocusZoneDownShortcut, shortcutsGlobalGroup, focusZoneDownKey, focusZoneDownShortcutChanged)
P_STORE_GET(QString, pushToEmptyZoneShortcut, shortcutsGlobalGroup, pushToEmptyZoneKey, QString)
P_STORE_SET_STRING(setPushToEmptyZoneShortcut, shortcutsGlobalGroup, pushToEmptyZoneKey, pushToEmptyZoneShortcutChanged)
P_STORE_GET(QString, restoreWindowSizeShortcut, shortcutsGlobalGroup, restoreWindowSizeKey, QString)
P_STORE_SET_STRING(setRestoreWindowSizeShortcut, shortcutsGlobalGroup, restoreWindowSizeKey,
                   restoreWindowSizeShortcutChanged)
P_STORE_GET(QString, toggleWindowFloatShortcut, shortcutsGlobalGroup, toggleWindowFloatKey, QString)
P_STORE_SET_STRING(setToggleWindowFloatShortcut, shortcutsGlobalGroup, toggleWindowFloatKey,
                   toggleWindowFloatShortcutChanged)
P_STORE_GET(QString, swapWindowLeftShortcut, shortcutsGlobalGroup, swapWindowLeftKey, QString)
P_STORE_SET_STRING(setSwapWindowLeftShortcut, shortcutsGlobalGroup, swapWindowLeftKey, swapWindowLeftShortcutChanged)
P_STORE_GET(QString, swapWindowRightShortcut, shortcutsGlobalGroup, swapWindowRightKey, QString)
P_STORE_SET_STRING(setSwapWindowRightShortcut, shortcutsGlobalGroup, swapWindowRightKey, swapWindowRightShortcutChanged)
P_STORE_GET(QString, swapWindowUpShortcut, shortcutsGlobalGroup, swapWindowUpKey, QString)
P_STORE_SET_STRING(setSwapWindowUpShortcut, shortcutsGlobalGroup, swapWindowUpKey, swapWindowUpShortcutChanged)
P_STORE_GET(QString, swapWindowDownShortcut, shortcutsGlobalGroup, swapWindowDownKey, QString)
P_STORE_SET_STRING(setSwapWindowDownShortcut, shortcutsGlobalGroup, swapWindowDownKey, swapWindowDownShortcutChanged)

// snapToZone1..9 — same dispatch pattern as quickLayout.
#define P_SNAP_TO_ZONE(N)                                                                                              \
    QString Settings::snapToZone##N##Shortcut() const                                                                  \
    {                                                                                                                  \
        return m_store->read<QString>(ConfigDefaults::shortcutsGlobalGroup(), ConfigDefaults::snapToZoneKey(N));       \
    }                                                                                                                  \
    void Settings::setSnapToZone##N##Shortcut(const QString& shortcut)                                                 \
    {                                                                                                                  \
        setSnapToZoneShortcut(N - 1, shortcut);                                                                        \
    }

P_SNAP_TO_ZONE(1)
P_SNAP_TO_ZONE(2)
P_SNAP_TO_ZONE(3)
P_SNAP_TO_ZONE(4)
P_SNAP_TO_ZONE(5)
P_SNAP_TO_ZONE(6)
P_SNAP_TO_ZONE(7)
P_SNAP_TO_ZONE(8)
P_SNAP_TO_ZONE(9)
#undef P_SNAP_TO_ZONE

QString Settings::snapToZoneShortcut(int index) const
{
    if (index < 0 || index >= 9) {
        return {};
    }
    return m_store->read<QString>(ConfigDefaults::shortcutsGlobalGroup(), ConfigDefaults::snapToZoneKey(index + 1));
}

void Settings::setSnapToZoneShortcut(int index, const QString& shortcut)
{
    if (index < 0 || index >= 9) {
        return;
    }
    const QString key = ConfigDefaults::snapToZoneKey(index + 1);
    if (m_store->read<QString>(ConfigDefaults::shortcutsGlobalGroup(), key) == shortcut) {
        return;
    }
    m_store->write(ConfigDefaults::shortcutsGlobalGroup(), key, shortcut);
    static constexpr ShortcutSignalFn signals[9] = {
        &Settings::snapToZone1ShortcutChanged, &Settings::snapToZone2ShortcutChanged,
        &Settings::snapToZone3ShortcutChanged, &Settings::snapToZone4ShortcutChanged,
        &Settings::snapToZone5ShortcutChanged, &Settings::snapToZone6ShortcutChanged,
        &Settings::snapToZone7ShortcutChanged, &Settings::snapToZone8ShortcutChanged,
        &Settings::snapToZone9ShortcutChanged,
    };
    Q_EMIT(this->*signals[index])();
    Q_EMIT settingsChanged();
}

P_STORE_GET(QString, rotateWindowsClockwiseShortcut, shortcutsGlobalGroup, rotateWindowsClockwiseKey, QString)
P_STORE_SET_STRING(setRotateWindowsClockwiseShortcut, shortcutsGlobalGroup, rotateWindowsClockwiseKey,
                   rotateWindowsClockwiseShortcutChanged)
P_STORE_GET(QString, rotateWindowsCounterclockwiseShortcut, shortcutsGlobalGroup, rotateWindowsCounterclockwiseKey,
            QString)
P_STORE_SET_STRING(setRotateWindowsCounterclockwiseShortcut, shortcutsGlobalGroup, rotateWindowsCounterclockwiseKey,
                   rotateWindowsCounterclockwiseShortcutChanged)
P_STORE_GET(QString, cycleWindowForwardShortcut, shortcutsGlobalGroup, cycleWindowForwardKey, QString)
P_STORE_SET_STRING(setCycleWindowForwardShortcut, shortcutsGlobalGroup, cycleWindowForwardKey,
                   cycleWindowForwardShortcutChanged)
P_STORE_GET(QString, cycleWindowBackwardShortcut, shortcutsGlobalGroup, cycleWindowBackwardKey, QString)
P_STORE_SET_STRING(setCycleWindowBackwardShortcut, shortcutsGlobalGroup, cycleWindowBackwardKey,
                   cycleWindowBackwardShortcutChanged)
P_STORE_GET(QString, resnapToNewLayoutShortcut, shortcutsGlobalGroup, resnapToNewLayoutKey, QString)
P_STORE_SET_STRING(setResnapToNewLayoutShortcut, shortcutsGlobalGroup, resnapToNewLayoutKey,
                   resnapToNewLayoutShortcutChanged)
P_STORE_GET(QString, snapAllWindowsShortcut, shortcutsGlobalGroup, snapAllWindowsKey, QString)
P_STORE_SET_STRING(setSnapAllWindowsShortcut, shortcutsGlobalGroup, snapAllWindowsKey, snapAllWindowsShortcutChanged)
P_STORE_GET(QString, layoutPickerShortcut, shortcutsGlobalGroup, layoutPickerKey, QString)
P_STORE_SET_STRING(setLayoutPickerShortcut, shortcutsGlobalGroup, layoutPickerKey, layoutPickerShortcutChanged)
P_STORE_GET(QString, toggleLayoutLockShortcut, shortcutsGlobalGroup, toggleLayoutLockKey, QString)
P_STORE_SET_STRING(setToggleLayoutLockShortcut, shortcutsGlobalGroup, toggleLayoutLockKey,
                   toggleLayoutLockShortcutChanged)
P_STORE_GET(QString, swapVirtualScreenLeftShortcut, shortcutsGlobalGroup, swapVirtualScreenLeftKey, QString)
P_STORE_SET_STRING(setSwapVirtualScreenLeftShortcut, shortcutsGlobalGroup, swapVirtualScreenLeftKey,
                   swapVirtualScreenLeftShortcutChanged)
P_STORE_GET(QString, swapVirtualScreenRightShortcut, shortcutsGlobalGroup, swapVirtualScreenRightKey, QString)
P_STORE_SET_STRING(setSwapVirtualScreenRightShortcut, shortcutsGlobalGroup, swapVirtualScreenRightKey,
                   swapVirtualScreenRightShortcutChanged)
P_STORE_GET(QString, swapVirtualScreenUpShortcut, shortcutsGlobalGroup, swapVirtualScreenUpKey, QString)
P_STORE_SET_STRING(setSwapVirtualScreenUpShortcut, shortcutsGlobalGroup, swapVirtualScreenUpKey,
                   swapVirtualScreenUpShortcutChanged)
P_STORE_GET(QString, swapVirtualScreenDownShortcut, shortcutsGlobalGroup, swapVirtualScreenDownKey, QString)
P_STORE_SET_STRING(setSwapVirtualScreenDownShortcut, shortcutsGlobalGroup, swapVirtualScreenDownKey,
                   swapVirtualScreenDownShortcutChanged)
P_STORE_GET(QString, rotateVirtualScreensClockwiseShortcut, shortcutsGlobalGroup, rotateVirtualScreensClockwiseKey,
            QString)
P_STORE_SET_STRING(setRotateVirtualScreensClockwiseShortcut, shortcutsGlobalGroup, rotateVirtualScreensClockwiseKey,
                   rotateVirtualScreensClockwiseShortcutChanged)
P_STORE_GET(QString, rotateVirtualScreensCounterclockwiseShortcut, shortcutsGlobalGroup,
            rotateVirtualScreensCounterclockwiseKey, QString)
P_STORE_SET_STRING(setRotateVirtualScreensCounterclockwiseShortcut, shortcutsGlobalGroup,
                   rotateVirtualScreensCounterclockwiseKey, rotateVirtualScreensCounterclockwiseShortcutChanged)

// Tiling shortcuts.
P_STORE_GET(QString, autotileToggleShortcut, shortcutsTilingGroup, toggleKey, QString)
P_STORE_SET_STRING(setAutotileToggleShortcut, shortcutsTilingGroup, toggleKey, autotileToggleShortcutChanged)
P_STORE_GET(QString, autotileFocusMasterShortcut, shortcutsTilingGroup, focusMasterKey, QString)
P_STORE_SET_STRING(setAutotileFocusMasterShortcut, shortcutsTilingGroup, focusMasterKey,
                   autotileFocusMasterShortcutChanged)
P_STORE_GET(QString, autotileSwapMasterShortcut, shortcutsTilingGroup, swapMasterKey, QString)
P_STORE_SET_STRING(setAutotileSwapMasterShortcut, shortcutsTilingGroup, swapMasterKey,
                   autotileSwapMasterShortcutChanged)
P_STORE_GET(QString, autotileIncMasterRatioShortcut, shortcutsTilingGroup, incMasterRatioKey, QString)
P_STORE_SET_STRING(setAutotileIncMasterRatioShortcut, shortcutsTilingGroup, incMasterRatioKey,
                   autotileIncMasterRatioShortcutChanged)
P_STORE_GET(QString, autotileDecMasterRatioShortcut, shortcutsTilingGroup, decMasterRatioKey, QString)
P_STORE_SET_STRING(setAutotileDecMasterRatioShortcut, shortcutsTilingGroup, decMasterRatioKey,
                   autotileDecMasterRatioShortcutChanged)
P_STORE_GET(QString, autotileIncMasterCountShortcut, shortcutsTilingGroup, incMasterCountKey, QString)
P_STORE_SET_STRING(setAutotileIncMasterCountShortcut, shortcutsTilingGroup, incMasterCountKey,
                   autotileIncMasterCountShortcutChanged)
P_STORE_GET(QString, autotileDecMasterCountShortcut, shortcutsTilingGroup, decMasterCountKey, QString)
P_STORE_SET_STRING(setAutotileDecMasterCountShortcut, shortcutsTilingGroup, decMasterCountKey,
                   autotileDecMasterCountShortcutChanged)
P_STORE_GET(QString, autotileRetileShortcut, shortcutsTilingGroup, retileKey, QString)
P_STORE_SET_STRING(setAutotileRetileShortcut, shortcutsTilingGroup, retileKey, autotileRetileShortcutChanged)

// Editor shortcuts.
P_STORE_GET(QString, editorDuplicateShortcut, editorShortcutsGroup, duplicateKey, QString)
P_STORE_SET_STRING(setEditorDuplicateShortcut, editorShortcutsGroup, duplicateKey, editorDuplicateShortcutChanged)
P_STORE_GET(QString, editorSplitHorizontalShortcut, editorShortcutsGroup, splitHorizontalKey, QString)
P_STORE_SET_STRING(setEditorSplitHorizontalShortcut, editorShortcutsGroup, splitHorizontalKey,
                   editorSplitHorizontalShortcutChanged)
P_STORE_GET(QString, editorSplitVerticalShortcut, editorShortcutsGroup, splitVerticalKey, QString)
P_STORE_SET_STRING(setEditorSplitVerticalShortcut, editorShortcutsGroup, splitVerticalKey,
                   editorSplitVerticalShortcutChanged)
P_STORE_GET(QString, editorFillShortcut, editorShortcutsGroup, fillKey, QString)
P_STORE_SET_STRING(setEditorFillShortcut, editorShortcutsGroup, fillKey, editorFillShortcutChanged)

// Editor snapping + fill-on-drop toggles.
P_STORE_GET(bool, editorGridSnappingEnabled, editorSnappingGroup, gridEnabledKey, bool)
P_STORE_SET_BOOL(setEditorGridSnappingEnabled, editorSnappingGroup, gridEnabledKey, editorGridSnappingEnabledChanged)
P_STORE_GET(bool, editorEdgeSnappingEnabled, editorSnappingGroup, edgeEnabledKey, bool)
P_STORE_SET_BOOL(setEditorEdgeSnappingEnabled, editorSnappingGroup, edgeEnabledKey, editorEdgeSnappingEnabledChanged)
P_STORE_GET(qreal, editorSnapIntervalX, editorSnappingGroup, intervalXKey, double)
P_STORE_SET_DOUBLE(setEditorSnapIntervalX, editorSnappingGroup, intervalXKey, editorSnapIntervalXChanged)
P_STORE_GET(qreal, editorSnapIntervalY, editorSnappingGroup, intervalYKey, double)
P_STORE_SET_DOUBLE(setEditorSnapIntervalY, editorSnappingGroup, intervalYKey, editorSnapIntervalYChanged)
P_STORE_GET(int, editorSnapOverrideModifier, editorSnappingGroup, overrideModifierKey, int)
P_STORE_SET_INT(setEditorSnapOverrideModifier, editorSnappingGroup, overrideModifierKey,
                editorSnapOverrideModifierChanged)
P_STORE_GET(bool, fillOnDropEnabled, editorFillOnDropGroup, enabledKey, bool)
P_STORE_SET_BOOL(setFillOnDropEnabled, editorFillOnDropGroup, enabledKey, fillOnDropEnabledChanged)
P_STORE_GET(int, fillOnDropModifier, editorFillOnDropGroup, modifierKey, int)
P_STORE_SET_INT(setFillOnDropModifier, editorFillOnDropGroup, modifierKey, fillOnDropModifierChanged)

// ── Color helpers ────────────────────────────────────────────────────────────

QString Settings::loadColorsFromFile(const QString& filePath)
{
    ColorImportResult result = ColorImporter::importFromFile(filePath);
    if (!result.success) {
        return result.errorMessage;
    }
    setHighlightColor(result.highlightColor);
    setInactiveColor(result.inactiveColor);
    setBorderColor(result.borderColor);
    setLabelFontColor(result.labelFontColor);
    if (useSystemColors()) {
        setUseSystemColors(false);
    }
    return QString(); // Success - no error
}

void Settings::trackSystemPaletteChanges()
{
    // Track system palette changes at runtime. load() derives the zone
    // colors from the CURRENT QGuiApplication palette when useSystemColors
    // is on — a one-time snapshot. The palette changes underneath every
    // long-running process whenever the desktop color scheme changes
    // (wallpaper-driven schemes switch often), so without re-deriving, the
    // daemon's overlays and the settings app's previews render colors from
    // whichever scheme was active when the process started — they only
    // matched again after a daemon restart. Qt 6 delivers
    // QEvent::ApplicationPaletteChange to the application object; there is
    // no signal for it, hence the event filter. Guarded: the config
    // library is also used by non-GUI tools where qGuiApp is null.
    //
    // Cost note: the filter is installed on the application object, so it
    // sees EVERY event delivered in the process; the leading guard in
    // eventFilter() keeps the per-event cost to two compares (watched
    // pointer + event type). That per-event tax also scales with instance
    // count — Settings must remain a per-process near-singleton.
    if (qGuiApp) {
        qGuiApp->installEventFilter(this);
    }
}

bool Settings::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == qGuiApp && event->type() == QEvent::ApplicationPaletteChange && useSystemColors()) {
        // Derived values, not user edits. TWO mechanisms keep a runtime theme
        // switch from reading as unsaved changes:
        //  1. m_applyingSystemPalette (RAII, restored even if a setter throws)
        //     is up for the whole synchronous re-derive, so
        //     SettingsController::onSettingsPropertyChanged() — wired to every
        //     color NOTIFY — sees isApplyingSystemPalette() and skips
        //     setNeedsSave(true), keeping the global footer quiet.
        //  2. rebaselineDerivedColorKeys() refreshes the committed baseline so
        //     value-based isKeyModified() checks stay false afterwards. See
        //     that function for why ONLY this path rebaselines — and it is
        //     further gated here: when the useSystemColors toggle itself is a
        //     PENDING unsaved edit, the toggle and the colors it derived must
        //     stay discardable together, so the baseline is left alone.
        QScopedValueRollback<bool> applying(m_applyingSystemPalette, true);
        applySystemColorScheme();
        if (!isKeyModified(ConfigDefaults::snappingZonesColorsGroup(), ConfigDefaults::useSystemKey())) {
            rebaselineDerivedColorKeys();
        }
    }
    return ISettings::eventFilter(watched, event);
}

void Settings::applySystemColorScheme()
{
    // QPalette respects QT_QPA_PLATFORMTHEME — on non-KDE desktops, Qt reads
    // the platform theme (qt6ct, gnome, lxqt) to populate the palette.
    const QPalette pal = QGuiApplication::palette();

    QColor highlight = pal.color(QPalette::Active, QPalette::Highlight);
    highlight.setAlpha(::PhosphorZones::ZoneDefaults::HighlightAlpha);
    setHighlightColor(highlight);

    // Inactive fill and border derive from the background family, not Text.
    // Text-at-alpha renders as a washed grey film on every dark scheme (the
    // same fabrication the QML side eliminated); AlternateBase is the View
    // alternate surface, and Mid is the palette's separator-grade shade, so
    // both follow the active color scheme with the intended emphasis.
    QColor inactive = pal.color(QPalette::Active, QPalette::AlternateBase);
    inactive.setAlpha(::PhosphorZones::ZoneDefaults::InactiveAlpha);
    setInactiveColor(inactive);

    QColor border = pal.color(QPalette::Active, QPalette::Mid);
    border.setAlpha(::PhosphorZones::ZoneDefaults::BorderAlpha);
    setBorderColor(border);

    const QColor fontColor = pal.color(QPalette::Active, QPalette::Text);
    setLabelFontColor(fontColor);
}

#undef P_STORE_GET
#undef P_STORE_SET_BOOL
#undef P_STORE_SET_INT
#undef P_STORE_SET_DOUBLE
#undef P_STORE_SET_COLOR
#undef P_STORE_SET_STRING

} // namespace PlasmaZones

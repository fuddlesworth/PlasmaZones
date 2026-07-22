// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config/settings.h"
#include "config/settings/settings_detail.h"
#include "config/configdefaults.h"
#include "core/platform/logging.h"

#include <PhosphorTileEngine/AutotileConfig.h>

#include <QVariantList>
#include <QVariantMap>

namespace PlasmaZones {

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
    // Trigger lists are whole-replace composites reachable over D-Bus from
    // both processes, so the stale-guard refresh applies: a `before` read
    // from a stale cache could equal the incoming value and swallow a write
    // the disk actually needs.
    refreshCleanBackendFromDisk();
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
    // Same composite stale-guard as writeTriggerList: the modifier synthesis
    // below read-modify-writes the whole zoneSpan trigger list, so a stale
    // cache would bake stale sibling trigger entries into the rewrite.
    refreshCleanBackendFromDisk();
    // Write-then-compare so the schema's validIntOr validator gets the first
    // word on whether the request is valid. A pre-write equality check like
    // `before == static_cast<int>(modifier)` would let an invalid modifier
    // (e.g. 99) sneak past: the store would snap it to Disabled=0 and skip
    // the disk write, but we'd then stamp a trigger list with modifier=99
    // baked in because the synthesis below ran with the raw input.
    const int before =
        m_store->read<int>(ConfigDefaults::snappingBehaviorZoneSpanGroup(), ConfigDefaults::modifierKey());
    // Snapshot the trigger list BEFORE writing the modifier: on configs with no
    // stored trigger list, zoneSpanTriggers() synthesizes its result from the
    // modifier key, so a post-write snapshot would already reflect the new
    // modifier and the trigger-change comparison below could never fire.
    const QVariantList beforeTriggers = zoneSpanTriggers();
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
    // Same composite stale-guard as writeTriggerList (zoneSpan keeps its own
    // setter for the legacy-modifier sync, not to skip the refresh).
    refreshCleanBackendFromDisk();
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
    // Dual-writer composite: the daemon's autotile engine persists this map
    // and the settings app edits it, so the stale-guard refresh matters here
    // as much as for the animation profile blob.
    refreshCleanBackendFromDisk();
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

// ── Autotile drag-insert triggers (PhosphorConfig::Store-backed) ────────────

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

// ── Numbered quick-layout + snap-to-zone shortcut dispatchers ───────────────

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
} // namespace PlasmaZones

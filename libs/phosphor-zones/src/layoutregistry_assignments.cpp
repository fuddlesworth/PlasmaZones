// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Layout assignment management (per-screen, per-desktop, per-activity).
// Part of LayoutRegistry — split from layoutregistry.cpp for SRP.

#include <PhosphorZones/LayoutRegistry.h>

#include "zoneslogging.h"

#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorScreens/VirtualScreen.h>

#include <QFile>
#include <QJsonDocument>
#include <optional>

namespace PhosphorZones {

namespace {

// Walk the assignment cascade for a screen/desktop/activity context.
// Visitor: (const AssignmentEntry&) -> std::optional<T>
// Returns the first non-nullopt result from the visitor, or std::nullopt if
// no entry satisfied the visitor at any cascade level.
//
// Cascade order — most specific first:
//   1. Exact match (screenId, virtualDesktop, activity)
//   2. Screen + activity, any desktop — `setAllActivityAssignments` stores
//      per-activity entries at (screen, 0, activity) so this is where
//      "Settings → Snapping → Assignments → Activity" toggles end up. Sits
//      above the desktop-keyed level so activity context wins when both
//      assignment dimensions are configured (matches the KDE Activities
//      semantics: an activity is a higher-level workspace context than a
//      virtual desktop).
//   3. Screen + desktop, any activity
//   4. Screen only (desktop=0, activity="") — the "display default" /
//      monitor assignment
//   5. Connector name fallback (recursive)
//   6. Virtual screen fallback — inherit from physical screen ID (recursive)
template<typename Visitor>
auto walkCascade(const QHash<LayoutAssignmentKey, AssignmentEntry>& assignments, const QString& screenId,
                 int virtualDesktop, const QString& activity, Visitor&& visitor)
    -> decltype(visitor(std::declval<const AssignmentEntry&>()))
{
    using Result = decltype(visitor(std::declval<const AssignmentEntry&>()));

    // 1. Exact match
    LayoutAssignmentKey exactKey{screenId, virtualDesktop, activity};
    if (auto it = assignments.constFind(exactKey); it != assignments.constEnd()) {
        if (auto r = visitor(it.value()))
            return r;
    }

    // 2. Screen + activity (any desktop). Activity entries are persisted
    // by `setAllActivityAssignments` at virtualDesktop=0 regardless of
    // which desktop happened to be current at save time, so the lookup
    // here pins desktop to 0 even when the caller passes a non-zero
    // desktop. Without this level, every per-activity assignment was
    // dead-letter — written but never read — and the cascade fell
    // straight to the monitor default below, masking the user's
    // activity choice (discussion #413).
    if (!activity.isEmpty()) {
        LayoutAssignmentKey activityKey{screenId, 0, activity};
        if (auto it = assignments.constFind(activityKey); it != assignments.constEnd()) {
            if (auto r = visitor(it.value()))
                return r;
        }
    }

    // 3. Screen + desktop (any activity)
    LayoutAssignmentKey desktopKey{screenId, virtualDesktop, QString()};
    if (auto it = assignments.constFind(desktopKey); it != assignments.constEnd()) {
        if (auto r = visitor(it.value()))
            return r;
    }

    // 4. Screen only (any desktop, any activity) — the "display default"
    LayoutAssignmentKey screenKey{screenId, 0, QString()};
    if (auto it = assignments.constFind(screenKey); it != assignments.constEnd()) {
        if (auto r = visitor(it.value()))
            return r;
    }

    // 5. Connector name fallback: if screenId looks like a connector name (no colons),
    // try resolving to a screen ID and looking up again.
    if (Phosphor::Screens::ScreenIdentity::isConnectorName(screenId)) {
        QString resolved = Phosphor::Screens::ScreenIdentity::idForName(screenId);
        if (resolved != screenId) {
            return walkCascade(assignments, resolved, virtualDesktop, activity, std::forward<Visitor>(visitor));
        }
    }

    // 6. Virtual screen fallback: try physical screen ID if this is a virtual screen.
    // This lets virtual screens inherit the physical screen's snapping layout assignment
    // when no per-virtual-screen assignment exists.
    //
    // Design note: the fallback is intentionally layout-resolution-only from the caller's
    // perspective. layoutForScreen()'s visitor already guards against Autotile mode by
    // returning std::nullopt for Autotile entries — so a VS inheriting a physical screen's
    // Autotile assignment from this cascade correctly falls through to the global default
    // layout rather than activating autotile on the VS. assignmentEntryForScreen() does
    // not apply this guard, so callers that need mode isolation (e.g. AutotileEngine
    // checking whether a VS is in autotile mode) must treat an inherited Autotile entry
    // as "no explicit VS assignment" and use hasExplicitAssignment() to distinguish.
    if (PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
        const QString physId = PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId);
        auto result = walkCascade(assignments, physId, virtualDesktop, activity, std::forward<Visitor>(visitor));
        if (result)
            return result;
    }

    return Result{};
}

} // anonymous namespace

void LayoutRegistry::assignLayout(const QString& screenId, int virtualDesktop, const QString& activity,
                                  PhosphorZones::Layout* layout)
{
    LayoutAssignmentKey key{screenId, virtualDesktop, activity};

    if (layout) {
        AssignmentEntry& entry = m_assignments[key];
        entry.mode = AssignmentEntry::Snapping;
        entry.snappingLayout = layout->id().toString();
        // Preserve existing tilingAlgorithm — only mode + snappingLayout change
        qCDebug(lcZonesLib) << "assignLayout: screen=" << screenId << "desktop=" << virtualDesktop
                            << "activity=" << (activity.isEmpty() ? QStringLiteral("(all)") : activity)
                            << "layout=" << layout->name();
    } else {
        // Clearing: remove the entry entirely.
        // Skip save/signal when there's nothing to remove — avoids a redundant
        // disk write and layoutAssigned emission (e.g. clearAssignment for an
        // activity-keyed entry that doesn't exist).
        bool hadAssignment = m_assignments.remove(key);
        if (!hadAssignment) {
            return;
        }
        qCDebug(lcZonesLib) << "assignLayout: removed screen=" << screenId << "desktop=" << virtualDesktop
                            << "activity=" << (activity.isEmpty() ? QStringLiteral("(all)") : activity);
    }

    Q_EMIT layoutAssigned(screenId, key.virtualDesktop, layout);
    saveAssignments();
}

void LayoutRegistry::assignLayoutById(const QString& screenId, int virtualDesktop, const QString& activity,
                                      const QString& layoutId)
{
    if (PhosphorLayout::LayoutId::isAutotile(layoutId)) {
        // Store autotile assignment — set mode to Autotile, preserve snappingLayout
        LayoutAssignmentKey key{screenId, virtualDesktop, activity};
        AssignmentEntry& entry = m_assignments[key];
        entry.mode = AssignmentEntry::Autotile;
        entry.tilingAlgorithm = PhosphorLayout::LayoutId::extractAlgorithmId(layoutId);
        // Preserve existing snappingLayout — only mode + tilingAlgorithm change
        Q_EMIT layoutAssigned(screenId, virtualDesktop, nullptr);
        saveAssignments();
    } else {
        assignLayout(screenId, virtualDesktop, activity, layoutById(QUuid::fromString(layoutId)));
    }
}

void LayoutRegistry::setAssignmentEntryDirect(const QString& screenId, int virtualDesktop, const QString& activity,
                                              const AssignmentEntry& entry)
{
    LayoutAssignmentKey key{screenId, virtualDesktop, activity};

    // Store the entry unconditionally — mode-only entries (empty snapping + empty tiling)
    // are valid when explicitly set by the KCM to preserve mode at a context level.
    m_assignments[key] = entry;

    qCDebug(lcZonesLib) << "setAssignmentEntryDirect: screen=" << screenId << "desktop=" << virtualDesktop
                        << "activity=" << activity << "mode=" << entry.mode << "snapping=" << entry.snappingLayout
                        << "tiling=" << entry.tilingAlgorithm;

    // Resolve layout for signal emission
    PhosphorZones::Layout* layout = nullptr;
    if (entry.mode == AssignmentEntry::Snapping && !entry.snappingLayout.isEmpty()) {
        layout = layoutById(QUuid::fromString(entry.snappingLayout));
    }
    Q_EMIT layoutAssigned(screenId, virtualDesktop, layout);
    saveAssignments();
}

namespace {
// Decide the AssignmentEntry to start from when a single-field write
// arrives at (screenId, virtualDesktop, activity). The KCM "Assignments"
// pages (per ADR-discussion-497) want the edit to record a preference at
// that exact slot WITHOUT flipping the rendered mode.
//
// If a local entry exists at the exact slot we always use it as the seed
// — even when its `activeLayoutId()` happens to be empty (e.g. an entry
// with {mode=Snapping, snap="", tile="cluster"} that holds a stored
// tile preference). Falling back to the cascade in that case would
// clobber the opposite-field value the user had explicitly recorded at
// this slot.
//
// Only when the slot has no local entry at all do we seed from the
// cascade-resolved ambient state, so the new value lands in the right
// mode and survives the cascade visitor's `activeLayoutId().isEmpty()`
// reject filter.
AssignmentEntry seedForPartialUpdate(const LayoutRegistry& self,
                                     const QHash<LayoutAssignmentKey, AssignmentEntry>& assignments,
                                     const QString& screenId, int virtualDesktop, const QString& activity)
{
    LayoutAssignmentKey key{screenId, virtualDesktop, activity};
    auto it = assignments.constFind(key);
    if (it != assignments.constEnd()) {
        return *it;
    }
    return self.assignmentEntryForScreen(screenId, virtualDesktop, activity);
}
} // namespace

namespace {
// Shared write path for partial-update / promote methods. Applies @p mutator
// to the seed entry (taken from the slot itself or from the cascade-resolved
// ambient state — see seedForPartialUpdate), then persists. When both fields
// end up empty the slot is removed rather than left as a cascade-skipped
// no-op record.
//
// Returns true when the persisted assignments actually changed; callers
// should skip the `layoutAssigned` emit + `saveAssignments` call when
// false (a clear that targeted an already-empty slot).
//
// @p caller is the calling method name; used only for log trace context.
template<typename Mutator>
bool applyPartialOrPromote(LayoutRegistry& self, QHash<LayoutAssignmentKey, AssignmentEntry>& assignments,
                           const QString& screenId, int virtualDesktop, const QString& activity, bool seedFromCascade,
                           const char* caller, Mutator&& mutator)
{
    LayoutAssignmentKey key{screenId, virtualDesktop, activity};

    // Seed entry: cascade-resolved fallback (preserve-mode path) or
    // local-only (promote path — shadows are cleared separately, so
    // pulling cascade data would defeat the point).
    AssignmentEntry entry;
    if (seedFromCascade) {
        entry = seedForPartialUpdate(self, assignments, screenId, virtualDesktop, activity);
    } else if (auto it = assignments.constFind(key); it != assignments.constEnd()) {
        entry = *it;
    }

    mutator(entry);

    if (entry.snappingLayout.isEmpty() && entry.tilingAlgorithm.isEmpty()) {
        bool hadAssignment = assignments.remove(key);
        if (hadAssignment) {
            qCDebug(lcZonesLib) << caller << ": removed empty entry screen=" << screenId << "desktop=" << virtualDesktop
                                << "activity=" << activity;
        }
        return hadAssignment;
    }
    assignments[key] = entry;
    qCDebug(lcZonesLib) << caller << ": screen=" << screenId << "desktop=" << virtualDesktop << "activity=" << activity
                        << "mode=" << entry.mode << "snapping=" << entry.snappingLayout
                        << "tiling=" << entry.tilingAlgorithm;
    return true;
}
} // namespace

void LayoutRegistry::setSnappingLayoutPreservingMode(const QString& screenId, int virtualDesktop,
                                                     const QString& activity, const QString& layoutId)
{
    const bool changed =
        applyPartialOrPromote(*this, m_assignments, screenId, virtualDesktop, activity, /*seedFromCascade=*/true,
                              "setSnappingLayoutPreservingMode", [&](AssignmentEntry& entry) {
                                  entry.snappingLayout = layoutId;
                              });
    if (!changed) {
        return;
    }
    PhosphorZones::Layout* layout = layoutId.isEmpty() ? nullptr : layoutById(QUuid::fromString(layoutId));
    Q_EMIT layoutAssigned(screenId, virtualDesktop, layout);
    saveAssignments();
}

void LayoutRegistry::clearShadowsForSlot(const QString& screenId, int virtualDesktop, const QString& activity)
{
    // Resolve the physical screen so VS variants of the same physical
    // also get treated as "same screen" — the cascade walks VS → phys
    // at level 6, so leaving VS entries intact would re-shadow the slot
    // we're about to promote.
    const QString targetPhysId = PhosphorIdentity::VirtualScreenId::isVirtual(screenId)
        ? PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId)
        : screenId;
    const LayoutAssignmentKey targetKey{screenId, virtualDesktop, activity};

    auto sameScreen = [&](const QString& s) -> bool {
        if (s == screenId)
            return true;
        if (s == targetPhysId)
            return true;
        if (PhosphorIdentity::VirtualScreenId::isVirtual(s)
            && PhosphorIdentity::VirtualScreenId::extractPhysicalId(s) == targetPhysId)
            return true;
        return false;
    };

    // Determine which keys on the same screen would win at a higher (or
    // equal) cascade level than the target slot for some context the
    // target slot is meant to cover. Reading bottom-up by slot shape:
    //   - Monitor row (vd=0, activity=""): the slot is the universal
    //     fallback for the screen; every other entry on the screen
    //     shadows it for some context → clear them all.
    //   - Per-desktop (vd>0, activity=""): the slot is the fallback
    //     for (this screen, this desktop, *). Two key shapes shadow it
    //     for context (vd, activity=X):
    //       L1 (vd, X)           — same-desktop+activity entries
    //       L2 (0, X)            — per-activity entries (cascade walks
    //                              activity before per-desktop, see
    //                              walkCascade level 2)
    //     Clear BOTH; leaving per-activity entries intact would let any
    //     activity-keyed entry continue to win over the promoted slot
    //     whenever that activity is current.
    //   - Per-activity (vd=0, activity!=""): the slot is the fallback
    //     for (this screen, *, this activity). Per-desktop+activity
    //     entries with the same activity (anyVd>0, activity) shadow
    //     at L1 → clear them.
    //   - Specific (vd>0, activity!=""): no other entry shadows it
    //     in the cascade, so nothing to clear.
    auto shouldClear = [&](const LayoutAssignmentKey& key) -> bool {
        if (key == targetKey)
            return false;
        if (!sameScreen(key.screenId))
            return false;

        if (virtualDesktop == 0 && activity.isEmpty())
            return true; // Monitor row: clear everything else on screen
        if (virtualDesktop > 0 && activity.isEmpty()) {
            // Per-desktop scope: same-desktop entries (L1 shadows) AND
            // per-activity entries (L2 shadows for contexts with matching
            // activity). Leaving the latter would let an activity-keyed
            // entry mask the promoted per-desktop slot when that activity
            // is current.
            return key.virtualDesktop == virtualDesktop || (key.virtualDesktop == 0 && !key.activity.isEmpty());
        }
        if (virtualDesktop == 0 && !activity.isEmpty())
            return key.activity == activity; // Per-activity scope
        return false; // Specific (vd, activity): no shadow to clear
    };

    QList<LayoutAssignmentKey> toRemove;
    for (auto it = m_assignments.constBegin(); it != m_assignments.constEnd(); ++it) {
        if (shouldClear(it.key()))
            toRemove.append(it.key());
    }
    for (const auto& key : std::as_const(toRemove)) {
        m_assignments.remove(key);
        qCDebug(lcZonesLib) << "clearShadowsForSlot: removed shadow screen=" << key.screenId
                            << "desktop=" << key.virtualDesktop << "activity=" << key.activity;
    }
}

void LayoutRegistry::setSnappingLayoutPromoting(const QString& screenId, int virtualDesktop, const QString& activity,
                                                const QString& layoutId)
{
    // clearShadowsForSlot itself emits no signal — its mutation is rolled
    // into the layoutAssigned emit below. The applyPartialOrPromote
    // return is intentionally discarded (cast to void to make intent
    // explicit): even when the helper reports "unchanged" for the
    // target slot, clearShadows may have removed shadow entries that
    // must still be persisted and observed.
    clearShadowsForSlot(screenId, virtualDesktop, activity);

    (void)applyPartialOrPromote(*this, m_assignments, screenId, virtualDesktop, activity, /*seedFromCascade=*/false,
                                "setSnappingLayoutPromoting", [&](AssignmentEntry& entry) {
                                    entry.mode = AssignmentEntry::Snapping;
                                    entry.snappingLayout = layoutId;
                                });

    PhosphorZones::Layout* layout = layoutId.isEmpty() ? nullptr : layoutById(QUuid::fromString(layoutId));
    Q_EMIT layoutAssigned(screenId, virtualDesktop, layout);
    saveAssignments();
}

void LayoutRegistry::setTilingAlgorithmPromoting(const QString& screenId, int virtualDesktop, const QString& activity,
                                                 const QString& algorithmId)
{
    // See note in setSnappingLayoutPromoting about always emitting.
    clearShadowsForSlot(screenId, virtualDesktop, activity);

    (void)applyPartialOrPromote(*this, m_assignments, screenId, virtualDesktop, activity, /*seedFromCascade=*/false,
                                "setTilingAlgorithmPromoting", [&](AssignmentEntry& entry) {
                                    entry.mode = AssignmentEntry::Autotile;
                                    entry.tilingAlgorithm = algorithmId;
                                });

    Q_EMIT layoutAssigned(screenId, virtualDesktop, nullptr);
    saveAssignments();
}

void LayoutRegistry::setTilingAlgorithmPreservingMode(const QString& screenId, int virtualDesktop,
                                                      const QString& activity, const QString& algorithmId)
{
    const bool changed =
        applyPartialOrPromote(*this, m_assignments, screenId, virtualDesktop, activity, /*seedFromCascade=*/true,
                              "setTilingAlgorithmPreservingMode", [&](AssignmentEntry& entry) {
                                  entry.tilingAlgorithm = algorithmId;
                              });
    if (!changed) {
        return;
    }
    // Emit with nullptr — tiling changes don't have a snap Layout* to
    // pass through; daemon-side recalc paths key off the screenId.
    Q_EMIT layoutAssigned(screenId, virtualDesktop, nullptr);
    saveAssignments();
}

// layoutForScreen, assignmentIdForScreen, and assignmentEntryForScreen share the
// same fallback cascade, implemented once in walkCascade() above. Each method
// supplies a visitor that decides whether to accept or cascade past each entry.

PhosphorZones::Layout* LayoutRegistry::layoutForScreen(const QString& screenId, int virtualDesktop,
                                                       const QString& activity) const
{
    auto result = walkCascade(m_assignments, screenId, virtualDesktop, activity,
                              [this](const AssignmentEntry& entry) -> std::optional<PhosphorZones::Layout*> {
                                  if (entry.mode == AssignmentEntry::Autotile)
                                      return std::nullopt;
                                  if (entry.snappingLayout.isEmpty())
                                      return std::nullopt;
                                  PhosphorZones::Layout* layout = layoutById(QUuid::fromString(entry.snappingLayout));
                                  return layout ? std::optional<PhosphorZones::Layout*>(layout) : std::nullopt;
                              });
    if (result)
        return *result;

    // No explicit assignment in the cascade — defer to the registry-wide
    // default (snap provider via defaultLayout(), then first layout by
    // defaultOrder). layoutForScreen returns a snap Layout* and has no
    // autotile counterpart; autotile-mode resolution is the autotile
    // engine's job, driven by assignmentIdForScreen returning an
    // "autotile:<algo>" id from the level-1 cascade.
    return defaultLayout();
}

void LayoutRegistry::clearAssignment(const QString& screenId, int virtualDesktop, const QString& activity)
{
    assignLayout(screenId, virtualDesktop, activity, nullptr);
}

bool LayoutRegistry::hasExplicitAssignment(const QString& screenId, int virtualDesktop, const QString& activity) const
{
    LayoutAssignmentKey key{screenId, virtualDesktop, activity};
    return m_assignments.contains(key);
}

QString LayoutRegistry::assignmentIdForScreen(const QString& screenId, int virtualDesktop,
                                              const QString& activity) const
{
    auto result = walkCascade(m_assignments, screenId, virtualDesktop, activity,
                              [](const AssignmentEntry& entry) -> std::optional<QString> {
                                  QString id = entry.activeLayoutId();
                                  return id.isEmpty() ? std::nullopt : std::optional<QString>(id);
                              });
    if (result) {
        return *result;
    }
    // No stored entry in the cascade — fall through to the level-1 global
    // default so callers (autotile engine activation, OSD, KCM) see the
    // user's intended mode for contexts that were never explicitly
    // configured. resolveDefaultAssignmentEntry handles the snap-then-
    // autotile precedence; if neither provider has a value we return the
    // historical empty string ("no assignment").
    const AssignmentEntry def = resolveDefaultAssignmentEntry();
    return def.activeLayoutId();
}

AssignmentEntry LayoutRegistry::assignmentEntryForScreen(const QString& screenId, int virtualDesktop,
                                                         const QString& activity) const
{
    // Acceptance rule must match assignmentIdForScreen so the two cascade
    // views of the same stored entry agree. `activeLayoutId()` returns
    // `"autotile:<algo>"` for Autotile mode (non-empty even when the
    // algorithm is blank — "use the default algorithm"), and the raw
    // snappingLayout for Snapping mode. Rejecting only when both a mode
    // and a content yield an empty identifier keeps mode-only Autotile
    // entries — which the KCM stores via setAssignmentEntryDirect — from
    // being silently skipped and replaced by a wider cascade entry.
    auto result = walkCascade(m_assignments, screenId, virtualDesktop, activity,
                              [](const AssignmentEntry& entry) -> std::optional<AssignmentEntry> {
                                  if (entry.activeLayoutId().isEmpty())
                                      return std::nullopt;
                                  return entry;
                              });
    if (result) {
        return *result;
    }
    // Cascade miss — synthesize from the level-1 global default so
    // callers that branch on mode (autotile engine activation, OSD,
    // KCM "current mode" displays) see the user's intended mode for
    // contexts that were never explicitly configured. The helper
    // returns a default-constructed entry when neither provider has a
    // value, matching pre-368 behaviour.
    return resolveDefaultAssignmentEntry();
}

AssignmentEntry::Mode LayoutRegistry::modeForScreen(const QString& screenId, int virtualDesktop,
                                                    const QString& activity) const
{
    return assignmentEntryForScreen(screenId, virtualDesktop, activity).mode;
}

QString LayoutRegistry::snappingLayoutForScreen(const QString& screenId, int virtualDesktop,
                                                const QString& activity) const
{
    // Per-field cascade: accept the first entry whose snap field is set.
    // Cannot route through `assignmentEntryForScreen` because its visitor
    // rejects entries whose `activeLayoutId()` is empty — which happens
    // for preserve-mode entries shaped like {mode=Snapping, snap="",
    // tile="cluster"} after a snap-clear that preserved the tile
    // preference (see setSnappingLayoutPreservingMode). Reading via the
    // mode-resolved cascade would hide the stored tile field from the
    // tile-row in the KCM Assignments page and vice versa.
    auto result = walkCascade(m_assignments, screenId, virtualDesktop, activity,
                              [](const AssignmentEntry& entry) -> std::optional<QString> {
                                  if (entry.snappingLayout.isEmpty())
                                      return std::nullopt;
                                  return entry.snappingLayout;
                              });
    if (result) {
        return *result;
    }
    // Cascade miss — consult the snap-side global default provider
    // directly. Cannot route through `resolveDefaultAssignmentEntry`
    // because that helper synthesizes a single mode-resolved entry and
    // returns Autotile-only state with snap="" when the user has
    // autotile preferred (and vice versa for the tile reader). This is
    // a per-field reader: the snap default exists independently of
    // which mode the cascade winner would render.
    return m_defaultLayoutIdProvider ? m_defaultLayoutIdProvider() : QString();
}

QString LayoutRegistry::tilingAlgorithmForScreen(const QString& screenId, int virtualDesktop,
                                                 const QString& activity) const
{
    // Per-field cascade — see snappingLayoutForScreen above.
    auto result = walkCascade(m_assignments, screenId, virtualDesktop, activity,
                              [](const AssignmentEntry& entry) -> std::optional<QString> {
                                  if (entry.tilingAlgorithm.isEmpty())
                                      return std::nullopt;
                                  return entry.tilingAlgorithm;
                              });
    if (result) {
        return *result;
    }
    // Symmetric per-field default: consult the autotile-side global
    // default provider directly so the KCM Tiling Assignments default
    // row shows the user's configured default autotile algorithm even
    // when the snapping-preferred provider would steer
    // `resolveDefaultAssignmentEntry` into a snap-mode entry with an
    // empty tile field.
    return m_defaultAutotileAlgorithmProvider ? m_defaultAutotileAlgorithmProvider() : QString();
}

void LayoutRegistry::clearAutotileAssignments()
{
    // Collect autotile keys first, then modify in a second pass.
    QList<LayoutAssignmentKey> autotileKeys;
    for (auto it = m_assignments.constBegin(); it != m_assignments.constEnd(); ++it) {
        if (it.value().mode == AssignmentEntry::Autotile) {
            autotileKeys.append(it.key());
        }
    }

    bool changed = !autotileKeys.isEmpty();
    for (const LayoutAssignmentKey& key : autotileKeys) {
        AssignmentEntry& entry = m_assignments[key];
        // Flip mode to Snapping — preserve both snappingLayout and tilingAlgorithm
        // so re-enabling autotile can restore the previous algorithm.
        entry.mode = AssignmentEntry::Snapping;
        qCDebug(lcZonesLib) << "clearAutotileAssignments: flipped to Snapping for screen=" << key.screenId
                            << "desktop=" << key.virtualDesktop;
        Q_EMIT layoutAssigned(key.screenId, key.virtualDesktop, nullptr);
    }

    // Also clear autotile quick layout slots
    for (auto it = m_quickLayoutShortcuts.begin(); it != m_quickLayoutShortcuts.end();) {
        if (PhosphorLayout::LayoutId::isAutotile(it.value())) {
            it = m_quickLayoutShortcuts.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }

    if (changed) {
        saveAssignments();
        qCInfo(lcZonesLib) << "Cleared all autotile assignments";
    }
}

void LayoutRegistry::setAllScreenAssignments(const QHash<QString, QString>& assignments)
{
    // Snapshot existing base entries so fromLayoutId can preserve the "other" field
    // (e.g. batch-setting snapping layout preserves tilingAlgorithm)
    QHash<LayoutAssignmentKey, AssignmentEntry> oldBase;
    for (auto it = m_assignments.begin(); it != m_assignments.end();) {
        if (it.key().virtualDesktop == 0 && it.key().activity.isEmpty()) {
            oldBase[it.key()] = it.value();
            it = m_assignments.erase(it);
        } else {
            ++it;
        }
    }

    // Set new assignments, tracking which screens were successfully stored
    int count = 0;
    QSet<QString> storedScreens;
    for (auto it = assignments.begin(); it != assignments.end(); ++it) {
        const QString& screenId = it.key();
        const QString& layoutId = it.value();

        if (screenId.isEmpty()) {
            qCWarning(lcZonesLib) << "Skipping assignment with empty screen ID";
            continue;
        }
        if (shouldSkipLayoutAssignment(layoutId, QStringLiteral("screen ") + screenId)) {
            continue;
        }

        LayoutAssignmentKey key{screenId, 0, QString()};
        AssignmentEntry entry = AssignmentEntry::fromLayoutId(layoutId, oldBase.value(key));
        m_assignments[key] = entry;
        storedScreens.insert(screenId);
        ++count;
        qCDebug(lcZonesLib) << "Batch: assigned layout" << layoutId << "to screen" << screenId;
    }

    saveAssignments();

    // Emit layoutAssigned for stored entries, deduplicated by screenId.
    // Resolve at desktop=0 (the base level we just wrote) so the KCM
    // receives the actual display default — NOT the current desktop's
    // effective layout, which would corrupt its m_screenAssignments cache.
    for (const QString& screenId : storedScreens) {
        emitLayoutAssigned(screenId, 0, assignmentIdForScreen(screenId, 0, QString()));
    }

    qCInfo(lcZonesLib) << "Batch set" << count << "screen assignments";
}

void LayoutRegistry::setAllDesktopAssignments(const QHash<QPair<QString, int>, QString>& assignments)
{
    // Snapshot existing per-desktop entries so fromLayoutId can preserve the "other" field
    QHash<LayoutAssignmentKey, AssignmentEntry> oldDesktop;
    for (auto it = m_assignments.begin(); it != m_assignments.end();) {
        if (it.key().virtualDesktop > 0 && it.key().activity.isEmpty()) {
            oldDesktop[it.key()] = it.value();
            it = m_assignments.erase(it);
        } else {
            ++it;
        }
    }

    // Set new assignments, tracking which screens were successfully stored
    int count = 0;
    QSet<QString> storedScreens;
    for (auto it = assignments.begin(); it != assignments.end(); ++it) {
        const QString& screenId = it.key().first;
        int virtualDesktop = it.key().second;
        const QString& layoutId = it.value();

        if (screenId.isEmpty() || virtualDesktop < 1) {
            qCWarning(lcZonesLib) << "Skipping invalid desktop assignment:" << screenId << virtualDesktop;
            continue;
        }
        QString context = QStringLiteral("%1 desktop %2").arg(screenId).arg(virtualDesktop);
        if (shouldSkipLayoutAssignment(layoutId, context)) {
            continue;
        }

        LayoutAssignmentKey key{screenId, virtualDesktop, QString()};
        AssignmentEntry entry = AssignmentEntry::fromLayoutId(layoutId, oldDesktop.value(key));
        m_assignments[key] = entry;
        storedScreens.insert(screenId);
        ++count;
        qCDebug(lcZonesLib) << "Batch: assigned layout" << layoutId << "to" << screenId << "desktop" << virtualDesktop;
    }

    saveAssignments();

    // Emit once per screen (deduplicated) — use cascading resolution so the
    // signal carries the effective layout for the current context.
    for (const QString& screenId : storedScreens) {
        emitLayoutAssigned(screenId, m_currentVirtualDesktop,
                           assignmentIdForScreen(screenId, m_currentVirtualDesktop, m_currentActivity));
    }

    qCInfo(lcZonesLib) << "Batch set" << count << "desktop assignments";
}

void LayoutRegistry::setAllActivityAssignments(const QHash<QPair<QString, QString>, QString>& assignments)
{
    // Snapshot existing per-activity entries so fromLayoutId can preserve the "other" field
    QHash<LayoutAssignmentKey, AssignmentEntry> oldActivity;
    for (auto it = m_assignments.begin(); it != m_assignments.end();) {
        if (!it.key().activity.isEmpty() && it.key().virtualDesktop == 0) {
            oldActivity[it.key()] = it.value();
            it = m_assignments.erase(it);
        } else {
            ++it;
        }
    }

    // Set new assignments, tracking which screens were successfully stored
    int count = 0;
    QSet<QString> storedScreens;
    for (auto it = assignments.begin(); it != assignments.end(); ++it) {
        const QString& screenId = it.key().first;
        const QString& activityId = it.key().second;
        const QString& layoutId = it.value();

        if (screenId.isEmpty() || activityId.isEmpty()) {
            qCWarning(lcZonesLib) << "Skipping invalid activity assignment:" << screenId << activityId;
            continue;
        }
        QString context = QStringLiteral("%1 activity %2").arg(screenId, activityId);
        if (shouldSkipLayoutAssignment(layoutId, context)) {
            continue;
        }

        LayoutAssignmentKey key{screenId, 0, activityId};
        AssignmentEntry entry = AssignmentEntry::fromLayoutId(layoutId, oldActivity.value(key));
        m_assignments[key] = entry;
        storedScreens.insert(screenId);
        ++count;
        qCDebug(lcZonesLib) << "Batch: assigned layout" << layoutId << "to" << screenId << "activity" << activityId;
    }

    saveAssignments();

    // Emit once per screen (deduplicated) — resolve at the base level
    // (desktop=0) so the KCM receives the display default, not the
    // current desktop's effective layout which would corrupt its cache.
    for (const QString& screenId : storedScreens) {
        emitLayoutAssigned(screenId, 0, assignmentIdForScreen(screenId, 0, QString()));
    }

    qCInfo(lcZonesLib) << "Batch set" << count << "activity assignments";
}

QHash<QPair<QString, int>, QString> LayoutRegistry::desktopAssignments() const
{
    QHash<QPair<QString, int>, QString> result;

    for (auto it = m_assignments.begin(); it != m_assignments.end(); ++it) {
        const LayoutAssignmentKey& key = it.key();
        // Per-desktop: virtualDesktop > 0 and activity is empty
        if (key.virtualDesktop > 0 && key.activity.isEmpty()) {
            result[qMakePair(key.screenId, key.virtualDesktop)] = it.value().activeLayoutId();
        }
    }

    return result;
}

QHash<QPair<QString, QString>, QString> LayoutRegistry::activityAssignments() const
{
    QHash<QPair<QString, QString>, QString> result;

    for (auto it = m_assignments.begin(); it != m_assignments.end(); ++it) {
        const LayoutAssignmentKey& key = it.key();
        // Per-activity: activity is non-empty (for any desktop value)
        if (!key.activity.isEmpty()) {
            result[qMakePair(key.screenId, key.activity)] = it.value().activeLayoutId();
        }
    }

    return result;
}

// Autotile layout overrides

QJsonObject LayoutRegistry::loadAllAutotileOverrides() const
{
    QFile file(m_layoutDirectory + QStringLiteral("/autotile-overrides.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return doc.isObject() ? doc.object() : QJsonObject();
}

void LayoutRegistry::saveAllAutotileOverrides(const QJsonObject& all)
{
    ensureLayoutDirectory();
    QFile file(m_layoutDirectory + QStringLiteral("/autotile-overrides.json"));
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(lcZonesLib) << "Failed to save autotile overrides:" << file.errorString();
        return;
    }
    file.write(QJsonDocument(all).toJson());
}

QJsonObject LayoutRegistry::loadAutotileOverrides(const QString& algorithmId) const
{
    return loadAllAutotileOverrides().value(algorithmId).toObject();
}

void LayoutRegistry::saveAutotileOverrides(const QString& algorithmId, const QJsonObject& overrides)
{
    QJsonObject all = loadAllAutotileOverrides();
    if (overrides.isEmpty()) {
        all.remove(algorithmId);
    } else {
        all[algorithmId] = overrides;
    }
    saveAllAutotileOverrides(all);
}

} // namespace PhosphorZones

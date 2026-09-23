// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Per-desktop membership for snapping: which of a screen's desktops a window
// may hold its own zone assignment on.
//
// The third arm of the same change the scroll and tile engines carry, and the
// one that differs most. Those engines PLACE a window, so adopting one into a
// desktop means inserting it into that desktop's layout. Snapping places
// nothing by itself — the user drops a window into a zone — so adoption here
// only grants the window a store of its own on the desktop in view. What it
// buys is that the next snap writes THERE instead of overwriting the zone the
// window occupies on the desktop it came from, which is what made a sticky
// window share one zone across every desktop.
//
// An adopted membership holds NO data until the user snaps or floats the
// window there. On that desktop the window therefore reads as neither snapped
// nor floating for this engine; float is per store in snapping, the same way
// it is per engine across modes, so a window floated on desktop 1 is not
// floating on desktop 2 until the user floats it there.
//
// The eviction in stateForWindowOnScreen is the other half: it spares stores
// the window is a member of, so the two assignments coexist instead of the
// newer one wiping the older.

#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/LayoutUtils.h>

#include "snapenginelogging.h"

#include <QVector>

#include <algorithm>
#include <optional>
#include <utility>

namespace PhosphorSnapEngine {

using PhosphorEngine::DesktopSpan;
using PhosphorEngine::MembershipReconcileResult;
using PhosphorEngine::PlacementStateKey;

void SnapEngine::installContextResolver()
{
    // Without this the container's primary resolution falls back to a window's
    // FIRST membership, so a window snapped on desktop 1 and then on desktop 2
    // resolves to desktop 1's store both times — and the second snap overwrites
    // the first rather than living beside it. The global-scalar holder sits
    // under the empty key and is never a membership, so it is unaffected.
    m_states.setContextKeyResolver([this](const QString& screenId) {
        return currentKeyForScreen(screenId);
    });
}

void SnapEngine::seedPersistedDesktopZones(const QString& windowId, const PhosphorEngine::EngineSlot& slot,
                                           const QString& screenId, int restoreDesktop, const QString& activity)
{
    // A restored record carries the zone the window occupied on EVERY desktop
    // it was present on. The open path applies only the one for the desktop
    // being restored onto; the rest have to be put back into their own stores
    // here, or switching to those desktops after a restart would find nothing
    // and leave the window wherever it happens to be.
    //
    // A desktop the record names but the session no longer has (the count
    // shrank while the daemon was down) gets a store like any other; the
    // first membership pass on the screen finds the window's span does not
    // cover it and releases it, forgetting the persisted entry with it.
    if (slot.zonesByDesktop.isEmpty() || screenId.isEmpty()) {
        return;
    }
    // Memberships are keyed by the canonical id everywhere else in this
    // engine (stateForWindowOnScreen canonicalizes before it adds), so the
    // same form here, or the restore desktop's membership and the seeded ones
    // would sit under two different keys and never count as one window.
    const QString canonical = canonicalWindowId(windowId);
    // The desktops the window is on NOW, when the registry knows: a record
    // written while the window was on {1,2} and restored onto a window the
    // compositor put on {2} alone must not seed desktop 1, where the window
    // is not; the membership pass would only release it again, and until it
    // ran the zone counted a phantom occupant. A known, narrower span drops
    // the persisted entry with the seed, so a later capture does not revive
    // it. Sticky, or unknown, seeds everything the record names.
    std::optional<QSet<int>> presentOn;
    if (m_windowRegistry) {
        if (const auto ctx = m_windowRegistry->desktopContext(windowId);
            ctx && !ctx->sticky.value_or(false) && !ctx->virtualDesktops.isEmpty()) {
            presentOn = QSet<int>(ctx->virtualDesktops.cbegin(), ctx->virtualDesktops.cend());
        }
    }
    for (auto it = slot.zonesByDesktop.constBegin(); it != slot.zonesByDesktop.constEnd(); ++it) {
        const int desktop = it.key();
        if (desktop < 1 || desktop == restoreDesktop || it.value().isEmpty()) {
            continue; // the restore desktop is applied by the caller
        }
        if (presentOn && !presentOn->contains(desktop)) {
            if (m_windowTracker) {
                m_windowTracker->forgetDesktopZones(windowId, engineId(), desktop);
            }
            continue;
        }
        // A desktop whose layout was switched while the window was away no
        // longer holds the remembered zone; seeding it anyway would put the
        // window back into a layout the desktop does not run (discussion
        // #1104). The entry stays on disk on purpose: the store merges the
        // per-desktop map rather than replacing it, so a capture's silence
        // never drops it, and the user may switch that layout back. It goes
        // when a fresh snap on that desktop overwrites its key or a forget
        // (span shrank, unsnap, close) removes it.
        if (!PhosphorZones::LayoutUtils::contextLayoutHoldsZones(m_layoutManager, screenId, desktop, activity,
                                                                 it.value())) {
            qCInfo(lcSnapEngine) << "seedPersistedDesktopZones: not seeding" << windowId << "into zone(s)" << it.value()
                                 << "on desktop" << desktop << "of" << screenId
                                 << "— not in the layout that desktop runs";
            continue;
        }
        const PlacementStateKey key{screenId, desktop, activity};
        SnapState* state = ensureStateForKey(key);
        if (!state) {
            continue;
        }
        state->assignWindowToZones(windowId, it.value(), screenId, desktop);
        m_states.addMembership(canonical, key);
        qCInfo(lcSnapEngine) << "seedPersistedDesktopZones: restored" << windowId << "to" << it.value().size()
                             << "zone(s) on desktop" << desktop << "of" << screenId;
    }
}

struct SnapEngine::PendingMembership
{
    QString windowId;
    QList<PlacementStateKey> stale;
    bool adopt = false;
    /// Where the window is NOW, so the pass can re-home a snap it is taking
    /// off a desktop rather than only letting go of it.
    DesktopSpan span;
};

namespace {

/// The desktops a window leaving @p sourceDesktop could carry its snap to:
/// the ones its span still covers, lowest number first so the choice does not
/// ride on QSet iteration order. The first one that can take the window wins,
/// because the window has one geometry to be placed at; on the ordinary move
/// there is exactly one candidate anyway.
///
/// A sticky or unknown span yields none. Neither describes a window that has
/// moved off a desktop, and both reach this pass through paths that release
/// nothing.
QList<int> carryDestinationDesktops(const DesktopSpan& span, int sourceDesktop)
{
    QList<int> destinations;
    if (!span.known || span.sticky) {
        return destinations;
    }
    for (const int desktop : span.desktops) {
        if (desktop >= 1 && desktop != sourceDesktop) {
            destinations.append(desktop);
        }
    }
    std::sort(destinations.begin(), destinations.end());
    return destinations;
}

} // namespace

void SnapEngine::collectMembershipWork(const QString& windowId, const QString& screenId,
                                       const PlacementStateKey& currentKey, const DesktopSpan& span,
                                       QList<PendingMembership>& pending) const
{
    // An UNKNOWN span (the registry has not stamped a desktop for the window
    // yet) adopts nothing and releases nothing: reading it as "every desktop"
    // put windows into every desktop the user visited.
    if (!span.known) {
        return;
    }
    const QList<PlacementStateKey> held = m_states.membershipsForWindow(windowId);
    PendingMembership entry;
    entry.windowId = windowId;
    entry.span = span;
    for (const PlacementStateKey& key : held) {
        if (key.screenId == screenId && !span.coversKey(key)) {
            entry.stale.append(key);
        }
    }
    entry.adopt = span.coversKey(currentKey) && !m_states.hasMembership(windowId, currentKey);
    if (entry.adopt || !entry.stale.isEmpty()) {
        pending.append(entry);
    }
}

MembershipReconcileResult SnapEngine::applyMembershipWork(const QString& screenId, const PlacementStateKey& currentKey,
                                                          const QList<PendingMembership>& pending)
{
    MembershipReconcileResult result;
    // A window that is snapped on the desktop it is being moved OFF is MOVING,
    // not un-snapping: the compositor relocates it and changes nothing about
    // its geometry, so left alone it sits on the destination desktop at a rect
    // that belongs to the layout it came from — the "ghost layout" of
    // discussion #1104, reached by KWin's own move-to-desktop shortcut and by
    // a drop in the desktop overview. PlasmaZones' own cross-desktop move
    // (tryCrossDesktopMove) answers this by landing the window in the
    // positionally-equivalent zone of the destination desktop's layout; this
    // is the same answer for the moves it does not drive. Collected here and
    // emitted as ONE batch after the loop, since the commit it goes through
    // mutates the very stores this loop walks.
    QVector<PhosphorEngine::ZoneAssignmentEntry> carried;
    // Planned before the release below wipes the zones it reads. Fills
    // @p carriedFrom with the context the snap was taken from when the window
    // stays snapped, and leaves it default for the float-back, which is a
    // genuine un-snap.
    const auto planCarry =
        [this, &screenId](const PendingMembership& entry,
                          PlacementStateKey& carriedFrom) -> std::optional<PhosphorEngine::ZoneAssignmentEntry> {
        if (!m_layoutManager || !m_windowTracker) {
            return std::nullopt;
        }
        // A FLOATED window carries nothing. It keeps its own free geometry, so
        // there is no ghost rect to correct, and its zone assignment is only
        // the memory a float-toggle would resnap into.
        QStringList zones;
        PlacementStateKey source;
        for (const PlacementStateKey& stale : entry.stale) {
            const SnapState* state = m_states.stateForKey(stale);
            if (!state || state->isFloating(entry.windowId)) {
                continue;
            }
            // A LIVE snap, not frozen memory. A desktop that has since been
            // given a tiling mode keeps its snap assignments for a return to
            // snapping, and the window was tiled there, not snapped, when it
            // moved: carrying that zone would place a window on its own terms
            // against the engine that was managing it.
            if (m_layoutManager->modeForScreen(screenId, stale.desktop, currentActivity())
                != PhosphorZones::AssignmentEntry::Mode::Snapping) {
                continue;
            }
            zones = state->zonesForWindow(entry.windowId);
            if (!zones.isEmpty()) {
                source = stale;
                break;
            }
        }
        if (zones.isEmpty()) {
            return std::nullopt;
        }
        bool snappingDestination = false;
        for (const int desktop : carryDestinationDesktops(entry.span, source.desktop)) {
            if (const SnapState* there = m_states.stateForKey({screenId, desktop, source.activity});
                there && !there->zonesForWindow(entry.windowId).isEmpty()) {
                continue; // it holds a zone of its own over there already
            }
            if (m_layoutManager->modeForScreen(screenId, desktop, currentActivity())
                != PhosphorZones::AssignmentEntry::Mode::Snapping) {
                continue; // tiling or scrolling owns the arrival on that desktop
            }
            snappingDestination = true;
            // A shared layout yields the same zone id and the window does not
            // move at all; a different layout yields the slot in the same
            // position. Mirrors calculateResnapFromPreviousLayout.
            const auto [zoneId, geometry] = resolveCrossDesktopZone(zones.first(), screenId, desktop);
            if (zoneId.isEmpty()) {
                continue;
            }
            PhosphorEngine::ZoneAssignmentEntry assign;
            assign.windowId = entry.windowId;
            assign.targetZoneId = zoneId;
            assign.targetGeometry = geometry;
            assign.targetScreenId = screenId;
            // The destination desktop, not the one in view: the commit pins
            // the assignment to the context the window moved to.
            assign.virtualDesktop = desktop;
            carriedFrom = source;
            return assign;
        }
        if (!snappingDestination) {
            return std::nullopt;
        }
        // Snapping over there, but its layout has no slot in this window's
        // position (zone 3 of a grid, moved onto a two-zone layout). A layout
        // switch answers that with the window's pre-snap geometry, and so does
        // this: anything else leaves it sitting in a zone the desktop has not
        // got.
        const auto freeGeometry = m_windowTracker->validatedUnmanagedGeometry(entry.windowId, screenId);
        if (!freeGeometry || !freeGeometry->isValid()) {
            qCInfo(lcSnapEngine) << "reconcileDesktopMemberships: no equivalent zone and no pre-snap geometry for"
                                 << entry.windowId << "— it stays where the compositor left it";
            return std::nullopt;
        }
        PhosphorEngine::ZoneAssignmentEntry restore;
        restore.windowId = entry.windowId;
        restore.targetZoneId = PhosphorEngine::RestoreSentinel;
        restore.targetGeometry = *freeGeometry;
        restore.targetScreenId = screenId;
        return restore;
    };
    for (const PendingMembership& entry : pending) {
        PlacementStateKey carriedFrom;
        if (const auto assign = planCarry(entry, carriedFrom)) {
            carried.append(*assign);
        }
        for (const PlacementStateKey& stale : entry.stale) {
            if (SnapState* state = m_states.stateForKey(stale)) {
                // The zone assignment on a desktop the window has left is not
                // a float-back: it is still snapped on the desktops its span
                // does cover, so there is nothing to restore here.
                state->removeWindowData(entry.windowId);
            }
            m_states.removeMembership(entry.windowId, stale);
            // Forgetting a desktop has to be explicit: the store MERGES
            // zonesByDesktop, so a capture that simply stops naming this
            // desktop leaves the old zone on disk forever and a later restart
            // would resurrect the window onto a desktop it no longer occupies.
            // Through the tracker's wrapper, not the store: only the wrapper
            // marks the placements dirty, and a forget that stays in memory
            // never reaches disk — which is where this one has to land.
            if (m_windowTracker) {
                m_windowTracker->forgetDesktopZones(entry.windowId, engineId(), stale.desktop);
            }
            if (stale == carriedFrom) {
                // Not reported as a release: the window is snapped again, on
                // the destination desktop, by the batch below. The daemon
                // answers a release by telling the effect the window occupies
                // no zone, which would land AFTER the carry and leave the
                // effect's zone mirror empty for a window that is in a zone.
                // The placement record is refreshed by the carry instead.
                qCInfo(lcSnapEngine) << "reconcileDesktopMemberships: carrying" << entry.windowId << "off desktop"
                                     << stale.desktop << "of" << stale.screenId << "into the zone its new desktop has";
                continue;
            }
            result.released.append({entry.windowId, stale});
            qCInfo(lcSnapEngine) << "reconcileDesktopMemberships: released" << entry.windowId << "from desktop"
                                 << stale.desktop << "of" << stale.screenId << "— its span no longer covers it";
        }
        if (entry.adopt) {
            // Membership, plus the zone the durable record remembers for this
            // desktop, if any. An unsnapped window on this desktop stays
            // unsnapped, and the store exists so that snapping it later lands
            // in ITS OWN assignment; but a window whose per-desktop map names
            // a zone here was snapped here and lost the live store to a mode
            // handoff (a screen that went to tiling and came back releases
            // every context it took), so its zone is put back and re-applied
            // below rather than waiting for the user to snap it again.
            SnapState* state = ensureStateForKey(currentKey);
            m_states.addMembership(entry.windowId, currentKey);
            if (state && m_windowTracker && state->zonesForWindow(entry.windowId).isEmpty()) {
                if (const auto rec = m_windowTracker->placementStore().peekExact(entry.windowId)) {
                    const QStringList remembered = rec->slotFor(engineId()).zonesByDesktop.value(currentKey.desktop);
                    // Only into the layout this desktop runs NOW: the zone
                    // was assigned under whatever layout the desktop had
                    // when the window snapped there, and the layout may have
                    // been switched since (discussion #1104).
                    if (!remembered.isEmpty()
                        && PhosphorZones::LayoutUtils::contextLayoutHoldsZones(m_layoutManager, currentKey.screenId,
                                                                               currentKey.desktop, currentKey.activity,
                                                                               remembered)) {
                        state->assignWindowToZones(entry.windowId, remembered, currentKey.screenId, currentKey.desktop);
                        qCInfo(lcSnapEngine) << "reconcileDesktopMemberships: restored" << entry.windowId
                                             << "to its remembered zone(s) on desktop" << currentKey.desktop;
                    } else if (!remembered.isEmpty()) {
                        qCInfo(lcSnapEngine) << "reconcileDesktopMemberships: not restoring" << entry.windowId
                                             << "to remembered zone(s)" << remembered << "on desktop"
                                             << currentKey.desktop << "— not in the layout that desktop runs";
                    }
                }
            }
            result.adopted.append({entry.windowId, currentKey});
            qCInfo(lcSnapEngine) << "reconcileDesktopMemberships: adopted" << entry.windowId << "into desktop"
                                 << currentKey.desktop << "of" << currentKey.screenId;
        }
    }

    if (!carried.isEmpty()) {
        // Through the ordinary resnap batch, so a carried window gets the whole
        // commit — the assignment in the DESTINATION desktop's store (pinned by
        // the entry's virtualDesktop), zone occupancy, the effect's zone mirror
        // and the geometry itself.
        qCInfo(lcSnapEngine) << "reconcileDesktopMemberships: carrying" << carried.size()
                             << "window(s) into the layout of the desktop they moved to on" << screenId;
        emitBatchedResnap(carried);
        for (const PhosphorEngine::ZoneAssignmentEntry& entry : std::as_const(carried)) {
            if (entry.targetZoneId == PhosphorEngine::RestoreSentinel || !m_windowTracker) {
                continue; // the float-back is reported as a release and captured with it
            }
            // Same record refresh the in-app cross-desktop move does: the store
            // would otherwise keep the desktop the window was moved off, and
            // the next login would restore it there.
            if (auto placement = capturePlacementAtDesktop(entry.windowId, entry.virtualDesktop)) {
                placement->virtualDesktop = entry.virtualDesktop;
                m_windowTracker->placementStore().record(std::move(*placement));
            } else {
                qCDebug(lcSnapEngine) << "reconcileDesktopMemberships: capturePlacement miss for" << entry.windowId
                                      << "— placement-store desktop not updated to" << entry.virtualDesktop;
            }
            if (entry.virtualDesktop == currentKey.desktop) {
                continue;
            }
            // The window went to a desktop nobody is looking at, where the
            // compositor suspends its client: it never acks the configure, and
            // a resize that asks for MORE room than the window has is dropped
            // for good (KWin does not re-send it when the desktop comes back).
            // So park it for the effect's desktop-arrival restore, which
            // re-drives the placement the moment the desktop is shown and finds
            // the record this pass has just written. Emitted AFTER the batch
            // above, because the geometry apply cancels a park it finds; on one
            // D-Bus connection the two keep that order. The move itself asks
            // for the desktop the window is already on, which is how the effect
            // learns it has a window to park.
            qCInfo(lcSnapEngine) << "reconcileDesktopMemberships: parking" << entry.windowId
                                 << "for a placement retry when desktop" << entry.virtualDesktop << "is shown";
            Q_EMIT windowDesktopMoveRequested(entry.windowId, entry.virtualDesktop);
        }
    }

    // A window that holds a zone in the context just entered AND a place on
    // another desktop is sitting at the other desktop's geometry right now (it
    // is one window), so its own assignment here has to be re-applied. Only
    // those: the single-desktop windows on the screen did not move, and
    // re-committing every one of them on every switch was churn the effect had
    // to absorb for nothing. Computed AFTER the arms above, not before: a
    // window adopted into this desktop by this very pass has to count. On a
    // restart that is the entire population — the membership map is rebuilt
    // from scratch, so every multi-desktop window is adopted here rather than
    // arriving already a member.
    // Only on a screen this engine is active on: a screen a tiling engine
    // owns keeps its snap memberships as frozen memory for a return to
    // snapping, and re-committing them here would fight the tiling engine's
    // own placement on every desktop switch (seen live: the window bounced
    // between its tile and its old zone).
    QSet<QString> reapply;
    if (const SnapState* currentState = isActiveOnScreen(screenId) ? m_states.stateForKey(currentKey) : nullptr) {
        for (const QString& windowId : m_states.trackedWindowIds()) {
            if (m_states.hasMembership(windowId, currentKey) && m_states.membershipsForWindow(windowId).size() > 1
                && !currentState->zonesForWindow(windowId).isEmpty()) {
                reapply.insert(windowId);
            }
        }
    }
    if (!reapply.isEmpty()) {
        // Scoped to this screen: the other outputs' assignments did not move,
        // and a whole-session resnap would fight whatever they are doing.
        qCInfo(lcSnapEngine) << "reconcileDesktopMemberships: re-applying" << reapply.size() << "window(s) on"
                             << screenId << "for desktop" << currentKey.desktop
                             << "— multi-desktop windows are snapped here";
        resnapCurrentAssignments(screenId, reapply);
    }
    return result;
}

MembershipReconcileResult SnapEngine::reconcileDesktopMemberships(const QString& screenId,
                                                                  const PhosphorEngine::DesktopSpanQuery& spanOf)
{
    if (!spanOf || screenId.isEmpty()) {
        return {};
    }
    // A screen a tiling engine owns has no snap placements to grant: the
    // zones belong to its layout, and a membership minted here would be a
    // store nothing snaps into. The release arm still runs, so a window that
    // left a desktop of a screen that has since switched mode stops being an
    // occupant of its old zone there.
    const bool snapping = isActiveOnScreen(screenId);
    const PlacementStateKey currentKey = currentKeyForScreen(screenId);

    // Snapshot: the arms mutate the membership map and create stores.
    QList<PendingMembership> pending;
    for (const QString& windowId : m_states.trackedWindowIds()) {
        const QList<PlacementStateKey> held = m_states.membershipsForWindow(windowId);
        const bool onThisScreen = std::any_of(held.cbegin(), held.cend(), [&screenId](const auto& key) {
            return key.screenId == screenId;
        });
        if (!onThisScreen) {
            continue;
        }
        collectMembershipWork(windowId, screenId, currentKey, spanOf(windowId), pending);
        if (!snapping && !pending.isEmpty()) {
            pending.last().adopt = false;
            if (pending.last().stale.isEmpty()) {
                pending.removeLast();
            }
        }
    }
    return applyMembershipWork(screenId, currentKey, pending);
}

MembershipReconcileResult SnapEngine::reconcileWindowMemberships(const QString& windowId,
                                                                 const PhosphorEngine::DesktopSpanQuery& spanOf)
{
    if (!spanOf || windowId.isEmpty()) {
        return {};
    }
    const QString canonical = canonicalWindowId(windowId);
    // A window is on exactly one screen: every membership shares it, and the
    // first one names it. An untracked window has no context to reconcile;
    // it gains its first membership when it is snapped or floated, and the
    // screen-wide pass on the next switch does the rest.
    QString screenId;
    for (const PlacementStateKey& key : m_states.membershipsForWindow(canonical)) {
        if (!key.screenId.isEmpty()) {
            screenId = key.screenId;
            break;
        }
    }
    if (screenId.isEmpty()) {
        return {};
    }
    const PlacementStateKey currentKey = currentKeyForScreen(screenId);
    QList<PendingMembership> pending;
    collectMembershipWork(canonical, screenId, currentKey, spanOf(canonical), pending);
    if (!isActiveOnScreen(screenId) && !pending.isEmpty()) {
        pending.last().adopt = false;
        if (pending.last().stale.isEmpty()) {
            pending.removeLast();
        }
    }
    return applyMembershipWork(screenId, currentKey, pending);
}

} // namespace PhosphorSnapEngine

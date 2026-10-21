// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Per-desktop membership for snapping: which of a screen's desktops a window
// may hold its own zone assignment on.
//
// The third arm of the same change the scroll and tile engines carry, and the
// one that differs most. Those engines PLACE a window, so adopting one into a
// desktop means inserting it into that desktop's layout. Snapping places a
// window only when the user drops it into a zone or when it moves to another
// desktop (the carry below), so adoption here only grants the window a store
// of its own on the desktop in view. What it buys is that the next snap writes
// THERE instead of overwriting the zone the window occupies on the desktop it
// came from, which is what made a sticky window share one zone across every
// desktop.
//
// An adopted membership holds NO data until the user snaps or floats the
// window there. On that desktop the window therefore reads as neither snapped
// nor floating for this engine; float is per store in snapping, the same way
// it is per engine across modes, so a window floated on desktop 1 is not
// floating on desktop 2 until the user floats it there or carries a float
// there.
//
// The eviction in stateForWindowOnScreen is the other half: it spares stores
// the window is a member of, so the two assignments coexist instead of the
// newer one wiping the older.

#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorSnapEngine/INavigationStateProvider.h>
#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/LayoutUtils.h>

#include "pendingcarries_p.h"
#include "snapenginelogging.h"

#include <QHash>
#include <QRect>
#include <QSet>
#include <QString>
#include <QVector>

#include <algorithm>
#include <memory>
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
    // it. Sticky, or unknown, seeds everything the record names. A window on
    // one desktop is a span of one, not unknown (F263).
    std::optional<QSet<int>> presentOn;
    if (m_windowRegistry) {
        if (const auto ctx = m_windowRegistry->desktopContext(windowId)) {
            presentOn = ctx->desktopSet();
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
/// ride on QSet iteration order. Each one is planned on its own (CarryPlan);
/// on the ordinary move there is exactly one. 0 excludes nothing (an activity
/// move lands on the desktop it left, in the activity in view).
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

/// A zone set on one destination desktop: the window's own there (kept), or
/// the slot the carry gives it.
struct ZoneLanding
{
    int desktop = 0;
    QStringList zones;
    QRect rect;
    bool kept = false;
};

/// One window's share of a desktop carry (ledger-pass37 EJ), planned before
/// the release wipes the stores it reads.
struct CarryPlan
{
    bool sourceFloating = false;
    std::optional<PhosphorEngine::ZoneAssignmentEntry> applied; // the one apply, through the batch
    QList<std::pair<int, QStringList>> silentZones; // pinned per-key writes, no apply
    QList<int> floats; // float bit and residence
    QHash<int, QStringList> preFloats; // a suspension float's pre-float slot
    QSet<int> wrote; // desktops given state
    bool zoneAfter = false; // a zone remains on this screen
    bool keptOrWrote = false;
    int statedDesktop = 0; // the desktop whose state the window is stated in (0: none)
    bool statedFloating = false;
    bool statedZoned = false;
    std::optional<PendingActivityCarry> pending; // a move to an activity not in view
};

/// What a carried window needs once the batch has run.
struct CarryAfter
{
    QString windowId;
    int recordDesktop = 0; // the desktop its record is refreshed at (0: none)
    std::optional<bool> floatStatement;
    bool focus = false;
};

SnapPendingCarries& pendingCarriesOf(std::unique_ptr<SnapPendingCarries>& holder)
{
    if (!holder) {
        holder = std::make_unique<SnapPendingCarries>();
    }
    return *holder;
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
                                                          const QList<PendingMembership>& pending,
                                                          const QString& onlyWindow,
                                                          const PhosphorEngine::DesktopSpanQuery& spanOf)
{
    MembershipReconcileResult result;
    // A window that is snapped on the desktop it is being moved OFF is MOVING,
    // not un-snapping: the compositor relocates it and changes nothing about
    // its geometry, so left alone it sits on the destination desktop at a rect
    // that belongs to the layout it came from — the "ghost layout" of
    // discussion #1104, reached by KWin's own move-to-desktop shortcut and by
    // a drop in the desktop overview. PlasmaZones' own cross-desktop move
    // (tryCrossDesktopMove) answers this by keeping the zones the window holds
    // on the destination desktop, else landing it in the positionally-
    // equivalent zone of that desktop's layout; this is the same answer for
    // the moves it does not drive (a destination keeping its own zone or float
    // is re-stated, not overwritten). Collected here and emitted as ONE batch
    // after the loop, since the commit it goes through mutates the very stores
    // this loop walks.
    QVector<PhosphorEngine::ZoneAssignmentEntry> carried;
    QList<CarryAfter> after;
    // Windows this pass already placed or stated: the re-apply below leaves
    // them alone, so a window gets at most one apply per pass.
    QSet<QString> statedThisPass;
    // The whole span on a shared layout, its first zone's slot on another
    // (span_carry_kwin_desktop_move, F959). Mirrors
    // calculateResnapFromPreviousLayout's span policy.
    const auto slotFor = [this, &screenId](const QStringList& zones, int fromDesktop, const QString& fromActivity,
                                           int toDesktop) -> std::pair<QStringList, QRect> {
        if (zones.isEmpty()) {
            return {};
        }
        if (m_layoutManager->layoutForScreen(screenId, fromDesktop, fromActivity)
            == m_layoutManager->layoutForScreen(screenId, toDesktop, currentActivity())) {
            return {zones, m_windowTracker->resolveZoneGeometry(zones, screenId)};
        }
        const auto [zoneId, geometry] = resolveCrossDesktopZone(zones.first(), screenId, toDesktop);
        if (zoneId.isEmpty()) {
            return {};
        }
        return {QStringList{zoneId}, geometry};
    };
    // Planned before the release below wipes the stores it reads.
    const auto planCarry = [this, &screenId, &currentKey,
                            &slotFor](const PendingMembership& entry) -> std::optional<CarryPlan> {
        if (!m_layoutManager || !m_windowTracker) {
            return std::nullopt;
        }
        // Snapping switched off, or a window snapping leaves alone, carries
        // nothing: the carry is a placement like any other (F445).
        if (snappingSwitchedOff() || isWindowExcluded(entry.windowId, screenId)) {
            return std::nullopt;
        }
        // A floating window carries its float: it keeps its frame, and floats
        // on the desktops it goes to (float_home_cross_desktop, F608); a
        // minimize suspension carries its pre-float slot too, so the restore
        // lands in the layout it arrives in (F953).
        CarryPlan plan;
        QStringList zones;
        QStringList preFloat;
        PlacementStateKey source;
        bool found = false;
        bool activityMove = false;
        const bool intoView = entry.span.coversActivity(currentActivity());
        for (const PlacementStateKey& stale : entry.stale) {
            const SnapState* state = m_states.stateForKey(stale);
            if (!state) {
                continue;
            }
            // A key can go stale because the window changed ACTIVITY rather
            // than desktop (DesktopSpan mismatches on either axis). A move
            // across both at once is left alone.
            const bool otherActivity = stale.activity != currentActivity();
            if ((otherActivity || !intoView) && !entry.span.coversDesktop(stale.desktop)) {
                continue;
            }
            // A LIVE snap, not frozen memory, judged in the context it left.
            // A desktop that has since been given a tiling mode keeps its snap
            // assignments for a return to snapping, and the window was tiled
            // there, not snapped, when it moved: carrying that zone would place
            // a window on its own terms against the engine that was managing it.
            if (m_layoutManager->modeForScreen(screenId, stale.desktop, stale.activity)
                != PhosphorZones::AssignmentEntry::Mode::Snapping) {
                continue;
            }
            // Onto the same desktop of the activity in view, an activity move
            // is carried like a desktop move, under currentActivity(), which
            // is where the commit lands (F550). Onto an activity not in view,
            // the zones wait for that activity to show their desktop, where
            // the flush below re-validates and lands them (ledger-pass26 DJ).
            if (!intoView) {
                const QStringList held = state->zonesForWindow(entry.windowId);
                if (!held.isEmpty() && !state->isFloating(entry.windowId)) {
                    if (!plan.pending) {
                        plan.pending = PendingActivityCarry{screenId, {}, stale.activity, entry.span.activity};
                    }
                    plan.pending->zonesByDesktop.insert(stale.desktop, held);
                }
                continue;
            }
            if (state->isFloating(entry.windowId)) {
                plan.sourceFloating = true;
                preFloat = state->preFloatZones(entry.windowId);
            } else {
                zones = state->zonesForWindow(entry.windowId);
                if (zones.isEmpty()) {
                    continue;
                }
            }
            source = stale;
            activityMove = otherActivity;
            found = true;
            break;
        }
        if (!found) {
            if (plan.pending) {
                return plan;
            }
            return std::nullopt;
        }
        const QList<int> destinations = carryDestinationDesktops(entry.span, activityMove ? 0 : source.desktop);
        const int viewDesktop = entry.span.coversKey(currentKey) ? currentKey.desktop : 0;
        const bool suspension = plan.sourceFloating && m_windowTracker->isSuspensionFloat(entry.windowId);
        QList<ZoneLanding> zoned;
        QSet<int> keptFloats;
        QList<int> noSlot;
        for (const int desktop : destinations) {
            // The very key the commit will write (stateForWindowOnScreen pins
            // {screen, desktop, currentActivity()}), so "what does it hold
            // over there" is asked of the store the carry would land in.
            const PlacementStateKey key{screenId, desktop, currentActivity()};
            if (const SnapState* there = m_states.stateForKey(key);
                there && holdsWindowInState(entry.windowId, there)) {
                // It keeps the state it has there (F611, F619).
                if (const QStringList own = there->zonesForWindow(entry.windowId); !own.isEmpty()) {
                    zoned.append({desktop, own, m_windowTracker->resolveZoneGeometry(own, screenId), true});
                    continue;
                }
                if (there->isFloating(entry.windowId)) {
                    keptFloats.insert(desktop);
                    continue;
                }
            }
            if (m_layoutManager->modeForScreen(screenId, desktop, currentActivity())
                != PhosphorZones::AssignmentEntry::Mode::Snapping) {
                continue; // tiling or scrolling owns the arrival on that desktop
            }
            // A desktop the user disabled snapping on, or one running no
            // layout, takes nothing, like a desktop another mode owns (F445).
            if ((m_shouldRestorePredicate && !m_shouldRestorePredicate(screenId, desktop))
                || !m_layoutManager->layoutForScreen(screenId, desktop, currentActivity())) {
                continue;
            }
            if (plan.sourceFloating) {
                plan.floats.append(desktop);
                plan.wrote.insert(desktop);
                if (suspension) {
                    if (const QStringList mapped = slotFor(preFloat, source.desktop, source.activity, desktop).first;
                        !mapped.isEmpty()) {
                        plan.preFloats.insert(desktop, mapped);
                    }
                }
                continue;
            }
            const auto [slotZones, rect] = slotFor(zones, source.desktop, source.activity, desktop);
            if (!slotZones.isEmpty() && rect.isValid()) {
                zoned.append({desktop, slotZones, rect, false});
                plan.wrote.insert(desktop);
            } else {
                noSlot.append(desktop);
            }
        }
        // A zone remains on this screen: one kept or given above, or one in a
        // membership the move did not take.
        const QList<PlacementStateKey> held = m_states.membershipsForWindow(entry.windowId);
        plan.zoneAfter = !zoned.isEmpty() || std::any_of(held.cbegin(), held.cend(), [&](const PlacementStateKey& key) {
            const SnapState* state =
                key.screenId == screenId && !entry.stale.contains(key) ? m_states.stateForKey(key) : nullptr;
            return state && !state->zonesForWindow(entry.windowId).isEmpty();
        });
        // Snapping over there, but its layout has no slot in this window's
        // position (zone 3 of a grid, moved onto a two-zone layout). A layout
        // switch answers that with the window's pre-snap geometry, and so does
        // this, on the desktop in view (F779), or on the first such desktop
        // when the window is visible on none here and keeps no zone (F535).
        // Any other such desktop takes nothing, so the zones the window holds
        // on its other desktops are left alone (F477).
        int restoreDesktop = 0;
        if (viewDesktop != 0 && noSlot.contains(viewDesktop)) {
            restoreDesktop = viewDesktop;
        } else if (viewDesktop == 0 && !noSlot.isEmpty() && !plan.zoneAfter) {
            restoreDesktop = noSlot.first();
        }
        std::optional<QRect> freeGeometry;
        if (restoreDesktop != 0) {
            freeGeometry = m_windowTracker->validatedUnmanagedGeometry(entry.windowId, screenId);
            if (!freeGeometry || !freeGeometry->isValid()) {
                qCInfo(lcSnapEngine) << "reconcileDesktopMemberships: no equivalent zone and no pre-snap geometry for"
                                     << entry.windowId << "— it stays where the compositor left it";
                restoreDesktop = 0;
            } else {
                plan.wrote.insert(restoreDesktop);
            }
        }
        // At most one apply per window per pass. With a desktop in view it is
        // that desktop's: a slot given there, or the pre-snap rect; a zone or
        // float it keeps there is what it already shows, and a float given
        // there keeps its frame. Visible nowhere here, it is the lowest zone,
        // kept or given (a kept one is a re-statement the effect parks for the
        // hidden desktop, L11.11), else the pre-snap rect.
        int appliedAt = -1;
        for (int i = 0; i < zoned.size(); ++i) {
            const ZoneLanding& landing = zoned.at(i);
            const bool applicable = landing.rect.isValid()
                && (!landing.kept
                    || PhosphorZones::LayoutUtils::contextLayoutHoldsZones(m_layoutManager, screenId, landing.desktop,
                                                                           currentActivity(), landing.zones));
            if (viewDesktop != 0 ? (landing.desktop == viewDesktop && !landing.kept) : applicable) {
                appliedAt = i;
                break;
            }
        }
        if (appliedAt >= 0) {
            const ZoneLanding& landing = zoned.at(appliedAt);
            PhosphorEngine::ZoneAssignmentEntry assign;
            assign.windowId = entry.windowId;
            assign.targetZoneId = landing.zones.first();
            if (landing.zones.size() > 1) {
                assign.targetZoneIds = landing.zones;
            }
            assign.targetGeometry = landing.rect;
            assign.targetScreenId = screenId;
            // The destination desktop, not the one in view: the commit pins
            // the assignment to the context the window moved to.
            assign.virtualDesktop = landing.desktop;
            // The window's own slot carried along: a re-statement (F461).
            assign.restatement = true;
            plan.applied = assign;
        } else if (restoreDesktop != 0) {
            PhosphorEngine::ZoneAssignmentEntry restore;
            restore.windowId = entry.windowId;
            restore.targetZoneId = PhosphorEngine::RestoreSentinel;
            restore.targetGeometry = *freeGeometry;
            restore.targetScreenId = screenId;
            // Pinned: the batch's restore arm floats the window on this
            // desktop and leaves its zones on the others alone.
            restore.virtualDesktop = restoreDesktop;
            restore.restatement = true;
            plan.applied = restore;
        }
        // Every other slot is written into its store with no apply, the same
        // per-key write the seed and the adoption make.
        for (int i = 0; i < zoned.size(); ++i) {
            if (i != appliedAt && !zoned.at(i).kept) {
                plan.silentZones.append({zoned.at(i).desktop, zoned.at(i).zones});
            }
        }
        plan.keptOrWrote = !zoned.isEmpty() || !keptFloats.isEmpty() || !plan.wrote.isEmpty();
        // The desktop whose state the window is stated in: the one in view,
        // else the lowest that has state now.
        const auto hasState = [&](int desktop) {
            return keptFloats.contains(desktop) || plan.wrote.contains(desktop)
                || std::any_of(zoned.cbegin(), zoned.cend(), [desktop](const ZoneLanding& landing) {
                       return landing.desktop == desktop;
                   });
        };
        if (viewDesktop != 0) {
            plan.statedDesktop = hasState(viewDesktop) ? viewDesktop : 0;
        } else {
            for (const int desktop : destinations) {
                if (hasState(desktop)) {
                    plan.statedDesktop = desktop;
                    break;
                }
            }
        }
        if (plan.statedDesktop != 0) {
            const int stated = plan.statedDesktop;
            plan.statedFloating =
                keptFloats.contains(stated) || plan.floats.contains(stated) || stated == restoreDesktop;
            plan.statedZoned = std::any_of(zoned.cbegin(), zoned.cend(), [stated](const ZoneLanding& landing) {
                return landing.desktop == stated;
            });
        }
        return plan;
    };
    for (const PendingMembership& entry : pending) {
        const std::optional<CarryPlan> plan = planCarry(entry);
        const int viewDesktop = entry.span.coversKey(currentKey) ? currentKey.desktop : 0;
        // The window is being carried into the very context the adopt arm
        // below would restore a remembered zone in. The carry wins outright
        // (the user has just moved the window, so its slot is the one that
        // carries over), so the remembered restore would be a write nothing
        // keeps and a log line claiming a restore that did not stand.
        const bool carriedIntoCurrentContext = plan && viewDesktop != 0 && plan->wrote.contains(viewDesktop);
        // A move supersedes a carry still waiting for an activity or a float-back
        // still owed, and a move to an activity not in view records one.
        if (!entry.stale.isEmpty() && m_pendingCarries) {
            m_pendingCarries->byWindow.remove(entry.windowId);
            m_pendingCarries->owedFloatBacks.remove(entry.windowId);
        }
        if (plan && plan->pending) {
            pendingCarriesOf(m_pendingCarries).byWindow.insert(entry.windowId, *plan->pending);
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
            if (plan && plan->zoneAfter) {
                // Not reported while a zone remains on this screen: the daemon
                // answers a release by telling the effect the window occupies
                // no zone.
                qCInfo(lcSnapEngine) << "reconcileDesktopMemberships: moving" << entry.windowId << "off desktop"
                                     << stale.desktop << "of" << stale.screenId << "— it keeps a zone on this screen";
                continue;
            }
            result.released.append({entry.windowId, stale});
            qCInfo(lcSnapEngine) << "reconcileDesktopMemberships: released" << entry.windowId << "from desktop"
                                 << stale.desktop << "of" << stale.screenId << "— its span no longer covers it";
        }
        // The plan read everything it needs from the source store, which the
        // release just emptied; a destination key is never a stale one.
        if (plan) {
            for (const auto& [desktop, zones] : std::as_const(plan->silentZones)) {
                stateForWindowOnScreen(entry.windowId, screenId, desktop)
                    ->assignWindowToZones(entry.windowId, zones, screenId, desktop);
            }
            for (const int desktop : std::as_const(plan->floats)) {
                SnapState* state = stateForWindowOnScreen(entry.windowId, screenId, desktop);
                state->setFloatingOnScreen(entry.windowId, screenId, desktop);
                if (const QStringList slot = plan->preFloats.value(desktop); !slot.isEmpty()) {
                    state->addPreFloatZone(entry.windowId, slot);
                    state->addPreFloatScreen(entry.windowId, screenId);
                }
            }
        }
        // Carried nowhere, not adopted here, and left with no snap membership
        // on this screen: the window moved to a desktop snap does not hold it
        // on, so the record's snap slot goes too. A single-desktop window's
        // zone lives in the flat slot, which the per-desktop forget above
        // never touches, and a later snapping pass on the desktop it moved to
        // would read it back as that desktop's zone (F624).
        if (!(plan && plan->keptOrWrote) && !entry.adopt && !entry.stale.isEmpty() && m_windowTracker) {
            const QString screen = entry.stale.first().screenId;
            const QList<PlacementStateKey> left = m_states.membershipsForWindow(entry.windowId);
            if (std::none_of(left.cbegin(), left.cend(), [&screen](const PlacementStateKey& key) {
                    return key.screenId == screen;
                })) {
                m_windowTracker->releaseEngineSlot(entry.windowId, engineId());
            }
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
            if (state && m_windowTracker && !carriedIntoCurrentContext && snapsInContext(currentKey)
                && state->zonesForWindow(entry.windowId).isEmpty()) {
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
        if (!plan || !plan->keptOrWrote) {
            continue;
        }
        CarryAfter carry;
        carry.windowId = entry.windowId;
        if (plan->applied) {
            carried.append(*plan->applied);
            statedThisPass.insert(entry.windowId);
        }
        // The batch's restore arm records its own float; any other write
        // refreshes the record once, at the desktop the window is stated in.
        const bool restoring = plan->applied && plan->applied->targetZoneId == PhosphorEngine::RestoreSentinel;
        if (plan->applied && !restoring) {
            carry.recordDesktop = plan->applied->virtualDesktop;
        } else if (!restoring && !plan->wrote.isEmpty()) {
            carry.recordDesktop = plan->wrote.contains(plan->statedDesktop)
                ? plan->statedDesktop
                : *std::min_element(plan->wrote.cbegin(), plan->wrote.cend());
        }
        // The float state the effect shows is that of the desktop the window
        // is stated in. A batch entry states its own (the commit, the restore
        // arm). A kept or carried float says so here, and so does a kept zone
        // after a float source, whose float the release ended before any
        // commit could see it (F776).
        if (!plan->applied && plan->statedFloating) {
            carry.floatStatement = true;
        } else if (plan->sourceFloating && plan->statedZoned) {
            carry.floatStatement = false;
        }
        if (carry.floatStatement) {
            statedThisPass.insert(entry.windowId);
        }
        // A float kept on a desktop out of view, by a window that was snapped
        // (a floating source keeps its frame): no apply goes now (EJ), and its
        // frame stands on the zone it left, so the float-back is owed for that
        // desktop's first showing (F780). So is the restore arm's float onto
        // such a desktop: its apply reaches a suspended client, which drops a
        // grow (F1019). A shrink that landed moved the frame, and the debt
        // lapses on the showing.
        if ((restoring || (!plan->applied && plan->statedFloating)) && !plan->sourceFloating && viewDesktop == 0
            && m_navState) {
            const int owedDesktop = restoring ? plan->applied->virtualDesktop : plan->statedDesktop;
            if (const QRect frame = m_navState->frameGeometry(entry.windowId); frame.isValid()) {
                pendingCarriesOf(m_pendingCarries)
                    .owedFloatBacks.insert(entry.windowId,
                                           {PlacementStateKey{screenId, owedDesktop, currentActivity()}, frame});
            }
        }
        // A window arriving on the desktop in view is an arrival, focused
        // like an open when "Focus new windows" is on (focus_new_windows_rule,
        // F564).
        carry.focus = entry.adopt && carriedIntoCurrentContext;
        after.append(carry);
    }

    // A snap moved to an activity not in view lands the first time its
    // desktop shows there (F550, ledger-pass26 DJ): re-validated now, put
    // into the matching zone of the layout it arrives in, and dropped either
    // way. Not an arrival the user watched, so it takes no focus.
    if (m_pendingCarries && m_layoutManager && m_windowTracker) {
        QStringList due;
        for (auto it = m_pendingCarries->byWindow.cbegin(); it != m_pendingCarries->byWindow.cend(); ++it) {
            const PendingActivityCarry& waiting = it.value();
            if (waiting.screenId == screenId && (onlyWindow.isEmpty() || it.key() == onlyWindow)
                && (waiting.toActivity.isEmpty() || waiting.toActivity == currentKey.activity)
                && waiting.zonesByDesktop.contains(currentKey.desktop)) {
                due.append(it.key());
            }
        }
        for (const QString& windowId : std::as_const(due)) {
            const PendingActivityCarry waiting = m_pendingCarries->byWindow.take(windowId);
            bool stillThere = true;
            if (m_windowRegistry) {
                const std::optional<PhosphorEngine::WindowDesktopContext> ctx =
                    m_windowRegistry->desktopContext(windowId);
                std::optional<QSet<int>> desktops;
                if (ctx) {
                    desktops = ctx->desktopSet();
                }
                stillThere = ctx
                    && (ctx->activity.isEmpty() || ctx->activity == currentKey.activity
                        || ctx->activities.contains(currentKey.activity))
                    && (!desktops || desktops->contains(currentKey.desktop));
            }
            const SnapState* there = m_states.stateForKey(currentKey);
            const bool holdsOwn = there && holdsWindowInState(windowId, there)
                && (there->isWindowSnapped(windowId) || there->isFloating(windowId));
            if (!stillThere || holdsOwn || !snapsInContext(currentKey) || isWindowExcluded(windowId, screenId)
                || m_layoutManager->modeForScreen(screenId, currentKey.desktop, currentKey.activity)
                    != PhosphorZones::AssignmentEntry::Mode::Snapping) {
                qCInfo(lcSnapEngine) << "reconcileDesktopMemberships: dropping the activity carry of" << windowId
                                     << "— it no longer lands on desktop" << currentKey.desktop << "of" << screenId;
                continue;
            }
            const auto [zones, rect] = slotFor(waiting.zonesByDesktop.value(currentKey.desktop), currentKey.desktop,
                                               waiting.fromActivity, currentKey.desktop);
            if (zones.isEmpty() || !rect.isValid()) {
                continue;
            }
            PhosphorEngine::ZoneAssignmentEntry assign;
            assign.windowId = windowId;
            assign.targetZoneId = zones.first();
            if (zones.size() > 1) {
                assign.targetZoneIds = zones;
            }
            assign.targetGeometry = rect;
            assign.targetScreenId = screenId;
            assign.virtualDesktop = currentKey.desktop;
            assign.restatement = true;
            carried.append(assign);
            statedThisPass.insert(windowId);
            CarryAfter carry;
            carry.windowId = windowId;
            carry.recordDesktop = currentKey.desktop;
            after.append(carry);
        }
    }

    if (!carried.isEmpty()) {
        // Through the ordinary resnap batch, so a carried window gets the whole
        // commit — the assignment in the DESTINATION desktop's store (pinned by
        // the entry's virtualDesktop), zone occupancy, the effect's zone mirror
        // and the geometry itself. The float-back needs nothing more: the
        // batch's restore arm floats the window where it lands and records it
        // there. A pre-snap rect that grows the window rides the suspended-
        // client limitation the move-to-desktop shortcut has always had, which
        // leaves it where the compositor put it, and the shrink direction lands.
        qCInfo(lcSnapEngine) << "reconcileDesktopMemberships: carrying" << carried.size()
                             << "window(s) into the layout of the desktop they moved to on" << screenId;
        emitBatchedResnap(carried);
    }
    for (const CarryAfter& carry : std::as_const(after)) {
        // Same record refresh the in-app cross-desktop move does: the store
        // would otherwise keep the desktop the window was moved off, and the
        // next login would restore it there. A carry onto a desktop nobody is
        // looking at needs no park request: the effect parks a window whose
        // zone apply lands on a hidden desktop and re-applies the zone when it
        // is shown, and a move request would have pinned a window on several
        // desktops to one.
        if (carry.recordDesktop >= 1 && m_windowTracker) {
            if (auto placement = capturePlacementAtDesktop(carry.windowId, carry.recordDesktop)) {
                placement->virtualDesktop = carry.recordDesktop;
                m_windowTracker->placementStore().record(std::move(*placement));
            } else {
                qCDebug(lcSnapEngine) << "reconcileDesktopMemberships: capturePlacement miss for" << carry.windowId
                                      << "— placement-store desktop not updated to" << carry.recordDesktop;
            }
        }
        if (carry.floatStatement) {
            Q_EMIT windowFloatingChanged(carry.windowId, *carry.floatStatement, screenId);
        }
        if (carry.focus) {
            if (auto* settings = snapSettings(); settings && settings->focusNewWindows()) {
                Q_EMIT activateWindowRequested(carry.windowId);
            }
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
    // arriving already a member. On a per-window reconcile only that window:
    // the other windows did not move (F777).
    // Only on a screen this engine is active on: a screen a tiling engine
    // owns keeps its snap memberships as frozen memory for a return to
    // snapping, and re-committing them here would fight the tiling engine's
    // own placement on every desktop switch (seen live: the window bounced
    // between its tile and its old zone).
    // Not with snapping switched off or disabled in this context (F445), and
    // only zones the layout this context runs now holds: a hidden desktop's
    // layout may have changed since the window was snapped there (F982).
    QSet<QString> reapply;
    if (const SnapState* currentState =
            isActiveOnScreen(screenId) && snapsInContext(currentKey) ? m_states.stateForKey(currentKey) : nullptr) {
        // Another of the window's member stores holds a zone (@p zone) or the
        // float bit.
        const auto otherMembershipHolds = [this, &currentKey](const QString& windowId, bool zone) {
            const QList<PlacementStateKey> held = m_states.membershipsForWindow(windowId);
            return std::any_of(held.cbegin(), held.cend(), [&](const PlacementStateKey& key) {
                const SnapState* state = key == currentKey ? nullptr : m_states.stateForKey(key);
                return state && (zone ? state->isWindowSnapped(windowId) : state->isFloating(windowId));
            });
        };
        // The frame stands on the zone rect of another of its desktops.
        const auto frameStandsOnAnotherZone = [this, &screenId, &currentKey](const QString& windowId) {
            if (!m_navState || !m_windowTracker) {
                return false;
            }
            const QRect frame = m_navState->frameGeometry(windowId);
            const QList<PlacementStateKey> held = m_states.membershipsForWindow(windowId);
            return frame.isValid() && std::any_of(held.cbegin(), held.cend(), [&](const PlacementStateKey& key) {
                       const SnapState* state = key == currentKey ? nullptr : m_states.stateForKey(key);
                       const QStringList zones = state ? state->zonesForWindow(windowId) : QStringList{};
                       return !zones.isEmpty() && m_windowTracker->resolveZoneGeometry(zones, screenId) == frame;
                   });
        };
        // Shown on a desktop besides this one. A desktop of the span that runs
        // tiling or scrolling holds no snap membership, so the membership count
        // alone misses a window another engine placed there (#1151: back from
        // a scrolling desktop, the window still stood at its strip rect). Only
        // on the screen-wide pass a desktop switch runs, the one that follows a
        // stay on another desktop. A per-window pass answers an edit of the
        // window's own desktop set, which leaves it standing where the desktop
        // in view put it (F477).
        const auto shownElsewhere = [&spanOf, &onlyWindow](const QString& windowId) {
            if (!spanOf || !onlyWindow.isEmpty()) {
                return false;
            }
            const DesktopSpan span = spanOf(windowId);
            return span.known && (span.sticky || span.desktops.size() > 1);
        };
        const QStringList candidates = onlyWindow.isEmpty() ? m_states.trackedWindowIds() : QStringList{onlyWindow};
        for (const QString& windowId : candidates) {
            if (statedThisPass.contains(windowId) || !m_states.hasMembership(windowId, currentKey)) {
                continue;
            }
            const bool otherSnapMemberships = m_states.membershipsForWindow(windowId).size() >= 2;
            if (!otherSnapMemberships && !shownElsewhere(windowId)) {
                continue;
            }
            const QStringList zones = currentState->zonesForWindow(windowId);
            if (!zones.isEmpty()) {
                if (PhosphorZones::LayoutUtils::contextLayoutHoldsZones(
                        m_layoutManager, currentKey.screenId, currentKey.desktop, currentKey.activity, zones)) {
                    reapply.insert(windowId);
                }
                continue;
            }
            // The float and unsnap statements below speak for the window's
            // other snap stores, so they need one.
            if (!onlyWindow.isEmpty() || !otherSnapMemberships) {
                continue;
            }
            // Entering a context where the window floats, or holds nothing,
            // states that context: the effect keeps whatever the desktop it
            // came from said (F742, F778). The float-back goes back on only
            // when the frame stands on another desktop's zone rect (F777); a
            // frame elsewhere is the user's.
            const bool floating = currentState->isFloating(windowId);
            if (floating) {
                Q_EMIT windowFloatingChanged(windowId, true, screenId);
                const bool minimized = m_windowRegistry && m_windowRegistry->minimizedState(windowId).value_or(false);
                if (!minimized && frameStandsOnAnotherZone(windowId)) {
                    if (const auto back = m_windowTracker->validatedUnmanagedGeometry(windowId, screenId);
                        back && back->isValid()) {
                        Q_EMIT restatementGeometryRequested(windowId, back->x(), back->y(), back->width(),
                                                            back->height(), QString(), screenId);
                    }
                }
            } else if (otherMembershipHolds(windowId, /*zone=*/true)) {
                Q_EMIT windowSnapStateChanged(windowId,
                                              PhosphorProtocol::WindowStateEntry{windowId, QString(), screenId, false,
                                                                                 QStringLiteral("unsnapped"),
                                                                                 QStringList{}, false});
            }
            if (!floating && otherMembershipHolds(windowId, /*zone=*/false)) {
                Q_EMIT windowFloatingChanged(windowId, false, screenId);
            }
        }
        // The float-backs owed to this context now that it shows (F780): only
        // while the window still floats here and its frame has not moved since
        // the carry (a frame elsewhere is the user's). A minimized window keeps
        // its debt for a later showing.
        if (m_pendingCarries && m_navState && m_windowTracker) {
            auto& owed = m_pendingCarries->owedFloatBacks;
            for (auto it = owed.begin(); it != owed.end();) {
                const QString windowId = it.key();
                if (!(it->key == currentKey) || (!onlyWindow.isEmpty() && windowId != onlyWindow)
                    || (m_windowRegistry && m_windowRegistry->minimizedState(windowId).value_or(false))) {
                    ++it;
                    continue;
                }
                const QRect carriedFrame = it->frame;
                it = owed.erase(it);
                if (statedThisPass.contains(windowId) || !m_states.hasMembership(windowId, currentKey)
                    || !currentState->isFloating(windowId) || m_navState->frameGeometry(windowId) != carriedFrame) {
                    continue;
                }
                if (const auto back = m_windowTracker->validatedUnmanagedGeometry(windowId, screenId);
                    back && back->isValid()) {
                    qCInfo(lcSnapEngine) << "reconcileDesktopMemberships: re-stating the float-back of" << windowId
                                         << "kept floating on desktop" << currentKey.desktop << "while it was hidden";
                    Q_EMIT restatementGeometryRequested(windowId, back->x(), back->y(), back->width(), back->height(),
                                                        QString(), screenId);
                }
            }
        }
    }
    if (!reapply.isEmpty()) {
        // Scoped to this screen: the other outputs' assignments did not move,
        // and a whole-session resnap would fight whatever they are doing.
        qCInfo(lcSnapEngine) << "reconcileDesktopMemberships: re-applying" << reapply.size() << "window(s) on"
                             << screenId << "for desktop" << currentKey.desktop
                             << "— multi-desktop windows are snapped here";
        // A re-statement batch: no "nothing to resnap" OSD on a desktop
        // switch, no Snap Assist (F461).
        QVector<PhosphorEngine::ZoneAssignmentEntry> entries = calculateResnapFromCurrentAssignments(screenId, reapply);
        for (PhosphorEngine::ZoneAssignmentEntry& entry : entries) {
            entry.restatement = true;
        }
        emitBatchedResnap(entries);
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
    return applyMembershipWork(screenId, currentKey, pending, QString(), spanOf);
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
        // A window whose carry waits for an activity holds no membership. Moved
        // again before it arrived, the carry follows it, so a move back lands
        // it in its zone at once when that activity is in view (F550).
        if (m_pendingCarries) {
            if (const auto it = m_pendingCarries->byWindow.find(canonical); it != m_pendingCarries->byWindow.end()) {
                if (const DesktopSpan span = spanOf(canonical); span.known) {
                    it->toActivity = span.activity;
                    const QString carryScreen = it->screenId;
                    return applyMembershipWork(carryScreen, currentKeyForScreen(carryScreen), {}, canonical, spanOf);
                }
            }
        }
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
    return applyMembershipWork(screenId, currentKey, pending, canonical, spanOf);
}

void SnapEngine::rekeyMembershipsToRecordedScreens(const QString& physicalScreenId)
{
    if (physicalScreenId.isEmpty()) {
        return;
    }
    const QString prefix = physicalScreenId + PhosphorIdentity::VirtualScreenId::Separator;
    const auto onMonitor = [&](const QString& screenId) {
        return screenId == physicalScreenId || screenId.startsWith(prefix);
    };
    // A split or an un-split rewrites the screen VALUES in place and leaves
    // each store filed under the monitor's old id. Left there, the next
    // pinned commit on the new id reads as a move off the monitor
    // (stateForWindowOnScreen's cross-screen arm) and releases the window's
    // other desktops (F105). Each membership moves to the key for the screen
    // its store records, desktop and activity kept, with the zone carried
    // verbatim: nothing is released, and the caller's prune judges the zones
    // afterwards. Collected first, because ensureStateForKey creates stores.
    struct Rekey
    {
        QString windowId;
        PlacementStateKey from;
        PlacementStateKey to;
    };
    QList<Rekey> moves;
    m_states.forEachMembership([&](const QString& windowId, const PlacementStateKey& key) {
        if (!onMonitor(key.screenId)) {
            return;
        }
        const SnapState* state = m_states.stateForKey(key);
        if (!state) {
            return;
        }
        QString recorded = state->screenForWindow(windowId);
        if (recorded.isEmpty()) {
            recorded = state->preFloatScreen(windowId);
        }
        if (!recorded.isEmpty() && recorded != key.screenId && onMonitor(recorded)) {
            moves.append({windowId, key, PlacementStateKey{recorded, key.desktop, key.activity}});
        }
    });
    for (const Rekey& move : moves) {
        SnapState* from = m_states.stateForKey(move.from);
        SnapState* to = ensureStateForKey(move.to);
        if (!from || !to) {
            continue;
        }
        from->migrateWindowTo(to, move.windowId, move.to.screenId);
        m_states.migrate(move.windowId, move.from, move.to);
    }
    if (!moves.isEmpty()) {
        qCInfo(lcSnapEngine) << "rekeyMembershipsToRecordedScreens:" << moves.size() << "membership(s) re-filed on"
                             << physicalScreenId;
    }
}

void SnapEngine::renameScreen(const QString& oldPhysicalId, const QString& newPhysicalId)
{
    if (oldPhysicalId.isEmpty() || newPhysicalId.isEmpty() || oldPhysicalId == newPhysicalId) {
        return;
    }
    // An EDID twin's identifier flip (F189): the facade has already rewritten
    // the screen values, and each store still filed under the old id would
    // read the next pinned commit as a move off the monitor. Same shape as
    // rekeyMembershipsToRecordedScreens: collected first, since
    // ensureStateForKey creates stores, and nothing is released.
    const QString oldPrefix = oldPhysicalId + PhosphorIdentity::VirtualScreenId::Separator;
    struct Rekey
    {
        QString windowId;
        PlacementStateKey from;
        PlacementStateKey to;
    };
    QList<Rekey> moves;
    m_states.forEachMembership([&](const QString& windowId, const PlacementStateKey& key) {
        QString to;
        if (key.screenId == oldPhysicalId) {
            to = newPhysicalId;
        } else if (key.screenId.startsWith(oldPrefix)) {
            to = newPhysicalId + key.screenId.mid(oldPhysicalId.size());
        } else {
            return;
        }
        moves.append({windowId, key, PlacementStateKey{to, key.desktop, key.activity}});
    });
    for (const Rekey& move : std::as_const(moves)) {
        SnapState* from = m_states.stateForKey(move.from);
        SnapState* to = ensureStateForKey(move.to);
        if (!from || !to) {
            continue;
        }
        from->migrateWindowTo(to, move.windowId, move.to.screenId);
        m_states.migrate(move.windowId, move.from, move.to);
    }
    if (!moves.isEmpty()) {
        qCInfo(lcSnapEngine) << "renameScreen:" << moves.size() << "membership(s) re-filed from" << oldPhysicalId
                             << "to" << newPhysicalId;
    }
}

} // namespace PhosphorSnapEngine

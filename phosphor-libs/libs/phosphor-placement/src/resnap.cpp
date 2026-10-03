// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Resnap, snap-all, and resolution change geometry calculations.
// Part of WindowTrackingService — split from windowtrackingservice.cpp for SRP.

#include <PhosphorPlacement/WindowTrackingService.h>
#include "placementutils.h"
#include <PhosphorZones/GeometryUtils.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutUtils.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorIdentity/WindowId.h>
#include "placementlogging.h"
#include <QScreen>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <tuple>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PhosphorPlacement {

void WindowTrackingService::populateResnapBufferForAllScreens(const QSet<QString>& excludeScreens,
                                                              const QSet<QString>& includeScreens, int desktopFilter)
{
    if (!hasSnapState() || !m_layoutManager)
        return;

    QVector<ResnapEntry> newBuffer;
    QSet<QString> addedIds;

    // Build per-screen zone position maps: for each screen, resolve the CURRENT
    // layout and build zoneId → position mapping. This captures the OLD state
    // before the KCM's new assignments take effect on the daemon.
    // (At this point, PhosphorZones::LayoutRegistry already has the new assignments from the KCM's
    // D-Bus calls, so resolveLayoutForScreen returns the NEW layout. But the
    // window zone assignments in WTS still reference zone IDs from the OLD layout.)
    //
    // Since zone IDs from the old layout may not exist in the new layout,
    // we need to find each window's POSITION in the old layout. The old layout
    // is the one whose zone IDs match the window's zone assignments.
    // We look up each zone ID in ALL loaded layouts to find the position.

    // Build a global zoneId → position map merged across all layouts.
    // Shared with `WindowTrackingService::onLayoutChanged` (lifecycle.cpp);
    // see `PhosphorZones::LayoutUtils::buildGlobalZonePositionMap` for
    // why the merge is unambiguous (zone UUIDs are unique across layouts).
    const QHash<QString, int> globalZoneIdToPosition =
        PhosphorZones::LayoutUtils::buildGlobalZonePositionMap(m_layoutManager->layouts());

    // Per-screen current-desktop memo for the filter below: one VDM lookup per
    // screen instead of one per candidate window (live pass + durable pass both
    // funnel through addCandidate).
    QHash<QString, int> screenDesktopMemo;

    // Windows whose snap assignment names a screen they have since LEFT while a
    // tiling engine held them. Collected by addCandidate and forgotten after
    // both passes (see the loop below the durable pass).
    QSet<QString> departed;

    // Per-window candidate processing, shared by the live and durable passes below.
    const auto addCandidate = [&](const QString& windowId, const QStringList& zoneIds, const QString& screenId,
                                  int virtualDesktop) {
        // SNAP's own float bit, not the mode-routed read. This candidate set is
        // snap-owned by construction (live snap-store assignments plus durable
        // records whose snap slot is stateSnapped), and the routed read
        // dispatches on the window's screen's CURRENT mode — which on a screen
        // mid-flip is the DESTINATION engine. Float is per engine, so a
        // destination-engine float bit must not decide whether a snap-owned
        // candidate participates. Same fix as resnap_calc.cpp's.
        const PhosphorSnapEngine::SnapState* snap = snapForWindow(windowId);
        if (zoneIds.isEmpty() || (snap && snap->isFloating(windowId)))
            return;
        if (screenId.isEmpty())
            return;
        // A window a tiling engine holds on ANOTHER screen is not this
        // resnap's to place. Its snap assignment here is memory from before
        // the screen went to tiling, and the window left while tiled (a
        // keyboard move, a drag), which the snap engine never hears of.
        // Replaying it pulled the window off the screen it is on and back
        // into its old zone (discussion #1124). Held on THIS screen is the
        // ordinary return to snapping, whatever order the release and this
        // pass run in, so only a different screen skips.
        if (const QString heldOn = tilingHeldScreenForWindow(windowId); !heldOn.isEmpty() && heldOn != screenId) {
            departed.insert(canonicalizeForLookup(windowId));
            return;
        }
        // Skip windows on excluded screens (e.g. autotile screens)
        if (excludeScreens.contains(screenId))
            return;
        // When include-filter is set, only process windows on the specified screens
        if (!includeScreens.isEmpty() && !includeScreens.contains(screenId))
            return;
        // Desktop filter: a per-desktop layout change should resnap only the
        // windows on that desktop. virtualDesktop==0 means sticky / unknown
        // (visible on every desktop) so include those regardless of the filter.
        // Under Plasma 6.7 per-output virtual desktops (#648) the "current desktop"
        // is per-screen, so when filtering (desktopFilter > 0) compare each window
        // against ITS screen's current desktop rather than the single global value
        // the caller passed. Fall back to that value both when no VDM is wired AND
        // when the VDM does not know the screen's desktop (returns <= 0) — without
        // the second fallback, an unknown screen desktop would exclude every
        // non-sticky window on that screen from the resnap.
        if (desktopFilter > 0 && virtualDesktop != 0) {
            int screenDesktop = screenDesktopMemo.value(screenId, 0);
            if (screenDesktop <= 0) {
                const int vdmDesktop =
                    m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktopForScreen(screenId) : 0;
                screenDesktop = vdmDesktop > 0 ? vdmDesktop : desktopFilter;
                screenDesktopMemo.insert(screenId, screenDesktop);
            }
            if (virtualDesktop != screenDesktop)
                return;
        }

        // Dedup on the CANONICAL key. The live pass feeds this canonical store
        // keys while the durable pass below feeds each record's own composite,
        // and for a class-mutating window (its store key is the first-seen
        // canonical, its record carries the current composite) those two
        // spellings differ — so a raw compare missed the duplicate and the
        // window contributed two resnap rows.
        const QString dedupKey = canonicalizeForLookup(windowId);
        if (addedIds.contains(dedupKey))
            return;
        addedIds.insert(dedupKey);

        // Look up the zone position from the global map
        const QString& primaryZoneId = zoneIds.first();
        int position = globalZoneIdToPosition.value(primaryZoneId, 0);
        if (position <= 0)
            return;

        ResnapEntry entry;
        entry.windowId = windowId;
        entry.zonePosition = position;
        entry.screenId = screenId;
        entry.virtualDesktop = virtualDesktop;
        newBuffer.append(entry);
    };

    // 1. Live snap assignments — this session's snaps (retained while a window is
    // autotiled, which is why the non-restart autotile→snap swap finds them here).
    // Per-state visitation: each window's screen/desktop come from the store that
    // owns it, never from a cross-store flat-map join.
    forEachZoneAssignedWindow(
        [&](const QString& windowId, const QStringList& zoneIds, const QString& screenId, int desktop) {
            addCandidate(windowId, zoneIds, screenId, desktop);
        });

    // 2. Restart-robustness: a window snapped in a PRIOR session and then autotiled
    // has its snap zones only in the durable WindowPlacement record — the live
    // snap store map above is cold after a daemon restart. Without this pass an
    // autotile→snapping swap right after a restart resnaps nothing (empty buffer →
    // no applyGeometriesBatch → the effect never marks the windows snapped, so the
    // per-mode snap border / title-bar appearance is never applied). Mirrors the
    // live-or-durable fallback in recordedSnapZones().
    for (const PhosphorEngine::WindowPlacement& rec : m_placementStore.records()) {
        // Canonical, matching what addCandidate inserts — see its own note.
        if (addedIds.contains(canonicalizeForLookup(rec.windowId)))
            continue;
        // Liveness gate: the store persists records of long-closed windows
        // (session history), and a dead-id entry in the resnap batch is NOT
        // inert — the compositor's appId fallback resolves it onto the one
        // live unclaimed window of the same app and teleports it to the dead
        // record's zone. The restart case this pass exists for survives the
        // gate: a daemon restart keeps the compositor-issued window ids, so
        // the re-announced live windows match their records exactly. No
        // registry wired (test envs) keeps the historical permissive path.
        // One spelling of liveness for the whole file: the store's probe is
        // the registry lookup plus the bare-id refusal, and a bare id would
        // otherwise read the whole string as an instance.
        if (m_windowRegistry && !m_placementStore.isLiveInstance(rec.windowId))
            continue;
        const PhosphorEngine::EngineSlot snapSlot = rec.slotFor(PhosphorEngine::WindowPlacement::snapEngineId());
        if (snapSlot.state != PhosphorEngine::WindowPlacement::stateSnapped())
            continue;
        // A multi-desktop record answers for the desktop its screen shows,
        // stamped with that desktop so the filter above keeps it.
        int desktop = rec.virtualDesktop;
        if (!snapSlot.zonesByDesktop.isEmpty() && m_virtualDesktopManager) {
            const int shown = m_virtualDesktopManager->currentDesktopForScreen(rec.screenId);
            if (shown > 0)
                desktop = shown;
        }
        addCandidate(rec.windowId, snapZonesOnDesktopInView(snapSlot, rec.screenId), rec.screenId, desktop);
    }

    // Skipping is not enough for a departed window: its membership stays in
    // the old screen's store, and zone occupancy reads every store, so once
    // that screen snaps again the window would count as an occupant of a zone
    // on a monitor it is not on. A window is on one screen, and the tiling
    // engine holding it elsewhere is the truth, so the snap engine forgets it.
    for (const QString& windowId : std::as_const(departed)) {
        qCInfo(lcPlacement) << "Resnap buffer: skipping" << windowId << "held by a tiling engine on"
                            << tilingHeldScreenForWindow(windowId) << "- forgetting its snap assignment";
        if (m_snapResolver.forgetWindow) {
            m_snapResolver.forgetWindow(windowId);
        }
    }

    if (!newBuffer.isEmpty()) {
        m_resnapBuffer = std::move(newBuffer);
        qCInfo(lcPlacement) << "Resnap buffer (all screens):" << m_resnapBuffer.size() << "windows";
    }
}

QStringList WindowTrackingService::buildZoneOrderedWindowList(const QString& screenId) const
{
    if (!m_layoutManager) {
        return {};
    }

    // Get the current layout to resolve zone numbers
    PhosphorZones::Layout* layout = m_layoutManager->resolveLayoutForScreen(screenId);
    if (!layout || layout->zoneCount() == 0) {
        return {};
    }

    // Build zone UUID → zone number lookup (braced-string keys, matching the
    // braced zone ids the assignments store)
    const QVector<PhosphorZones::Zone*> zones = layout->zones();
    QHash<QString, int> zoneNumberMap;
    for (PhosphorZones::Zone* zone : zones) {
        zoneNumberMap[zone->id().toString()] = zone->zoneNumber();
    }

    // Collect (zoneNumber, insertionIndex, windowId) for windows on this screen.
    // Screen assignments may store connector names or EDID-based screen IDs
    // depending on the code path. Use screensMatch() for format-agnostic comparison.

    // This list SEEDS the autotile state for (screenId, CURRENT virtual desktop).
    // Snap assignments are screen-keyed but desktop-agnostic, so the same screen
    // can hold windows snapped on a DIFFERENT desktop (e.g. screen S snaps on VD1
    // and autotiles on VD2 via per-desktop rules). Those off-desktop windows must
    // NOT be pulled into this desktop's autotile state — doing so eagerly inserts
    // and tiles a window that lives on another desktop, overwriting its snap
    // geometry there (switching to the autotile desktop would corrupt the snap
    // desktop's window positions). Scope to the current desktop; desktop==0
    // (sticky / unknown) stays desktop-agnostic and is kept. Mirrors the
    // desktopFilter guard in populateResnapBufferForAllScreens (addCandidate).
    const int currentDesktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktopForScreen(screenId) : 0;

    int insertionIdx = 0;
    QVector<std::tuple<int, int, QString>> windowsByZone; // (zoneNum, insertionIdx, windowId)
    // Per-window dedup. The single-owning-store invariant makes duplicates
    // impossible today, but a duplicate here would double-seed the autotile
    // order (double-tile), so the guard is kept.
    //
    // Not identical to populateResnapBufferForAllScreens' addedIds, which
    // keys on canonicalizeForLookup: this pass has a single source, so the
    // raw id cannot spell the same window two ways.
    QSet<QString> seenWindowIds;
    forEachZoneAssignedWindow(
        [&](const QString& windowId, const QStringList& zoneIds, const QString& windowScreen, int windowDesktop) {
            if (!PhosphorScreens::ScreenIdentity::screensMatch(windowScreen, screenId)) {
                return;
            }
            if (!desktopMatchesFilter(windowDesktop, currentDesktop)) {
                return;
            }
            if (seenWindowIds.contains(windowId)) {
                return;
            }
            seenWindowIds.insert(windowId);
            // Skip floating windows — the user's manual-mode float choice is
            // preserved across the transition.
            //
            // SNAP's own bit, not the mode-routed read. This list is the
            // snapping→tiling SEED source, and by the time it is built the
            // screen's mode has already flipped to the DESTINATION engine, so
            // the routed read would let the destination engine's own float bit
            // decide what enters its own seed order — a transition-path read
            // must be a SOURCE-mode read. The seed filter downstream applies
            // the destination engine's per-engine rule separately.
            const PhosphorSnapEngine::SnapState* snap = snapForWindow(windowId);
            if (snap && snap->isFloating(windowId)) {
                return;
            }
            if (zoneIds.isEmpty()) {
                return;
            }

            // Use primary zone's zone number
            auto numIt = zoneNumberMap.constFind(zoneIds.first());
            if (numIt != zoneNumberMap.constEnd()) {
                windowsByZone.append({numIt.value(), insertionIdx++, windowId});
            } else {
                qCWarning(lcPlacement) << "buildZoneOrderedWindowList: zone UUID" << zoneIds.first() << "for window"
                                       << windowId << "not found in layout - skipping";
            }
        });

    // Sort by zone number ascending, preserving iteration order as tie-breaker
    std::stable_sort(windowsByZone.begin(), windowsByZone.end(), [](const auto& a, const auto& b) {
        if (std::get<0>(a) != std::get<0>(b))
            return std::get<0>(a) < std::get<0>(b);
        return std::get<1>(a) < std::get<1>(b); // preserve iteration order
    });

    QStringList result;
    result.reserve(windowsByZone.size());
    for (const auto& entry : windowsByZone) {
        result.append(std::get<2>(entry));
    }

    qCDebug(lcPlacement) << "buildZoneOrderedWindowList for" << screenId << ":" << result;
    return result;
}

// calculateSnapAllWindows moved to SnapEngine (libs/phosphor-snap-engine/src/navigation.cpp).

// ═══════════════════════════════════════════════════════════════════════════════
// Resolution Change Handling
// ═══════════════════════════════════════════════════════════════════════════════

QHash<QString, QRect> WindowTrackingService::updatedWindowGeometries() const
{
    QHash<QString, QRect> result;

    if (!m_config.keepWindowsInZonesOnResolutionChange) {
        return result;
    }

    forEachZoneAssignedWindow(
        [&](const QString& windowId, const QStringList& zoneIds, const QString& screenId, int /*desktop*/) {
            if (zoneIds.isEmpty()) {
                return;
            }
            QRect geo = resolveZoneGeometry(zoneIds, screenId);
            if (geo.isValid()) {
                result[windowId] = geo;
            }
        });

    return result;
}

QHash<QString, QList<WindowTrackingService::PendingRestoreTarget>>
WindowTrackingService::pendingRestoreGeometries() const
{
    QHash<QString, QList<PendingRestoreTarget>> result;

    // Source the effect's instant-restore cache from the unified placement store:
    // every snapped WindowPlacement of a closed window, resolved to its zone
    // geometry, grouped by appId. The async resolveWindowRestore re-validates and
    // corrects, so this is a best-effort anti-flash fast path (an invalid/stale
    // zone resolves to an empty rect and is skipped). Each app's list is ordered
    // NEWEST record first, because that is the record the daemon hands the
    // first opener: claimForOpen reserves the newest unclaimed record and the
    // engine's take() then consumes exactly the claimed one, so a cache that
    // teleported the first opener into the OLDEST record's zone was corrected
    // a moment later by the resolve, the flash-then-move this cache exists to
    // prevent. The effect keeps the whole list so a record it can see is
    // still open (daemon-only restart, before re-announce) costs nothing.
    QHash<QString, QList<quint64>> sequences;
    for (const PhosphorEngine::WindowPlacement& p : m_placementStore.records()) {
        const PhosphorEngine::EngineSlot snapSlot = p.slotFor(PhosphorEngine::WindowPlacement::snapEngineId());
        if (snapSlot.state != PhosphorEngine::WindowPlacement::stateSnapped()) {
            continue;
        }
        const QStringList zoneIds = snapSlot.zoneIds;
        if (zoneIds.isEmpty() || p.appId.isEmpty()) {
            continue;
        }
        // A record whose window is still open is that window's, not a pending
        // restore: served through the appId-keyed cache it teleported the
        // next same-app window into the open sibling's zone. Answers false
        // without a registry (daemon-only restart, before the effect has
        // re-announced), which is why the target also carries the window id
        // for the effect to check against what it can see.
        if (m_placementStore.isLiveInstance(p.windowId)) {
            continue;
        }

        const QString screenId = resolveEffectiveScreenId(p.screenId);
        // Per-output virtual desktops (#648): validate the record against ITS
        // screen's current desktop, not the global current.
        const int currentDesktop =
            m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktopForScreen(screenId) : 0;

        // Skip screens currently in autotile mode — autotile owns placement there
        // and would otherwise fight a stale snap teleport. Both context
        // dimensions come from the RECORD (mirrors the cross-engine claim
        // gates, which key desktop AND activity off the record so the
        // engines reach identical verdicts).
        if (m_layoutManager
            && m_layoutManager->modeForScreen(screenId, p.virtualDesktop, p.activity)
                != PhosphorZones::AssignmentEntry::Mode::Snapping) {
            continue;
        }

        // Validate desktop context (sticky-0 windows pass; unknown current passes).
        if (!desktopMatchesFilter(p.virtualDesktop, currentDesktop)) {
            continue;
        }

        // The zone must belong to the layout the record's context currently
        // runs, the same gate the async resolver applies (#1104): zoneGeometry
        // finds a zone in ANY loaded layout, and a record naming a zone of a
        // layout no longer assigned there teleported the window onto that
        // ghost zone before the resolver moved it again.
        if (!PhosphorZones::LayoutUtils::contextLayoutHoldsZones(m_layoutManager, screenId, p.virtualDesktop,
                                                                 p.activity, zoneIds)) {
            continue;
        }

        const QRect geo = resolveZoneGeometry(zoneIds, screenId);
        if (!geo.isValid()) {
            continue;
        }
        // Insert in descending sequence order (newest first).
        QList<PendingRestoreTarget>& targets = result[p.appId];
        QList<quint64>& order = sequences[p.appId];
        int at = 0;
        while (at < order.size() && order.at(at) > p.sequence) {
            ++at;
        }
        order.insert(at, p.sequence);
        targets.insert(at, PendingRestoreTarget{geo, screenId, p.windowId});
    }

    return result;
}

} // namespace PhosphorPlacement

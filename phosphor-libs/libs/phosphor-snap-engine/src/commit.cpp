// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorSnapEngine/INavigationStateProvider.h>
#include <PhosphorEngine/IWindowRegistry.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include "snapenginelogging.h"

#include <QGuiApplication>
#include <QScreen>

#include <algorithm>

namespace PhosphorSnapEngine {

using PhosphorEngine::SnapIntent;
using PhosphorEngine::ZoneAssignmentEntry;

namespace {

bool hasEmptyZone(const QStringList& zoneIds)
{
    return std::any_of(zoneIds.cbegin(), zoneIds.cend(), [](const QString& id) {
        return id.isEmpty();
    });
}

/// The screen whose geometry sits nearest @p point: @p mgr's effective screens,
/// so a split monitor answers a virtual screen, else the QScreens. Empty when
/// there is nothing to measure against.
QString nearestScreenTo(PhosphorScreens::ScreenManager* mgr, const QPoint& point)
{
    QString nearest;
    qint64 bestDistSq = -1;
    const auto consider = [&](const QString& id, const QRect& g) {
        if (id.isEmpty() || !g.isValid()) {
            return;
        }
        // QRect::right() and bottom() are the last pixel inside, so the far
        // edge proper is x + width; measuring to it keeps a point on the edge
        // at distance 0 rather than 1.
        const qint64 dx = qMax(0, qMax(g.left() - point.x(), point.x() - (g.x() + g.width())));
        const qint64 dy = qMax(0, qMax(g.top() - point.y(), point.y() - (g.y() + g.height())));
        const qint64 distSq = dx * dx + dy * dy;
        if (bestDistSq < 0 || distSq < bestDistSq) {
            bestDistSq = distSq;
            nearest = id;
        }
    };
    if (mgr) {
        for (const QString& id : mgr->effectiveScreenIds()) {
            consider(id, mgr->screenGeometry(id));
        }
    } else {
        for (QScreen* screen : QGuiApplication::screens()) {
            consider(PhosphorScreens::ScreenIdentity::identifierFor(screen), screen->geometry());
        }
    }
    return nearest;
}

} // namespace

void SnapEngine::commitSnapImpl(const QString& windowId, const QStringList& zoneIds, const QString& screenId,
                                SnapIntent intent, int virtualDesktop)
{
    Q_ASSERT(m_globals);
    if (!m_globals) {
        return;
    }
    // Guarded locally like m_globals, per the ctor contract: this is public
    // API, and the clearFloatingForSnap deref below is unconditional, so a
    // stub-dependency engine crashed here in release while the assert above
    // only covered the other dependency.
    Q_ASSERT(m_windowTracker);
    if (!m_windowTracker) {
        qCWarning(PhosphorSnapEngine::lcSnapEngine) << "commitSnapImpl: no window tracker for" << windowId;
        return;
    }
    Q_ASSERT(!zoneIds.isEmpty());
    if (Q_UNLIKELY(zoneIds.isEmpty())) {
        qCWarning(PhosphorSnapEngine::lcSnapEngine) << "commitSnapImpl: empty zoneIds for" << windowId;
        return;
    }
    const QString& primaryZoneId = zoneIds.first();

    // The broadcast is gated on EITHER float verdict, not the routed one
    // alone. clearFloatingForSnap reads the mode-routed isWindowFloating,
    // which on a screen mid-flip answers the FOREIGN engine's bit — false —
    // while snap's OWN bit is true and the zone assignment below clears it
    // silently. Subscribers that last heard "floating" (the adaptor's float
    // bookkeeping, the effect's per-screen float cache) then keep stale float
    // chrome on a window snap just committed to a zone. The routed call still
    // runs for its own bookkeeping; when only the own bit was set, the
    // pre-float capture is cleared to match the normal path.
    const bool ownFloating = isFloating(windowId);
    const bool routedCleared = m_windowTracker->clearFloatingForSnap(windowId);
    if (ownFloating && !routedCleared) {
        m_windowTracker->clearPreFloatZone(windowId);
    }
    if (ownFloating || routedCleared) {
        Q_EMIT windowFloatingClearedForSnap(windowId, screenId);
    }

    // A pinned desktop (virtualDesktop >= 1) is preserved as-is: RouteToDesktop
    // placements pin their destination desktop, and resnap batch entries pin each
    // window's recorded desktop (ZoneAssignmentEntry::virtualDesktop) so a
    // cross-desktop batch never re-stamps an off-desktop window. Unpinned (0)
    // commits record on the window's current desktop — which means a window whose
    // RECORDED desktop was 0 (all-desktops) re-records on the current desktop when
    // a resnap batch re-commits it; desktop-0 stickiness is not round-tripped here
    // (sticky visibility itself is separate WTS state), matching the pre-batch
    // behaviour. Tracking the right desktop keeps zone occupancy, snap-assist,
    // and empty-zone detection correct on both the source and destination desktops.
    const int assignmentDesktop = virtualDesktop >= 1 ? virtualDesktop : currentVirtualDesktopForScreen(screenId);

    // A RE-STATEMENT of the zones the window already holds in that context
    // (a membership reapply, a virtual-screen resnap: system resnap batches
    // commit UserInitiated) is not the user snapping it, so it neither clears
    // the auto-snapped flag nor records a last-used zone (F456).
    // The store the assignment below lands in: the pinned key, or the
    // screen's current one when no desktop is known.
    const SnapState* target = m_states.stateForKey(
        assignmentDesktop >= 1 ? PhosphorEngine::PlacementStateKey{screenId, assignmentDesktop, currentActivity()}
                               : currentKeyForScreen(screenId));
    const bool restatement = target && target->zonesForWindow(canonicalWindowId(windowId)) == zoneIds;
    const bool userSnap = intent == SnapIntent::UserInitiated && !restatement;
    if (userSnap) {
        m_windowTracker->clearAutoSnapped(windowId);
    }

    if (zoneIds.size() > 1) {
        m_windowTracker->assignWindowToZones(windowId, zoneIds, screenId, assignmentDesktop);
    } else {
        m_windowTracker->assignWindowToZone(windowId, primaryZoneId, screenId, assignmentDesktop);
    }

    // The last-used zone belongs to the desktop the screen shows: a commit
    // pinned to a desktop not in view (a membership carry, a background
    // replay) would write it into the in-view desktop's store (F409). A
    // user's first snap of an auto-restored window counts like any other
    // (F209).
    if (userSnap && assignmentDesktop == currentVirtualDesktopForScreen(screenId)) {
        const QString windowClass = m_windowTracker->currentAppIdFor(windowId);
        m_windowTracker->updateLastUsedZone(primaryZoneId, screenId, windowClass, assignmentDesktop);
    }

    qCInfo(PhosphorSnapEngine::lcSnapEngine)
        << "commitSnap:" << windowId << "zones=" << zoneIds << "screen=" << screenId << "intent="
        << (intent == SnapIntent::UserInitiated ? "user" : (intent == SnapIntent::AutoRestored ? "auto" : "replaced"));

    Q_EMIT windowSnapStateChanged(windowId,
                                  PhosphorProtocol::WindowStateEntry{windowId, primaryZoneId, screenId, false,
                                                                     QStringLiteral("snapped"), zoneIds, false});

    // Focus-new-windows: activate a window that was just auto-placed into a zone
    // because it genuinely opened. Only AutoRestored asks for it: the D-Bus restore
    // facade commits AutoReplaced for every other reason (a restart or pending
    // sweep, an unminimize, a desktop arrival), and manual drag, keyboard snap,
    // snap-all, unfloat and navigation commit UserInitiated, so all of those keep
    // KWin's focus. Only for a commit onto the desktop its screen shows: a commit
    // pinned to another desktop (RouteToDesktop, a background-desktop restore)
    // would activate a window the user cannot see. Emitted immediately, unlike
    // autotile's deferred focus, because snap has no post-commit raise loop to
    // bury it; this stays the single chokepoint for single- and multi-zone commits.
    if (intent == SnapIntent::AutoRestored && assignmentDesktop == currentVirtualDesktopForScreen(screenId)) {
        if (auto* settings = snapSettings(); settings && settings->focusNewWindows()) {
            Q_EMIT activateWindowRequested(windowId);
        }
    }
}

void SnapEngine::commitSnap(const QString& windowId, const QString& zoneId, const QString& screenId, SnapIntent intent,
                            int virtualDesktop)
{
    if (windowId.isEmpty() || zoneId.isEmpty()) {
        qCWarning(PhosphorSnapEngine::lcSnapEngine) << "commitSnap: empty windowId or zoneId";
        return;
    }
    commitSnapImpl(windowId, QStringList{zoneId}, screenId, intent, virtualDesktop);
}

void SnapEngine::commitMultiZoneSnap(const QString& windowId, const QStringList& zoneIds, const QString& screenId,
                                     SnapIntent intent, int virtualDesktop)
{
    // Every member, not only the primary: an empty one would be committed and
    // broadcast as a zone id (F459).
    if (windowId.isEmpty() || zoneIds.isEmpty() || hasEmptyZone(zoneIds)) {
        qCWarning(PhosphorSnapEngine::lcSnapEngine) << "commitMultiZoneSnap: empty windowId or zone id" << zoneIds;
        return;
    }
    commitSnapImpl(windowId, zoneIds, screenId, intent, virtualDesktop);
}

void SnapEngine::recordFreeFrameBeforeUserSnap(const QString& windowId, const QString& screenId)
{
    if (!m_windowTracker || !m_navState || windowId.isEmpty() || screenId.isEmpty()) {
        return;
    }
    // No floating gate: a free window snap does not hold floating (one a
    // screen change unsnapped, one snap never tracked) has a free frame too.
    // A window in a zone or on a tile is refused at the write point, by the
    // zone, tile and managed-frame refusals of recordFreeGeometry.
    //
    // The frame is a sample, so the window-state refusals live here: a
    // minimized window's frame is the hidden rect, a minimize-suspended float
    // still stands on the zone it is suspended from (F156), and a maximized
    // or fullscreen window fills the output.
    if (m_windowRegistry
        && (m_windowRegistry->minimizedState(windowId).value_or(false)
            || m_windowRegistry->fillsOutputState(windowId).value_or(false))) {
        return;
    }
    if (m_windowTracker->isSuspensionFloat(windowId)) {
        return;
    }
    const QRect frame = m_navState->frameGeometry(windowId);
    if (!frame.isValid()) {
        return;
    }
    // Filed under the screen the frame is on, which a cross-screen snap is
    // leaving and the snap's tracked screen may not name; @p screenId only
    // when no screen manager can place it (F23). A mismatch is
    // recordFreeGeometry's to refuse, which it logs (F641). Overwrite, like
    // toggleFocusedFloat: the live frame is the most recent free position.
    PhosphorScreens::ScreenManager* const mgr = m_windowTracker->screenManager();
    const QString frameScreen = mgr ? mgr->effectiveScreenAt(frame.center()) : QString();
    m_windowTracker->recordFreeGeometry(windowId, frameScreen.isEmpty() ? screenId : frameScreen, frame,
                                        /*overwrite=*/true);
}

void SnapEngine::commitUserSnap(const QString& windowId, const QStringList& zoneIds, const QString& screenId,
                                const QRect& geometry, const QString& captureScreen, int virtualDesktop)
{
    if (!m_windowTracker || windowId.isEmpty() || zoneIds.isEmpty() || !geometry.isValid()) {
        qCWarning(PhosphorSnapEngine::lcSnapEngine)
            << "commitUserSnap: refused for" << windowId << "zones=" << zoneIds << "geometry=" << geometry;
        return;
    }
    if (!captureScreen.isEmpty()) {
        recordFreeFrameBeforeUserSnap(windowId, captureScreen);
    }
    if (zoneIds.size() > 1) {
        commitMultiZoneSnap(windowId, zoneIds, screenId, SnapIntent::UserInitiated, virtualDesktop);
    } else {
        commitSnap(windowId, zoneIds.first(), screenId, SnapIntent::UserInitiated, virtualDesktop);
    }
    m_windowTracker->recordSnapIntent(windowId, true);
    Q_EMIT applyGeometryRequested(windowId, geometry.x(), geometry.y(), geometry.width(), geometry.height(),
                                  zoneIds.first(), screenId, false);
}

void SnapEngine::uncommitSnap(const QString& windowId)
{
    Q_ASSERT(m_globals);
    if (!m_globals) {
        return;
    }
    // Guarded locally like m_globals, per the ctor contract: the tracker derefs
    // below are unconditional, and a stub-dependency engine would crash here in
    // release where the assert compiles out.
    Q_ASSERT(m_windowTracker);
    if (!m_windowTracker) {
        qCWarning(PhosphorSnapEngine::lcSnapEngine) << "uncommitSnap: no window tracker for" << windowId;
        return;
    }
    if (windowId.isEmpty()) {
        return;
    }

    const QString previousZoneId = zoneForWindow(windowId);
    if (previousZoneId.isEmpty()) {
        qCDebug(PhosphorSnapEngine::lcSnapEngine) << "uncommitSnap: window not in any zone:" << windowId;
        return;
    }

    m_windowTracker->unassignWindow(windowId);

    qCInfo(PhosphorSnapEngine::lcSnapEngine) << "uncommitSnap:" << windowId << "from zone" << previousZoneId;

    Q_EMIT windowSnapStateChanged(windowId,
                                  PhosphorProtocol::WindowStateEntry{windowId, QString(), QString(), false,
                                                                     QStringLiteral("unsnapped"), QStringList{},
                                                                     false});
}

PhosphorProtocol::WindowGeometryList SnapEngine::applyBatchAssignments(const QVector<ZoneAssignmentEntry>& entries,
                                                                       SnapIntent intent,
                                                                       std::function<QString()> fallbackScreenResolver)
{
    PhosphorProtocol::WindowGeometryList geometries;
    if (entries.isEmpty()) {
        return geometries;
    }

    // Every non-restore entry below routes through commitSnap/commitMultiZoneSnap,
    // which require a live engine. Guard the whole batch symmetrically with
    // commitSnapImpl/uncommitSnap rather than doing the full per-entry resolution
    // pass against a half-dead engine.
    Q_ASSERT(m_globals);
    if (!m_globals) {
        return geometries;
    }
    Q_ASSERT(m_windowTracker);
    if (!m_windowTracker) {
        qCWarning(PhosphorSnapEngine::lcSnapEngine) << "applyBatchAssignments: no window tracker";
        return geometries;
    }

    auto* mgr = m_windowTracker->screenManager();

    // Each geometry entry carries the screen its commit resolved, so the
    // compositor never re-derives it from geometry.center() against its
    // (possibly stale) m_virtualScreenDefs, which races with VS swap/rotate and
    // produces spurious cross-VS unsnap events. An entry that commits nothing
    // sends no geometry either (F459).
    geometries.reserve(entries.size());
    for (const auto& entry : entries) {
        if (entry.targetZoneId == PhosphorEngine::RestoreSentinel) {
            // uncommitSnap already dereferences m_windowTracker unconditionally
            // (production deps are non-null; tests never reach this path with a
            // null tracker), so no separate guard is needed here.
            uncommitSnap(entry.windowId);
            // Screen-scoped when a screen is resolvable: this restore
            // consumes ONE screen's float-back, and the all-screens form
            // destroyed the position remembered for every other monitor.
            // The empty-screen escalation to the wholesale clear is the
            // pre-existing fallback for an unresolvable entry.
            QString restoreScreen = entry.targetScreenId;
            if (restoreScreen.isEmpty() && mgr && entry.targetGeometry.isValid()) {
                restoreScreen = mgr->effectiveScreenAt(entry.targetGeometry.center());
            }
            m_windowTracker->clearFreeGeometry(entry.windowId, restoreScreen);
            // Applied with an empty screen: the window returns to free
            // floating, so no tracked-screen seeding should override the
            // compositor's geometry-based resolution for it.
            geometries.append(
                PhosphorProtocol::WindowGeometryEntry::fromRect(entry.windowId, entry.targetGeometry, QString()));
            continue;
        }
        if (entry.windowId.isEmpty() || entry.targetZoneId.isEmpty() || hasEmptyZone(entry.targetZoneIds)) {
            qCWarning(PhosphorSnapEngine::lcSnapEngine)
                << "applyBatchAssignments: skipping an entry with an empty zone for" << entry.windowId;
            continue;
        }

        QString screenId = entry.targetScreenId;
        const QPoint center = entry.targetGeometry.center();
        if (screenId.isEmpty() && mgr) {
            screenId = mgr->effectiveScreenAt(center);
        }
        if (screenId.isEmpty()) {
            for (QScreen* screen : QGuiApplication::screens()) {
                if (screen->geometry().contains(center)) {
                    screenId = PhosphorScreens::ScreenIdentity::identifierFor(screen);
                    break;
                }
            }
        }
        if (screenId.isEmpty() && fallbackScreenResolver) {
            screenId = fallbackScreenResolver();
        }
        if (screenId.isEmpty()) {
            // Last resort: a real (non-restore) snap commit MUST carry a
            // non-empty screenId. The compositor treats an empty screenId on a
            // batch entry as the float/restore marker, so a real commit that
            // resolved to nothing would be misclassified as a float and lose
            // its snap border/title-bar tracking. The screen nearest the target
            // center (off-screen, pre-attach) beats an arbitrary one; with no
            // screen to measure against the entry is skipped (F899).
            screenId = nearestScreenTo(mgr, center);
            qCWarning(PhosphorSnapEngine::lcSnapEngine)
                << "applyBatchAssignments: last-resort nearest-screen heuristic fired for" << entry.windowId
                << "center=" << center << "resolved screen=" << screenId;
            if (screenId.isEmpty()) {
                continue;
            }
        }

        if (entry.targetZoneIds.size() > 1) {
            commitMultiZoneSnap(entry.windowId, entry.targetZoneIds, screenId, intent, entry.virtualDesktop);
        } else {
            commitSnap(entry.windowId, entry.targetZoneId, screenId, intent, entry.virtualDesktop);
        }
        geometries.append(
            PhosphorProtocol::WindowGeometryEntry::fromRect(entry.windowId, entry.targetGeometry, screenId));
    }
    return geometries;
}

} // namespace PhosphorSnapEngine

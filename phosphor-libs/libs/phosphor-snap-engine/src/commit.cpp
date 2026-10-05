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

namespace PhosphorSnapEngine {

using PhosphorEngine::SnapIntent;
using PhosphorEngine::ZoneAssignmentEntry;

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
    if (windowId.isEmpty() || zoneIds.isEmpty() || zoneIds.first().isEmpty()) {
        qCWarning(PhosphorSnapEngine::lcSnapEngine) << "commitMultiZoneSnap: empty windowId or zoneIds";
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

    // Resolve and remember per-entry screenId in a single pass so the geometry
    // payload below can carry it to the compositor. Without this, the wire
    // entry is built from entry.targetGeometry alone and the compositor must
    // re-derive the screen via geometry.center() against its (possibly stale)
    // m_virtualScreenDefs — which races with VS swap/rotate and produces
    // spurious cross-VS unsnap events.
    QVector<QString> resolvedScreens;
    resolvedScreens.reserve(entries.size());

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
            resolvedScreens.append(QString());
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
            // batch entry as the float/restore marker (only the RestoreSentinel
            // branch above legitimately emits empty), so a real commit that
            // resolved to nothing here would be misclassified as a float and
            // lose its snap border/title-bar tracking. Pick the screen whose
            // geometry sits nearest the target center (the window's intended
            // position) — a better heuristic than an arbitrary primary screen
            // when the center lands on no known screen (off-screen, pre-attach)
            // and no fallbackScreenResolver was supplied. Falls back to the
            // primary screen only if there are no screens to measure against.
            QScreen* nearest = nullptr;
            qint64 bestDistSq = -1;
            for (QScreen* screen : QGuiApplication::screens()) {
                const QRect g = screen->geometry();
                // Use the half-open far edges (x + width / y + height) rather than
                // QRect::right()/bottom() (which return x + width - 1): the latter's
                // off-by-one scores a point sitting exactly on a screen's far edge as
                // 1px outside it.
                const qint64 dx = qMax(0, qMax(g.left() - center.x(), center.x() - (g.x() + g.width())));
                const qint64 dy = qMax(0, qMax(g.top() - center.y(), center.y() - (g.y() + g.height())));
                const qint64 distSq = dx * dx + dy * dy;
                if (bestDistSq < 0 || distSq < bestDistSq) {
                    bestDistSq = distSq;
                    nearest = screen;
                }
            }
            if (!nearest) {
                nearest = QGuiApplication::primaryScreen();
            }
            if (nearest) {
                screenId = PhosphorScreens::ScreenIdentity::identifierFor(nearest);
            }
            qCWarning(PhosphorSnapEngine::lcSnapEngine)
                << "applyBatchAssignments: last-resort nearest-screen heuristic fired for" << entry.windowId
                << "center=" << center << "resolved screen=" << screenId;
        }

        if (entry.targetZoneIds.size() > 1) {
            commitMultiZoneSnap(entry.windowId, entry.targetZoneIds, screenId, intent, entry.virtualDesktop);
        } else {
            commitSnap(entry.windowId, entry.targetZoneId, screenId, intent, entry.virtualDesktop);
        }
        resolvedScreens.append(screenId);
    }

    geometries.reserve(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        // Restore sentinels carry their pre-tile geometry and get applied like
        // any other entry — but with empty screenId, since the window is being
        // returned to free-floating state and no tracked-screen seeding should
        // override the compositor's geometry-based resolution for it.
        geometries.append(PhosphorProtocol::WindowGeometryEntry::fromRect(entry.windowId, entry.targetGeometry,
                                                                          resolvedScreens.value(i)));
    }
    return geometries;
}

} // namespace PhosphorSnapEngine

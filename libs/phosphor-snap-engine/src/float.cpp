// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorSnapEngine/ISnapSettings.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>
#include "snapenginelogging.h"

namespace PhosphorSnapEngine {

using PhosphorEngine::SnapIntent;
using PhosphorEngine::UnfloatResult;

// ═══════════════════════════════════════════════════════════════════════════════
// Float toggle / set
// ═══════════════════════════════════════════════════════════════════════════════

void SnapEngine::toggleWindowFloat(const QString& windowId, const QString& screenId)
{
    const bool currentlyFloating = m_snapState->isFloating(windowId);
    const bool currentlySnapped = m_snapState->isWindowSnapped(windowId);

    if (!currentlyFloating && !currentlySnapped) {
        return;
    }

    if (currentlyFloating) {
        if (!unfloatToZone(windowId, screenId)) {
            Q_EMIT navigationFeedback(false, QStringLiteral("float"), QStringLiteral("no_pre_float_zone"), QString(),
                                      QString(), screenId);
            return;
        }
        Q_EMIT navigationFeedback(true, QStringLiteral("float"), QStringLiteral("unfloated"), QString(), QString(),
                                  screenId);
    } else {
        m_windowTracker->unsnapForFloat(windowId);
        m_windowTracker->setWindowFloating(windowId, true);
        Q_EMIT windowFloatingChanged(windowId, true, screenId);
        applyGeometryForFloat(windowId, screenId);
        Q_EMIT navigationFeedback(true, QStringLiteral("float"), QStringLiteral("floated"), QString(), QString(),
                                  screenId);
    }
}

void SnapEngine::setWindowFloat(const QString& windowId, bool shouldFloat)
{
    // IPlacementEngine::setWindowFloat has no screenId param, so resolve it:
    // 1. Try the window's tracked screen from WTS (most accurate)
    // 2. Fall back to m_lastActiveScreenId (from last windowFocused)
    // 3. Fall back to empty (unfloatToZone/applyGeometryForFloat handle gracefully)
    QString screenId = m_snapState->screenForWindow(windowId);
    if (screenId.isEmpty()) {
        screenId = m_lastActiveScreenId;
    }
    if (screenId.isEmpty()) {
        qCDebug(PhosphorSnapEngine::lcSnapEngine)
            << "setWindowFloat: no screen context for" << windowId << "- using empty screenId";
    }

    if (shouldFloat) {
        m_windowTracker->unsnapForFloat(windowId);
        m_windowTracker->setWindowFloating(windowId, true);
        Q_EMIT windowFloatingChanged(windowId, true, screenId);
        applyGeometryForFloat(windowId, screenId);
    } else {
        if (!unfloatToZone(windowId, screenId)) {
            // No pre-float zone to restore to — keep the window floating rather than
            // leaving it in a limbo state (not floating, not snapped to any zone).
            qCDebug(PhosphorSnapEngine::lcSnapEngine)
                << "setWindowFloat: cannot unfloat" << windowId << "- no pre-float zone, keeping floating";
            return;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Private helpers
// ═══════════════════════════════════════════════════════════════════════════════

bool SnapEngine::unfloatToZone(const QString& windowId, const QString& screenId)
{
    // Highest-priority un-float target: a matched SnapToZone rule. Toggling a
    // window out of float lands it in the rule's zones, not a stale pre-float
    // zone, so the rule stays authoritative for both open and Meta+F. Falls
    // through to the pre-float / fallback zone when no rule matches.
    {
        const PhosphorEngine::SnapResult ruleSnap =
            calculateSnapToPlacementRule(windowId, screenId, /*isSticky=*/false);
        if (ruleSnap.shouldSnap && !ruleSnap.zoneIds.isEmpty()) {
            // Forward the routed desktop (RouteToDesktop): calculateSnapToPlacementRule
            // resolved the zones against ruleSnap.virtualDesktop's layout, so the commit
            // must record the assignment on that same desktop — otherwise a
            // SnapToZone + RouteToDesktop rule lands zones from the routed desktop's
            // layout under the current desktop. Mirrors the open path (lifecycle.cpp);
            // 0 ⇒ current desktop, the historical behaviour for unrouted rules.
            if (ruleSnap.zoneIds.size() > 1) {
                commitMultiZoneSnap(windowId, ruleSnap.zoneIds, ruleSnap.screenId, SnapIntent::UserInitiated,
                                    ruleSnap.virtualDesktop);
            } else {
                commitSnap(windowId, ruleSnap.zoneIds.first(), ruleSnap.screenId, SnapIntent::UserInitiated,
                           ruleSnap.virtualDesktop);
            }
            // Non-empty zoneId so the effect treats this as a snap commit (re-applies
            // snap chrome), mirroring the pre-float-zone path below.
            Q_EMIT applyGeometryRequested(windowId, ruleSnap.geometry.x(), ruleSnap.geometry.y(),
                                          ruleSnap.geometry.width(), ruleSnap.geometry.height(),
                                          ruleSnap.zoneIds.first(), ruleSnap.screenId, false);
            return true;
        }
    }

    UnfloatResult unfloat = resolveUnfloatGeometry(windowId, screenId);
    if (!unfloat.found) {
        // No pre-float zone (a never-snapped window that defaulted to floating).
        // With the unfloatFallbackToZone setting on, snap it to a fallback zone
        // instead of refusing; otherwise return false so the caller keeps it
        // floating with feedback.
        unfloat = resolveFallbackUnfloatGeometry(windowId, screenId);
        if (!unfloat.found) {
            return false;
        }
    }

    // Both resolvers populate zoneIds before setting found, so a found result
    // always carries at least one zone — but UnfloatResult does not structurally
    // enforce that, and the commit / applyGeometryRequested calls below deref
    // zoneIds.first() unconditionally. Guard the invariant so a future resolver
    // change can never turn a found-but-empty result into an out-of-range crash.
    if (unfloat.zoneIds.isEmpty()) {
        return false;
    }

    // Whether the target came from the pre-float zone or the no-pre-float-zone
    // fallback, there is no saved-float entry to consume — the snap commit below
    // re-captures the window's snap slot as "snapped" in the unified record, so a
    // future mode transition restores it snapped, not floating (single source of
    // truth).

    // Commit the snap via the unified orchestration. User-initiated because
    // the user just toggled float off — they want this snap to update the
    // last-used-zone tracking. commitSnap handles clearing floating state
    // (and emits windowFloatingClearedForSnap which WTA relays as
    // windowFloatingChanged), plus the zone assignment.
    if (unfloat.zoneIds.size() > 1) {
        commitMultiZoneSnap(windowId, unfloat.zoneIds, unfloat.screenId, SnapIntent::UserInitiated);
    } else {
        commitSnap(windowId, unfloat.zoneIds.first(), unfloat.screenId, SnapIntent::UserInitiated);
    }

    // Carry the (representative) zone id, NOT an empty string. The KWin effect's
    // applyGeometryRequested handler uses an empty zoneId as the "float-restore"
    // discriminator (→ clearWindowSnapped, which strips the snap title-bar /
    // border chrome) and a non-empty zoneId as the "snap commit" discriminator
    // (→ markWindowSnapped, which re-applies it). Unfloat-to-zone IS a snap
    // commit, so an empty zoneId here would leave the re-snapped window wearing
    // its floating chrome (no hidden title bar, no snap border).
    Q_EMIT applyGeometryRequested(windowId, unfloat.geometry.x(), unfloat.geometry.y(), unfloat.geometry.width(),
                                  unfloat.geometry.height(), unfloat.zoneIds.first(), unfloat.screenId, false);
    return true;
}

bool SnapEngine::applyGeometryForFloat(const QString& windowId, const QString& screenId)
{
    // Prefer the unified placement record's float-back geometry. It is the single
    // source of truth: appId-keyed (survives the uuid change on logout/login),
    // one-record-per-window, and — unlike the legacy m_unmanagedGeometries store —
    // not silently dropped on load by the disabled-context gate when the user has
    // toggled snapping off/on. The legacy store is consulted only as a fallback
    // for windows with no record yet (pre-migration / first float of the session).
    if (m_windowTracker) {
        const QString appId = m_windowTracker->currentAppIdFor(windowId);
        auto rec = m_windowTracker->placementStore().peek(windowId, appId);
        if (rec) {
            // The shared free/float geometry, per screen — never a zone/tile rect by
            // construction. Prefer this screen's remembered spot, else any captured
            // free spot.
            QRect g = rec->freeGeometryFor(screenId);
            if (!g.isValid()) {
                g = rec->anyFreeGeometry();
            }
            if (g.isValid()) {
                qCInfo(PhosphorSnapEngine::lcSnapEngine)
                    << "applyGeometryForFloat:" << windowId << "restoring to" << g << "(placement record)";
                Q_EMIT applyGeometryRequested(windowId, g.x(), g.y(), g.width(), g.height(), QString(), screenId,
                                              false);
                return true;
            }
            // A record exists but the window has no genuine free geometry yet (it has
            // only ever been snapped/tiled). Do NOT fall back to the legacy unmanaged
            // store — that may still hold a stale zone rect, which is exactly the
            // geometry leak this model removes. Leave the window where it is; the next
            // move while floating captures a real free position into the record.
            qCInfo(PhosphorSnapEngine::lcSnapEngine)
                << "applyGeometryForFloat:" << windowId << "no free geometry on record — leaving in place";
            return false;
        }
    }

    // Legacy-store fallback (no placement record yet). Guard m_windowTracker:
    // the placement-record block above is itself gated on a non-null tracker, so
    // a null tracker falls straight here — deref it unconditionally and a
    // headless-test engine (nullptr tracker) would crash.
    if (m_windowTracker) {
        auto geo = m_windowTracker->validatedUnmanagedGeometry(windowId, screenId);
        if (geo) {
            qCInfo(PhosphorSnapEngine::lcSnapEngine)
                << "applyGeometryForFloat:" << windowId << "restoring to" << *geo << "(legacy unmanaged store)";
            Q_EMIT applyGeometryRequested(windowId, geo->x(), geo->y(), geo->width(), geo->height(), QString(),
                                          screenId, false);
            return true;
        }
    }
    qCWarning(PhosphorSnapEngine::lcSnapEngine) << "applyGeometryForFloat:" << windowId << "no pre-tile geometry found";
    return false;
}

// SnapEngine::clearFloatingStateForSnap was removed — its two callers
// (windowOpened in lifecycle.cpp, unfloatToZone above) now go through
// SnapEngine::commitSnap which handles clearing floating
// state as step 1 of its orchestration. The D-Bus-visible behaviour is
// identical: commitSnap emits windowFloatingClearedForSnap, WTA relays
// it as windowFloatingChanged on the same D-Bus interface.

QString SnapEngine::resolveUnfloatScreen(const QString& primaryScreen, const QString& fallbackScreen) const
{
    QString screen = primaryScreen;
    if (!screen.isEmpty()) {
        screen = m_windowTracker->resolveEffectiveScreenId(screen);
        auto* mgr = m_windowTracker->screenManager();
        const bool screenExists = mgr ? mgr->physicalScreenFor(screen).isValid()
                                      : (PhosphorScreens::ScreenIdentity::findByIdOrName(screen) != nullptr);
        if (!screenExists) {
            screen.clear();
        }
    }
    if (screen.isEmpty() && !fallbackScreen.isEmpty()) {
        screen = m_windowTracker->resolveEffectiveScreenId(fallbackScreen);
    }
    return screen;
}

UnfloatResult SnapEngine::resolveUnfloatGeometry(const QString& windowId, const QString& fallbackScreen) const
{
    UnfloatResult result;

    QStringList zoneIds = m_windowTracker->preFloatZones(windowId);
    if (zoneIds.isEmpty()) {
        return result;
    }

    const QString preFloatScreen = m_windowTracker->preFloatScreen(windowId);

    // Cross-monitor guard: a pre-float zone id belongs to the layout of the monitor
    // the window was floated from. If the window is being unfloated on a DIFFERENT
    // physical monitor (it crossed monitors while floating), that zone is stale and
    // restoring to it would teleport the window back to the source monitor. Refuse —
    // the caller keeps it floating / falls back to a current-screen zone. Compare by
    // PHYSICAL monitor (not screensMatch, which treats any virtual-vs-physical or
    // differing-virtual pair as a mismatch) so a same-monitor id-form difference is
    // never misread as a monitor change. Backstops the pre-float clear on the
    // cross-monitor handoff (handoffReceive) for move routes that skip that branch.
    if (!preFloatScreen.isEmpty() && !fallbackScreen.isEmpty()
        && !PhosphorIdentity::VirtualScreenId::samePhysical(preFloatScreen, fallbackScreen)) {
        qCInfo(PhosphorSnapEngine::lcSnapEngine)
            << "resolveUnfloatGeometry:" << windowId << "pre-float screen" << preFloatScreen
            << "is a different monitor than unfloat screen" << fallbackScreen << "- not restoring across monitors";
        return result;
    }

    const QString restoreScreen = resolveUnfloatScreen(preFloatScreen, fallbackScreen);

    QRect geo = m_windowTracker->resolveZoneGeometry(zoneIds, restoreScreen);
    if (!geo.isValid()) {
        return result;
    }

    result.found = true;
    result.zoneIds = zoneIds;
    result.geometry = geo;
    result.screenId = restoreScreen;
    return result;
}

UnfloatResult SnapEngine::resolveFallbackUnfloatGeometry(const QString& windowId, const QString& fallbackScreen) const
{
    UnfloatResult result;

    // Opt-in only: when the setting is off, a no-pre-float-zone unfloat leaves the
    // window floating (the caller emits feedback). The engine reads the bool via the
    // settings-agnostic ISnapSettings seam, like moveNewWindowsToLastZone.
    auto* s = snapSettings();
    if (!s || !s->unfloatFallbackToZone()) {
        return result;
    }

    // Resolve the window's effective screen — its tracked float screen, else the
    // caller's fallback. A tracked screen that no longer exists (output unplugged)
    // is discarded in favour of the caller's fallback. Zone geometry is resolved on
    // the resulting screen so the fallback lands where the window currently is.
    const QString screen = resolveUnfloatScreen(m_snapState->screenForWindow(windowId), fallbackScreen);
    if (screen.isEmpty() || !m_layoutManager) {
        return result;
    }
    PhosphorZones::Layout* layout = m_layoutManager->resolveLayoutForScreen(screen);
    if (!layout) {
        return result;
    }

    // Target resolution order: last-used zone (if it exists in this screen's layout)
    // → first empty zone → first zone in the layout. The last two reuse the same
    // accessors as the auto-snap chain (findEmptyZoneInLayout / zoneGeometry).
    QString zoneId;
    const QString lastUsed = m_snapState->lastUsedZoneId();
    // lastUsedZoneId() is GLOBAL (last zone used on any screen). zoneGeometry()
    // resolves a zone from any layout against this screen, so the geometry check
    // alone would let a zone from another monitor's layout win here. Scope it to
    // THIS screen's resolved layout via zoneById so "exists in this screen's
    // layout" (above) actually holds.
    if (!lastUsed.isEmpty()) {
        const QUuid lastUsedUuid(lastUsed);
        if (!lastUsedUuid.isNull() && layout->zoneById(lastUsedUuid)
            && m_windowTracker->zoneGeometry(lastUsed, screen).isValid()) {
            zoneId = lastUsed;
        }
    }
    if (zoneId.isEmpty()) {
        const int desktopFilter = currentVirtualDesktopForScreen(screen);
        zoneId = m_windowTracker->findEmptyZoneInLayout(layout, screen, desktopFilter);
    }
    if (zoneId.isEmpty()) {
        // Final fallback: the first zone in the layout. May already be occupied —
        // snapping supports multiple windows per zone (stacking), so that is fine.
        const QVector<PhosphorZones::Zone*> zones = layout->zones();
        if (!zones.isEmpty() && zones.first()) {
            zoneId = zones.first()->id().toString();
        }
    }
    if (zoneId.isEmpty()) {
        return result;
    }

    const QRect geo = m_windowTracker->zoneGeometry(zoneId, screen);
    if (!geo.isValid()) {
        return result;
    }

    result.found = true;
    result.zoneIds = QStringList{zoneId};
    result.geometry = geo;
    result.screenId = screen;
    qCInfo(PhosphorSnapEngine::lcSnapEngine)
        << "resolveFallbackUnfloatGeometry:" << windowId << "→ zone" << zoneId << "on" << screen;
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Cross-engine handoff (see IPlacementEngine.h for contract)
// ═══════════════════════════════════════════════════════════════════════════════

void SnapEngine::handoffReceive(const HandoffContext& ctx)
{
    if (ctx.windowId.isEmpty() || ctx.toScreenId.isEmpty()) {
        return;
    }
    qCInfo(PhosphorSnapEngine::lcSnapEngine) << "SnapEngine::handoffReceive:" << ctx.windowId << "to" << ctx.toScreenId
                                             << "from" << ctx.fromEngineId << "wasFloating=" << ctx.wasFloating;

    if (!ctx.sourceZoneIds.isEmpty()) {
        QRect zoneGeo = m_windowTracker->resolveZoneGeometry(ctx.sourceZoneIds, ctx.toScreenId);
        if (zoneGeo.isValid()) {
            const int curDesktop = currentVirtualDesktopForScreen(ctx.toScreenId);
            if (ctx.toDesktop > 0 && ctx.toDesktop != curDesktop) {
                // Cross-DESKTOP handoff: the target desktop isn't the visible one,
                // so assign the snap slot directly on SnapState for that desktop
                // (commitSnap would stamp the current desktop) and refresh the
                // placement-store record. This is the same path tryCrossDesktopMove
                // uses, and it is safe to bypass commitSnap's WTS orchestration
                // here: SnapState is the very store WTS queries (Daemon wires
                // setSnapState(snapEngine->snapState())), so zoneForWindow et al.
                // see this assignment; the snap chrome is applied below via the
                // non-empty-zoneId applyGeometryRequested (→ markWindowSnapped); and
                // persistence flows through the placement-store record. The only
                // caller, handleCrossModeMove, always passes wasFloating==false, so
                // there is no floating flag to clear.
                if (ctx.sourceZoneIds.size() > 1) {
                    m_snapState->assignWindowToZones(ctx.windowId, ctx.sourceZoneIds, ctx.toScreenId, ctx.toDesktop);
                } else {
                    m_snapState->assignWindowToZone(ctx.windowId, ctx.sourceZoneIds.first(), ctx.toScreenId,
                                                    ctx.toDesktop);
                }
                if (auto placement = capturePlacement(ctx.windowId)) {
                    placement->virtualDesktop = ctx.toDesktop;
                    m_windowTracker->placementStore().record(std::move(*placement));
                } else {
                    // Mirror tryCrossDesktopMove: surface the SnapState↔placement
                    // divergence rather than letting it hide.
                    qCDebug(PhosphorSnapEngine::lcSnapEngine)
                        << "handoffReceive: capturePlacement miss for" << ctx.windowId
                        << "— placement-store desktop not updated to" << ctx.toDesktop;
                }
            } else if (ctx.sourceZoneIds.size() > 1) {
                commitMultiZoneSnap(ctx.windowId, ctx.sourceZoneIds, ctx.toScreenId, SnapIntent::UserInitiated);
            } else {
                commitSnap(ctx.windowId, ctx.sourceZoneIds.first(), ctx.toScreenId, SnapIntent::UserInitiated);
            }
            // Non-empty zoneId so the effect routes this cross-engine snap to
            // markWindowSnapped (snap chrome), not clearWindowSnapped — see the
            // matching note in unfloatToZone().
            Q_EMIT applyGeometryRequested(ctx.windowId, zoneGeo.x(), zoneGeo.y(), zoneGeo.width(), zoneGeo.height(),
                                          ctx.sourceZoneIds.first(), ctx.toScreenId, false);
            return;
        }
    }

    const int currentDesktop = ctx.toDesktop > 0 ? ctx.toDesktop : currentVirtualDesktopForScreen(ctx.toScreenId);
    m_snapState->setFloatingOnScreen(ctx.windowId, ctx.toScreenId, currentDesktop);
    m_windowTracker->setWindowFloating(ctx.windowId, true);
    // The window's floating-screen association is now the destination monitor, so
    // any saved pre-float zone/screen (from the source-monitor float that started
    // this cross-screen move) is stale: it names a zone in the SOURCE monitor's
    // layout. Left in place, the next unfloat (Meta+F float-toggle, or the snap
    // minimize→unminimize float driver) restores the window to that source zone,
    // teleporting it back across monitors. Clear it so unfloat resolves against the
    // destination monitor instead. Mirrors the screen-assignment refresh this same
    // cross-monitor move path already performs (see WindowTrackingAdaptor::
    // windowScreenChanged), which historically only covered snap assignments.
    // Route through the tracker (not SnapState directly) so BOTH the windowId key
    // and its appId alias are dropped — the pre-float save writes both.
    m_windowTracker->clearPreFloatZone(ctx.windowId);
    Q_EMIT windowFloatingChanged(ctx.windowId, true, ctx.toScreenId);
}

void SnapEngine::handoffRelease(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return;
    }
    qCInfo(PhosphorSnapEngine::lcSnapEngine) << "SnapEngine::handoffRelease:" << windowId;

    if (m_snapState->isWindowSnapped(windowId)) {
        m_snapState->unassignWindow(windowId);
    }
    if (m_snapState->isFloating(windowId)) {
        m_snapState->setFloating(windowId, false);
    }
}

QString SnapEngine::screenForTrackedWindow(const QString& windowId) const
{
    return m_snapState->screenForWindow(windowId);
}

bool SnapEngine::isWindowTracked(const QString& windowId) const
{
    // All three arms must resolve a class-mutated window (issue #628).
    // isWindowSnapped/isFloating canonicalize the id internally; the screen arm
    // goes through screenForWindow (which canonicalizes) instead of a raw
    // screenAssignments().contains() on the canonical-keyed map. A screen
    // assignment is never empty, so a non-empty result means "present".
    return m_snapState->isWindowSnapped(windowId) || m_snapState->isFloating(windowId)
        || !m_snapState->screenForWindow(windowId).isEmpty();
}

} // namespace PhosphorSnapEngine

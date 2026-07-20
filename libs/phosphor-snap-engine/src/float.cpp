// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorSnapEngine/ISnapSettings.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/ScreenIdentity.h>
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
    SnapState* state = stateForWindow(windowId);
    const bool currentlyFloating = isFloating(windowId);
    const bool currentlySnapped = state && state->isWindowSnapped(windowId);

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

void SnapEngine::setWindowFloat(const QString& windowId, bool shouldFloat, const QString& callerScreenId)
{
    // Resolve the screen this float/unfloat acts on:
    // 1. The caller-provided screen (the effect's authoritative live output,
    //    threaded from setWindowFloatingForScreen) — ALWAYS preferred when set.
    //    The tracked association below is stale after a floating window drifts
    //    across monitors (the daemon never saw a windowScreenChanged for the
    //    drift), and feeding that stale screen to unfloatToZone/applyGeometryForFloat
    //    would resolve the float-back geometry and the unfloat fallback screen on the
    //    wrong monitor (Discussion #724). The effect always knows the real screen here.
    // 2. The window's tracked screen from SnapState (internal 2-arg callers).
    // 3. m_lastActiveScreenId (from last windowFocused).
    // 4. Empty (unfloatToZone/applyGeometryForFloat handle it gracefully).
    QString screenId = callerScreenId;
    if (screenId.isEmpty()) {
        if (const SnapState* state = stateForWindow(windowId)) {
            screenId = state->screenForWindow(windowId);
        }
    }
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
    QString preFloatScreenId = m_windowTracker->preFloatScreen(windowId);
    if (zoneIds.isEmpty()) {
        // The in-memory pre-float capture does not survive a daemon restart, but
        // the persisted placement record's snap slot does: a floating capture
        // carries the pre-float zones in slot.zoneIds, and a stale snapped
        // capture (daemon died before the float toggle was persisted) carries
        // the zones the window occupied before it floated. Either is the
        // window's home zone — without this fallback, unfloating after a
        // restart dead-ends ("no pre-float zone, keeping floating") with no way
        // out short of re-snapping by hand.
        using PhosphorEngine::WindowPlacement;
        const QString appId = m_windowTracker->currentAppIdFor(windowId);
        if (const auto rec = m_windowTracker->placementStore().peek(windowId, appId)) {
            const PhosphorEngine::EngineSlot slot = rec->slotFor(engineId());
            if (!slot.zoneIds.isEmpty()
                && (slot.state == WindowPlacement::stateFloating() || slot.state == WindowPlacement::stateSnapped())) {
                zoneIds = slot.zoneIds;
                preFloatScreenId = rec->screenId;
                qCInfo(PhosphorSnapEngine::lcSnapEngine)
                    << "resolveUnfloatGeometry:" << windowId << "no live pre-float capture — using placement record's"
                    << slot.state << "slot zones" << zoneIds << "on" << preFloatScreenId;
            }
        }
    }
    if (zoneIds.isEmpty()) {
        return result;
    }

    // Cross-monitor restore is ALLOWED (Discussion #724 follow-up): unfloat returns
    // the window to its remembered home zone regardless of which monitor it is
    // currently on. resolveUnfloatScreen prefers the pre-float (home) screen, so the
    // zone resolves on the monitor the window was snapped on and the window goes
    // home. This is deterministic and, unlike a cross-monitor refusal guard, does
    // not depend on the daemon knowing the window's exact current monitor — which is
    // unreliable for identical-model monitors whose per-window screen the compositor
    // and daemon can resolve differently.
    const QString restoreScreen = resolveUnfloatScreen(preFloatScreenId, fallbackScreen);

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
    const SnapState* trackedState = stateForWindow(windowId);
    const QString screen =
        resolveUnfloatScreen(trackedState ? trackedState->screenForWindow(windowId) : QString(), fallbackScreen);
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
    // Last-used is per-key: read THIS screen's store (falling back to the global
    // holder's representative for the restored-from-disk case). That already keeps a
    // different monitor's last-used out. The layout-membership guard below still
    // matters: a screen can have its assigned layout swapped, and zoneGeometry()
    // resolves a zone from any registered layout against this screen — so scope the
    // last-used tier to THIS screen's resolved layout via zoneById.
    const QString lastUsed = lastUsedStateForScreen(screen)->lastUsedZoneId();
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
                // here: this SnapState is a store the WTS facade queries through the
                // snap-state resolver (Daemon wires setSnapStateResolver()), so zoneForWindow et al.
                // see this assignment; the snap chrome is applied below via the
                // non-empty-zoneId applyGeometryRequested (→ markWindowSnapped); and
                // persistence flows through the placement-store record. The only
                // caller, handleCrossModeMove, always passes wasFloating==false, so
                // there is no floating flag to clear.
                SnapState* targetState = stateForWindowOnScreen(ctx.windowId, ctx.toScreenId);
                if (ctx.sourceZoneIds.size() > 1) {
                    targetState->assignWindowToZones(ctx.windowId, ctx.sourceZoneIds, ctx.toScreenId, ctx.toDesktop);
                } else {
                    targetState->assignWindowToZone(ctx.windowId, ctx.sourceZoneIds.first(), ctx.toScreenId,
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
    // Re-home the window onto the destination monitor's per-key store when it is
    // already tracked here (same-engine cross-screen float drift). This moves its
    // per-window state (including the floating bit and the live screen, rewritten to
    // the destination) so screenForTrackedWindow reflects the new monitor (#724). The
    // pre-float zone rides along UNCHANGED (behaviour A): an unfloat on any monitor
    // restores the home zone, and cross-monitor restore is allowed (there is no
    // refusal guard). A no-op when the window is being
    // adopted fresh from another engine (untracked here); stateForWindowOnScreen then
    // registers it under the destination key below.
    migrateWindowToScreen(ctx.windowId, ctx.toScreenId);
    stateForWindowOnScreen(ctx.windowId, ctx.toScreenId)
        ->setFloatingOnScreen(ctx.windowId, ctx.toScreenId, currentDesktop);
    m_windowTracker->setWindowFloating(ctx.windowId, true);
    Q_EMIT windowFloatingChanged(ctx.windowId, true, ctx.toScreenId);
}

void SnapEngine::handoffRelease(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return;
    }
    qCInfo(PhosphorSnapEngine::lcSnapEngine) << "SnapEngine::handoffRelease:" << windowId;

    if (SnapState* state = stateForWindow(windowId)) {
        if (state->isWindowSnapped(windowId)) {
            const QStringList removedZones = state->zonesForWindow(windowId);
            state->unassignWindow(windowId);
            syncGlobalLastUsedForRemovedZones(removedZones);
        }
        if (state->isFloating(windowId)) {
            state->setFloating(windowId, false);
        }
        // The destination engine now owns the window. unassignWindow / setFloating
        // above cleared its zone/screen/desktop and floating bit — but NOT the
        // pre-float capture, which is deliberately PRESERVED (see
        // testHandoffRelease_preservesPreFloatCapture): a future return handoff may
        // consult it for size restoration. Finally drop the reverse-map ownership
        // record so this engine no longer claims the window.
        forgetWindow(windowId);
    }
}

QString SnapEngine::screenForTrackedWindow(const QString& windowId) const
{
    if (const SnapState* state = stateForWindow(windowId)) {
        return state->screenForWindow(windowId);
    }
    return {};
}

bool SnapEngine::isWindowTracked(const QString& windowId) const
{
    // All three arms must resolve a class-mutated window (issue #628).
    // isWindowSnapped/isFloating canonicalize the id internally; the screen arm
    // goes through screenForWindow (which canonicalizes) instead of a raw
    // map lookup on the canonical-keyed store. A screen
    // assignment is never empty, so a non-empty result means "present".
    const SnapState* state = stateForWindow(windowId);
    return (state
            && (state->isWindowSnapped(windowId) || state->isFloating(windowId)
                || !state->screenForWindow(windowId).isEmpty()))
        || isFloating(windowId);
}

} // namespace PhosphorSnapEngine

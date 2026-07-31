// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorSnapEngine/ISnapSettings.h>
#include <PhosphorIdentity/VirtualScreenId.h>
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
        // Report instead of absorbing the press silently: every other
        // navigation shortcut produces feedback, and a silent shortcut reads
        // as broken (mirrors the autotile facade's not_managed report).
        Q_EMIT navigationFeedback(false, QStringLiteral("float"), QStringLiteral("not_managed"), QString(), QString(),
                                  screenId);
        return;
    }

    if (currentlyFloating) {
        // An explicit user float toggle is ALWAYS user semantics, even for a
        // window still classified as a suspension float. Otherwise a window
        // whose unminimize unfloat was refused (cross-monitor home) and whose
        // retry budget then ran out would be permanently stuck: it keeps the
        // classification, so Meta+F would refuse forever with no way back.
        if (!unfloatToZone(windowId, screenId, UnfloatCause::UserToggle)) {
            Q_EMIT navigationFeedback(false, QStringLiteral("float"), QStringLiteral("no_pre_float_zone"), QString(),
                                      QString(), screenId);
            return;
        }
        // The float is over, so the suspension classification is too. This
        // path never crosses the adaptor edges that normally clear it (the
        // shortcut calls the engine directly), and a stranded bit would keep
        // every later capture on the minimize-preserve path and make the next
        // effect-driven unfloat wrongly read as a suspension.
        m_windowTracker->clearSuspensionFloat(windowId);
        Q_EMIT navigationFeedback(true, QStringLiteral("float"), QStringLiteral("unfloated"), QString(), QString(),
                                  screenId);
    } else {
        m_windowTracker->unsnapForFloat(windowId);
        m_windowTracker->setWindowFloating(windowId, true);
        Q_EMIT windowFloatingChanged(windowId, true, screenId);
        applyFloatGeometryUnlessMinimized(windowId, screenId);
        Q_EMIT navigationFeedback(true, QStringLiteral("float"), QStringLiteral("floated"), QString(), QString(),
                                  screenId);
    }
}

void SnapEngine::applyFloatGeometryUnlessMinimized(const QString& windowId, const QString& screenId)
{
    // A minimize-suspension float must NOT move the frame: the window is
    // hidden, and applying the remembered float-back rect here would park it
    // at a stale position that KWin then restores to on unminimize, before
    // the unfloat re-snaps it (the "wrong geometry first, then resnaps"
    // defect). The autotile engine documents and guards the identical hazard
    // in float_handoff.cpp; this is the snap-side twin. The guard fires only
    // on ENGAGED true: the effect pushes fresh metadata on the minimize edge
    // BEFORE any float traffic from that edge (same ordered D-Bus stream), so
    // a genuine minimize-float always arrives with the state engaged, while
    // an unknown reading means a visible-window float whose float-back
    // reposition must not be dropped.
    // A NULL registry also fails open (geometry applies): production always
    // wires the registry before any float traffic (daemon init), so null only
    // occurs in reduced test wirings, where suppressing the apply would hide
    // the very behaviour those tests exercise.
    if (m_windowRegistry && m_windowRegistry->minimizedState(windowId).value_or(false)) {
        return;
    }
    applyGeometryForFloat(windowId, screenId);
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
        // NOTE: deliberately NO re-home to `screenId` here. Migrating the
        // window to the caller's live screen first looks like it would fix
        // the stale-home problem at its source, but SnapState::migrateWindowTo
        // carries the ZONE assignment across verbatim while rewriting the live
        // screen — so unsnapForFloat below would then capture monitor A's zone
        // id paired with monitor B as its home screen. That is strictly worse
        // than a stale home: the later unfloat resolves a foreign layout's
        // zone onto B (zone lookup spans all layouts), and the suspension
        // confinement can never fire because home == live by construction.
        m_windowTracker->unsnapForFloat(windowId);
        m_windowTracker->setWindowFloating(windowId, true);
        Q_EMIT windowFloatingChanged(windowId, true, screenId);
        // Guarded: the minimize path reaches here via setWindowFloatingForScreen
        // and must not teleport the hidden frame (see the helper's comment).
        applyFloatGeometryUnlessMinimized(windowId, screenId);
    } else {
        // Cause derived from the live classification: a minimize-suspension
        // unfloat restores prior state only, a user float toggle gets the
        // rule tier and the cross-monitor go-home restore.
        const UnfloatCause cause = (m_windowTracker && m_windowTracker->isSuspensionFloat(windowId))
            ? UnfloatCause::Suspension
            : UnfloatCause::UserToggle;
        if (!unfloatToZone(windowId, screenId, cause)) {
            // No restore target — keep the window floating rather than leaving
            // it in a limbo state (not floating, not snapped to any zone). For
            // a SUSPENSION unfloat this is also the deliberate refusal outcome
            // (see unfloatToZone): the window stays exactly where it is. The
            // refusal emits no windowFloatingChanged — subscribers already
            // believe "floating", which is still true; the effect's unminimize
            // retry budget is the terminating condition, and retries are
            // harmless because the suspension classification is retained by
            // the adaptor until an unfloat actually lands.
            qCDebug(PhosphorSnapEngine::lcSnapEngine)
                << "setWindowFloat: cannot unfloat" << windowId << "- no restore target, keeping floating";
            return;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Private helpers
// ═══════════════════════════════════════════════════════════════════════════════

bool SnapEngine::unfloatToZone(const QString& windowId, const QString& screenId, UnfloatCause cause)
{
    // One fact, one parameter (Discussion #724): a SUSPENSION
    // (minimize-as-float) unfloat is not a user float toggle. The round trip
    // exists to put the window back where it was before the minimize, so a
    // suspension unfloat (a) never takes a SnapToZone rule target, (b) is
    // confined to the caller's live monitor, and (c) never falls through to
    // the fallback-zone tier — a window that was not snapped at minimize time
    // must come back floating, not freshly snapped.
    const bool suspension = cause == UnfloatCause::Suspension;

    // Highest-priority un-float target: a matched SnapToZone rule. Toggling a
    // window out of float lands it in the rule's zones, not a stale pre-float
    // zone, so the rule stays authoritative for both open and Meta+F. Falls
    // through to the pre-float / fallback zone when no rule matches.
    if (!suspension) {
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

    UnfloatResult unfloat = resolveUnfloatGeometry(windowId, screenId, /*confineToFallbackScreen=*/suspension);
    if (!unfloat.found) {
        // Not-found here means either "no pre-float zone at all" (a
        // never-snapped window that defaulted to floating) or, under
        // confinement, "the remembered home names another monitor — refused".
        // A SUSPENSION unfloat stops in both cases: the round trip restores
        // prior state only, and the fallback tier below would snap the window
        // FRESH — on the stale tracked screen first, no less — which is
        // exactly the cross-monitor teleport the confinement refuses. The
        // refusal deliberately leaves the pre-float capture and placement
        // record untouched: they are the remembered home a later USER float
        // toggle is entitled to restore to (the deliberate cross-monitor
        // go-home behaviour).
        if (suspension) {
            return false;
        }
        // User toggle with no pre-float zone: with the unfloatFallbackToZone
        // setting on, snap it to a fallback zone instead of refusing;
        // otherwise return false so the caller keeps it floating with
        // feedback.
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
    // Desktop deliberately left at 0 (= the restore screen's CURRENT desktop).
    // The rule tier above forwards a routed desktop because RouteToDesktop
    // also MOVES the window there; an unfloat has no such move, so stamping a
    // placement record's remembered desktop would record occupancy on a
    // desktop the window is not actually on after a desktop switch.
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

// TRACKER CONTRACT for this file: every float/unfloat/handoff entry point
// derefs m_windowTracker WITHOUT a null guard. The daemon always constructs
// the engine with a live WindowTrackingService, and the reduced test wirings
// that pass a null tracker (screen-mode routing, exclude rules) never call
// into these paths. applyGeometryForFloat below is the one deliberate
// exception — it is reachable from D-Bus relays in reduced wirings, so it
// keeps its guard.
bool SnapEngine::applyGeometryForFloat(const QString& windowId, const QString& screenId)
{
    if (!m_windowTracker) {
        return false;
    }
    // ONE resolver, shared with the WTA twin (WindowTrackingAdaptor::
    // applyGeometryForFloat): validatedUnmanagedGeometry reads the unified
    // placement record — this screen's remembered spot first, then the
    // deterministic cross-screen fallback — and cross-screen-validates the
    // rect. The previous open-coded peek here skipped that validation, so a
    // rect captured on another monitor was applied with raw coordinates.
    //
    // The resolver's appId-FIFO fallback is DELIBERATE (unlike the exact-only
    // pre-float zone read in resolveUnfloatGeometry): a record-less instance
    // floating for the first time restores to where its app last floated — the
    // cross-instance float-back share that collapsePureFloatSiblings manages.
    // A shared free position is a sensible default; a shared ZONE assignment
    // is not. A window with no free geometry on record anywhere simply stays
    // where it is; the next move while floating captures a real free position.
    const auto geo = m_windowTracker->validatedUnmanagedGeometry(windowId, screenId);
    if (geo) {
        qCInfo(PhosphorSnapEngine::lcSnapEngine)
            << "applyGeometryForFloat:" << windowId << "restoring to" << *geo << "(placement record)";
        Q_EMIT applyGeometryRequested(windowId, geo->x(), geo->y(), geo->width(), geo->height(), QString(), screenId,
                                      false);
        return true;
    }
    qCInfo(PhosphorSnapEngine::lcSnapEngine)
        << "applyGeometryForFloat:" << windowId << "no free geometry on record — leaving in place";
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
    // The existence check spans two identity domains: ScreenManager's tracked
    // ids when a manager is wired (production), else QScreen connector-name
    // matching via ScreenIdentity. Both strip virtual-screen suffixes; the
    // residual stable-id-vs-connector-name asymmetry is reachable only in
    // reduced wirings without a manager.
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
    // ABI-preserving forwarder: this two-argument form is the signature the
    // installed library exported before the confinement parameter existed
    // (SOVERSION unchanged), and it is the unconfined (user-toggle) semantic
    // every pre-existing caller wants.
    return resolveUnfloatGeometry(windowId, fallbackScreen, /*confineToFallbackScreen=*/false);
}

UnfloatResult SnapEngine::resolveUnfloatGeometry(const QString& windowId, const QString& fallbackScreen,
                                                 bool confineToFallbackScreen) const
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
        // Same-instance records ONLY: a daemon restart keeps KWin uuids, so the
        // window's own record always matches. The appId-FIFO fallback
        // would hand a record-less floating window a SIBLING's home zone (same
        // app, different instance) and unfloat-snap it there — cross-window
        // zone bleed. Logout/login (new uuids) restores through
        // resolveWindowRestore's take(), never this path.
        if (const auto rec = m_windowTracker->placementStore().peekExact(windowId)) {
            const PhosphorEngine::EngineSlot slot = rec->slotFor(engineId());
            if (!slot.zoneIds.isEmpty()
                && (slot.state == WindowPlacement::stateFloating() || slot.state == WindowPlacement::stateSnapped())) {
                zoneIds = slot.zoneIds;
                // Home-screen hint. Exact for a stale SNAPPED slot (captured as the
                // snap screen); an approximation for a FLOATING slot, whose record
                // screen is the screen the window was floating on at capture time —
                // after a cross-monitor drift while floating that is the drift
                // monitor, not the pre-float home. resolveUnfloatScreen validates it
                // and falls back to the caller's live screen, and cross-monitor
                // unfloat-to-home is allowed anyway (#724), so the approximation is
                // bounded.
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

    // Cross-monitor restore is ALLOWED for user float toggles (Discussion #724
    // follow-up): unfloat returns the window to its remembered home zone
    // regardless of which monitor it is currently on. resolveUnfloatScreen
    // prefers the pre-float (home) screen, so the zone resolves on the monitor
    // the window was snapped on and the window goes home.
    //
    // A SUSPENSION (minimize) unfloat is confined to the caller's screen
    // instead (Discussion #724, the 3.3.x regression): the minimize round trip
    // exists to put the window back where it was before the minimize, so a
    // home screen naming a different physical monitor can only be stale state
    // — a pre-float capture or placement-record snap slot left behind by a
    // cross-monitor move that bypassed windowScreenChanged (drag routes) and
    // handoffRelease (which deliberately preserves the capture). Restoring it
    // would teleport the unminimized window across monitors. Refuse — the
    // caller keeps the window floating where it is. The comparison uses the
    // RAW home screen, not resolveUnfloatScreen's output: for a suspension
    // unfloat the caller's screen is the effect's authoritative live output,
    // and a home screen that no longer resolves must also refuse rather than
    // degrade into snapping a foreign layout's zone onto the live screen.
    // Fail-open on an EMPTY id on either side is deliberate and benign: an
    // empty home screen makes resolveUnfloatScreen fall to the caller's live
    // screen (a same-monitor restore), and an empty fallbackScreen only occurs
    // for engine-internal callers whose restore then resolves on the home
    // screen — neither combination can cross monitors.
    if (confineToFallbackScreen && !preFloatScreenId.isEmpty() && !fallbackScreen.isEmpty()
        && !PhosphorIdentity::VirtualScreenId::samePhysical(preFloatScreenId, fallbackScreen)) {
        qCInfo(PhosphorSnapEngine::lcSnapEngine)
            << "resolveUnfloatGeometry:" << windowId << "suspension home screen" << preFloatScreenId
            << "is a different monitor than the live screen" << fallbackScreen
            << "— not restoring across monitors, keeping the window floating";
        return result;
    }
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
    // stateForWindow is never null; an untracked window yields an empty
    // tracked screen and the caller's fallback wins.
    const QString screen = resolveUnfloatScreen(stateForWindow(windowId)->screenForWindow(windowId), fallbackScreen);
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

    // Re-home FIRST, on every path: a window already tracked here under a
    // different (screen, desktop, activity) key keeps its old owning SnapState
    // otherwise — stateForWindowOnScreen deliberately does not re-home, and the
    // zone-resolved branches below place through it — so
    // pruneStatesForRemovedScreen / pruneStatesForDesktop on the SOURCE context
    // would silently drop a snap that now lives on the destination. A
    // documented no-op when the key is unchanged or the window is being
    // adopted fresh from another engine (untracked here). This also moves the
    // per-window state (floating bit, live screen rewritten to the
    // destination) so screenForTrackedWindow reflects the new monitor (#724);
    // the pre-float zone rides along UNCHANGED (behaviour A).
    migrateWindowToScreen(ctx.windowId, ctx.toScreenId);

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
                // persistence flows through the placement-store record. Every
                // caller that sets toDesktop (the cross-desktop move paths)
                // passes wasFloating==false, so there is no floating flag to
                // clear in THIS branch; other handoffReceive callers land in
                // the tail below.
                // The cross-desktop callers' wasFloating==false invariant,
                // enforced rather than comment-only: debug asserts, release
                // clears the flag so a violating caller cannot leave a
                // floating bit dangling behind the direct slot assignment.
                Q_ASSERT(!ctx.wasFloating);
                if (Q_UNLIKELY(ctx.wasFloating)) {
                    qCWarning(PhosphorSnapEngine::lcSnapEngine)
                        << "handoffReceive: cross-desktop handoff with wasFloating=true for" << ctx.windowId
                        << "— clearing the float before the slot assignment";
                    m_windowTracker->setWindowFloating(ctx.windowId, false);
                }
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
    // Re-homing already happened at the top of the function (it must cover the
    // zone-resolved branches too); a USER unfloat on any monitor restores the
    // home zone (cross-monitor restore is allowed for float toggles; only
    // suspension unfloats carry the cross-monitor refusal — see
    // resolveUnfloatGeometry).
    if (!ctx.wasFloating) {
        // Explicit cross-mode MOVE of a MANAGED window whose source zones did
        // not resolve on this screen (foreign zone ids after a layout change).
        // Floating it here converted the user's move into a float toggle. It
        // arrives as a plain FREE window instead — snapping's default for
        // unmanaged windows — keeping its live frame. Broadcast not-floating
        // so subscribers that last heard the source mode's state converge
        // (the adaptor's last-broadcast gate dedups when they already agree).
        m_windowTracker->setWindowFloating(ctx.windowId, false);
        Q_EMIT windowFloatingChanged(ctx.windowId, false, ctx.toScreenId);
        return;
    }
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

    // Plain statement, no conditional: stateForWindow is NEVER null (the
    // globals holder is constructed in the ctor — see the accessor's
    // contract), and every query below answers empty/false for an untracked
    // window, so the operations no-op harmlessly.
    SnapState* state = stateForWindow(windowId);
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

QString SnapEngine::screenForTrackedWindow(const QString& windowId) const
{
    // stateForWindow is never null (ctor-constructed globals holder); an
    // untracked window resolves to an empty screen.
    return stateForWindow(windowId)->screenForWindow(windowId);
}

bool SnapEngine::isWindowTracked(const QString& windowId) const
{
    // All three arms must resolve a class-mutated window (issue #628).
    // isWindowSnapped/isFloating canonicalize the id internally; the screen arm
    // goes through screenForWindow (which canonicalizes) instead of a raw
    // map lookup on the canonical-keyed store. A screen
    // assignment is never empty, so a non-empty result means "present".
    // Plain statement, no null-check: stateForWindow is NEVER null (see the
    // accessor's contract, which names this function as the tracked/untracked
    // test). The trailing facade-level isFloating is NOT redundant — the
    // state's own isFloating consults only the owning store, while the facade
    // sweeps every store, catching a float recorded outside the reverse map's
    // target.
    const SnapState* state = stateForWindow(windowId);
    return state->isWindowSnapped(windowId) || state->isFloating(windowId)
        || !state->screenForWindow(windowId).isEmpty() || isFloating(windowId);
}

} // namespace PhosphorSnapEngine

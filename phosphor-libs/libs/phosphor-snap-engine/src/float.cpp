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
    // Guarded locally per the ctor contract: the tracker derefs below are
    // unconditional, so a stub-dependency engine crashes here in release.
    Q_ASSERT(m_windowTracker);
    if (!m_windowTracker) {
        qCWarning(PhosphorSnapEngine::lcSnapEngine) << "toggleWindowFloat: no window tracker";
        return;
    }
    SnapState* state = stateForWindow(windowId);
    const bool currentlyFloating = isFloating(windowId);
    // Managed here means snapped in the primary store OR held anywhere: a
    // window adopted into the desktop in view without being snapped there
    // (its zone is on another desktop) is this engine's, and Meta+F floats
    // it the way it floated the single-store window before adoption existed.
    const bool currentlySnapped = (state && state->isWindowSnapped(windowId)) || heldKeyForWindow(windowId).has_value();

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
        // Own store FIRST — this engine is the sole owner of its float bit,
        // the same ownership rule the daemon's float writer documents for
        // autotile and scrolling. The routed WTS write below dispatches on the
        // screen's CURRENT mode, which mid-transition can resolve to the
        // engine still releasing the window: its writer then no-ops, and its
        // change gate reads the FOREIGN engine's bit — so snap's own store
        // never recorded the float, the chrome said floating, and the next
        // toggle read "not floating". With the own-write applied the WTS call
        // is a no-op whenever the routing agrees; it stays for the unwired
        // legacy path's bookkeeping. With a screen in hand the float records
        // its residence too: a window adopted into this desktop without ever
        // being snapped here has no screen/desktop in the store, and a bare
        // float bit would leave the capture screenless and the cross-desktop
        // focus walk blind to it.
        setFloatingWithResidence(windowId, screenId);
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
    // Guarded locally per the ctor contract: the tracker derefs below are
    // unconditional, so a stub-dependency engine crashes here in release.
    Q_ASSERT(m_windowTracker);
    if (!m_windowTracker) {
        qCWarning(PhosphorSnapEngine::lcSnapEngine) << "setWindowFloat: no window tracker";
        return;
    }
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
        // window to the caller's live screen first would unassign its zone on
        // the screen it is snapped on before unsnapForFloat below could
        // capture it, so the float would remember no home at all. A home
        // naming another monitor than the one a later unfloat runs on is
        // refused there instead.
        m_windowTracker->unsnapForFloat(windowId);
        // Own store first — see toggleWindowFloat's float branch for why the
        // routed WTS write alone cannot be trusted to land here, and why the
        // residence is recorded with it.
        setFloatingWithResidence(windowId, screenId);
        m_windowTracker->setWindowFloating(windowId, true);
        Q_EMIT windowFloatingChanged(windowId, true, screenId);
        // Guarded: the minimize path reaches here via setWindowFloatingForScreen
        // and must not teleport the hidden frame (see the helper's comment).
        applyFloatGeometryUnlessMinimized(windowId, screenId);
    } else {
        // Cause derived from the live classification: a minimize-suspension
        // unfloat restores prior state only, a user float toggle gets the
        // rule tier and the fallback-zone tier.
        // No null guard on the tracker: this function already derefs it
        // unguarded on the float branch above, per this file's TRACKER CONTRACT.
        const UnfloatCause cause =
            m_windowTracker->isSuspensionFloat(windowId) ? UnfloatCause::Suspension : UnfloatCause::UserToggle;
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
    // The frame a user unfloat leaves is the next float's float-back, from
    // Meta+F and the D-Bus float calls alike (F52). A suspension's frame is
    // the hidden rect, which the helper refuses anyway.
    if (!suspension) {
        recordFreeFrameBeforeUserSnap(windowId, screenId);
    }

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
        // never-snapped window that defaulted to floating) or "the remembered
        // home names another monitor", refused for every cause. A SUSPENSION
        // unfloat stops in both cases: the round trip restores prior state
        // only, and the fallback tier below would snap the window FRESH, on
        // the stale tracked screen first, no less.
        if (suspension) {
            return false;
        }
        // User toggle with no pre-float zone on this monitor: with the
        // unfloatFallbackToZone setting on, snap it to a fallback zone on the
        // monitor it is on instead of refusing; otherwise return false so the
        // caller keeps it floating with feedback.
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

    // Commit the snap via the unified orchestration. A user toggle is
    // user-initiated: the user just snapped the window back, so it records
    // the last-used zone. A SUSPENSION unfloat (an unminimize) only puts the
    // window back where it was, so it commits as a replacement and writes no
    // user-snap bookkeeping (F484). commitSnap handles clearing floating
    // state (and emits windowFloatingClearedForSnap which WTA relays as
    // windowFloatingChanged), plus the zone assignment.
    // Desktop deliberately left at 0 (= the restore screen's CURRENT desktop).
    // The rule tier above forwards a routed desktop because RouteToDesktop
    // also MOVES the window there; an unfloat has no such move, so stamping a
    // placement record's remembered desktop would record occupancy on a
    // desktop the window is not actually on after a desktop switch.
    const SnapIntent unfloatIntent = suspension ? SnapIntent::AutoReplaced : SnapIntent::UserInitiated;
    if (unfloat.zoneIds.size() > 1) {
        commitMultiZoneSnap(windowId, unfloat.zoneIds, unfloat.screenId, unfloatIntent);
    } else {
        commitSnap(windowId, unfloat.zoneIds.first(), unfloat.screenId, unfloatIntent);
    }

    // Carry the (representative) zone id, NOT an empty string. The KWin effect's
    // applyGeometryRequested handler uses an empty zoneId as the "float-restore"
    // discriminator (→ clearWindowSnapped, which strips the snap title-bar /
    // border chrome) and a non-empty zoneId as the "snap commit" discriminator
    // (→ markWindowSnapped, which re-applies it). Unfloat-to-zone IS a snap
    // commit, so an empty zoneId here would leave the re-snapped window wearing
    // its floating chrome (no hidden title bar, no snap border).
    // A suspension's return re-states the zone the window was snapped in.
    if (suspension) {
        Q_EMIT restatementGeometryRequested(windowId, unfloat.geometry.x(), unfloat.geometry.y(),
                                            unfloat.geometry.width(), unfloat.geometry.height(),
                                            unfloat.zoneIds.first(), unfloat.screenId);
    } else {
        Q_EMIT applyGeometryRequested(windowId, unfloat.geometry.x(), unfloat.geometry.y(), unfloat.geometry.width(),
                                      unfloat.geometry.height(), unfloat.zoneIds.first(), unfloat.screenId, false);
    }
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
    // placement record for THIS screen and validates that the rect's
    // coordinates actually lie there. The previous open-coded peek here skipped
    // that validation, so a rect captured on another monitor was applied with
    // raw coordinates.
    //
    // The lookup is per-window and screen-local. It does NOT borrow a same-app
    // sibling's rect, and there is no cross-screen fallback: both were removed
    // in discussion #1028, where an app's bucket filling with dead instances at
    // MaxPerApp meant a live window with no record of its own inherited a
    // ghost's absolute coordinates and was moved to whatever monitor that ghost
    // last occupied. "Restore to where this app last floated" reads like a
    // sensible default and has a bad failure mode. A window with no free
    // geometry on record for this screen simply stays where it is; the next
    // move while floating captures a real free position.
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
    // Nothing moves the window, so a fullscreen one would stay covering the
    // monitor as a float: the effect ends its fullscreen in place (F546).
    Q_EMIT fullscreenHandBackRequested(windowId);
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
    // Guarded locally per the ctor contract: the tracker derefs below are
    // unconditional, so a stub-dependency engine crashes here in release.
    Q_ASSERT(m_windowTracker);
    if (!m_windowTracker) {
        qCWarning(PhosphorSnapEngine::lcSnapEngine) << "resolveUnfloatScreen: no window tracker";
        return fallbackScreen;
    }
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
            // A window present on several desktops has one home per desktop,
            // and the flat zoneIds is whichever desktop was in view at the
            // last capture. Its home HERE is the map's entry for the desktop
            // the unfloat lands on; with none, the window was unsnapped on
            // this desktop and there is nothing to return it to.
            QStringList recordZones = slot.zoneIds;
            if (m_states.membershipsForWindow(canonicalWindowId(windowId)).size() > 1) {
                recordZones = slot.zonesByDesktop.value(currentKeyForScreen(fallbackScreen).desktop);
            }
            if (!recordZones.isEmpty()
                && (slot.state == WindowPlacement::stateFloating() || slot.state == WindowPlacement::stateSnapped())) {
                zoneIds = recordZones;
                // Home-screen hint. Exact for a stale SNAPPED slot (captured as the
                // snap screen); for a FLOATING slot it is the screen the window was
                // floating on at capture time, which is the home monitor for a slot
                // written before a cross-monitor move. The refusal below then turns
                // that leftover away; a capture after the move carries no home.
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

    // An unfloat never restores across monitors, whatever asked for it. A
    // floating window moved to another monitor forgets the zone it floated
    // from (the move drops the capture), so a home naming a different
    // physical monitor than the caller's can only be stale: a capture or
    // placement-record slot a move left behind on a path that does not drop
    // it. Restoring it would throw the window back to the monitor it left.
    // Not found here sends a user toggle on to the fallback-zone tier, which
    // places it on the monitor it is on when "Unfloat to a zone when there is
    // no previous zone" is on, and a suspension unfloat leaves it floating.
    // The comparison uses the RAW home screen, not resolveUnfloatScreen's
    // output, so a home screen that no longer resolves (monitor unplugged)
    // also refuses rather than degrading into snapping a foreign layout's
    // zone onto the live screen. Fail-open on an EMPTY id on either side is
    // deliberate and benign: an empty home makes resolveUnfloatScreen fall to
    // the caller's live screen, and an empty fallbackScreen only occurs for
    // engine-internal callers whose restore then resolves on the home screen.
    // Neither can cross monitors. confineToFallbackScreen is inert.
    Q_UNUSED(confineToFallbackScreen)
    if (!preFloatScreenId.isEmpty() && !fallbackScreen.isEmpty()
        && !PhosphorIdentity::VirtualScreenId::samePhysical(preFloatScreenId, fallbackScreen)) {
        qCInfo(PhosphorSnapEngine::lcSnapEngine)
            << "resolveUnfloatGeometry:" << windowId << "home screen" << preFloatScreenId
            << "is a different monitor than the live screen" << fallbackScreen << "— not restoring across monitors";
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
    // Guarded locally per the ctor contract: the tracker derefs below are
    // unconditional, so a stub-dependency engine crashes here in release.
    Q_ASSERT(m_windowTracker);
    if (!m_windowTracker) {
        qCWarning(PhosphorSnapEngine::lcSnapEngine) << "resolveFallbackUnfloatGeometry: no window tracker";
        return {};
    }
    UnfloatResult result;

    // Resolve the window's effective screen — the CALLER's screen first, else
    // the window's tracked float screen. The caller's screen is the effect's
    // authoritative live output threaded down from setWindowFloatingForScreen,
    // and it wins for the same reason setWindowFloat's tier list gives it tier 1:
    // the tracked association goes stale the moment a floating
    // window drifts across monitors without a windowScreenChanged (Discussion
    // #724), and this screen is load-bearing twice over — the fallback zone is
    // resolved on it AND the rule predicate below stamps ScreenId / derives Mode
    // from it, so a stale monitor evaluates a ScreenId-pinned rule against the
    // wrong output. A caller screen that no longer exists (output unplugged) is
    // discarded in favour of the tracked screen. stateForWindow is never null;
    // an untracked window yields an empty tracked screen, which only matters
    // when the caller passed none either.
    //
    // Resolved BEFORE the opt-in gate below so the rule predicate receives
    // the actual restore screen.
    const QString screen = resolveUnfloatScreen(fallbackScreen, stateForWindow(windowId)->screenForWindow(windowId));
    if (screen.isEmpty() || !m_layoutManager) {
        return result;
    }

    // Opt-in only: when neither the rule predicate nor the setting says
    // fall back, a no-pre-float-zone unfloat leaves the window floating (the
    // caller emits feedback). The injected predicate — when set — implements
    // the full rule ?? config layering; unset, the engine reads the bool via
    // the settings-agnostic ISnapSettings seam, like moveNewWindowsToLastZone.
    bool fallbackEnabled = false;
    if (m_unfloatFallbackPredicate) {
        fallbackEnabled = m_unfloatFallbackPredicate(windowId, screen);
    } else {
        auto* s = snapSettings();
        fallbackEnabled = s && s->unfloatFallbackToZone();
    }
    if (!fallbackEnabled) {
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
    // Guarded locally per the ctor contract: the tracker derefs below are
    // unconditional, so a stub-dependency engine crashes here in release.
    Q_ASSERT(m_windowTracker);
    if (!m_windowTracker) {
        qCWarning(PhosphorSnapEngine::lcSnapEngine) << "handoffReceive: no window tracker";
        return;
    }
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
    // across monitors the zone and the pre-float home stay behind. The key
    // carries the handoff's desktop: re-homing onto the screen's current one
    // and then placing on ctx.toDesktop left the window a member of both.
    PhosphorEngine::PlacementStateKey arrivalKey = currentKeyForScreen(ctx.toScreenId);
    if (ctx.toDesktop > 0) {
        arrivalKey.desktop = ctx.toDesktop;
    }
    // A window arriving on a desktop where it already holds zones keeps them
    // (F611). Read before the migrate, which can move another context's data
    // into the arrival key.
    QStringList landing = ctx.sourceZoneIds;
    if (ctx.toDesktop > 0) {
        const QString canonical = canonicalWindowId(ctx.windowId);
        const SnapState* const arrival = m_states.stateForKey(arrivalKey);
        if (arrival && holdsWindowInState(canonical, arrival)) {
            if (const QStringList kept = arrival->zonesForWindow(canonical); !kept.isEmpty()) {
                landing = kept;
            }
        }
    }
    migrateWindowToKey(ctx.windowId, arrivalKey);
    // The migrate only reaches a store the window is a member of. A home kept
    // where it is not (a tiling engine took it and the snap release kept the
    // capture, or it was never re-keyed) still answers every pre-float lookup,
    // so one naming another screen than the arrival goes here too.
    for (SnapState* state : m_states.states()) {
        const QString home = state ? state->preFloatScreen(ctx.windowId) : QString();
        if (!home.isEmpty() && !PhosphorScreens::ScreenIdentity::screensMatch(home, ctx.toScreenId)) {
            dropPreFloatHome(state, ctx.windowId);
        }
    }

    if (!landing.isEmpty()) {
        const QRect zoneGeo = m_windowTracker->resolveZoneGeometry(landing, ctx.toScreenId);
        if (zoneGeo.isValid()) {
            // A desktop not in view is committed pinned to it, so the arrival
            // is stated snapped to the effect like any other commit (F534's
            // handoff sibling); a commit clears any float bit it carries.
            const int curDesktop = currentVirtualDesktopForScreen(ctx.toScreenId);
            const int pinned = (ctx.toDesktop > 0 && ctx.toDesktop != curDesktop) ? ctx.toDesktop : 0;
            if (landing.size() > 1) {
                commitMultiZoneSnap(ctx.windowId, landing, ctx.toScreenId, SnapIntent::UserInitiated, pinned);
            } else {
                commitSnap(ctx.windowId, landing.first(), ctx.toScreenId, SnapIntent::UserInitiated, pinned);
            }
            // Gate at the DESTINATION desktop: the daemon routed this handoff
            // here because (screen, toDesktop) is snapping, but the screen's
            // visible desktop may be a tiling one, and the plain capture's
            // current-desktop gate then refused, so the durable record kept
            // the OLD desktop and the next login restored the window there.
            if (pinned > 0) {
                if (auto placement = capturePlacementAtDesktop(ctx.windowId, pinned)) {
                    placement->virtualDesktop = pinned;
                    m_windowTracker->placementStore().record(std::move(*placement));
                } else {
                    qCDebug(PhosphorSnapEngine::lcSnapEngine)
                        << "handoffReceive: capturePlacement miss for" << ctx.windowId
                        << "— placement-store desktop not updated to" << pinned;
                }
            }
            // Non-empty zoneId so the effect routes this cross-engine snap to
            // markWindowSnapped (snap chrome), not clearWindowSnapped — see the
            // matching note in unfloatToZone().
            Q_EMIT applyGeometryRequested(ctx.windowId, zoneGeo.x(), zoneGeo.y(), zoneGeo.width(), zoneGeo.height(),
                                          landing.first(), ctx.toScreenId, false);
            return;
        }
    }

    const int currentDesktop = ctx.toDesktop > 0 ? ctx.toDesktop : currentVirtualDesktopForScreen(ctx.toScreenId);
    // Re-homing already happened at the top of the function (it must cover the
    // zone-resolved branches too). A home that names another monitor is
    // refused by any later unfloat (resolveUnfloatGeometry).
    if (!ctx.wasFloating) {
        // Explicit cross-mode MOVE of a MANAGED window whose source zones did
        // not resolve on this screen (foreign zone ids after a layout change).
        // Floating it here converted the user's move into a float toggle. It
        // arrives as a plain FREE window instead — snapping's default for
        // unmanaged windows — keeping its live frame. Broadcast not-floating
        // so subscribers that last heard the source mode's state converge
        // (the adaptor's last-broadcast gate dedups when they already agree).
        //
        // Residence is RECORDED, not just broadcast. guardedHandoff verifies
        // the adoption with isWindowTracked() after this returns, and a free
        // arrival satisfies neither the snapped nor the floating arm — without
        // the screen-assignment write the deliberate free adoption read as a
        // REFUSAL and the window bounced straight back to the source engine.
        // handoffRelease clears this symmetrically (clearScreenAndDesktop).
        // Pinned to the destination desktop when the handoff names one, so
        // the residence lands in the store whose key says that desktop (an
        // unpinned resolve minted the membership under the VIEWED desktop
        // and the next membership pass released it).
        stateForWindowOnScreen(ctx.windowId, ctx.toScreenId, ctx.toDesktop)
            ->recordResidence(ctx.windowId, ctx.toScreenId, currentDesktop);
        // Own store first (a re-adoption of a window snap once floated could
        // still carry the bit); the routed WTS clear follows for the shared
        // bookkeeping.
        setFloating(ctx.windowId, false);
        m_windowTracker->setWindowFloating(ctx.windowId, false);
        Q_EMIT windowFloatingChanged(ctx.windowId, false, ctx.toScreenId);
        return;
    }
    stateForWindowOnScreen(ctx.windowId, ctx.toScreenId, ctx.toDesktop)
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

    // Clear one store's data for the window: zone (with the last-used
    // coupling), floating bit, and the residence entries. The residence goes
    // unconditionally: unassignWindow clears it, but only for a SNAPPED
    // window; a merely FLOATING one kept the screen/desktop that
    // setFloatingOnScreen wrote, and raw store scans (windowsOnScreenAndDesktop
    // feeds tryCrossDesktopFocus) would offer a window the destination engine
    // now owns. The pre-float capture is deliberately PRESERVED (see
    // testHandoffRelease_preservesPreFloatCapture): a return handoff may
    // consult it for size restoration. This release has no destination, so
    // it cannot tell a cross-monitor handoff from a same-monitor mode flip;
    // the receiving side forgets a home on another monitor (the daemon's
    // float relays clear it, and every unfloat refuses one).
    //
    // Each store's unassign clears its own last-used naming the zone; only the
    // global representative is swept after, so another screen's store keeps
    // its last-used of the same zone id in a shared layout (F167).
    QStringList removed;
    bool lastUsedCleared = false;
    const auto releaseFrom = [&windowId, &removed, &lastUsedCleared](SnapState* state) {
        if (state->isWindowSnapped(windowId)) {
            removed += state->zonesForWindow(windowId);
            lastUsedCleared |= state->unassignWindow(windowId).lastUsedZoneCleared;
        }
        if (state->isFloating(windowId)) {
            state->setFloating(windowId, false);
        }
        state->clearScreenAndDesktop(windowId);
    };

    // Per CONTEXT, not per window. The destination engine took the window in
    // the context its screen is showing. A membership on another desktop
    // whose context is still snapping is kept, zone and all: the window is
    // still snapped there, and the switch to that desktop re-applies it
    // (seen live without this: a sticky window snapped on two desktops,
    // one desktop flipped to tiling, sat at its tile rect on the other,
    // still-snapping desktop until re-snapped by hand). The handed-off
    // context goes, and so does any membership whose own context has left
    // snapping.
    const QString canonical = canonicalWindowId(windowId);
    QList<PhosphorEngine::PlacementStateKey> kept;
    for (const PhosphorEngine::PlacementStateKey& key : m_states.membershipsForWindow(canonical)) {
        const bool handedOff = (key == currentKeyForScreen(key.screenId));
        const bool stillSnapping = m_layoutManager && !key.screenId.isEmpty()
            && m_layoutManager->modeForScreen(key.screenId, key.desktop, key.activity)
                == PhosphorZones::AssignmentEntry::Mode::Snapping;
        if (!handedOff && stillSnapping) {
            kept.append(key);
            continue;
        }
        if (SnapState* state = m_states.stateForKey(key)) {
            releaseFrom(state);
        }
        m_states.removeMembership(canonical, key);
    }
    // The global holder carries the screenless float bookkeeping no
    // membership names (and is the store an untracked window resolves to).
    releaseFrom(m_globals);
    lastUsedCleared |= clearGlobalLastUsedIfRemoved(removed);
    if (lastUsedCleared && m_windowTracker) {
        m_windowTracker->markLastUsedZoneDirty();
    }
    if (kept.isEmpty()) {
        // Nothing of the window is snapping's any more: drop the reverse-map
        // record so this engine no longer claims it.
        forgetWindow(windowId);
        return;
    }
    qCInfo(PhosphorSnapEngine::lcSnapEngine) << "SnapEngine::handoffRelease:" << windowId << "keeps" << kept.size()
                                             << "snapping context(s) on other desktops";
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
    // test). The facade-level isFloating is NOT redundant — the state's own
    // isFloating consults only the owning store, while the facade also reads
    // the global holder, catching a screenless float recorded there.
    // The trailing heldKeyForWindow is the multi-desktop arm: a window
    // present on several desktops is adopted into the desktop in view with a
    // membership and NO data, so its primary store answers false on every
    // predicate above while a background store genuinely holds it. Tracked
    // means "some store of this engine holds the window", in any context.
    const SnapState* state = stateForWindow(windowId);
    return state->isWindowSnapped(windowId) || state->isFloating(windowId)
        || !state->screenForWindow(windowId).isEmpty() || isFloating(windowId)
        || heldKeyForWindow(windowId).has_value();
}

} // namespace PhosphorSnapEngine

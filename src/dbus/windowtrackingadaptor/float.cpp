// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// toggleFloatForWindow, calculateUnfloatRestore, windowUnsnappedForFloat
// moved to SnapAdaptor (src/dbus/snapadaptor/commit.cpp).
//
// This file retains cross-mode float methods that stay on WTA:
//   notifyDragOutUnsnap, getPreFloatZone, clearPreFloatZone,
//   isWindowFloating, queryWindowFloating, setWindowFloating (WTS delegate),
//   getFloatingWindows, applyGeometryForFloat, setWindowFloatingForScreen.

#include "windowtrackingadaptor.h"
#include "internal.h"
#include "core/interfaces/interfaces.h"
#include "core/platform/logging.h"
#include "core/utils/utils.h"
#include <array>
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorSnapEngine/SnapEngine.h>
namespace PlasmaZones {

void WindowTrackingAdaptor::notifyDragOutUnsnap(const QString& windowId)
{
    if (!validateWindowId(windowId, QStringLiteral("drag-out unsnap"))) {
        return;
    }

    QString zoneId = m_service->zoneForWindow(windowId);
    if (zoneId.isEmpty()) {
        // Window was not snapped — nothing to do
        return;
    }

    QString screenId = m_service->screenForWindow(windowId, m_lastActiveScreenId);
    qCInfo(lcDbusWindow) << "Drag-out unsnap (no activation trigger) for" << windowId << "screen:" << screenId;

    // Latch the pre-snap float-back BEFORE the float write: setWindowFloating
    // synchronously runs a placement capture that records the LIVE dragged
    // frame (zone dimensions) into the same per-screen free-geometry map this
    // read consumes — reading afterwards would "restore" the zone size and
    // destroy the genuine pre-snap size in the process.
    std::optional<QRect> preSnapGeo;
    if (shouldRestoreSizeOnUnsnap(windowId)) {
        preSnapGeo = m_service->validatedUnmanagedGeometry(windowId, screenId);
    }

    // Delegate unsnap-for-float to the service directly (the SnapAdaptor
    // method windowUnsnappedForFloat does the same thing, but this path
    // doesn't need the SnapAdaptor detour for a WTS-level operation).
    m_service->unsnapForFloat(windowId);
    setWindowFloating(windowId, true);

    // Restore pre-snap size (not position — window stays where the user dropped it).
    // This mirrors the activated-drag path in WindowDragAdaptor::dragStopped. A
    // matched SetRestoreSizeOnUnsnap rule overrides the global setting per window.
    // Dimension guard mirrors the drop.cpp twin: a degenerate stored rect
    // produces a RestoreSize outcome that validates to "requires non-zero
    // size" and is dropped effect-side — don't claim success for it.
    if (preSnapGeo && preSnapGeo->width() > 0 && preSnapGeo->height() > 0) {
        Q_EMIT applyGeometryRequested(windowId, 0, 0, preSnapGeo->width(), preSnapGeo->height(), QString(), screenId,
                                      true);
        // Consume-once, per screen: only this screen's float-back was used;
        // other monitors' remembered positions stay intact.
        m_service->clearFreeGeometry(windowId, screenId);
        qCInfo(lcDbusWindow) << "Drag-out unsnap: restoring size" << preSnapGeo->width() << "x" << preSnapGeo->height();
    } else if (preSnapGeo) {
        qCInfo(lcDbusWindow) << "Drag-out unsnap: degenerate stored size for" << windowId << "— skipping restore";
    }
}

bool WindowTrackingAdaptor::getPreFloatZone(const QString& windowId, QString& zoneId)
{
    if (windowId.isEmpty()) {
        qCDebug(lcDbusWindow) << "getPreFloatZone: empty windowId";
        zoneId.clear();
        return false;
    }
    // Delegate to service
    zoneId = m_service->preFloatZone(windowId);
    qCDebug(lcDbusWindow) << "getPreFloatZone for" << windowId << "-> found:" << !zoneId.isEmpty() << "zone:" << zoneId;
    return !zoneId.isEmpty();
}

void WindowTrackingAdaptor::clearPreFloatZone(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return;
    }
    // Only log if there was something to clear
    bool hadPreFloatZone = !m_service->preFloatZone(windowId).isEmpty();
    // Delegate to service
    m_service->clearPreFloatZone(windowId);
    if (hadPreFloatZone) {
        qCDebug(lcDbusWindow) << "Cleared pre-float zone for window" << windowId;
    }
}

bool WindowTrackingAdaptor::isWindowFloating(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return false;
    }
    // KNOWN ASYMMETRY with setWindowFloatingForScreen: this query answers
    // through the WTS resolver, which routes by the window's TRACKED screen's
    // current mode, while the setter routes by the caller-supplied (effect's
    // authoritative live) screen. Mid-mode-flip the two can briefly disagree —
    // the query reads the slot of the mode the tracking still points at, one
    // beat behind the screen set the setter targets. Accepted: every caller of
    // this query wants the mode-lens answer (rule predicates, the effect's
    // float cache), the flip converges within the same signal cascade, and a
    // screen-scoped query would push the mode-resolution burden onto callers
    // that don't have an authoritative screen to supply.
    return m_service->isWindowFloating(windowId);
}

bool WindowTrackingAdaptor::queryWindowFloating(const QString& windowId)
{
    return isWindowFloating(windowId);
}

void WindowTrackingAdaptor::setWindowFloating(const QString& windowId, bool floating)
{
    if (!validateWindowId(windowId, QStringLiteral("set float state"))) {
        return;
    }
    // Suspension classification — see WindowTrackingService::isSuspensionFloat.
    // Unlike setWindowFloatingForScreen below, the clear here is UNCONDITIONAL
    // and happens before the write: this writer delegates to WTS, whose
    // engine-float writer sets the raw float bit and never routes through
    // SnapEngine::setWindowFloat, so there is no suspension gate downstream to
    // poison and nothing to preserve the classification for. Gating it instead
    // would strand the bit on any window the registry still reports minimized
    // at the moment of a genuine user unfloat (the registry lags the
    // unminimize edge by design).
    if (floating && m_windowRegistry && m_windowRegistry->minimizedState(windowId).value_or(false)) {
        m_service->markSuspensionFloat(windowId);
    } else if (!floating) {
        m_service->clearSuspensionFloat(windowId);
    }
    m_service->setWindowFloating(windowId, floating);
    // Gate the signal emissions on a real change in what we last BROADCAST, not
    // on a re-query of the service's float state. Under the per-engine float
    // model the owning engine flips its float bit BEFORE the daemon's sync slot
    // reaches this writer (e.g. AutotileEngine::performToggleFloat toggles then
    // emits windowFloatingChanged, which Daemon::syncAutotileFloatState relays
    // here), so m_service->isWindowFloating() already reports the post-transition
    // value — the old `floating == wasFloating` gate then suppressed every
    // autotile float broadcast, leaving subscribers (the effect's float cache,
    // which the autotile FFM float-pause depends on) permanently stale.
    // The gate itself lives in relayWindowFloatingChanged (the single owner of
    // the last-broadcast contract); the unified state change and placement
    // capture below ride the same edge.
    // Use the window's tracked screen if available, otherwise fall back to last
    // active screen.
    const QString screen = m_service->screenForWindow(windowId, m_lastActiveScreenId);
    if (!relayWindowFloatingChanged(windowId, floating, screen)) {
        return;
    }
    qCInfo(lcDbusWindow) << "Window" << windowId << "is now" << (floating ? "floating" : "not floating");

    // Emit unified state change
    Q_EMIT windowStateChanged(windowId,
                              PhosphorProtocol::WindowStateEntry{
                                  windowId,
                                  m_service->zoneForWindow(windowId),
                                  screen,
                                  floating,
                                  floating ? QStringLiteral("floated") : QStringLiteral("unfloated"),
                                  QStringList{},
                                  false,
                              });

    // Unified model: refresh the live placement record on float toggle.
    captureWindowPlacement(windowId);
}

QStringList WindowTrackingAdaptor::getFloatingWindows()
{
    // USER floats only — suspension floats (minimize-as-float) are filtered
    // out, and consumers re-seeding float state from this list therefore
    // never adopt a suspension float (the same contract the D-Bus XML
    // annotation states). Known consumers: the effect's daemon-bringup
    // re-seed of its FloatingCache, and PhosphorCompositor::DaemonClient's
    // queryFloatingWindows relay. Seeding a minimize-float into the effect's
    // cache makes its restart claim sweep read it as a user float and skip
    // adopting it into minimize-float ownership, leaving an unowned float
    // that never re-tiles on unminimize. The effect's own claim paths (batch
    // claim at daemon ready, claimAlreadyMinimizedAsFloated) are the owners
    // of suspension floats and re-establish their state without this reply.
    // A REFUSED suspension unfloat keeps its classification (see
    // setWindowFloatingForScreen), so a still-floating window on the
    // unminimize path stays filtered here too.
    QStringList result = m_service->floatingWindows();
    result.removeIf([this](const QString& windowId) {
        return m_service->isSuspensionFloat(windowId);
    });
    return result;
}

bool WindowTrackingAdaptor::applyGeometryForFloat(const QString& windowId, const QString& screenId)
{
    // CALLER CONTRACT: callers must guarantee a non-minimized window. Unlike
    // the snap engine's applyFloatGeometryUnlessMinimized twin, this method
    // has no minimize guard — it is not exposed on D-Bus, and its sole caller
    // (Daemon::syncAutotileFloatState) is the user-toggle path whose passive
    // sync twin deliberately skips it for exactly this reason.
    auto geo = m_service->validatedUnmanagedGeometry(windowId, screenId);
    if (geo) {
        qCInfo(lcDbusWindow) << "applyGeometryForFloat: windowId=" << windowId << "geo=" << *geo
                             << "screen=" << screenId;
        Q_EMIT applyGeometryRequested(windowId, geo->x(), geo->y(), geo->width(), geo->height(), QString(), screenId,
                                      false);
        return true;
    }

    qCInfo(lcDbusWindow) << "applyGeometryForFloat: no geometry found for" << windowId;
    return false;
}

// WindowTrackingAdaptor::clearFloatingStateForSnap was removed — all
// snap-commit paths now route through PhosphorSnapEngine::SnapEngine::commitSnap
// which handles clearing the floating state internally and emits
// windowFloatingClearedForSnap, which this adaptor relays to its own
// windowFloatingChanged D-Bus signal in the constructor wiring.

bool WindowTrackingAdaptor::relayWindowFloatingChanged(const QString& windowId, bool floating, const QString& screenId)
{
    // The single owner of the last-broadcast dedup contract — see the header
    // doc: every emission channel must keep m_broadcastFloating equal to what
    // subscribers last heard, or the gate turns from a dedup into a
    // suppressor of genuine changes. setWindowFloating delegates here too.
    // DELIBERATE divergence: the dedup keys on the CANONICAL (shadow) id so a
    // class-mutated window dedups as one window, while the signal carries the
    // CALLER'S raw id — subscribers resolve raw ids through their own
    // canonicalization, and rewriting the id here would desync them from the
    // other per-window signals on the same edge.
    const QString shadowId = shadowWindowId(windowId);
    // Absent entry NEVER counts as a match. The map is populated only by this
    // relay, so after a daemon restart it is empty while WTS has restored
    // floating windows and subscribers still believe "floating" — treating
    // absent as false would swallow the first genuine unfloat entirely (no
    // signal, and via setWindowFloating's early return no state-change
    // emission or capture refresh either). The cost is one redundant
    // broadcast per window on its first relay, which subscribers absorb
    // idempotently.
    const auto it = m_broadcastFloating.constFind(shadowId);
    if (it != m_broadcastFloating.constEnd() && it.value() == floating) {
        return false;
    }
    m_broadcastFloating[shadowId] = floating;
    Q_EMIT windowFloatingChanged(windowId, floating, screenId);
    return true;
}

void WindowTrackingAdaptor::setWindowFloatingForScreen(const QString& windowId, const QString& screenId, bool floating)
{
    if (!validateWindowId(windowId, QStringLiteral("set float for screen"))) {
        return;
    }
    // Boundary validation: an empty screenId makes isActiveOnScreen("") read
    // false and silently routes an autotiled window's float traffic to the
    // snap engine — a dead-end on the wrong slot. Recover via the window's
    // tracked screen when possible; otherwise refuse loudly.
    QString effectiveScreenId = screenId;
    if (effectiveScreenId.isEmpty() && m_service) {
        effectiveScreenId = m_service->screenForWindow(windowId);
    }
    if (effectiveScreenId.isEmpty()) {
        qCWarning(lcDbusWindow) << "setWindowFloatingForScreen: no screen for" << windowId << "— refusing";
        return;
    }

    qCInfo(lcDbusWindow) << "setWindowFloatingForScreen: windowId=" << windowId << "floating=" << floating
                         << "screen=" << effectiveScreenId;

    // Classify BEFORE routing so any capture the write triggers synchronously
    // already sees the suspension bit. The minimize-edge caller pushes fresh
    // metadata ahead of float traffic on the same edge; the effect's mode-swap
    // and re-minimize RE-ASSERT callers arrive later and rely on the registry
    // bit still being set from that edge.
    // DECLASSIFY (the unfloat side) only AFTER routing, and only
    // conditionally: the SNAP engine's setWindowFloat reads isSuspensionFloat
    // to gate the suspension unfloat contract (no SnapToZone rule target, no
    // cross-monitor restore, no fallback zone — Discussion #724), and
    // clearing the bit before the routed call turned that gate into a
    // constant false — every suspension unfloat ran as a user float toggle.
    if (floating && m_windowRegistry && m_windowRegistry->minimizedState(windowId).value_or(false)) {
        m_service->markSuspensionFloat(windowId);
    }

    // Route to the correct engine based on screen mode. Both directions go
    // through the explicit cross-engine handoff contract when the window
    // isn't yet tracked by the destination engine. The scrolling engine is
    // a first-class destination here: the effect fires this call for
    // minimize-float / unminimize-unfloat and drag ApplyFloat on EVERY
    // engine-managed screen, and routing a scrolling screen to snap would
    // apply the float bit to an engine that does not own the window.
    PhosphorEngine::PlacementEngineBase* dest = nullptr;
    if (m_autotileEngine && m_autotileEngine->isActiveOnScreen(effectiveScreenId)) {
        dest = m_autotileEngine.data();
    } else if (m_scrollEngine && m_scrollEngine->isActiveOnScreen(effectiveScreenId)) {
        dest = m_scrollEngine.data();
    } else if (m_snapEngine) {
        dest = m_snapEngine.data();
    } else {
        // Every engine QPointer null (late-shutdown D-Bus traffic): a silent
        // no-op on a public entry point is undiagnosable — log like every
        // other bail in this file.
        qCWarning(lcDbusWindow) << "setWindowFloatingForScreen: no engine wired for" << effectiveScreenId;
        return;
    }
    // Source: whichever OTHER engine currently tracks the window (the
    // window may be crossing engines when its float bit flips mid-move).
    PhosphorEngine::PlacementEngineBase* source = nullptr;
    const std::array<PhosphorEngine::PlacementEngineBase*, 3> candidates{
        static_cast<PhosphorEngine::PlacementEngineBase*>(m_autotileEngine.data()),
        static_cast<PhosphorEngine::PlacementEngineBase*>(m_scrollEngine.data()),
        static_cast<PhosphorEngine::PlacementEngineBase*>(m_snapEngine.data())};
    for (PhosphorEngine::PlacementEngineBase* candidate : candidates) {
        if (candidate && candidate != dest && candidate->isWindowTracked(windowId)) {
            source = candidate;
            break;
        }
    }

    // Float: adopt an untracked window unconditionally (a brand-new floating
    // dialog is a valid target — fromEngineId stays empty when neither side
    // tracks it, so receive-side reasoning that depends on the source mode
    // correctly degrades).
    // Unfloat of a window whose float bit lives in the OTHER engine: without
    // handling, the unfloat dead-ends — setWindowFloat below targets the
    // destination engine, which doesn't track the window and early-returns,
    // while the source engine's float bit stays set with no broadcast, so
    // the window reads floating through its own context's mode lens forever.
    // ADOPTING destinations (autotile AND scrolling) differ from a SNAP one:
    //   - Adopting dest: adopt via the handoff (release source, receive with
    //     wasFloating=false tiles the arrival and announces it on the
    //     passive sync channel); the trailing relay dedups against that.
    //   - Snap dest: NO adoption. A handoffReceive here would add nothing:
    //     with no sourceZoneIds and wasFloating=false its tail lands the
    //     window as a plain free window, and the setWindowFloat(false) below
    //     fails open when no rule/pre-float zone resolves ("keeping
    //     floating"). Instead just release the source's bit and broadcast
    //     the unfloat; the setWindowFloat below still gives snap its
    //     rule/pre-float re-snap chance, and a window it cannot re-snap
    //     stays a free (unmanaged) window, which is what unfloating a
    //     window snap never tracked means.
    bool recaptureAfterFloatWrite = false;
    bool skipDestFloatCall = false;
    if (dest && !dest->isWindowTracked(windowId)) {
        const bool sourceTracked = source && source->isWindowTracked(windowId);
        // Autotile AND scrolling destinations adopt via the handoff (their
        // handoffReceive tiles a wasFloating=false arrival); only a snap
        // destination takes the no-adoption unfloat path documented above.
        const bool destAdopts =
            (m_autotileEngine && dest == m_autotileEngine.data()) || (m_scrollEngine && dest == m_scrollEngine.data());
        if (floating || (sourceTracked && destAdopts)) {
            PhosphorEngine::IPlacementEngine::HandoffContext ctx;
            ctx.windowId = windowId;
            ctx.toScreenId = effectiveScreenId;
            ctx.wasFloating = floating;
            // Canonical-vs-canonical (windowActivated stores the shadow id):
            // a floating arrival's receive seeds its focus memory from this,
            // since a focus-keeping handoff produces no report.
            ctx.heldFocus = m_lastActiveWindowId == shadowWindowId(windowId);
            QString recoverScreen;
            if (sourceTracked) {
                ctx.fromEngineId = source->engineId();
                ctx.sourceGeometry = frameGeometry(windowId);
                ctx.minSize = source->windowMinimumSize(windowId);
                recoverScreen = source->screenForTrackedWindow(windowId);
            }
            // Guarded: a dest whose screen left its engine set between the
            // isActiveOnScreen pick above and the receive would otherwise
            // strand the window (released, refused) while subscribers are
            // told it is not floating.
            const bool adopted =
                WindowTrackingInternal::guardedHandoff(sourceTracked ? source : nullptr, dest, ctx, recoverScreen);
            if (!floating && adopted) {
                relayWindowFloatingChanged(windowId, false, effectiveScreenId);
            }
            // A failed FLOAT adoption whose recovery TOOK was already
            // resolved by guardedHandoff: the window is re-homed into the
            // source engine as floating and the source's own handoffReceive
            // announced the truthful floating state (autotile/scroll on the
            // passive synced channel, snap on the active one — either way it
            // matches the caller's pre-latch). The dest call below could only
            // take the no-key refusal branch and announce synced(false) on
            // top of that recovery — a last-write-wins clobber of durable
            // float state for a window the source genuinely holds floating.
            //
            // Skip ONLY when the recovery verifiably holds the window: with
            // no source, an empty recover screen, or a double refusal,
            // guardedHandoff returns false having announced NOTHING, and the
            // dest refusal branch is then the one edge that corrects the
            // caller's pre-latched cache (the #1028 stale-latch class).
            if (floating && !adopted && sourceTracked && source->isWindowTracked(windowId)) {
                skipDestFloatCall = true;
            }
        } else if (sourceTracked) {
            source->handoffRelease(windowId);
            relayWindowFloatingChanged(windowId, false, effectiveScreenId);
            recaptureAfterFloatWrite = true;
        } else if (!floating) {
            // Fourth case: unfloat with NEITHER engine tracking the window
            // (its tracking was torn down while minimized/mode-flipped). The
            // setWindowFloat below can fail open with no signal at all, which
            // leaves the effect's float cache stuck at the last broadcast
            // (typically true from a minimize-float) until a restart — so the
            // not-floating edge must always reach subscribers.
            relayWindowFloatingChanged(windowId, false, effectiveScreenId);
            recaptureAfterFloatWrite = true;
        }
    }

    // dest is provably non-null here: the routing chain above returns when
    // neither engine is wired.
    // Thread the effect's authoritative live screen so the engine resolves
    // this float/unfloat against the window's REAL current monitor, not its
    // (possibly stale) tracked association. Without this, unfloating a window
    // that drifted to another monitor while floating non-deterministically
    // teleports it back to its source-monitor zone (Discussion #724).
    if (!skipDestFloatCall) {
        dest->setWindowFloat(windowId, floating, effectiveScreenId);
    }

    // Declassify only after the routed engine call above has consumed the
    // suspension bit, and — on the SNAP destination — only when the unfloat
    // actually landed. A REFUSED suspension unfloat (the engine's
    // cross-monitor confinement kept the window floating in place) must stay
    // classified: the effect retries the unminimize unfloat on a timer, and an
    // unconditional clear here made every retry run as an unconfined USER
    // unfloat — reinstating the Discussion #724 teleport 250 ms late.
    //
    // The question is asked of the SNAP ENGINE directly, not through
    // WTS::isWindowFloating: that facade answers through the mode lens of the
    // window's TRACKED screen, while this function routed by the caller's
    // live screen, so mid-mode-flip it can report the OTHER engine's bit (see
    // the KNOWN ASYMMETRY note on isWindowFloating). Only the snap engine
    // implements the suspension contract; an autotile destination never reads
    // the bit, so it declassifies unconditionally as before.
    // The clear lands before the recapture below so that capture reads the
    // window's real post-unfloat state instead of taking the minimize-preserve
    // path. (The commitSnap-wired capture that fires INSIDE the routed call,
    // while the bit is still set, stays correct for a different reason: it
    // passes fromStateChange=true, which bypasses that guard entirely.)
    if (!floating) {
        const bool snapSlotDest = m_snapEngine && dest == m_snapEngine.data();
        if (snapSlotDest && !m_cachedSnapEngine) {
            // Reduced wiring (a non-SnapEngine in the snap slot): the
            // suspension contract cannot be evaluated, so the clear degrades
            // to unconditional. Say so rather than degrading silently.
            qCDebug(lcDbusWindow) << "setWindowFloatingForScreen: snap slot holds a non-SnapEngine for" << windowId
                                  << "— declassifying unconditionally";
        }
        const bool stillFloating = snapSlotDest && m_cachedSnapEngine && m_cachedSnapEngine->isFloating(windowId);
        if (!stillFloating) {
            m_service->clearSuspensionFloat(windowId);
        }
    }
    if (recaptureAfterFloatWrite) {
        // Snap-dest unfloat: re-anchor the live placement record after the
        // float write. When unfloatToZone above re-snapped the window this is
        // an idempotent repeat of the commitSnap-wired capture, kept so the
        // record refresh doesn't silently depend on that wiring's ordering.
        // When unfloatToZone FAILED OPEN (no rule/pre-float zone) the window
        // is tracked by NEITHER engine, so this capture deliberately writes
        // nothing and the persisted record keeps its floating-at-position
        // slot from the source engine's float. That is the accepted outcome:
        // a later restore of "floating at its position" and of "free window
        // at its position" land the window identically, and the only writer
        // that could record the free state has no engine placement to read.
        // Live subscribers are unaffected either way — the broadcast above
        // already told them not-floating.
        captureWindowPlacement(windowId);
    }
}

} // namespace PlasmaZones

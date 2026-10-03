// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Per-window output wiring for PlasmaZonesEffect: the cross-output move
// (KWin outputChanged), the virtual-screen crossing detector that runs off
// frame geometry because outputChanged cannot see a crossing inside one
// monitor, the tracked-screen stamp both diff against, and the flags-settle
// eviction backstop. Called once per window from setupWindowConnections.

#include "plasmazoneseffect.h"

#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <effect/effecthandler.h>
#include <window.h>

#include <QPointer>

#include "tilinghandler/tilinghandler.h"
#include "handlers/dragtracker.h"
#include "handlers/screenchangehandler.h"

namespace PlasmaZones {

void PlasmaZonesEffect::wireOutputChangeHandlers(KWin::EffectWindow* w)
{
    // Detect when a window moves between monitors (e.g., "Move to Screen Right").
    // KWin::Window::outputChanged fires once when the window's output property changes.
    // Transfer the window from the old screen's autotile state to the new screen's state,
    // and unsnap any snapped window that crosses screens.
    KWin::Window* kw = w->window();
    if (kw) {
        QPointer<KWin::EffectWindow> safeW = w;
        // Track the window's screen ID so we can detect cross-screen moves for snapping windows
        // (not tracked by the autotile handler's m_notifiedWindowScreens). Where
        // it is going, when a move is in flight: a window wired by a reloaded
        // effect between a move request and its ack sits on the output it is
        // leaving.
        m_trackedScreenPerWindow[w] = pendingWindowScreenId(w);
        // Flags-settle eviction backstop: a client can set keep-above,
        // skip-switcher or its transient parent AFTER mapping (Yakuake
        // queues the first two in its map-time request burst; another client
        // may flip one seconds later). Each of those flips a structural
        // placement filter, and without these the map-time tileability
        // verdict was permanent — the pre-settle window got inserted,
        // focused and column-sized. The one-tick routing defer in
        // slotWindowAdded harvests the same-burst case before any insert;
        // these catch the late case and release the window
        // (reevaluateWindowEligibility gates itself on announced windows, so
        // the connection is free for everything else).
        //
        // transientChanged / modalChanged are the arms the keep-above pair
        // could not reach: on Wayland an xdg_toplevel's set_parent and
        // set_modal arrive as their own requests after the initial commit,
        // so a dialog can map as a parentless normal toplevel and only
        // become transient a beat later. Both are structural rejects in
        // shouldHandleWindow and isTileableWindow, and window TYPE has no
        // signal of its own, so transientChanged is also the only handle on
        // the isDialog() reject for clients whose dialog type KWin derives
        // from the transient relationship.
        //
        // What this does NOT cover, so nobody re-derives it from the
        // Yakuake bug report: a dialog whose parent toplevel is destroyed
        // BEFORE the dialog maps. Measured live 2026-08-30 — Yakuake's
        // dropdown closed 32 ms before its First Run dialog arrived, so
        // transientFor() was null permanently rather than late, and the
        // dialog presented as a plain normal toplevel (resizable,
        // unbounded maxSize, not special, not modal) that KWin never
        // revises. No signal fires because no state changes, so neither
        // these arms nor a longer settle defer can catch it; an Exclude
        // rule is the only lever.
        connect(kw, &KWin::Window::keepAboveChanged, this, [this, safeW](bool) {
            m_tilingHandler->reevaluateWindowEligibility(safeW.data());
        });
        connect(kw, &KWin::Window::skipSwitcherChanged, this, [this, safeW]() {
            m_tilingHandler->reevaluateWindowEligibility(safeW.data());
        });
        connect(kw, &KWin::Window::transientChanged, this, [this, safeW]() {
            m_tilingHandler->reevaluateWindowEligibility(safeW.data());
        });
        connect(kw, &KWin::Window::modalChanged, this, [this, safeW]() {
            m_tilingHandler->reevaluateWindowEligibility(safeW.data());
        });
        connect(kw, &KWin::Window::outputChanged, this, [this, safeW]() {
            if (!safeW || safeW->isDeleted()) {
                return;
            }
            // The window's screen, and where it is going when this output
            // change is the ack of a move KWin has since been asked to
            // replace: a held "move to output" key (or a move reversed inside
            // one round trip) acks the intermediate output after the daemon
            // stored the final one, and reading the frame unsnapped the window
            // there (see pendingWindowScreenId).
            const QString newScreenId = pendingWindowScreenId(safeW);
            // The daemon's focused window moved: report it (see
            // reportActiveWindowScreen). Ahead of the apply gate below on
            // purpose: a move the daemon drives is still a move of the focused
            // window, and the screen answers from the engine for a strip tile,
            // so a parked column crossing outputs does not flip the record.
            reportActiveWindowScreen(safeW, newScreenId);
            // Daemon-driven geometry applies must not be mistaken for user
            // moves (symmetric with the frameGeometryChanged VS-crossing
            // handler below). This matters for the scrolling engine: parked
            // columns sit ENTIRELY outside the screen rect, so on a
            // multi-head layout the parked frame's centre can land on the
            // neighbouring output — KWin fires outputChanged and, without
            // this guard, the parked window would be handed to the other
            // screen's engine mid-apply.
            if (m_daemonGate.inGeometryApply) {
                return;
            }
            const QString oldScreenId = m_trackedScreenPerWindow.value(safeW);
            m_trackedScreenPerWindow[safeW] = newScreenId;
            // A cross-screen move changes the Mode/screenId inputs of the
            // window's cached rule verdict (tiling vs scrolling screens
            // especially); nothing else invalidates it when the window stays
            // tiled through the move. The invalidation itself is issued
            // below, once the involuntary-move and mid-drag gates have been
            // applied — an unconditional one here bypassed both (and ran the
            // per-window decoration rebuild mid-drag, which the drag deferral
            // exists to avoid).

            // Detect involuntary moves up front: when a monitor drops out
            // (DPMS standby on Wayland, hotplug-unplug) KWin reassigns the
            // windows that were on it to a remaining output and fires
            // outputChanged for each — even though the user did nothing. Both
            // the autotile and snapping paths below must skip these, because
            // routing them through the normal cross-screen logic would either
            // tile a window from the disabled monitor into the active
            // autotile zone (discussion #527) or fire a spurious unsnap.
            // Recovery is owned by the daemon's virtualScreensReconfigured /
            // ScreenChangeHandler debounce, which resettles assignments once
            // the screen change has stopped chattering.
            bool oldScreenStillConnected = false;
            for (const auto* output : KWin::effects->screens()) {
                if (outputScreenId(output) == oldScreenId) {
                    oldScreenStillConnected = true;
                    break;
                }
            }
            const bool involuntaryMove = !oldScreenId.isEmpty()
                && (!oldScreenStillConnected || m_screenChangeHandler->isScreenChangeInProgress());

            // Delegate autotile handling (autotile→autotile, autotile→snapping, etc.)
            // This must run even during drag so the autotile engine removes the
            // window from the old screen's tiling state immediately. The
            // involuntary-move guard is the symmetric partner of the snapping
            // guard further down — before #527, only the snapping path was
            // protected and KWin's orphan-reassignment got mistaken for the
            // window genuinely entering autotile.
            if (!involuntaryMove) {
                m_tilingHandler->handleWindowOutputChanged(safeW);
            }

            // A genuine screen change stales this window's cached rule verdict.
            // The verdict cache is keyed on (windowId, rule-set revision) and
            // neither moves here, while ScreenId, ScreenOrientation and (since the
            // ActiveLayout wire) the screen's active layout are all per-screen
            // match inputs — so without this the window keeps matching against the
            // monitor it came FROM, indefinitely, because two monitors sitting on
            // unchanged layouts produce no broadcast to correct it.
            //
            // Gated exactly like the daemon notify below: not for KWin's
            // orphan-reassignment when a monitor drops out, and not mid-drag,
            // where the drag system owns the transitions and the deferred flush's
            // decoration rebuild has not been established as safe.
            //
            // Mid-drag the invalidation is deferred, not dropped: the id goes
            // into m_dragSuppressedRuleInvalidations and callEndDrag drains it
            // once the daemon's outcome has been applied. Nothing at drag end
            // could rediscover the crossing on its own, because the stamp above
            // already made the tracked screen equal to the live one.
            if (!oldScreenId.isEmpty() && oldScreenId != newScreenId && !involuntaryMove) {
                if (m_dragTracker->isDragging()) {
                    m_dragSuppressedRuleInvalidations.insert(getWindowId(safeW));
                } else {
                    invalidateRuleCacheForStateChange(getWindowId(safeW));
                }
            }

            // For snapping→snapping cross-screen moves: notify the daemon which
            // decides whether to unsnap based on its own state. If the daemon just
            // assigned this window to the new screen (restore/resnap/snap assist),
            // the stored screen matches and no unsnap occurs. If the user moved
            // the window via "Move to Screen" shortcut, the stored screen differs
            // and the daemon unsnaps.
            // Skip during drag: the drag system owns snap state transitions
            // (float, unsnap, size restore, pre-tile cleanup) and handles them
            // in dragStopped() with richer context.
            // Skip involuntary moves: see the involuntaryMove computation above.
            if (!oldScreenId.isEmpty() && oldScreenId != newScreenId && !m_tilingHandler->isManagedScreen(oldScreenId)
                && !m_tilingHandler->isManagedScreen(newScreenId) && !m_dragTracker->isDragging() && !involuntaryMove) {
                const QString windowId = getWindowId(safeW);
                PhosphorProtocol::ClientHelpers::fireAndForget(
                    this, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("windowScreenChanged"),
                    {windowId, newScreenId}, QStringLiteral("cross-screen move"));
            }
        });
        // Virtual screen boundary detection: KWin's outputChanged only fires when
        // the physical monitor changes. Moving a window between virtual screens on the
        // same physical monitor (e.g., A/vs:0 → A/vs:1) is invisible to outputChanged.
        // Detect these crossings via frameGeometryChanged, using the same trackedScreen
        // state as the outputChanged handler above.
        // (The autotile handler has its own detection in slotWindowFrameGeometryChanged;
        // this covers snapping-mode windows which autotile doesn't track.)
        //
        // VS crossing detection uses PhosphorIdentity::VirtualScreenId::isVirtualScreenCrossing()
        // (<PhosphorIdentity/VirtualScreenId.h>) — the same predicate used by
        // tilinghandler/tiling.cpp.
        connect(safeW, &KWin::EffectWindow::windowFrameGeometryChanged, this, [this, safeW]() {
            if (!safeW || safeW->isDeleted() || m_virtualScreenDefs.isEmpty() || !m_daemonGate.virtualScreensReady) {
                return;
            }
            // Suppress crossing detection while the daemon is moving this window in response
            // to a VS swap/rotate or resnap. The cached m_virtualScreenDefs may still hold
            // pre-rotation regions when the geometry change fires synchronously from
            // applyWindowGeometry, so getWindowScreenId would resolve the new position against
            // stale boundaries and report a phantom crossing.
            if (m_daemonGate.inGeometryApply) {
                return;
            }
            // Pending-aware like the output arm: a frame change that acks a
            // superseded move must not read as a crossing to where the window
            // is no longer going.
            const QString newScreenId = pendingWindowScreenId(safeW);
            const QString oldScreenId = m_trackedScreenPerWindow.value(safeW);
            if (!PhosphorIdentity::VirtualScreenId::isVirtualScreenCrossing(oldScreenId, newScreenId)) {
                return;
            }
            m_trackedScreenPerWindow[safeW] = newScreenId;
            // A virtual-screen crossing moves the focused window between the
            // screens the daemon's shortcuts act on, exactly like an output
            // change, and outputChanged never fires for one.
            reportActiveWindowScreen(safeW, newScreenId);

            // A virtual-screen crossing stales this window's cached rule verdict
            // exactly like the physical cross-screen move above: ScreenId,
            // ScreenOrientation and the screen's active layout are all per-screen
            // match inputs, and the verdict cache is keyed on (windowId, rule-set
            // revision), neither of which moves here. Same gating as the sibling —
            // not mid-drag, where the drag system owns the transitions — and it
            // runs ahead of the autotile / daemon delegation below, which return
            // early for tracked and autotile-screen windows whose verdicts are
            // stale all the same. The sibling's non-empty / differs terms are
            // already guaranteed here by isVirtualScreenCrossing above. Mid-drag
            // the id is parked in m_dragSuppressedRuleInvalidations for
            // callEndDrag to drain, exactly as the sibling does, because the
            // stamp above leaves nothing at drag end to detect the crossing from.
            if (m_dragTracker->isDragging()) {
                m_dragSuppressedRuleInvalidations.insert(getWindowId(safeW));
            } else {
                invalidateRuleCacheForStateChange(getWindowId(safeW));
            }

            // Skip during drag — the drag system owns state transitions.
            // Autotile drag handles VS transfers via the drag-policy-changed path.
            // Snapping drag handles cross-screen unsnap on drag-stop via the daemon.
            if (m_dragTracker->isDragging()) {
                return;
            }

            // Skip VS detection for autotile-tracked windows — the autotile
            // handler's slotWindowFrameGeometryChanged owns VS crossing for
            // windows it already tracks (m_notifiedWindows). Only untracked
            // windows (snapping-mode entering an autotile VS) need delegation.
            const QString windowId = getWindowId(safeW);
            if (m_tilingHandler->isTrackedWindow(windowId)) {
                return;
            }

            // Delegate autotile handling for untracked cross-VS transitions
            // (snapping→autotile). The autotile handler's own detection only
            // covers windows it already tracks.
            m_tilingHandler->handleWindowOutputChanged(safeW);

            // For snapping→snapping cross-VS moves: notify the daemon
            if (!m_tilingHandler->isManagedScreen(oldScreenId) && !m_tilingHandler->isManagedScreen(newScreenId)
                && !m_screenChangeHandler->isScreenChangeInProgress()) {
                PhosphorProtocol::ClientHelpers::fireAndForget(
                    this, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("windowScreenChanged"),
                    {windowId, newScreenId}, QStringLiteral("virtual screen crossing"));
            }
        });

        // Clean up the tracked screen entry when the window is destroyed. Capture the RAW
        // pointer value, not the QPointer: inside a destroyed() slot the QPointer has already
        // been nulled, so removing `safeW` would remove the null key and leave the real entry
        // to leak. The pointer is only ever a lookup key here, never dereferenced, so its
        // value is exactly what remove() needs.
        KWin::EffectWindow* const rawW = safeW;
        connect(safeW, &QObject::destroyed, this, [this, rawW]() {
            m_trackedScreenPerWindow.remove(rawW);
        });
    }
}

} // namespace PlasmaZones

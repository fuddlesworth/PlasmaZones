// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Per-window output wiring for PlasmaZonesEffect: the cross-output move
// (KWin outputChanged), the virtual-screen crossing detector that runs off
// frame geometry because outputChanged cannot see a crossing inside one
// monitor, the tracked-screen stamp both diff against, and the flags-settle
// eviction backstop. Called once per window from setupWindowConnections. The
// crossing bodies live in ScreenChangeHandler, which also replays a crossing
// deferred during a screen change at its settle.

#include "plasmazoneseffect.h"

#include <PhosphorIdentity/VirtualScreenId.h>

#include <core/output.h>
#include <effect/effecthandler.h>
#include <window.h>

#include <QPointer>

#include "tilinghandler/tilinghandler.h"
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
            // so a parked column crossing outputs does not flip the record (a
            // window a snap placement took off its strip is untracked by then
            // and answers by position).
            // Not onto KWin's placeholder output, which the daemon has no
            // screen for (F729).
            if (const KWin::Window* const win = safeW->window();
                !win || !win->moveResizeOutput() || !win->moveResizeOutput()->isPlaceholder()) {
                reportActiveWindowScreen(safeW, newScreenId);
            }
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
            // During a screen change the crossing waits for the settle. KWin
            // moves every window off an output that goes away, and back onto
            // one that returns, and neither is the user's move: the settle
            // reports what KWin moved to the daemon, which floats an evacuee
            // where it landed or re-seats a returned one in its parked place,
            // and replays only the crossings it calls user moves. Nothing is
            // re-homed and nothing is skipped (the old involuntary-move skip
            // lost a genuine move made during the debounce, and misread every
            // move off a split monitor as involuntary). Outside one, the body
            // runs at once and the daemon hears of every crossing that is the
            // window's own, whichever modes the two screens run.
            if (m_screenChangeHandler->isScreenChangeInProgress()) {
                m_screenChangeHandler->deferCrossing(safeW, oldScreenId);
                return;
            }
            m_screenChangeHandler->applyOutputCrossing(safeW, oldScreenId, newScreenId);
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

            // Inside a screen change the virtual-screen definitions may still
            // describe the old layout (they refresh on the daemon's replies),
            // so the crossing is deferred and resolved against the new ones
            // at the settle.
            if (m_screenChangeHandler->isScreenChangeInProgress()) {
                m_screenChangeHandler->deferCrossing(safeW, oldScreenId);
                return;
            }
            m_screenChangeHandler->applyVirtualScreenCrossing(safeW, oldScreenId, newScreenId);
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

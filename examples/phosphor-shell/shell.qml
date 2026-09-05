// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// NO `pragma ComponentBehavior: Bound` here, and it must not be added back.
// Both inline Components in this file are instantiated from C++ with a context
// that is not their declaring one: PerScreenPanels builds each BarHost with a
// fresh QQmlContext carrying `modelData`, and LayerPopoutTransport builds
// powerMenuComponent against the engine's root context. Bound rejects exactly
// that, with "Cannot instantiate bound component outside its creation context"
// — every bar and the power menu silently fail to create.

import Phosphor.Bar
import Phosphor.ControlCenter
import Phosphor.Ipc
import Phosphor.Launcher
import Phosphor.Notifications
import Phosphor.OSD
import Phosphor.Popout
import Phosphor.Power
import Phosphor.Shell
import Phosphor.Theme
import QtQuick

// Top-level composer for the dogfood shell. Phase 4.1 replaces the old
// single TopPanel + pushed-in data sources with the production bar:
// BarHost mounts one spectrum-rail bar per output, and each bar widget
// owns its own data source (Clock its SystemClock, Battery its UPowerHost,
// Tray its StatusNotifierHost, ...), so this file no longer wires
// clock/CPU/memory/battery into a panel.
//
// BarHost reads the BarRegistry context property (the IBarWidgetFactory
// owner, set by src/shell/main.cpp) to mount its widgets.
//
// PerScreenPanels rather than a Repeater: ShellEngine discovers panels by
// walking QObject children and then takes ownership of each one, so the
// instantiator has to parent what it builds and must never destroy it
// afterwards. See PerScreenPanels' class docs.
//
// This file is also the composition root for anything that spans several
// services. The bar's trailing buttons are only triggers; what they open
// lives elsewhere, and BarRegistry.widgetActivated is the seam that keeps
// Phosphor.Bar from depending on every surface it can summon.
//
// The legacy panel/popup/settings demo components (TopPanel, PanelPopupHost,
// SettingsWindow, ...) still ship in this module but are no longer composed
// here; their replacements are the Phase 4 surfaces (control center,
// notification center, power menu) reached through that seam.
Item {
    id: root

    PerScreenPanels {
        id: bars

        model: PhosphorShell.screens

        // The control center is tethered to THIS bar's chip (05 §8). Phase
        // 2 opens it as an ENGINE-PLACED PANE, a real toplevel the daemon
        // positions (A2 §4), and the bar draws only the tether to it; the
        // phase-1 pane painted inside the bar's surface stays as the
        // floating fallback for an output with no placement engine.
        //
        // Everything the delegate reads must come from a CONTEXT PROPERTY,
        // never an id in this file: PerScreenPanels builds each delegate
        // with a fresh QQmlContext carrying `modelData`, so shell.qml's ids
        // do not resolve inside one. Hence the open state lives on
        // ControlCenterRegistry and the fallback pane content is declared
        // inline here, in the delegate's own scope (the engine-placed
        // pane's content is `paneComponent` below, built by the transport
        // against the root context).
        delegate: BarHost {
            id: bar

            paneAnchor: "controlcenter"
            paneWidth: 380
            paneDepth: 460
            // Open only on the screen the registry says owns it, so a
            // multi-head setup shows one pane, on the bar that summoned it.
            //
            // Read through PanelWindow.screen, NOT modelData.screen:
            // PerScreenPanels deliberately withholds the screen role from
            // modelData because that map snapshots a raw QScreen* which
            // dangles on hot-unplug, while this property is QPointer-backed
            // and simply reads null once the output dies. Hence the guard.
            paneOpen: bar.screen ? ControlCenterRegistry.openScreen === bar.screen.name : false
            // External: the open pane is a toplevel, so no inline pane,
            // only the tether routed to where the placement map says it is.
            paneExternal: ControlCenterRegistry.paneExternal

            // The bar is the only thing that can locate the pane (a
            // Wayland client is never told where its toplevel went), so it
            // reports the frame back for the pane's own top band.
            onPaneScreenRectChanged: ControlCenterRegistry.reportPaneRect(bar.paneScreenRect)

            // The pane transport asks which mode this output runs before
            // opening; none means the floating fallback.
            readonly property int screenMode: bar.placementMap ? bar.placementMap.mode : -1
            function reportMode(): void {
                if (bar.screen)
                    ControlCenterRegistry.reportScreenMode(bar.screen.name, bar.screenMode);
            }
            onScreenModeChanged: reportMode()
            onScreenChanged: reportMode()

            // Band material (05 §5, A2 §3.2): phosphor-glass is real
            // backdrop blur under a navy tint. A client cannot sample what
            // is behind its surface, so the blur is the compositor's,
            // requested behind the band's rect once the surface exists and
            // again whenever the band's rect changes.
            readonly property var bandWindow: bar.bandItem ? bar.bandItem.Window.window : null
            function applyBlur(): void {
                if (bar.bandWindow)
                    ShellEffects.setBlurBehind(bar.bandItem, bar.bandRect);
            }
            onBandWindowChanged: applyBlur()
            onBandRectChanged: applyBlur()
            Component.onCompleted: {
                reportMode();
                applyBlur();
            }

            // ONE CONTROL CENTER PER SCREEN for the fallback, built on that
            // screen's first open and kept: the pane Loader latches active,
            // and the tiles hold live service connections that would be
            // torn down and re-enumerated on every close.
            //
            // The pane depth is a constant on the bar, and the pane
            // CLIPS: a rail list taller than the depth is cut off, so
            // adding rails means raising paneDepth to match, since the
            // surface reservation cannot grow after materialization.
            paneContent: Component {
                ControlCenter {
                    provider: ControlCenterRegistry
                    tileIds: ControlCenterRegistry.tileIds
                }
            }
        }
    }

    // The OSD overlay, one per output. OSDHost places a band on any screen
    // edge (a volume band on the focused window's bottom edge, a
    // brightness band down the right), so it needs the WHOLE screen: a
    // Top panel whose thickness is the screen's height, on the Overlay
    // layer so it draws above the bar, with no exclusive zone (an overlay
    // cannot reserve one, and this one must not) and an EMPTY input
    // region, which is PanelWindow's click-through case. Nothing here is
    // ever clickable; the OSD is pure feedback.
    //
    // Same context rule as the bar above: the delegate reads OsdRegistry
    // (a context property) and never an id from this file. The host
    // attaches itself to the registry so the `osd` IpcTarget below can
    // reach every screen's host through it; OSDHost's own targetScreen
    // filter decides which one draws.
    PerScreenPanels {
        id: osdOverlays

        model: PhosphorShell.screens

        delegate: PanelWindow {
            id: osdSurface

            edge: PanelWindow.Top
            alignment: PanelWindow.Fill
            thickness: modelData.height
            panelLayer: PanelWindow.LayerOverlay
            exclusiveZoneEnabled: false
            keyboardFocus: PanelWindow.None
            inputRegion: []

            OSDHost {
                id: osdHost

                anchors.fill: parent
                provider: OsdRegistry
                // Through PanelWindow.screen, not modelData.screen, for the
                // hot-unplug reason the bar delegate gives.
                screenName: osdSurface.screen ? osdSurface.screen.name : ""
                topInset: Tokens.bar_thickness
                Component.onCompleted: OsdRegistry.attachHost(osdHost)
                Component.onDestruction: OsdRegistry.detachHost(osdHost)
            }
        }
    }

    // The toast overlay, one per output, on its own surface rather than
    // the OSD's because the two want different input: OSD bands never
    // take a click, while a toast card needs hover (which pauses its
    // timer) and a close button. Same full-screen Overlay panel, but the
    // input region follows the cards: ToastHost publishes their rects and
    // PanelWindow opens input over exactly those, so everything around
    // them stays click-through and an empty stack passes every click to
    // the window beneath.
    PerScreenPanels {
        id: toastOverlays

        model: PhosphorShell.screens

        delegate: PanelWindow {
            id: toastSurface

            edge: PanelWindow.Top
            alignment: PanelWindow.Fill
            thickness: modelData.height
            panelLayer: PanelWindow.LayerOverlay
            exclusiveZoneEnabled: false
            keyboardFocus: PanelWindow.None
            inputRegion: toastHost.inputRects

            ToastHost {
                id: toastHost

                anchors.fill: parent
                screenName: toastSurface.screen ? toastSurface.screen.name : ""
                // The `notify` IpcTarget below lands on the primary
                // output's host, which is why the flag travels with the
                // attachment.
                Component.onCompleted: ToastRegistry.attachHost(toastHost, toastHost.screenName, modelData.isPrimary)
                Component.onDestruction: ToastRegistry.detachHost(toastHost)
            }
        }
    }

    // The engine-placed pane's content, built FRESH on every open by
    // PanePopoutTransport against the engine's root context (so only
    // context properties, never ids from this file), and destroyed with
    // the toplevel on close. Same declaration as the fallback above; the
    // two cannot share one Component because the delegate's is in a
    // PerScreenPanels context the transport cannot reach.
    Component {
        id: paneComponent

        ControlCenter {
            provider: ControlCenterRegistry
            tileIds: ControlCenterRegistry.tileIds
        }
    }

    // The one live SessionHost in the process. It must be a singleton in
    // practice even though the type is instantiable: each instance opens its
    // own logind connection and takes two real inhibitors (a sleep
    // delay-lock, and a grab on the power/suspend/hibernate/lid keys), so a
    // second one would double them. Everything that needs session actions
    // takes this one by reference.
    //
    // Instantiating it here also starts the lock-before-sleep handshake,
    // which had never actually run: the coordinator shipped in this module
    // but nothing ever created it.
    SessionLockCoordinator {
        id: sessionCoordinator
    }

    // The session menu, per docs/phosphor-shell-design/mockups/power-menu.svg:
    // a screen-centred Modal popout over a dimmed scrim, which is what
    // PhosphorPopout.ExclusiveMode.Modal means to the controller. It closes
    // every cooperative popout and suppresses new ones while it is up.
    //
    // `session` is NOT bound here. LayerPopoutTransport builds this
    // component against the ENGINE'S ROOT CONTEXT, where `sessionCoordinator`
    // — an id belonging to this file's scope — does not resolve, so the
    // binding threw "ReferenceError: sessionCoordinator is not defined" on
    // every open and the menu ran with an undefined session. It is handed
    // in through the request's `props` instead (see togglePowerMenu), which
    // the transport applies with setInitialProperties inside its
    // beginCreate/completeCreate window. That path also warns on a name the
    // delegate does not declare, where a stray binding fails silently.
    //
    // The same constraint is why this file must never gain
    // `pragma ComponentBehavior: Bound` (see the header): both inline
    // Components here are instantiated from C++ against a foreign context.
    Component {
        id: powerMenuComponent

        PowerMenu {}
    }

    // The control center is a pane tethered to the bar. main.cpp routes
    // the "control-center" id to a PanePopoutTransport, which opens it as
    // a toplevel the placement engine places (A2 §4), with the bar-socket
    // transport behind it as the floating fallback (an output with no
    // engine, or a toplevel that could not be built); both drive
    // ControlCenterRegistry.openScreen. Going through Popouts rather than
    // writing that property directly is what makes the Modal power menu
    // close it, refuses it while a modal is up, and drains it on reload,
    // all without this file remembering to.
    //
    // `content` is the pane's content for the toplevel; the fallback
    // ignores it and the delegate above mounts its own. `targetScreen` is
    // the output whose bar button fired, so a multi-head setup tethers the
    // pane to that bar. Keyboard focus: a toplevel takes it as a window,
    // which is what lets Escape close the pane (A2 §4.7). No dismiss on
    // focus loss: a pane is a tile, and tiles do not vanish when you look
    // elsewhere.
    // The control center and the launcher share the default popout
    // scope, so opening one CLOSES the other. That is the intended
    // behaviour for two full-attention surfaces triggered from the same
    // bar, and it is worth stating because nothing at either call site
    // hints at it: give one of them its own scope and they would happily
    // sit open together. The same scope is the A2 §4.7 arbitration for
    // panes: a second pane id opened here replaces the first.
    function toggleControlCenter(source: Item): void {
        // screenOf hands back a QScreen the C++ side owns; the controller
        // marks it CppOwnership before returning, so the JS GC cannot
        // delete the live screen when this wrapper is collected. Do not
        // reach for a QScreen any other way from QML.
        const target = ControlCenterRegistry.screenOf(source);
        const request = {
            "popoutId": "control-center",
            "content": paneComponent,
            "targetScreen": target,
            "exclusive": PhosphorPopout.ExclusiveMode.Cooperative,
            "keyboardFocus": true,
            "dismissOnFocusLoss": false
        };
        // The arbiter keys on the popout id alone, which is right for the
        // launcher and the power menu but not for a pane that belongs to
        // one bar. Without this check, pressing the button on a SECOND
        // monitor while the panel is open on the first just closes it, and
        // the screen this call went to the trouble of resolving is thrown
        // away. Move it instead: close there, open here.
        const openOn = ControlCenterRegistry.openScreen;
        if (openOn !== "" && target && openOn !== target.name) {
            const handle = Popouts.handleFor("control-center");
            if (handle !== "")
                Popouts.close(handle);
            Popouts.open(request);
            return;
        }
        Popouts.toggle(request);
    }

    function togglePowerMenu(): void {
        // Nothing to do about the control center here: it is a Cooperative
        // popout on the same controller, so opening this Modal one closes
        // it through the controller's own arbitration.
        // toggle rather than open: pressing the bar button (or Ctrl+Alt+Del)
        // a second time should put the menu away, and the controller rejects
        // a plain open for an id that is already showing.
        // Anchor and ExclusiveMode live on the PhosphorPopout namespace
        // element, so they are reached through it. QML_ELEMENT on a
        // Q_NAMESPACE publishes the namespace itself as the type name;
        // it does not put the enums in scope unqualified.
        Popouts.toggle({
            "popoutId": "power",
            "content": powerMenuComponent,
            // The session the menu acts on, passed as a prop rather than
            // bound in the Component: ids from this file do not resolve in
            // the root context the transport builds the delegate against.
            "props": {
                "session": sessionCoordinator.session
            },
            "anchor": PhosphorPopout.Anchor.ScreenCenter,
            "exclusive": PhosphorPopout.ExclusiveMode.Modal,
            "keyboardFocus": true,
            "dismissOnFocusLoss": true
        });
    }

    Connections {
        target: BarRegistry

        // `source` is the bar widget that fired. The session menu is
        // screen-centred and ignores it; the control center is anchored to
        // a bar, so it uses the widget's window to pick which output's
        // capsule to grow out of.
        function onWidgetActivated(id: string, source: Item): void {
            // A Cooperative open is refused outright while a Modal popout is
            // up, and the refusal is silent: the user would press the button
            // and see nothing happen, with nothing logged. The power menu is
            // itself Modal and toggles, so it stays reachable.
            if (Popouts.modalActive && id !== "power")
                return;
            if (id === "power")
                root.togglePowerMenu();
            else if (id === "controlcenter")
                // "controlcenter" is the bar widget's registered id
                // (barcontroller.cpp), not the IPC target name below.
                root.toggleControlCenter(source);
        }
    }

    // The wire surface, per the mockup's `phosphorctl call power.show`. The
    // bar button is one way in; this is the one a compositor keybind uses,
    // which is how Ctrl+Alt+Del reaches the menu without the shell claiming a
    // global shortcut of its own.
    //
    // `show` and `toggle` are separate because a method named show that hides
    // on the second call is a trap for anything scripting it. Bind a key to
    // toggle; call show from a script that wants the menu up regardless.
    // No hide() on this one, unlike the control center and the launcher.
    // The power menu is Modal and its own toggle() is the way back out,
    // so a caller that wants it gone calls that. A hide() would give two
    // ways to close one surface whose open state is already single-valued.
    IpcTarget {
        target: "power"

        function show(): void {
            if (!Popouts.isOpen("power"))
                root.togglePowerMenu();
        }

        function toggle(): void {
            root.togglePowerMenu();
        }
    }

    // The control center's wire surface, per the mockup's
    // `phosphorctl call control-center.open`. Same show/toggle split as the
    // power menu, and the same reason: bind a compositor key to toggle, and
    // call show from a script that wants the panel up regardless.
    //
    // All three are argument-free, which is the form a keybind uses:
    // IpcTarget arity is strict, so a `toggle(screen)` would reject a bare
    // `phosphorctl call control-center.toggle` with "argument count
    // mismatch". A null source resolves to the primary output's bar.
    IpcTarget {
        target: "control-center"

        function show(): void {
            if (!Popouts.isOpen("control-center"))
                root.toggleControlCenter(null);
        }

        function toggle(): void {
            root.toggleControlCenter(null);
        }

        function hide(): void {
            // close() on an empty handle is a no-op, so this is safe when
            // nothing is open.
            Popouts.close(Popouts.handleFor("control-center"));
        }
    }

    // The launcher, per docs/phosphor-shell-design/mockups/launcher-spotlight.svg:
    // a screen-centred Cooperative popout that takes keyboard focus (it is
    // a search field) and goes away on focus loss. Cooperative, not Modal:
    // it should close when you click away, not dim the screen and
    // suppress every other popout; and being Cooperative is what lets the
    // Modal power menu close it.
    //
    // Launcher paints its own card, so unlike the control center it needs
    // no panel wrapped around it. Everything it reads comes from the
    // LauncherResults context property src/shell/main.cpp installs on
    // every engine, and Popouts is a context property too, so this
    // Component is safe to build against the root context the transport
    // uses (the constraint that bit the power menu).
    Component {
        id: launcherComponent

        Launcher {
            results: LauncherResults
            // The viewfinder: this output's placement map, keyed by the
            // output the transport's layer window landed on.
            map: Screen.name ? PlacementMap.forScreen(Screen.name) : null
            // Built fresh on every open by the transport, so the reset that
            // clears the query and takes focus belongs here; the providers
            // behind the model are process-global and keep their state.
            Component.onCompleted: reset()
            onActivated: Popouts.close(Popouts.handleFor("launcher"))
            onDismissed: Popouts.close(Popouts.handleFor("launcher"))
        }
    }

    function toggleLauncher(): void {
        Popouts.toggle({
            "popoutId": "launcher",
            "content": launcherComponent,
            "anchor": PhosphorPopout.Anchor.ScreenCenter,
            "exclusive": PhosphorPopout.ExclusiveMode.Cooperative,
            "keyboardFocus": true,
            "dismissOnFocusLoss": true
        });
    }

    // The OSD's wire surface, per the OSD demo:
    // `phosphorctl call osd.show --arg kind=volume --arg value=62`. This is
    // how a compositor keybind for a volume or brightness key reaches the
    // band. Broadcast to every screen (the demo's "" target), which is what
    // a hardware key means. For the stateful kinds (mic, caps) a value of 0
    // reads as off and non-zero as on; the value-based kinds ignore that.
    // Returns false for a kind no factory serves, so a typo reports failure
    // over the wire rather than vanishing.
    IpcTarget {
        target: "osd"

        function show(kind: string, value: int): bool {
            return OsdRegistry.show(kind, value, "");
        }
    }

    // A toast over the wire:
    // `phosphorctl call notify.send --arg summary=Hi --arg body=There`. It
    // lands on the primary output's stack and returns the toast id the host
    // assigned (-1 when suppressed or when no host is up). This is the
    // shell's own path, for scripts and for seeing the stack at all; the
    // org.freedesktop.Notifications server is not run by this process yet,
    // so notify-send does not arrive here.
    IpcTarget {
        target: "notify"

        function send(summary: string, body: string): int {
            return ToastRegistry.send(summary, body);
        }
    }

    // The launcher's wire surface, per the mockup's
    // `phosphorctl call launcher.toggle`. Argument-free, the form a
    // compositor keybind uses (Meta / Alt+Space bound to
    // `phosphorctl call launcher.toggle`).
    IpcTarget {
        target: "launcher"

        function show(): void {
            if (!Popouts.isOpen("launcher"))
                root.toggleLauncher();
        }

        function toggle(): void {
            root.toggleLauncher();
        }

        function hide(): void {
            Popouts.close(Popouts.handleFor("launcher"));
        }
    }
}

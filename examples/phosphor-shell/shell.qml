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
import Phosphor.Dashboard
import Phosphor.Ipc
import Phosphor.Launcher
import Phosphor.Lock
import Phosphor.Notifications
import Phosphor.OSD
import Phosphor.Picker
import Phosphor.Polkit
import Phosphor.Popout
import Phosphor.Power
import Phosphor.Service.UPower
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

    // The wallpaper, one surface per output, and the first of the
    // per-screen surfaces because it is the bottom layer: a Background
    // panel that draws what WallpaperService resolves for its output.
    // The shell owns the wallpaper, so the picker's hover preview and
    // its Apply both land here (through the service, a crossfade each).
    // The delegate reads only the PhosphorShell context property, for the
    // delegate-context reason the bar gives.
    PerScreenPanels {
        id: wallpapers

        model: PhosphorShell.screens

        delegate: WallpaperSurface {
            service: PhosphorShell.wallpaper
        }
    }

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

    // The wallpaper and theme picker, one strip per output (A3 §8): a
    // 96 px Bottom panel on the Overlay layer with no exclusive zone, so
    // it lies over the windows it covers and reserves nothing. Closed, its
    // input region is empty and it is click-through; open, the region is
    // the whole strip. Keyboard is OnDemand, the only interactivity a
    // panel can change nothing about after creation: the compositor gives
    // the strip focus on the first click into it, from which point Escape
    // returns and Enter applies. Which output's strip is open lives on
    // PickerRegistry (a context property, for the delegate-context reason
    // the bar gives); the strip restores the palette and the wallpaper
    // preview (on the `wallpapers` surfaces above) when it closes.
    PerScreenPanels {
        id: pickerStrips

        model: PhosphorShell.screens

        delegate: PanelWindow {
            id: pickerSurface

            edge: PanelWindow.Bottom
            alignment: PanelWindow.Fill
            thickness: 96
            panelLayer: PanelWindow.LayerOverlay
            exclusiveZoneEnabled: false
            keyboardFocus: PanelWindow.OnDemand
            inputRegion: pickerStrip.open ? [Qt.rect(0, 0, pickerSurface.width, pickerSurface.height)] : []

            Picker {
                id: pickerStrip

                anchors.fill: parent
                screenName: pickerSurface.screen ? pickerSurface.screen.name : ""
                wallpaper: PhosphorShell.wallpaper
                targetCount: PickerRegistry.targetCount
                open: screenName !== "" && PickerRegistry.openScreen === screenName
                opacity: open ? 1 : 0
                visible: opacity > 0
                onClosed: PickerRegistry.hide()

                Behavior on opacity {
                    NumberAnimation {
                        duration: pickerStrip.open ? Motion.duration_enter_content : Motion.duration_release_long
                        easing: pickerStrip.open ? Motion.reveal : Motion.release
                    }
                }
            }
        }
    }

    // The polkit dim, one per output (A3 §9c): while a request is open the
    // screen it was placed on dims 20%, with the requester's window left
    // clear. Pure feedback on a full-screen click-through Overlay panel,
    // like the OSD's. The card itself is a popout (below), because a
    // panel cannot take keyboard focus on demand after creation and the
    // prompt must land focus in its field the moment it appears.
    PerScreenPanels {
        id: polkitDims

        model: PhosphorShell.screens

        delegate: PanelWindow {
            id: polkitDimSurface

            edge: PanelWindow.Top
            alignment: PanelWindow.Fill
            thickness: modelData.height
            panelLayer: PanelWindow.LayerOverlay
            exclusiveZoneEnabled: false
            keyboardFocus: PanelWindow.None
            inputRegion: []

            PolkitDim {
                anchors.fill: parent
                readonly property string screenName: polkitDimSurface.screen ? polkitDimSurface.screen.name : ""
                active: PolkitRegistry.activeRequest !== null && screenName !== "" && PolkitRegistry.promptScreen === screenName
                hole: active && PolkitRegistry.anchorRect.width > 0 ? PolkitRegistry.anchorRect : null
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

    // The lockscreen (A3 §6): the layout you left, as an outline. One
    // LockSurface window per output, each an ext_session_lock_surface_v1
    // the compositor presents while the session is locked, with the
    // screen's placement map drawn as static spectrum outlines and the
    // clock + auth field in its largest empty region.
    //
    // PerScreen, not PerScreenPanels: these are real windows (a lock
    // surface is its own protocol role, not a layer panel), created with a
    // null parent as Wayland needs, and destroyed with the screen. The
    // delegate's Component is declared HERE, so ids from this file resolve
    // inside it; that is what lets every surface share one controller.
    //
    // One controller for every screen: there is one password being typed,
    // whichever output the compositor gave keyboard focus. `surfacesWanted`
    // is true from the compositor's `locked` through the unlock's dismiss,
    // and the windows follow it; a LockSurface refuses to show outside a
    // held lock, so binding on `locked` (rather than the lock request) is
    // what keeps the two in step. The compositor blanks the outputs itself
    // between the request and `locked`.
    LockController {
        id: lockController

        lock: sessionCoordinator.lock
    }

    // The battery figure in the lock's bottom-left eyebrow. One host for
    // every screen's LockScreen rather than one per surface, and declared
    // here rather than inside Phosphor.Lock so that module stays free of
    // Phosphor.Service.* imports (its tests run without the services).
    UPowerHost {
        id: lockBattery
    }

    PerScreen {
        id: lockSurfaces

        model: PhosphorShell.screens

        delegate: LockSurface {
            id: lockSurface

            // PerScreen hands these in as initial properties.
            required property var phosphorScreen
            property string name: ""
            property int index: 0
            property bool isPrimary: false

            screen: lockSurface.phosphorScreen
            visible: lockController.surfacesWanted

            LockScreen {
                anchors.fill: parent
                controller: lockController
                map: lockSurface.name.length > 0 ? PlacementMap.forScreen(lockSurface.name) : null
                wallpaperPath: PhosphorShell.wallpaper.path
                battery: lockBattery
                isPrimary: lockSurface.isPrimary
            }
        }
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

    // The lock's wire surface: `phosphorctl call lock.lock`, the form a
    // compositor keybind (Super+L) uses. Argument-free like the others.
    // Only lock, never unlock: the way out of a lock is the password, and
    // a wire call that unlocked a session would be a hole in it.
    // lock() is a no-op while already locked (A3 §6 d: Super+L re-locks
    // to nothing), through LockService's own guard.
    IpcTarget {
        target: "lock"

        function lock(): void {
            sessionCoordinator.lock.lock();
        }
    }

    // The picker's wire surface: `phosphorctl call picker.toggle`. An
    // empty screen means the focused output; a keybind uses the
    // argument-free form. hide() puts the previous palette back, the same
    // as Escape.
    IpcTarget {
        target: "picker"

        function show(): void {
            PickerRegistry.show("");
        }

        function toggle(): void {
            PickerRegistry.toggle("");
        }

        function hide(): void {
            PickerRegistry.hide();
        }
    }

    // The polkit prompt (A3 §9): a card hanging from the requesting
    // window's top edge, built as a popout so it can appear on any output
    // with keyboard focus landing in its field, and torn down with the
    // request. Detached: it opens even while the power menu holds a
    // modal, draws no scrim of its own (the per-screen PolkitDim above is
    // the 20% dim with the requester left clear), and no other popout
    // closes it. Same root-context rule as the launcher: everything it
    // reads is a context property.
    //
    // Closing the popout any other way (the host's Escape, a hot reload)
    // destroys this content; a request still open at that point is
    // cancelled so polkit never waits on a card nobody can see.
    Component {
        id: polkitComponent

        PolkitPrompt {
            agent: PolkitRegistry
            request: PolkitRegistry.activeRequest
            requester: PolkitRegistry.requesterName
            errorText: PolkitRegistry.lastError
            Component.onCompleted: forceActiveFocus()
            Component.onDestruction: {
                if (PolkitRegistry.activeRequest)
                    PolkitRegistry.cancel();
            }
        }
    }

    // Where the prompt goes: the requester's pid through the window
    // tracker (a capability the dashboard work adds; probed, never
    // assumed) to a window id, then across every output's placement map
    // to the cell that shows it. Found: the band spans that window's top
    // edge on that output. Not found: the screen's top edge, centred,
    // under the bar, on the primary output.
    function openPolkitPrompt(): void {
        const pid = PolkitRegistry.requesterPid;
        let windowId = "";
        if (pid > 0 && typeof WindowTracking !== "undefined" && WindowTracking && typeof WindowTracking.findWindowByPid === "function") {
            const found = WindowTracking.findWindowByPid(pid);
            windowId = found === undefined || found === null ? "" : String(found);
        }
        let screenName = "";
        let rect = Qt.rect(0, 0, 0, 0);
        if (windowId !== "") {
            const names = PolkitRegistry.screenNames();
            for (let i = 0; i < names.length; ++i) {
                const map = PlacementMap.forScreen(names[i]);
                if (!map)
                    continue;
                const r = map.cellRect(windowId);
                if (r && r.width > 0 && r.height > 0) {
                    screenName = names[i];
                    rect = r;
                    break;
                }
            }
        }
        // screenNamed marks the QScreen CppOwnership before returning it,
        // the same guard ControlCenterRegistry.screenOf relies on.
        const screen = PolkitRegistry.screenNamed(screenName);
        if (!screen)
            return;
        const anchored = rect.width > 0;
        // PopoutHost caps a frame at the surface width minus its margins,
        // so a band on a full-width window is centred within that cap.
        const bandWidth = anchored ? Math.min(rect.width, screen.geometry.width - 2 * Tokens.spacing_l) : 360;
        const x = anchored ? rect.x + (rect.width - bandWidth) / 2 : (screen.geometry.width - 360) / 2;
        const y = anchored ? rect.y : Tokens.bar_thickness + Tokens.spacing_l;
        PolkitRegistry.setPlacement(screen.name, anchored ? rect : Qt.rect(0, 0, 0, 0));
        Popouts.open({
            "popoutId": "polkit",
            "content": polkitComponent,
            "targetScreen": screen,
            "anchor": PhosphorPopout.Anchor.Custom,
            "customAnchor": Qt.point(x, y),
            "exclusive": PhosphorPopout.ExclusiveMode.Detached,
            "keyboardFocus": true,
            // A password prompt holds the keyboard until it closes; Detached
            // keeps it free of the Modal scrim, the flag adds the grab.
            "exclusiveKeyboard": true,
            "dismissOnFocusLoss": false,
            "props": {
                "anchored": anchored,
                "bandWidth": bandWidth
            }
        });
    }

    Connections {
        target: PolkitRegistry

        function onActiveRequestChanged(): void {
            if (PolkitRegistry.activeRequest)
                root.openPolkitPrompt();
            else
                // close() on an empty handle is a no-op.
                Popouts.close(Popouts.handleFor("polkit"));
        }
    }

    // The dashboard (A3 §7): every desktop's placement map at once, a
    // screen-filling Modal popout like the power menu (Exclusive keyboard
    // focus, so Escape and the digit keys land; it closes the launcher and
    // the control center through the controller's arbitration). A popout
    // rather than an always-mounted overlay because a PanelWindow's
    // keyboard interactivity is read once at materialization, and an
    // always-mapped Exclusive surface would hold the keyboard forever.
    // It lands on ONE output (the transport's target), not every output.
    //
    // Built fresh per open against the root context, so everything it
    // reads is a singleton, an attached property or a context property:
    // Workspaces and PlacementMap (Phosphor.Shell), Screen, and
    // DashboardMedia (the MprisHost src/shell/main.cpp installs).
    //
    // Close runs in two steps so the release scale plays: closeRequested
    // (Escape, a click, a desktop chosen) flips `open`, and `released`
    // fires when the scale has run, which is when the popout goes.
    Component {
        id: dashboardComponent

        Dashboard {
            implicitWidth: Screen.width
            implicitHeight: Screen.height
            screenName: Screen.name
            workspaces: Workspaces
            mapFor: index => Screen.name ? PlacementMap.forScreenDesktop(Screen.name, index) : null
            media: DashboardMedia
            open: true
            onCloseRequested: open = false
            onReleased: Popouts.close(Popouts.handleFor("dashboard"))
        }
    }

    function toggleDashboard(): void {
        Popouts.toggle({
            "popoutId": "dashboard",
            "content": dashboardComponent,
            "anchor": PhosphorPopout.Anchor.ScreenCenter,
            "exclusive": PhosphorPopout.ExclusiveMode.Modal,
            "keyboardFocus": true,
            "dismissOnFocusLoss": true
        });
    }

    // The dashboard's wire surface: `phosphorctl call dashboard.toggle`
    // is what a compositor keybind (or the four-finger swipe's handler)
    // binds. Same show/toggle/hide split as the launcher.
    IpcTarget {
        target: "dashboard"

        function show(): void {
            if (!Popouts.isOpen("dashboard"))
                root.toggleDashboard();
        }

        function toggle(): void {
            root.toggleDashboard();
        }

        function hide(): void {
            Popouts.close(Popouts.handleFor("dashboard"));
        }
    }

    // The keybind cheatsheet (A3 §10): the daemon's chords drawn on this
    // output's placement map. A Cooperative popout with OnDemand keyboard
    // focus, so Escape and the type-to-filter keys land on the sheet while
    // every real chord still reaches the compositor's global-shortcut
    // filter first: press one and the map moves under the labels. Not
    // Modal: the desktop stays live behind it and nothing is dimmed by
    // the compositor.
    //
    // The sheet's own ShortcutCatalog is declared inside the Component,
    // where its id resolves; it reads Control.getShortcutsJson and follows
    // shortcutsChanged.
    Component {
        id: cheatsheetComponent

        Cheatsheet {
            implicitWidth: Screen.width
            implicitHeight: Screen.height
            screenName: Screen.name
            map: Screen.name ? PlacementMap.forScreen(Screen.name) : null
            catalog: chords.rows
            open: true
            onCloseRequested: open = false
            onReleased: Popouts.close(Popouts.handleFor("cheatsheet"))

            ShortcutCatalog {
                id: chords
            }
        }
    }

    function toggleCheatsheet(): void {
        Popouts.toggle({
            "popoutId": "cheatsheet",
            "content": cheatsheetComponent,
            "anchor": PhosphorPopout.Anchor.ScreenCenter,
            "exclusive": PhosphorPopout.ExclusiveMode.Cooperative,
            "keyboardFocus": true,
            "dismissOnFocusLoss": false
        });
    }

    // `phosphorctl call cheatsheet.toggle`, for the compositor keybind
    // (the daemon's own toggle_cheatsheet chord drives its KWin-overlay
    // sheet, not this one).
    IpcTarget {
        target: "cheatsheet"

        function toggle(): void {
            root.toggleCheatsheet();
        }
    }
}

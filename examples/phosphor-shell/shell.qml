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
import Phosphor.Popout
import Phosphor.Power
import Phosphor.Shell
import Phosphor.Theme
import QtQuick

// Top-level composer for the dogfood shell. Phase 4.1 replaces the old
// single TopPanel + pushed-in data sources with the production bar:
// BarHost mounts one connected-corner bar per output, and each bar widget
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

        // The control center grows out of THIS bar's capsule as one
        // continuous painted surface (the connected-corner design), rather
        // than opening as a separate centred popout.
        //
        // Everything the delegate reads must come from a CONTEXT PROPERTY,
        // never an id in this file: PerScreenPanels builds each delegate
        // with a fresh QQmlContext carrying `modelData`, so shell.qml's ids
        // do not resolve inside one. Hence the open state lives on
        // ControlCenterRegistry and the socket content is declared inline
        // here, in the delegate's own scope.
        delegate: BarHost {
            id: bar

            socketWidth: 380
            socketDepth: 460
            // Open only on the screen the registry says owns it, so a
            // multi-head setup shows one panel, on the bar that summoned it.
            //
            // Read through PanelWindow.screen, NOT modelData.screen:
            // PerScreenPanels deliberately withholds the screen role from
            // modelData because that map snapshots a raw QScreen* which
            // dangles on hot-unplug, while this property is QPointer-backed
            // and simply reads null once the output dies. Hence the guard.
            socketOpen: bar.screen ? ControlCenterRegistry.openScreen === bar.screen.name : false

            socketContent: Component {
                ControlCenter {
                    provider: ControlCenterRegistry
                    tileIds: ControlCenterRegistry.tileIds
                    columns: 2
                }
            }
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

    // The control center grows out of the bar's own capsule (the
    // connected-corner socket). There is no surface to place — BarHost
    // paints the body as part of the bar's Shape and the delegate above
    // mounts the content — but it IS still a popout to the controller:
    // main.cpp routes the "control-center" id to a SocketPopoutTransport
    // that drives ControlCenterRegistry.openScreen. Going through
    // Popouts rather than writing that property directly is what makes
    // the Modal power menu close it, refuses it while a modal is up, and
    // drains it on reload, all without this file remembering to.
    //
    // No `content`: the socket transport creates nothing, and the
    // controller never reads it. `targetScreen` is the output whose bar
    // button fired, so a multi-head setup grows the pocket on that bar.
    // No keyboard focus: a bar-painted pocket is not a surface that can
    // take a layer-shell grab.
    function toggleControlCenter(source: Item): void {
        // screenOf hands back a QScreen the C++ side owns; the controller
        // marks it CppOwnership before returning, so the JS GC cannot
        // delete the live screen when this wrapper is collected. Do not
        // reach for a QScreen any other way from QML.
        Popouts.toggle({
            "popoutId": "control-center",
            "targetScreen": ControlCenterRegistry.screenOf(source),
            "exclusive": PhosphorPopout.ExclusiveMode.Cooperative,
            "keyboardFocus": false,
            "dismissOnFocusLoss": false
        });
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

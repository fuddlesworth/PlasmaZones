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

        delegate: BarHost {}
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

    // The control center, per docs/phosphor-shell-design/mockups/control-center.svg.
    //
    // ControlCenter renders into whatever it is parented to and owns no
    // surface, so the presentation is chosen HERE. It is a Cooperative
    // popout rather than the power menu's Modal one: a quick-settings
    // panel should close when you click away or open something else, not
    // dim the screen and suppress every other popout.
    //
    // The eventual connected-corner form grows this out of the bar through
    // a BarCanvas socket, which is a change of anchor and transport in this
    // file rather than a change to the surface.
    // ControlCenter is a transparent Item that paints only its tiles, so
    // the PANEL behind them is the host's to draw. It is deliberately not
    // in the library: the connected-corner form paints the body as part of
    // the bar's own Shape, where a separate panel rectangle would be wrong.
    Component {
        id: controlCenterComponent

        Rectangle {
            id: panel

            // PopoutHost's contract: the content root MUST carry implicit
            // sizes. contentFrame measures the delegate by them, and a root
            // without them collapses the popout to 0x0 and renders empty.
            implicitWidth: centre.implicitWidth
            implicitHeight: centre.implicitHeight

            radius: Tokens.radius_xl
            color: Theme.surface_container

            // Drops in from just above its resting place. PopoutHost fades
            // and scales its content but never translates it, so without
            // this the panel simply appears. Driven off Component.onCompleted
            // rather than an `open` binding because the content is built on
            // open and destroyed on close, so its whole life IS the open
            // state.
            y: -Tokens.spacing_l
            opacity: 0

            Component.onCompleted: {
                y = 0;
                opacity = 1;
            }

            Behavior on y {
                NumberAnimation {
                    duration: Motion.duration_medium_2
                    easing: Motion.emphasized
                }
            }

            Behavior on opacity {
                NumberAnimation {
                    duration: Motion.duration_medium_2
                    easing: Motion.standard
                }
            }

            ControlCenter {
                id: centre

                anchors.fill: parent
                // The registry context property src/shell/main.cpp installs
                // on every engine. tileIds comes from the registry too, so
                // the catalog is declared in one place (the controller)
                // instead of being restated here.
                provider: ControlCenterRegistry
                tileIds: ControlCenterRegistry.tileIds
                columns: 2
            }
        }
    }

    function toggleControlCenter(): void {
        Popouts.toggle({
            "popoutId": "control-center",
            "content": controlCenterComponent,
            // BarCenter, which LayerPopoutTransport treats as "centre on
            // the screen" — the only placement it implements. BarRight
            // would be the right request (this hangs off a trailing bar
            // button) but the transport rejects positional anchors with a
            // warning and centres anyway, so asking for it would only add
            // a log line. Bar-anchored placement arrives with the
            // connected-corner socket, which positions the body by
            // construction rather than by asking the transport.
            "anchor": PhosphorPopout.Anchor.BarCenter,
            "exclusive": PhosphorPopout.ExclusiveMode.Cooperative,
            "keyboardFocus": true,
            "dismissOnFocusLoss": true
        });
    }

    function togglePowerMenu(): void {
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

        // The signal also carries `source` (the bar widget that fired),
        // dropped from this handler's signature because the session menu is
        // screen-centred; a bar-anchored surface takes it to pick which
        // output's bar it hangs from.
        function onWidgetActivated(id: string): void {
            if (id === "power")
                root.togglePowerMenu();
            else if (id === "controlcenter")
                // The bar widget's registered id (barcontroller.cpp), which
                // is NOT the popout id below: the popout is "control-center".
                root.toggleControlCenter();
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
    // `phosphorctl call control-center.open`. Same show/toggle split and
    // the same reason: bind a compositor key to toggle, and call show from
    // a script that wants the panel up regardless of what is already open.
    IpcTarget {
        target: "control-center"

        function show(): void {
            if (!Popouts.isOpen("control-center"))
                root.toggleControlCenter();
        }

        function toggle(): void {
            root.toggleControlCenter();
        }
    }
}

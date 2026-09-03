// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Power.PowerMenu, the session menu.
//
// The panel that sits in the middle of a screen-centred Modal popout. The
// surface, the dimmed scrim, click-outside and Escape all belong to the
// transport and PopoutHost; this file is only the panel.
//
// Six actions in two rows, per docs/phosphor-shell-design/mockups/power-menu.svg,
// each with a one-letter shortcut shown in its tile. Lock is the default: the
// menu focuses the VISIBLE Lock tile on open and draws it filled, because it
// is the only entry that cannot lose the user anything.
//
// Availability comes from logind through SessionHost, and an action logind
// will not perform is HIDDEN rather than greyed — the mockup's "Hibernate
// hidden if logind says no". A greyed tile invites a click that would be
// silently dropped by SessionHost's own capability gate.
//
// Capabilities are refreshed on open and the answers arrive asynchronously,
// so a tile can appear or vanish shortly after the menu is painted. That is
// accepted rather than papered over: holding the menu back until the first
// reply lands would delay every open to hide a case that only really bites
// on the very first one after startup, when nothing has been queried yet.
// The grid re-lays out around whatever is showing, and the rows stay centred.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets
import Phosphor.Service.Session

Item {
    id: root

    /// The shell's SessionHost. Injected rather than constructed: every
    /// SessionHost instance opens its own logind connection AND takes two
    /// real inhibitors (a sleep delay-lock and a grab on the power/suspend/
    /// hibernate/lid keys), so a second one here would double them.
    required property SessionHost session

    /// Set by the transport so the menu can close itself after acting.
    ///
    /// Typed `Item` rather than PopoutHost on purpose: this module must not
    /// depend on Phosphor.Popout, which would make the menu unusable outside a
    /// popout. The cost is that `dismiss()` and `open` are not statically
    /// checked here, which is what qmllint reports on those two lines.
    property Item _popoutHost: null

    // Announced as a dialog so assistive tech says what opened, rather than
    // reading six unexplained buttons.
    Accessible.role: Accessible.Dialog
    Accessible.name: qsTr("Session")

    implicitWidth: panel.implicitWidth
    implicitHeight: panel.implicitHeight

    // An action is offered only when logind will actually perform it.
    // Yes and Challenge both count: Challenge means polkit will prompt, and
    // SessionHost's own gate accepts both, so anything narrower would hide
    // actions that work.
    function _offered(availability: int): bool {
        return availability === SessionHost.Yes || availability === SessionHost.Challenge;
    }

    function _runAndClose(thunk: var): void {
        thunk();
        // Close on act. The popout is Modal, so leaving it up over a
        // suspending or restarting session would be the only thing on screen.
        if (root._popoutHost)
            root._popoutHost.dismiss();
    }

    // One entry is the WHOLE definition of an action: how it looks, whether
    // logind will do it, what it does, and which key runs it. An earlier
    // revision spread those across four tables keyed by the same id string,
    // where adding an action meant four coordinated edits and missing one was
    // silent (the tile rendered and did nothing, or the displayed letter and
    // the handled keycode drifted apart).
    //
    // Hoisted rather than inlined into the Repeater's `model`, so the array is
    // built once. An inline literal re-evaluates and rebuilds every delegate,
    // which resets focus and hover mid-interaction. For the same reason the
    // entries carry no Theme reads: `accent` is a semantic NAME resolved to a
    // Theme token inside the tile delegate, so a live retheme retints tiles
    // in place instead of rebuilding the array (and every tile) via this
    // binding's Theme dependencies.
    //
    // `available` and `run` are functions, not values: capability answers
    // arrive asynchronously from logind and move with lid state, swap and
    // inhibitors, so evaluating per binding keeps the grid honest.
    readonly property var actions: [
        {
            "id": "suspend",
            "label": qsTr("Suspend"),
            "icon": "system-suspend",
            "key": "S",
            "code": Qt.Key_S,
            "accent": "info",
            "row": 0,
            "available": s => root._offered(s.canSuspend),
            "run": s => s.suspend()
        },
        {
            "id": "hibernate",
            "label": qsTr("Hibernate"),
            "icon": "system-suspend-hibernate",
            "key": "H",
            "code": Qt.Key_H,
            "accent": "tertiary",
            "row": 0,
            "available": s => root._offered(s.canHibernate),
            "run": s => s.hibernate()
        },
        {
            // Lock and log out are not logind capabilities: lock goes to the
            // session object and log out is a shell-level signal, so neither
            // has a Can* to consult and both are always offered.
            "id": "lock",
            "label": qsTr("Lock"),
            "icon": "system-lock-screen",
            "key": "L",
            "code": Qt.Key_L,
            "accent": "primary",
            "row": 0,
            // The default action: drawn filled and focused on open. One flag
            // rather than three separate `id === "lock"` tests, which is the
            // drift this array exists to prevent.
            "isDefault": true,
            "available": () => true,
            "run": s => s.lock()
        },
        {
            "id": "logout",
            "label": qsTr("Log out"),
            "icon": "system-log-out",
            "key": "O",
            "code": Qt.Key_O,
            "accent": "secondary",
            "row": 1,
            "available": () => true,
            "run": s => s.logout()
        },
        {
            "id": "reboot",
            "label": qsTr("Restart"),
            "icon": "system-reboot",
            "key": "R",
            "code": Qt.Key_R,
            "accent": "warning",
            "row": 1,
            "available": s => root._offered(s.canReboot),
            "run": s => s.reboot()
        },
        {
            "id": "poweroff",
            "label": qsTr("Shut down"),
            "icon": "system-shutdown",
            "key": "P",
            "code": Qt.Key_P,
            "accent": "error",
            "row": 1,
            "available": s => root._offered(s.canPowerOff),
            "run": s => s.powerOff()
        }
    ]

    function _isAvailable(action: var): bool {
        const s = root.session;
        return s ? action.available(s) : false;
    }

    function _invoke(action: var): void {
        const s = root.session;
        if (!s || !root._isAvailable(action))
            return;
        root._runAndClose(() => action.run(s));
    }

    // Capabilities move with lid state, swap and inhibitor locks, and
    // SessionHost documents refreshCapabilities as existing for exactly this
    // moment. Without it a menu opened long after startup shows whatever was
    // true when the shell launched.
    Component.onCompleted: {
        if (root.session)
            root.session.refreshCapabilities();
        // Claim focus now AND again when the popout opens. The transport
        // completes this component before the layer surface exists, so this
        // first call runs on an item with no window and Qt may not keep the
        // grant; the host's open transition is the moment the menu is really
        // on screen and can hold it.
        defaultTile.forceActiveFocus();
    }

    Connections {
        target: root._popoutHost

        function onOpenChanged(): void {
            if (!root._popoutHost.open)
                return;
            // Refresh here as well as at construction. Both shipped transports
            // build a fresh menu per open, so today this is the same moment,
            // but PopoutHost documents transports that reuse one host across
            // popouts, and against those the menu would otherwise show the
            // capabilities that were true when it was first built.
            if (root.session)
                root.session.refreshCapabilities();
            defaultTile.forceActiveFocus();
        }
    }

    // Single-key activation. Handled on the panel rather than per tile so a
    // key works wherever focus happens to be, and so a letter whose tile is
    // hidden is simply inert rather than reaching a different action. The
    // keycode comes from the action entry, so it cannot drift from the letter
    // the tile displays.
    Keys.onPressed: event => {
        // Bare letters only. These actions suspend or power off the machine,
        // so a Ctrl- or Meta-held keystroke meant for something else must not
        // reach one, and a held key must not fire twice.
        if (event.isAutoRepeat || (event.modifiers & ~Qt.ShiftModifier) !== Qt.NoModifier)
            return;
        const hit = root.actions.find(a => a.code === event.key);
        if (hit === undefined)
            return;
        event.accepted = true;
        root._invoke(hit);
    }

    PhosphorCard {
        id: panel

        // Menus sit at elevation 3 in the M3 scale ElevationShadow
        // documents; the bar capsule is 2.
        elevation: 3
        radius: Tokens.radius_xl
        padding: Tokens.spacing_xl

        ColumnLayout {
            spacing: Tokens.spacing_l

            Text {
                // Translated in natural casing; the shout is styling, and
                // casing rules differ per locale.
                text: qsTr("Session")
                font.capitalization: Font.AllUppercase
                color: Theme.on_surface_variant
                font.pixelSize: Tokens.font_size_label_s
                font.weight: Tokens.font_weight_medium
                font.family: Tokens.font_family
                Layout.alignment: Qt.AlignHCenter
                Layout.bottomMargin: Tokens.spacing_xs
            }

            // Two fixed rows rather than a flow: the mockup's rows have
            // different tile heights. Each row centres itself, so hiding an
            // unavailable action shortens that row and leaves it centred
            // rather than leaving a hole where the tile was.
            Repeater {
                model: 2

                delegate: RowLayout {
                    id: tileRow

                    required property int index

                    spacing: Tokens.spacing_m
                    Layout.alignment: Qt.AlignHCenter

                    Repeater {
                        // Filtered to this row, NOT the whole list with the
                        // off-row entries hidden. Building all six in both rows
                        // made twelve tiles for six actions, six of them alive
                        // only to be invisible (each still resolving a themed
                        // icon), and gave the row-1 Lock copy — the invisible
                        // one — a second chance to claim the default focus,
                        // which it won by completing last. The menu then opened
                        // focused on a tile nobody could see.
                        model: root.actions.filter(a => a.row === tileRow.index)

                        delegate: PowerTile {
                            required property var modelData

                            visible: root._isAvailable(modelData)
                            // Metrics come off the tile itself, which is the
                            // one definition of them, so the grid and the tile
                            // cannot drift apart. QQuickLayout skips invisible
                            // items entirely, so a hidden action costs neither
                            // width nor a spacing slot and this only ever
                            // sizes a tile that is actually shown.
                            Layout.preferredWidth: tileWidth
                            Layout.preferredHeight: tileRow.index === 0 ? tallHeight : shortHeight

                            label: modelData.label
                            iconName: modelData.icon
                            shortcut: modelData.key
                            // Resolved HERE, not in the actions array, so a
                            // retheme retints this binding alone instead of
                            // rebuilding the array and every tile with it.
                            // The switch reads the Theme properties directly,
                            // so each is a tracked dependency.
                            accent: {
                                switch (modelData.accent) {
                                case "info":
                                    return Theme.info;
                                case "tertiary":
                                    return Theme.tertiary;
                                case "primary":
                                    return Theme.primary;
                                case "secondary":
                                    return Theme.secondary;
                                case "warning":
                                    return Theme.warning;
                                case "error":
                                    return Theme.error;
                                }
                                return Theme.on_surface_variant;
                            }
                            primary: modelData.isDefault === true

                            onActivated: root._invoke(modelData)

                            Component.onCompleted: {
                                if (modelData.isDefault === true)
                                    defaultTile.target = this;
                            }
                        }
                    }
                }
            }
        }
    }

    // Indirection so Component.onCompleted can focus the default tile
    // without reaching into the Repeater's delegate tree by index.
    QtObject {
        id: defaultTile

        property Item target: null

        function forceActiveFocus() {
            if (target)
                target.forceActiveFocus();
        }
    }
}

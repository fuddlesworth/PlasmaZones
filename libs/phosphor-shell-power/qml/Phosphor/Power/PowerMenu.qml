// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Power.PowerMenu, the session menu.
//
// A column of words over your dimmed desktop (A3 §5). No dialog box and
// no tiles: the surface is a full-screen layer with a void ground at
// 60 % (phase 1's stand-in for the compositor dim, which is phase 2), and
// the six session actions are 24 px words down the left edge, coloured
// on the state axis by how destructive they are (Lock cyan through Shut
// down rose), each with its one-letter shortcut underlined. A 3 px
// selection line on the screen's left edge slides between words.
//
// Destructive actions (log out, restart, shut down) take two presses: the
// first grows the line to 6 px and starts a 3 s countdown that shortens
// it, the second confirms. Lock is the default and pre-focused, because it
// is the only entry that cannot lose the user anything.
//
// Availability comes from logind through SessionHost, and an action logind
// will not perform is HIDDEN rather than greyed.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets
import Phosphor.Service.Session

FocusScope {
    id: root

    required property SessionHost session
    property Item _popoutHost: null

    Accessible.role: Accessible.Dialog
    Accessible.name: qsTr("Session")

    // Fills the screen: the popout host centres this, so claim the output.
    implicitWidth: Screen.width
    implicitHeight: Screen.height

    readonly property int rowHeight: 72
    readonly property int confirmMs: 3000

    // Id of the selected action, and the id awaiting its second press
    // (or ""). Ids, not indices: rows come and go with logind's answers.
    property string selectedId: "lock"
    property string armed: ""

    function _offered(availability: int): bool {
        return availability === SessionHost.Yes || availability === SessionHost.Challenge;
    }

    function _runAndClose(thunk: var): void {
        thunk();
        if (root._popoutHost)
            root._popoutHost.dismiss();
    }

    // One entry is the WHOLE definition of an action. `t` is the
    // state-axis position: how much the action costs the user.
    readonly property var actions: [
        {
            "id": "lock",
            "label": qsTr("Lock"),
            "key": "L",
            "code": Qt.Key_L,
            "t": 0.0,
            "destructive": false,
            "isDefault": true,
            "available": () => true,
            "run": s => s.lock()
        },
        {
            "id": "suspend",
            "label": qsTr("Suspend"),
            "key": "S",
            "code": Qt.Key_S,
            "t": 0.2,
            "destructive": false,
            "available": s => root._offered(s.canSuspend),
            "run": s => s.suspend()
        },
        {
            "id": "hibernate",
            "label": qsTr("Hibernate"),
            "key": "H",
            "code": Qt.Key_H,
            "t": 0.4,
            "destructive": false,
            "available": s => root._offered(s.canHibernate),
            "run": s => s.hibernate()
        },
        {
            "id": "logout",
            "label": qsTr("Log out"),
            "key": "O",
            "code": Qt.Key_O,
            "t": 0.6,
            "destructive": true,
            "available": () => true,
            "run": s => s.logout()
        },
        {
            "id": "reboot",
            "label": qsTr("Restart"),
            "key": "R",
            "code": Qt.Key_R,
            "t": 0.8,
            "destructive": true,
            "available": s => root._offered(s.canReboot),
            "run": s => s.reboot()
        },
        {
            "id": "poweroff",
            "label": qsTr("Shut down"),
            "key": "P",
            "code": Qt.Key_P,
            "t": 1.0,
            "destructive": true,
            "available": s => root._offered(s.canPowerOff),
            "run": s => s.powerOff()
        }
    ]

    function _isAvailable(action: var): bool {
        const s = root.session;
        return s ? action.available(s) : false;
    }

    // Rows by action id, filled as the Repeater builds them.
    property var _rows: ({})

    function _visibleIds(): var {
        return root.actions.filter(a => root._isAvailable(a)).map(a => a.id);
    }

    // Activate an action: destructive ones arm first and run on the
    // second press within the countdown.
    function _invoke(action: var): void {
        const s = root.session;
        if (!s || !root._isAvailable(action))
            return;
        if (action.destructive && root.armed !== action.id) {
            root.armed = action.id;
            confirmTimer.restart();
            return;
        }
        confirmTimer.stop();
        root.armed = "";
        root._runAndClose(() => action.run(s));
    }

    function _select(id: string): void {
        if (id === "")
            return;
        root.selectedId = id;
        if (root.armed !== "" && root.armed !== id) {
            root.armed = "";
            confirmTimer.stop();
        }
    }

    // Move the selection by `delta` among the rows logind offers.
    function _step(delta: int): void {
        const ids = root._visibleIds();
        if (ids.length === 0)
            return;
        const at = Math.max(0, ids.indexOf(root.selectedId));
        root._select(ids[Math.max(0, Math.min(ids.length - 1, at + delta))]);
    }

    function _selectedAction(): var {
        return root.actions.find(a => a.id === root.selectedId) ?? null;
    }

    Timer {
        id: confirmTimer

        interval: root.confirmMs
        repeat: false
        onTriggered: root.armed = ""
    }

    Component.onCompleted: {
        if (root.session)
            root.session.refreshCapabilities();
        root._select("lock");
        root.forceActiveFocus();
    }

    Connections {
        target: root._popoutHost

        function onOpenChanged(): void {
            if (!root._popoutHost.open)
                return;
            if (root.session)
                root.session.refreshCapabilities();
            root.forceActiveFocus();
        }
    }

    focus: true
    Keys.onPressed: event => {
        if (event.isAutoRepeat)
            return;
        if (event.key === Qt.Key_Up) {
            root._step(-1);
            event.accepted = true;
            return;
        }
        if (event.key === Qt.Key_Down) {
            root._step(1);
            event.accepted = true;
            return;
        }
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
            const a = root._selectedAction();
            if (a)
                root._invoke(a);
            event.accepted = true;
            return;
        }
        if ((event.modifiers & ~Qt.ShiftModifier) !== Qt.NoModifier)
            return;
        const hit = root.actions.find(a => a.code === event.key);
        if (hit === undefined || !root._isAvailable(hit))
            return;
        event.accepted = true;
        root._select(hit.id);
        root._invoke(hit);
    }

    // Phase 1 ground: void at 60 %. The compositor dim (35 % brightness,
    // 40 % saturation on every window) is phase 2.
    Rectangle {
        anchors.fill: parent
        color: Theme.background
        opacity: 0.6
    }

    // The selection line on the screen's left edge. 3 px, 6 px while an
    // action is armed, shortening with the countdown.
    Rectangle {
        id: selectionLine

        readonly property var _action: root._selectedAction()
        readonly property Item _row: root._rows[root.selectedId] ?? null
        readonly property bool _armed: _action !== null && root.armed === _action.id

        x: 0
        y: column.y + (_row ? _row.y : 0)
        width: _armed ? 6 : 3
        height: root.rowHeight * (_armed ? countdown.remaining : 1)
        color: _action ? Spectrum.at(_action.t) : Spectrum.resting

        Behavior on y {
            NumberAnimation {
                duration: 110
                easing: Motion.reveal
            }
        }
        Behavior on width {
            NumberAnimation {
                duration: Motion.duration_enter
                easing: Motion.enter
            }
        }
    }

    // Countdown fraction while armed.
    QtObject {
        id: countdown

        property real remaining: 1
    }
    NumberAnimation {
        id: countdownAnim

        target: countdown
        property: "remaining"
        from: 1
        to: 0
        duration: root.confirmMs
    }
    onArmedChanged: {
        if (root.armed !== "")
            countdownAnim.restart();
        else {
            countdownAnim.stop();
            countdown.remaining = 1;
        }
    }

    Column {
        id: column

        x: 48
        anchors.verticalCenter: parent.verticalCenter
        spacing: 0

        // One row per action, hidden (and skipped by the Column) when
        // logind will not perform it.
        Repeater {
            model: root.actions

            delegate: PowerRow {}
        }
    }

    // A row: the word, its key hint, and the armed suffix.
    component PowerRow: Item {
        id: row

        required property var modelData
        required property int index

        // Named so the tests can walk the visual tree for rows.
        objectName: "powerRow"

        // The one activation seam: pointer, keyboard and assistive tech
        // all route through it. A destructive action arms on the first
        // activation and runs on the second.
        signal activated

        onActivated: {
            root._select(row.modelData.id);
            root._invoke(row.modelData);
        }

        readonly property string label: row.modelData.label
        readonly property bool primary: row.modelData.isDefault === true
        readonly property bool isSelected: root.selectedId === row.modelData.id
        readonly property bool isArmed: root.armed === row.modelData.id
        readonly property color hue: Spectrum.at(row.modelData.t)

        width: 360
        height: root.rowHeight
        visible: root._isAvailable(row.modelData)
        // The selected row holds focus; keys it does not handle bubble
        // to the menu's own handler.
        focus: row.isSelected

        Accessible.role: Accessible.Button
        Accessible.name: row.label
        Accessible.onPressAction: row.activated()

        Component.onCompleted: {
            const rows = root._rows;
            rows[row.modelData.id] = row;
            root._rows = rows;
        }

        HoverHandler {
            cursorShape: Qt.PointingHandCursor
            onHoveredChanged: {
                if (hovered)
                    root._select(row.modelData.id);
            }
        }
        TapHandler {
            onTapped: row.activated()
        }

        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: Tokens.spacing_m

            Text {
                id: word

                text: row.modelData.label
                color: row.hue
                opacity: row.isSelected ? 1 : 0.75
                font.family: Tokens.font_family_ui
                font.pixelSize: Tokens.font_size_display_m
                font.weight: Tokens.font_weight_regular

                Behavior on opacity {
                    NumberAnimation {
                        duration: row.isSelected ? Motion.duration_enter : Motion.duration_release
                        easing: row.isSelected ? Motion.enter : Motion.release
                    }
                }

                // The key hint: the first letter underlined.
                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.bottom
                    anchors.topMargin: -2
                    width: firstLetter.advanceWidth
                    height: 1
                    color: row.hue
                    opacity: 0.9
                }
                TextMetrics {
                    id: firstLetter

                    font: word.font
                    text: row.modelData.label.length > 0 ? row.modelData.label[0] : ""
                }
            }

            TabularText {
                anchors.baseline: word.baseline
                visible: row.isArmed
                text: qsTr("Enter again")
                color: Theme.on_surface_variant
                font.pixelSize: Tokens.font_size_body_m
            }
        }
    }
}

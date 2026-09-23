// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Dashboard.Cheatsheet, the keybind sheet drawn on the
// placement map (A3 §10).
//
// A full-screen overlay on the void ground at 60 % over the live
// desktop: the screen's cells at 1:1 (FullMap) with a chord label on
// each rect or gap the chord acts on (ChordLayout.place), and the
// non-spatial chords in a 300 px column on the right edge. Top-left
// reads `Mode · desktop N`; typing filters both the labels and the
// column, Escape closes.
//
// Live mode comes from the map: the host surface takes keyboard focus
// on demand, never exclusively, so a chord pressed while the sheet is
// up still reaches the compositor and the daemon, the map moves, and
// the labels ride the rects (FullMap retargets, the labels follow their
// anchors). The pill of the chord just performed cannot be pulsed
// here: the compositor consumes the chord and nothing reports which one
// fired, so `hot` stays a hover signal.
//
// Inputs are properties: `map` (a PlacementMapScreen, or a fake with
// `workArea`, `cells`, `mode`, `focusedCellId()` and `changed()`),
// `catalog` (ShortcutCatalog.rows, or a fake list), `open`.

import QtQuick
import Phosphor.Theme
import Phosphor.Widgets
import "ChordLayout.js" as Layout

FocusScope {
    id: root

    property string screenName: ""
    property var map: null
    property var catalog: []
    property bool open: false
    property string filter: ""
    property int columnWidth: 300
    property int rowHeight: 32

    signal closeRequested

    readonly property int mode: map ? map.mode : -1
    readonly property int currentDesktop: map ? map.currentDesktop : -1
    readonly property string modeName: {
        switch (mode) {
        case 0:
            return qsTr("Snapping");
        case 1:
            return qsTr("Tiling");
        case 2:
            return qsTr("Scrolling");
        default:
            return qsTr("No placement");
        }
    }

    // The placement, recomputed when the rects, the focus, the catalog or
    // the filter move. `spatial` labels carry x/y as their centres.
    property var spatial: []
    property var column: []
    // The shell surfaces that have no daemon chord. They are bound as
    // compositor keybinds to `phosphorctl call`, which no daemon surface
    // reports, so the cheatsheet lists them without a chord rather than
    // guessing at one.
    readonly property var shellVerbs: [
        {
            "id": "shell:launcher",
            "label": qsTr("Launcher")
        },
        {
            "id": "shell:control-center",
            "label": qsTr("Control center")
        },
        {
            "id": "shell:notifications",
            "label": qsTr("Notifications")
        },
        {
            "id": "shell:lock",
            "label": qsTr("Lock")
        },
        {
            "id": "shell:screenshot",
            "label": qsTr("Screenshot")
        },
        {
            "id": "shell:power",
            "label": qsTr("Power")
        },
        {
            "id": "shell:dashboard",
            "label": qsTr("Dashboard")
        }
    ]

    function relayout(): void {
        const cells = fullMap.cellRects;
        const focused = map && typeof map.focusedCellId === "function" ? map.focusedCellId() : fullMap.focusedId;
        const rows = (catalog || []).concat(shellVerbs.map(v => ({
                    "id": v.id,
                    "label": v.label,
                    "triggers": [],
                    "assigned": false,
                    "mode": "all"
                })));
        const placed = Layout.place(cells, focused, rows, mode);
        spatial = placed.spatial.filter(l => Layout.matches(l, filter));
        column = placed.column.filter(l => Layout.matches(l, filter));
        _syncSpatial(spatial);
    }

    // Reconcile the pill rows in place, keyed by the row's id. Handing the
    // Repeater a fresh array destroys and rebuilds every delegate, which both
    // discards the x/y Behaviors below (the labels jump instead of riding the
    // rects, contradicting this file's own header) and re-runs the staggered
    // enter from `Component.onCompleted` — in live mode that is every window
    // move. Same shape FullMap and PlacementMiniature use.
    //
    // Role names are suffixed because ChordPill already declares `chord`,
    // `label`, `description` and `assigned`; a role of the same name would
    // collide with the property it is meant to feed.
    function _syncSpatial(rows): void {
        for (let i = 0; i < rows.length; ++i) {
            const r = rows[i];
            let at = -1;
            for (let j = i; j < spatialModel.count; ++j) {
                if (spatialModel.get(j).rowId === r.id) {
                    at = j;
                    break;
                }
            }
            if (at === -1) {
                spatialModel.insert(i, _spatialRow(r));
                continue;
            }
            if (at !== i)
                spatialModel.move(at, i, 1);
            const row = _spatialRow(r);
            for (const key in row)
                spatialModel.setProperty(i, key, row[key]);
        }
        while (spatialModel.count > rows.length)
            spatialModel.remove(spatialModel.count - 1);
    }

    function _spatialRow(r) {
        return {
            "rowId": String(r.id),
            "chordText": String(r.chord),
            "labelText": String(r.label),
            "descriptionText": String(r.description),
            "assignedFlag": !!r.assigned,
            "px": Number(r.x) || 0,
            "py": Number(r.y) || 0
        };
    }

    ListModel {
        id: spatialModel
    }

    onCatalogChanged: relayout()
    onFilterChanged: relayout()
    onModeChanged: relayout()
    Connections {
        target: fullMap
        function onCellRectsChanged() {
            root.relayout();
        }
    }
    Component.onCompleted: {
        relayout();
        // Built with open already true (a popout builds its content fresh
        // per open): run the enter from zero.
        if (open) {
            _opened = true;
            progress = 0;
            progress = 1;
            root.forceActiveFocus();
        }
    }

    // Enter / release.
    property real progress: 0
    Behavior on progress {
        NumberAnimation {
            duration: root.open ? Motion.duration_enter_content : Motion.duration_release
            easing: root.open ? Motion.reveal : Motion.release
        }
    }
    // Released fires once the close animation has run, for a host that
    // tears the surface down afterwards.
    signal released
    property bool _opened: false
    onOpenChanged: {
        progress = open ? 1 : 0;
        if (open) {
            _opened = true;
            filter = "";
            root.forceActiveFocus();
        }
    }
    onProgressChanged: {
        if (!open && _opened && progress === 0) {
            _opened = false;
            root.released();
        }
    }
    visible: progress > 0 || open
    enabled: open
    opacity: progress

    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Keyboard shortcuts")

    // Escape closes; printable keys filter; Backspace edits the filter.
    // Modifier chords are left unaccepted so the compositor's grab (which
    // runs first anyway) is the only consumer.
    focus: true
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape) {
            event.accepted = true;
            if (root.filter !== "")
                root.filter = "";
            else
                root.closeRequested();
            return;
        }
        if ((event.modifiers & ~Qt.ShiftModifier) !== Qt.NoModifier)
            return;
        if (event.key === Qt.Key_Backspace) {
            event.accepted = true;
            root.filter = root.filter.slice(0, -1);
            return;
        }
        if (event.text !== "" && event.text.charCodeAt(0) >= 32) {
            event.accepted = true;
            root.filter += event.text;
        }
    }

    // Ground: void at 60 %.
    Rectangle {
        anchors.fill: parent
        color: Theme.background
        opacity: 0.6
    }

    FullMap {
        id: fullMap

        anchors.fill: parent
        model: root.map
    }

    // Top-left: `Mode · desktop N`, and the filter while one is typed.
    Column {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: Tokens.spacing_xl
        spacing: Tokens.spacing_xs

        Text {
            text: root.currentDesktop >= 0 ? qsTr("%1 · desktop %2").arg(root.modeName).arg(root.currentDesktop + 1) : root.modeName
            color: Theme.on_surface
            font.family: Tokens.font_family_ui
            font.pixelSize: Tokens.font_size_body_m
        }
        Row {
            spacing: Tokens.spacing_xs
            Text {
                text: root.filter !== "" ? qsTr("Search") : qsTr("Type to search")
                color: Theme.on_surface_variant
                font.family: Tokens.font_family_ui
                font.pixelSize: Tokens.font_size_label_m
            }
            TabularText {
                text: root.filter
                font.pixelSize: Tokens.font_size_label_m
                color: Theme.on_surface
            }
        }
    }

    // The spatial labels, centred on their anchors. Enter outward from the
    // focused window with a 6 ms stagger (A3 §10 c).
    Repeater {
        id: spatialLabels

        model: spatialModel
        delegate: ChordPill {
            id: pill

            required property int index
            required property string chordText
            required property string labelText
            required property string descriptionText
            required property bool assignedFlag
            required property real px
            required property real py

            chord: pill.chordText
            label: pill.labelText
            description: pill.descriptionText
            assigned: pill.assignedFlag
            x: Math.round(pill.px - width / 2)
            y: Math.round(pill.py - height / 2)

            opacity: 0
            Component.onCompleted: opacity = 1
            Behavior on opacity {
                SequentialAnimation {
                    PauseAnimation {
                        // `index` is -1 while the delegate is still being
                        // incubated, and a negative duration is a QML error
                        // that discards the whole animation. Clamp at zero.
                        duration: Motion.reducedMotion ? 0 : Math.max(0, Math.min(150, pill.index * 6))
                    }
                    NumberAnimation {
                        duration: Motion.duration_enter_content
                        easing: Motion.reveal
                    }
                }
            }
            Behavior on x {
                NumberAnimation {
                    duration: Motion.duration_release
                    easing: Motion.release
                }
            }
            Behavior on y {
                NumberAnimation {
                    duration: Motion.duration_release
                    easing: Motion.release
                }
            }
        }
    }

    // The right-edge column: non-spatial chords as 32 px rows, the key in
    // a pill on the left and the description on the right.
    Column {
        id: columnList

        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Tokens.spacing_xl
        width: root.columnWidth
        spacing: 0

        Text {
            text: qsTr("Not on the map")
            color: Theme.on_surface_variant
            font.family: Tokens.font_family_ui
            font.pixelSize: Tokens.font_size_label_m
            font.letterSpacing: 1
            font.capitalization: Font.AllUppercase
            height: root.rowHeight
            verticalAlignment: Text.AlignVCenter
        }

        Repeater {
            model: root.column
            delegate: Item {
                id: row

                required property var modelData

                width: columnList.width
                height: root.rowHeight

                ChordPill {
                    id: rowPill

                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    chord: row.modelData.chord
                    label: row.modelData.label
                    assigned: row.modelData.assigned
                }
                Text {
                    anchors.left: rowPill.right
                    anchors.leftMargin: Tokens.spacing_m
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.modelData.label
                    color: row.modelData.assigned ? Theme.on_surface : Theme.on_surface_variant
                    font.family: Tokens.font_family_ui
                    font.pixelSize: Tokens.font_size_body_m
                    elide: Text.ElideRight
                }
            }
        }
    }
}

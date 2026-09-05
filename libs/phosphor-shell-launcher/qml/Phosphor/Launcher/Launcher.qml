// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Launcher.Launcher, the viewfinder launcher surface.
//
// A query column on the left and a live placement-map miniature of the
// current screen on the right (A3 §1). The query field has no box: its
// top edge is a 1 px spectrum line that draws out from the caret on
// focus. Providers are a tracked label with their prefix characters,
// not a pill row; Tab still cycles them. Results are rows with a 2 px
// blue selection line on the left edge that slides between them, and
// when the active provider is Windows the results are drawn ON their
// cells in the miniature, coloured on the state axis by score, with the
// selected one edged white.
//
// Like the other surfaces it renders into whatever it is parented to and
// owns no window. All the data comes from `results`, a LauncherModel the
// host builds from its provider registry.
//
// Keyboard, all from the search field so focus never has to leave it:
//   Up / Down      move the selection
//   Return         primary action on the selection
//   Alt+Return     alternate action, when the row offers one
//   Tab / Shift+Tab cycle the provider filter
//   Escape         dismissed()
//
// Phase 1 matches Windows results to cells by the daemon window id the
// model exposes; the Apps "next placement" rect uses the focused cell
// (`// [NEW] engine next-placement query`).

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

FocusScope {
    id: root

    required property var results

    signal activated
    signal dismissed

    LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true

    implicitWidth: Math.min(880, Screen.width - 2 * Tokens.spacing_xl)
    implicitHeight: Math.min(500, Screen.height - 2 * Tokens.spacing_xl)

    readonly property int _rowHeight: 44
    readonly property int _sectionHeight: 24

    readonly property alias queryText: field.text

    // This screen's placement map (a PlacementMapScreen), for the
    // viewfinder. Injected by the host, which knows the output the
    // launcher opened on; null draws an empty viewfinder.
    property var map: null

    // Rail-axis hue of the card's stroke: the card is centred.
    readonly property real t: 0.5

    // The surface pack on the card (A1 §2.4, `shell.phosphor.popout`),
    // set by the composition root. With a chain engaged the card's own
    // stroke steps aside for the pack's.
    property Component decoration: null

    Binding {
        target: root.results
        property: "active"
        value: root.visible
        restoreMode: Binding.RestoreNone
    }

    function reset(): void {
        field.text = "";
        root.results.query = "";
        root.results.providerFilter = "";
        list.currentIndex = 0;
        field.forceActiveFocus();
    }

    function activateCurrent(alternate: bool): void {
        if (list.currentIndex < 0 || list.currentIndex >= list.count)
            return;
        const row = list.currentIndex;
        const repeatable = alternate && root.results.alternateIsRepeatable(row);
        if (!root.results.activate(row, alternate))
            return;
        if (repeatable)
            return;
        root.activated();
    }

    // The provider the list is filtered to, or null for all.
    readonly property var _provider: {
        const id = root.results.providerFilter;
        const ps = root.results.providers;
        for (let i = 0; i < ps.length; ++i) {
            if (ps[i].id === id)
                return ps[i];
        }
        return null;
    }

    // Card: container ground, stroke, no shadow.
    Rectangle {
        id: ground

        // The pack's capture item: the ground alone, so the content above
        // stays crisp and interactive.
        property bool shaderAnchor: true

        anchors.fill: parent
        radius: Tokens.radius_container
        color: Theme.surface_container
        opacity: 0.92
    }
    DecorationSlot {
        id: decorationSlot

        anchors.fill: parent
        component: root.decoration
        contentItem: ground
        surfacePath: "shell.phosphor.popout"
        focused: root.activeFocus
    }
    SpectrumStroke {
        anchors.fill: parent
        radius: Tokens.radius_container
        t: root.t
        active: true
        visible: !decorationSlot.active
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: Tokens.spacing_l
        spacing: Tokens.spacing_l

        // ─── Query column ────────────────────────────────────────────────
        ColumnLayout {
            id: queryColumn

            readonly property real _w: Math.round((root.width - 3 * Tokens.spacing_l) * 0.38)
            Layout.preferredWidth: _w
            Layout.minimumWidth: _w
            Layout.maximumWidth: _w
            Layout.fillWidth: false
            Layout.fillHeight: true
            spacing: Tokens.spacing_m

            // The field: no box; its top edge is the spectrum line that
            // draws out from the caret on focus.
            Item {
                Layout.fillWidth: true
                implicitHeight: field.implicitHeight + Tokens.spacing_s

                SpectrumUnderline {
                    id: fieldEdge

                    anchors.left: parent.left
                    anchors.top: parent.top
                    height: 1
                    length: parent.width
                    value: field.activeFocus ? 1 : 0.25
                    t: root.t
                    opacity: field.activeFocus ? 1 : 0.5
                }

                Text {
                    id: prefix

                    anchors.left: parent.left
                    anchors.verticalCenter: field.verticalCenter
                    text: root._provider && root._provider.prefix !== undefined ? root._provider.prefix : ""
                    visible: text.length > 0
                    color: Theme.on_surface_variant
                    font.family: Tokens.font_family_mono
                    font.pixelSize: Tokens.font_size_title_l
                }

                TextInput {
                    id: field

                    anchors.left: prefix.visible ? prefix.right : parent.left
                    anchors.leftMargin: prefix.visible ? Tokens.spacing_s : 0
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.topMargin: Tokens.spacing_s
                    color: Theme.on_surface
                    font.family: Tokens.font_family_ui
                    font.pixelSize: Tokens.font_size_title_l
                    focus: true
                    selectByMouse: true
                    Accessible.name: qsTr("Search")

                    Text {
                        anchors.fill: parent
                        visible: field.text.length === 0
                        text: qsTr("Search apps, windows and commands")
                        color: Theme.on_surface_variant
                        opacity: 0.6
                        font: field.font
                        elide: Text.ElideRight
                    }

                    onTextChanged: {
                        root.results.query = text;
                        list.currentIndex = 0;
                    }

                    Keys.onUpPressed: event => {
                        list.currentIndex = Math.max(0, list.currentIndex - 1);
                        event.accepted = true;
                    }
                    Keys.onDownPressed: event => {
                        list.currentIndex = Math.min(list.count - 1, list.currentIndex + 1);
                        event.accepted = true;
                    }
                    Keys.onReturnPressed: event => {
                        root.activateCurrent((event.modifiers & Qt.AltModifier) !== 0);
                        event.accepted = true;
                    }
                    Keys.onEnterPressed: event => {
                        root.activateCurrent((event.modifiers & Qt.AltModifier) !== 0);
                        event.accepted = true;
                    }
                    Keys.onTabPressed: event => {
                        root.results.cycleProviderFilter(1);
                        list.currentIndex = 0;
                        event.accepted = true;
                    }
                    Keys.onBacktabPressed: event => {
                        root.results.cycleProviderFilter(-1);
                        list.currentIndex = 0;
                        event.accepted = true;
                    }
                    Keys.onEscapePressed: event => {
                        root.dismissed();
                        event.accepted = true;
                    }
                }
            }

            // Provider labels: a tracked eyebrow, "All" then one per
            // provider with rows. Text with an underline, never a pill.
            Row {
                id: pills

                objectName: "providerPills"

                Layout.fillWidth: true
                spacing: Tokens.spacing_m
                visible: list.count > 0 || root.results.providerFilter !== ""

                ProviderLabel {
                    text: qsTr("All")
                    selected: root.results.providerFilter === ""
                    onClicked: {
                        root.results.providerFilter = "";
                        list.currentIndex = 0;
                        field.forceActiveFocus();
                    }
                }

                Repeater {
                    model: root.results.providers

                    ProviderLabel {
                        id: pill

                        required property var modelData

                        visible: pill.modelData.count > 0 || pill.selected
                        text: qsTr("%1 %2", "provider filter: provider name, then its result count").arg(pill.modelData.name).arg(Number(pill.modelData.count).toLocaleString(Qt.locale()))
                        selected: root.results.providerFilter === pill.modelData.id
                        onClicked: {
                            root.results.providerFilter = pill.modelData.id;
                            list.currentIndex = 0;
                            field.forceActiveFocus();
                        }
                    }
                }
            }

            ListView {
                id: list

                objectName: "resultList"

                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.results
                currentIndex: 0
                keyNavigationEnabled: false
                highlightFollowsCurrentItem: true
                highlightMoveDuration: 110
                highlightResizeDuration: 0
                boundsBehavior: Flickable.StopAtBounds

                // The selection: a 2 px blue line on the left edge, the
                // only translating element.
                highlight: Item {
                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.topMargin: Tokens.spacing_s
                        anchors.bottomMargin: Tokens.spacing_s
                        width: 2
                        color: Spectrum.active
                    }
                }

                section.property: "providerName"
                section.criteria: ViewSection.FullString
                section.delegate: Item {
                    id: sectionHeader

                    required property string section

                    width: ListView.view.width
                    height: root._sectionHeight

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: Tokens.spacing_m
                        anchors.verticalCenter: parent.verticalCenter
                        text: sectionHeader.section
                        textFormat: Text.PlainText
                        color: Theme.on_surface_variant
                        font.family: Tokens.font_family_ui
                        font.pixelSize: Tokens.font_size_label_s
                        font.weight: Tokens.font_weight_medium
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: 1
                    }
                }

                delegate: LauncherResultRow {
                    id: row

                    width: ListView.view.width
                    current: ListView.isCurrentItem
                    onClicked: {
                        list.currentIndex = row.index;
                        root.activateCurrent(false);
                    }
                }

                onCountChanged: {
                    if (currentIndex < 0 || currentIndex >= count)
                        currentIndex = 0;
                }
            }

            Text {
                Layout.fillWidth: true
                visible: list.count === 0
                text: root.results.query.length > 0 ? qsTr("No results for %1").arg(root.results.query) : qsTr("Type to search")
                textFormat: Text.PlainText
                color: Theme.on_surface_variant
                font.family: Tokens.font_family_ui
                font.pixelSize: Tokens.font_size_body_s
                elide: Text.ElideRight
            }

            TabularText {
                Layout.fillWidth: true
                readonly property LauncherResultRow currentRow: list.currentItem as LauncherResultRow
                text: qsTr("↑↓ navigate · ↵ %1 · Alt+↵ alternate · Tab cycles providers · Esc closes").arg(currentRow ? currentRow.primaryActionLabel : qsTr("open"))
                textFormat: Text.PlainText
                color: Theme.on_surface_variant
                font.pixelSize: Tokens.font_size_label_s
                elide: Text.ElideRight
            }
        }

        // ─── Viewfinder ──────────────────────────────────────────────────
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            PlacementMiniature {
                id: mini

                anchors.centerIn: parent
                width: Math.min(parent.width, parent.height * aspect)
                height: width / aspect
                model: root.map
                cellRadius: Tokens.radius_mini
                interactive: true
                onCellClicked: id => {
                    if (root.map)
                        root.map.activate(id);
                }
            }

            // Windows results drawn on their cells: a state-axis fill by
            // score, white edge on the selected one. The model's
            // `resultId` for the Windows provider is the toplevel id, and
            // the map's cell id is the daemon window id; they only agree
            // once the daemon exposes toplevel ids on cells.
            // [NEW] WindowStateEntry.appId/title for the match.
            Repeater {
                model: root.results.providerFilter === "windows" ? root.results : null

                delegate: Item {
                    id: overlay

                    required property int index
                    required property string resultId
                    required property real score

                    readonly property var cell: {
                        if (!root.map)
                            return null;
                        const cells = root.map.cells;
                        for (let i = 0; i < cells.length; ++i) {
                            if (cells[i].id === overlay.resultId)
                                return cells[i];
                        }
                        return null;
                    }
                    visible: cell !== null
                    x: mini.x + (cell ? cell.x * mini.width : 0)
                    y: mini.y + (cell ? cell.y * mini.height : 0)
                    width: cell ? cell.w * mini.width : 0
                    height: cell ? cell.h * mini.height : 0

                    Rectangle {
                        anchors.fill: parent
                        radius: Tokens.radius_mini
                        color: Spectrum.at(Math.max(0, Math.min(1, overlay.score)))
                        opacity: 0.35
                        border.width: 1
                        border.color: list.currentIndex === overlay.index ? Spectrum.focus : color
                    }
                }
            }

            // Apps: where the launch will land. Phase 1 uses the focused
            // cell. [NEW] engine next-placement query.
            Rectangle {
                readonly property var cell: {
                    if (!root.map || root.results.providerFilter !== "apps")
                        return null;
                    const cells = root.map.cells;
                    for (let i = 0; i < cells.length; ++i) {
                        if (cells[i].focused)
                            return cells[i];
                    }
                    return null;
                }
                visible: cell !== null
                x: mini.x + (cell ? cell.x * mini.width : 0)
                y: mini.y + (cell ? cell.y * mini.height : 0)
                width: cell ? cell.w * mini.width : 0
                height: cell ? cell.h * mini.height : 0
                radius: Tokens.radius_mini
                color: "transparent"
                border.width: 1
                border.color: Spectrum.active
                opacity: 0.9

                Text {
                    anchors.centerIn: parent
                    text: qsTr("next")
                    color: Theme.on_surface_variant
                    font.family: Tokens.font_family_mono
                    font.pixelSize: Tokens.font_size_label_s
                }
            }

            TabularText {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: mini.bottom
                anchors.topMargin: Tokens.spacing_s
                visible: root.map !== null
                readonly property var _nouns: [qsTr("%n zone(s)", "", root.map ? root.map.cells.length : 0), qsTr("%n tile(s)", "", root.map ? root.map.cells.length : 0), qsTr("%n column(s)", "", root.map ? root.map.cells.length : 0)]
                text: root.map && root.map.mode >= 0 ? [qsTr("Snapping"), qsTr("Tiling"), qsTr("Scrolling")][root.map.mode] + " · " + _nouns[root.map.mode] : qsTr("No placement engine")
                color: Theme.on_surface_variant
                font.pixelSize: Tokens.font_size_label_s
            }
        }
    }

    component ProviderLabel: Item {
        id: pl

        property string text: ""
        property bool selected: false

        signal clicked

        width: label.implicitWidth
        height: label.implicitHeight + 4

        Accessible.role: Accessible.Button
        Accessible.name: pl.text
        Accessible.onPressAction: pl.clicked()

        Text {
            id: label

            text: pl.text
            color: pl.selected ? Theme.on_surface : Theme.on_surface_variant
            font.family: Tokens.font_family_ui
            font.pixelSize: Tokens.font_size_label_m
            font.weight: Tokens.font_weight_medium
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1
        }
        SpectrumUnderline {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            length: label.implicitWidth
            color: Spectrum.active
            t: 0.33
            opacity: pl.selected ? 1 : (plHover.hovered ? 0.5 : 0)
        }
        HoverHandler {
            id: plHover

            cursorShape: Qt.PointingHandCursor
        }
        TapHandler {
            onTapped: pl.clicked()
        }
    }
}

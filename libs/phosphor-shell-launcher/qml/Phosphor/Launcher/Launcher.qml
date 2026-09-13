// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

FocusScope {
    id: root
    required property var results
    property var map: null
    property Component decoration: null
    signal activated
    signal dismissed
    LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true
    implicitWidth: Math.min(Appearance.stage ? 760 : 640, Screen.width - 2 * Appearance.gap)
    implicitHeight: Math.min(540, Screen.height - 2 * Appearance.gap)
    readonly property int _rowHeight: 54
    readonly property int _sectionHeight: 26
    readonly property alias queryText: field.text
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

    ShellSurface {
        id: ground
        property bool shaderAnchor: true
        anchors.fill: parent
    }
    DecorationSlot {
        anchors.fill: parent
        component: root.decoration
        contentItem: ground
        surfacePath: "shell.phosphor.popout"
        focused: root.activeFocus
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Appearance.padding
        spacing: 14
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            radius: Math.min(14, Appearance.radius)
            color: Appearance.recess
            border.width: 1
            border.color: field.activeFocus ? Qt.alpha(Appearance.accent, 0.6) : Appearance.outline
            TextInput {
                id: field
                anchors.fill: parent
                anchors.margins: 16
                color: Appearance.text
                font.family: Tokens.font_family_ui
                font.pixelSize: 17
                focus: true
                selectByMouse: true
                clip: true
                Accessible.name: qsTr("Search apps, windows and commands")
                Text {
                    anchors.fill: parent
                    visible: !field.text.length
                    text: qsTr("Search apps, windows and commands…")
                    color: Appearance.muted
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
        Flow {
            id: pills
            objectName: "providerPills"
            Layout.fillWidth: true
            spacing: 6
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
                    visible: modelData.count > 0 || selected
                    text: qsTr("%1 %2").arg(modelData.name).arg(Number(modelData.count).toLocaleString(Qt.locale()))
                    selected: root.results.providerFilter === modelData.id
                    onClicked: {
                        root.results.providerFilter = modelData.id;
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
            spacing: 4
            keyNavigationEnabled: false
            boundsBehavior: Flickable.StopAtBounds
            highlightFollowsCurrentItem: true
            ScrollBar.vertical: ScrollBar {}
            section.property: "providerName"
            section.criteria: ViewSection.FullString
            section.delegate: Text {
                required property string section
                width: ListView.view.width
                height: root._sectionHeight
                text: section
                textFormat: Text.PlainText
                color: Appearance.muted
                font.pixelSize: 10
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 1
                verticalAlignment: Text.AlignVCenter
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
            onCountChanged: if (currentIndex < 0 || currentIndex >= count)
                currentIndex = 0
        }
        Text {
            Layout.fillWidth: true
            visible: list.count === 0
            text: root.results.query.length ? qsTr("No results for %1").arg(root.results.query) : qsTr("Type to search")
            textFormat: Text.PlainText
            color: Appearance.muted
            font.pixelSize: 12
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("↑ ↓ Navigate · Enter Open · Tab Filter · Esc Close")
            color: Appearance.muted
            font.pixelSize: 10
            elide: Text.ElideRight
        }
    }
    component ProviderLabel: ShellButton {
        property bool selected: false
        highlighted: selected
        height: 30
    }
}

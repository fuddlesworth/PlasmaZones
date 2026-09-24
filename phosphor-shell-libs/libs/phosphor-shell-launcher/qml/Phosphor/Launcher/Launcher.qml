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
    property var catalog: null
    property Component decoration: null
    property bool wide: false
    readonly property bool stageHome: wide && catalog !== null && field.text.length === 0 && results.providerFilter === ""
    readonly property real panelPadding: wide ? 26 : 20
    readonly property int popoutTopInset: 145
    readonly property alias queryText: field.text
    property int selectedApp: -1
    readonly property var workspaceWindows: map ? map.windows : []
    signal activated
    signal dismissed
    LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true
    implicitWidth: Math.min(wide ? 1100 : 620, Screen.width - 44)
    implicitHeight: Math.min(Screen.height - 80, wide ? 420 : 183 + Math.min(7, Math.max(1, list.count)) * 59)
    Binding {
        target: root.results
        property: "active"
        value: root.visible
        restoreMode: Binding.RestoreNone
    }
    function reset(): void {
        field.text = "";
        results.query = "";
        results.providerFilter = "";
        list.currentIndex = 0;
        recent.currentIndex = 0;
        selectedApp = -1;
        field.forceActiveFocus();
    }
    function activateCurrent(alternate: bool): void {
        if (stageHome) {
            if (selectedApp >= 0) {
                const app = catalog.pinnedApplications[selectedApp];
                if (app && catalog.launchPinned(app.id))
                    root.activated();
            } else if (recent.currentIndex >= 0 && recent.currentIndex < workspaceWindows.length) {
                map.activateNavigationWindow(workspaceWindows[recent.currentIndex].windowId);
                root.activated();
            }
            return;
        }
        if (list.currentIndex < 0 || list.currentIndex >= list.count)
            return;
        const row = list.currentIndex;
        const repeatable = alternate && results.alternateIsRepeatable(row);
        if (results.activate(row, alternate) && !repeatable)
            root.activated();
    }
    function moveSelection(delta: int): void {
        if (stageHome) {
            selectedApp = -1;
            recent.currentIndex = Math.max(0, Math.min(recent.count - 1, recent.currentIndex + delta));
        } else
            list.currentIndex = Math.max(0, Math.min(list.count - 1, list.currentIndex + delta));
    }
    function filter(id: string): void {
        results.providerFilter = id;
        list.currentIndex = 0;
        selectedApp = -1;
        field.forceActiveFocus();
    }
    function cycleFilter(delta: int): void {
        if (!catalog) {
            results.cycleProviderFilter(delta);
            return;
        }
        const filters = ["", "windows", "apps", "actions"];
        filter(filters[(filters.indexOf(results.providerFilter) + delta + filters.length) % filters.length]);
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
        layeredStages: true
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: root.panelPadding + 1
        spacing: 0
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.wide ? 66 : 55
            radius: Appearance.radius * 0.6
            color: Appearance.recess
            RowLayout {
                anchors.fill: parent
                anchors.margins: root.wide ? 17 : 15
                spacing: 13
                ShellIcon {
                    source: "system-search"
                    implicitWidth: 19
                    implicitHeight: 19
                    color: Appearance.text
                }
                TextInput {
                    id: field
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    verticalAlignment: TextInput.AlignVCenter
                    color: Appearance.text
                    font.family: Tokens.font_family_ui
                    font.pixelSize: Math.round((root.wide ? 22 : 17) * Appearance.textScale)
                    focus: true
                    selectByMouse: true
                    clip: true
                    Accessible.name: qsTr("Search apps, windows and actions")
                    Text {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        visible: !field.text.length
                        text: root.wide ? qsTr("Search your desktop…") : qsTr("Apps, windows, actions…")
                        color: Appearance.muted
                        font: field.font
                        elide: Text.ElideRight
                    }
                    onTextChanged: {
                        root.results.query = text;
                        list.currentIndex = 0;
                    }
                    Keys.onUpPressed: event => {
                        root.moveSelection(-1);
                        event.accepted = true;
                    }
                    Keys.onDownPressed: event => {
                        root.moveSelection(1);
                        event.accepted = true;
                    }
                    Keys.onLeftPressed: event => {
                        if (root.stageHome && root.catalog.pinnedApplications.length) {
                            root.selectedApp = root.selectedApp < 0 ? root.catalog.pinnedApplications.length - 1 : Math.max(0, root.selectedApp - 1);
                            event.accepted = true;
                        } else
                            event.accepted = false;
                    }
                    Keys.onRightPressed: event => {
                        if (root.stageHome && root.selectedApp >= 0) {
                            root.selectedApp = root.selectedApp + 1 < root.catalog.pinnedApplications.length ? root.selectedApp + 1 : -1;
                            event.accepted = true;
                        } else
                            event.accepted = false;
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
                        root.cycleFilter(1);
                        event.accepted = true;
                    }
                    Keys.onBacktabPressed: event => {
                        root.cycleFilter(-1);
                        event.accepted = true;
                    }
                    Keys.onEscapePressed: event => {
                        root.dismissed();
                        event.accepted = true;
                    }
                }
                Keycap {
                    text: qsTr("Esc")
                }
            }
        }
        Flow {
            id: pills
            objectName: "providerPills"
            Layout.fillWidth: true
            Layout.topMargin: 17
            Layout.bottomMargin: 12
            spacing: 8
            ProviderLabel {
                text: qsTr("All")
                selected: root.results.providerFilter === ""
                onClicked: root.filter("")
            }
            Repeater {
                model: root.catalog ? [
                    {
                        id: "windows",
                        name: qsTr("Windows")
                    },
                    {
                        id: "apps",
                        name: qsTr("Apps")
                    },
                    {
                        id: "actions",
                        name: qsTr("Actions")
                    }
                ] : root.results.providers
                ProviderLabel {
                    required property var modelData
                    text: modelData.name
                    selected: root.results.providerFilter === modelData.id
                    onClicked: root.filter(modelData.id)
                }
            }
        }
        RowLayout {
            id: homeColumns
            visible: root.stageHome
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 24
            spacing: 32
            ColumnLayout {
                Layout.fillWidth: false
                Layout.minimumWidth: 0
                Layout.preferredWidth: (homeColumns.width - 32) * 0.6
                Layout.alignment: Qt.AlignTop
                spacing: 16
                Eyebrow {
                    text: qsTr("PINNED APPLICATIONS")
                }
                Flickable {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredHeight: 120
                    clip: true
                    contentWidth: width
                    contentHeight: appGrid.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    GridLayout {
                        id: appGrid
                        width: parent.width
                        columns: width < 360 ? 2 : 4
                        columnSpacing: 10
                        rowSpacing: 10
                        Repeater {
                            model: root.catalog ? root.catalog.pinnedApplications : []
                            AbstractButton {
                                id: appTile
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                implicitHeight: 100
                                Accessible.name: modelData.name
                                background: Rectangle {
                                    color: Appearance.card
                                    radius: Appearance.radius * 0.7
                                    border.width: 1
                                    border.color: root.selectedApp === appTile.index || appTile.hovered ? Appearance.text : Appearance.outline
                                }
                                contentItem: ColumnLayout {
                                    spacing: 12
                                    Item {
                                        Layout.fillHeight: true
                                    }
                                    ShellIcon {
                                        Layout.alignment: Qt.AlignHCenter
                                        source: iconMapper.appIcon(appTile.modelData.iconName)
                                        implicitWidth: 26
                                        implicitHeight: 26
                                        color: Appearance.accent
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        Layout.leftMargin: 8
                                        Layout.rightMargin: 8
                                        horizontalAlignment: Text.AlignHCenter
                                        text: appTile.modelData.name
                                        color: Appearance.text
                                        font.family: Tokens.font_family_ui
                                        font.pixelSize: Math.round((11) * Appearance.textScale)
                                        elide: Text.ElideRight
                                    }
                                    Item {
                                        Layout.fillHeight: true
                                    }
                                }
                                onClicked: {
                                    if (root.catalog.launchPinned(modelData.id))
                                        root.activated();
                                }
                                TapHandler {
                                    acceptedButtons: Qt.RightButton
                                    onTapped: unpinMenu.popup()
                                }
                                Menu {
                                    id: unpinMenu
                                    MenuItem {
                                        text: qsTr("Unpin from launcher")
                                        onTriggered: root.catalog.togglePinned(appTile.modelData.id)
                                    }
                                }
                            }
                        }
                    }
                    Text {
                        visible: root.catalog && root.catalog.pinnedApplications.length === 0
                        width: parent.width
                        text: qsTr("Right-click an application in search to pin it here.")
                        wrapMode: Text.WordWrap
                        color: Appearance.muted
                        font.pixelSize: Math.round((11) * Appearance.textScale)
                    }
                }
            }
            Rectangle {
                Layout.fillHeight: true
                implicitWidth: 1
                color: Appearance.outline
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.leftMargin: -6
                spacing: 16
                Eyebrow {
                    text: qsTr("ON THIS WORKSPACE")
                }
                ListView {
                    id: recent
                    objectName: "workspaceResults"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: root.workspaceWindows
                    clip: true
                    currentIndex: 0
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    delegate: LauncherResultRow {
                        required property var modelData
                        width: ListView.view.width
                        compact: true
                        current: ListView.isCurrentItem && root.selectedApp < 0
                        title: modelData.title || modelData.appId
                        subtitle: {
                            const name = (modelData.appId || "").split(".").pop();
                            return name.charAt(0).toUpperCase() + name.slice(1);
                        }
                        iconName: modelData.appId
                        primaryActionLabel: qsTr("Open")
                        alternateActionLabel: ""
                        hasAlternateAction: false
                        onClicked: {
                            recent.currentIndex = index;
                            root.selectedApp = -1;
                            root.activateCurrent(false);
                        }
                    }
                    Text {
                        visible: recent.count === 0
                        width: parent.width
                        text: qsTr("No open windows")
                        color: Appearance.muted
                        font.pixelSize: Math.round((11) * Appearance.textScale)
                    }
                }
            }
        }
        ListView {
            id: list
            objectName: "resultList"
            visible: !root.stageHome
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.results
            currentIndex: 0
            clip: true
            spacing: 0
            keyNavigationEnabled: false
            boundsBehavior: Flickable.StopAtBounds
            highlightFollowsCurrentItem: true
            ScrollBar.vertical: ScrollBar {}
            delegate: LauncherResultRow {
                id: resultRow
                required providerId
                required resultId
                width: ListView.view.width
                current: ListView.isCurrentItem
                compact: root.wide
                catalog: root.catalog
                onClicked: {
                    list.currentIndex = resultRow.index;
                    root.activateCurrent(false);
                }
            }
            onCountChanged: if (currentIndex < 0 || currentIndex >= count)
                currentIndex = 0
            Text {
                visible: list.count === 0
                anchors.centerIn: parent
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: root.results.query.length ? qsTr("No results for %1").arg(root.results.query) : qsTr("Type to search")
                textFormat: Text.PlainText
                color: Appearance.muted
                font.pixelSize: Math.round((12) * Appearance.textScale)
                elide: Text.ElideRight
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 13
            spacing: 3
            Keycap {
                text: "↑"
            }
            Keycap {
                text: "↓"
            }
            Text {
                text: qsTr("Select")
                color: Appearance.muted
                font.pixelSize: Math.round((10) * Appearance.textScale)
            }
            Keycap {
                Layout.leftMargin: 5
                text: qsTr("Enter")
            }
            Text {
                text: qsTr("Open")
                color: Appearance.muted
                font.pixelSize: Math.round((10) * Appearance.textScale)
            }
            Item {
                Layout.fillWidth: true
            }
            Text {
                text: qsTr("Search by title or application")
                color: Appearance.muted
                font.pixelSize: Math.round((10) * Appearance.textScale)
                elide: Text.ElideRight
            }
        }
    }
    LauncherResultRow {
        id: iconMapper
        visible: false
        index: -1
        title: ""
        subtitle: ""
        iconName: ""
        primaryActionLabel: ""
        alternateActionLabel: ""
        hasAlternateAction: false
    }
    component ProviderLabel: ShellButton {
        property bool selected: false
        implicitHeight: 28
        implicitWidth: label.implicitWidth + 22
        highlighted: selected
        background: Rectangle {
            radius: 7
            color: parent.highlighted || parent.hovered ? Appearance.card : "transparent"
        }
        contentItem: Text {
            id: label
            text: parent.text
            color: parent.highlighted ? Appearance.text : Appearance.muted
            font.family: Tokens.font_family_ui
            font.pixelSize: Math.round((10) * Appearance.textScale)
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }
    component Eyebrow: Text {
        color: Appearance.muted
        font.family: Tokens.font_family_ui
        font.pixelSize: Math.round((10) * Appearance.textScale)
        font.letterSpacing: 1
    }
}

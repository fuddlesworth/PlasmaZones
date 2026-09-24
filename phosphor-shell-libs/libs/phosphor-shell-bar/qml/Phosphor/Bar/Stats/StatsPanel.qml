// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import Phosphor.Shell
import Phosphor.Theme
import Phosphor.Widgets

FocusScope {
    id: root
    property real railT: 0.8
    property real panelWidth: 452
    property string page: "overview"
    property int minutes: 1
    signal closeRequested
    signal networkSettingsRequested
    implicitWidth: panelWidth
    implicitHeight: Math.min(800, header.implicitHeight + 38 + body.implicitHeight + 38 + footer.implicitHeight + 24 + 2)
    readonly property real bodyPadding: Appearance.compact ? 16 : 20
    focus: true
    LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true
    function open(value: string): void {
        page = value;
        scroller.contentY = 0;
        Qt.callLater(() => value === "overview" ? body.forceActiveFocus() : back.forceActiveFocus());
    }
    Keys.onEscapePressed: event => {
        if (page === "overview")
            closeRequested();
        else
            open("overview");
        event.accepted = true;
    }
    onVisibleChanged: SystemStats.watch(root, visible)
    Component.onCompleted: {
        SystemStats.watch(root, visible);
        forceActiveFocus();
    }
    Component.onDestruction: SystemStats.watch(root, false)
    component HeaderButton: ShellButton {
        implicitWidth: 32
        implicitHeight: 32
        foreground: Appearance.muted
        background: Rectangle {
            radius: 8
            color: Qt.alpha(Appearance.text, parent.hovered ? 0.1 : 0.04)
            border.width: parent.visualFocus ? 1 : 0
            border.color: Appearance.text
        }
    }
    ShellSurface {
        anchors.fill: parent
        railT: root.railT
        accented: true
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            id: header
            Layout.fillWidth: true
            Layout.margins: Appearance.compact ? 16 : 20
            Layout.bottomMargin: 17
            spacing: 12
            Rectangle {
                visible: root.page === "overview"
                Layout.preferredWidth: 39
                Layout.preferredHeight: 39
                radius: 12
                color: Qt.alpha(Appearance.stops[0], 0.1)
                border.width: 1
                border.color: Qt.alpha(Appearance.stops[0], 0.2)
                ShellIcon {
                    anchors.centerIn: parent
                    width: 23
                    height: 23
                    source: "cpu"
                    isMask: true
                    color: Appearance.stops[0]
                }
            }
            HeaderButton {
                id: back
                objectName: "statsBack"
                visible: root.page !== "overview"
                iconName: "go-previous-symbolic"
                label: qsTr("Back to system overview")
                onClicked: root.open("overview")
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                DetailText {
                    Layout.fillWidth: true
                    text: root.page === "overview" ? qsTr("DESKTOP ACTIVITY") : root.page === "customize" ? qsTr("MAKE IT YOURS") : qsTr("SYSTEM / %1").arg(StatsModel.label(root.page).toUpperCase())
                    size: 8
                    font.letterSpacing: 1.6
                    muted: true
                }
                DetailText {
                    Layout.fillWidth: true
                    text: root.page === "overview" ? qsTr("System") : root.page === "customize" ? qsTr("Bar widget") : StatsModel.label(root.page)
                    size: Appearance.compact ? 21 : 23
                    font.letterSpacing: -0.5
                }
            }
            HeaderButton {
                objectName: "statsCustomize"
                visible: root.page !== "customize"
                iconName: "configure"
                label: qsTr("Customize stats widget")
                onClicked: root.open("customize")
            }
            HeaderButton {
                objectName: "statsClose"
                iconName: "window-close-symbolic"
                label: qsTr("Close system stats")
                onClicked: root.closeRequested()
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Appearance.outline
        }
        Flickable {
            id: scroller
            objectName: "statsScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            contentWidth: width
            contentHeight: body.implicitHeight + root.bodyPadding * 2
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentHeight > height + 2
            Basic.ScrollBar.vertical: Basic.ScrollBar {
                active: scroller.interactive
                policy: scroller.interactive ? Basic.ScrollBar.AlwaysOn : Basic.ScrollBar.AlwaysOff
            }
            Loader {
                id: body
                x: root.bodyPadding
                y: root.bodyPadding
                width: Math.max(0, scroller.width - root.bodyPadding * 2)
                sourceComponent: root.page === "overview" ? overview : root.page === "customize" ? customization : details
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Appearance.outline
        }
        RowLayout {
            id: footer
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: 10
            Layout.bottomMargin: 10
            DetailText {
                text: qsTr("Uptime")
                size: 9
                muted: true
            }
            DetailText {
                text: StatsModel.uptime
                size: 9
            }
            Item {
                Layout.fillWidth: true
            }
            ShellButton {
                objectName: "statsPause"
                text: SystemStats.paused ? qsTr("Paused") : qsTr("Live · %1s").arg(Appearance.settings.statsInterval)
                iconName: SystemStats.paused ? "media-playback-start" : "media-playback-pause"
                label: SystemStats.paused ? qsTr("Resume live readings") : qsTr("Pause live readings")
                Accessible.checked: SystemStats.paused
                flat: true
                foreground: Appearance.muted
                labelSize: 10
                implicitHeight: 28
                onClicked: SystemStats.paused = !SystemStats.paused
            }
        }
    }
    Component {
        id: overview
        StatsOverview {
            onSelected: metric => root.open(metric)
        }
    }
    Component {
        id: customization
        StatsWidgetSettings {}
    }
    Component {
        id: details
        StatsDetails {
            page: root.page
            minutes: root.minutes
            onRangeSelected: value => root.minutes = value
            onNetworkSettingsRequested: root.networkSettingsRequested()
        }
    }
    Connections {
        target: root.Window.window
        function onActiveFocusItemChanged(): void {
            const item = root.Window.window.activeFocusItem;
            let ancestor = item;
            while (ancestor && ancestor !== body)
                ancestor = ancestor.parent;
            if (!ancestor || !item)
                return;
            const point = item.mapToItem(scroller, 0, 0);
            const offset = point.y < 8 ? point.y - 8 : Math.max(0, point.y + item.height - scroller.height + 8);
            scroller.contentY = Math.max(0, Math.min(Math.max(0, scroller.contentHeight - scroller.height), scroller.contentY + offset));
        }
    }
}

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import Phosphor.Theme

FocusScope {
    id: root
    property var controller: typeof StatusIcons !== "undefined" ? StatusIcons : null
    property real railT: 0.93
    property string requestedStatusPage: ""
    property Item requestAnchor: null
    property string expandedStatusPage: ""
    property bool quickSettingsOpen: false
    property string focusedIcon: ""
    readonly property var shownIcons: controller ? controller.shownIcons : []
    readonly property int overflowCount: controller ? controller.overflowIcons.length : 0
    signal activated
    implicitWidth: symbols.implicitWidth + Tokens.spacing_xs * 2
    implicitHeight: symbols.implicitHeight + Tokens.spacing_xxs * 2
    Accessible.role: Accessible.Grouping
    Accessible.name: i18n("System status")
    LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true
    function request(page: string, anchor: Item): void {
        requestedStatusPage = page;
        requestAnchor = anchor;
        activated();
    }
    function focusIcon(id: string): void {
        for (let index = 0; index < buttons.count; ++index) {
            const button = buttons.itemAt(index);
            if (button.entry.id === id) {
                button.forceActiveFocus();
                return;
            }
        }
        if (overflow.visible)
            overflow.forceActiveFocus();
        else if (buttons.count)
            buttons.itemAt(0).forceActiveFocus();
        else
            fallback.forceActiveFocus();
    }
    function moveFocus(index: int, direction: int): void {
        const count = buttons.count + (overflow.visible ? 1 : 0);
        if (!count)
            return;
        const next = (index + direction + count) % count;
        (next === buttons.count ? overflow : buttons.itemAt(next)).forceActiveFocus();
    }
    onShownIconsChanged: if (activeFocus)
        Qt.callLater(() => root.focusIcon(root.focusedIcon))
    Rectangle {
        anchors.fill: parent
        radius: Tokens.radius_s
        color: Appearance.card
    }
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: eventPoint => {
            if (!symbols.childAt(eventPoint.position.x - symbols.x, eventPoint.position.y - symbols.y))
                root.request("settings", root);
        }
    }
    Row {
        id: symbols
        x: Tokens.spacing_xs
        y: Tokens.spacing_xxs
        Repeater {
            id: buttons
            model: root.shownIcons
            delegate: StatusIconButton {
                required property var modelData
                required property int index
                objectName: "statusIcon-" + modelData.id
                entry: modelData
                showPercentage: root.controller && root.controller.showBatteryPercentage
                expanded: root.quickSettingsOpen || root.expandedStatusPage === entry.id
                onActiveFocusChanged: if (activeFocus)
                    root.focusedIcon = entry.id
                onClicked: root.request("", this)
                onContextRequested: root.request(entry.id, this)
                Keys.onLeftPressed: root.moveFocus(index, -1)
                Keys.onRightPressed: root.moveFocus(index, 1)
            }
        }
        StatusIconButton {
            id: overflow
            objectName: "statusOverflow"
            activationDescription: i18n("Show more status icons")
            visible: root.overflowCount > 0
            entry: ({
                    id: "overflow",
                    title: i18n("More status icons"),
                    summary: i18n("%1 more indicators").arg(root.overflowCount),
                    iconName: "view-more-symbolic"
                })
            expanded: root.expandedStatusPage === "overflow"
            onClicked: root.request("overflow", this)
            onContextRequested: root.request("settings", this)
            Keys.onLeftPressed: root.moveFocus(buttons.count, -1)
            Keys.onRightPressed: root.moveFocus(buttons.count, 1)
        }
        StatusIconButton {
            id: fallback
            objectName: "statusFallback"
            visible: !buttons.count && !overflow.visible
            entry: ({
                    id: "settings",
                    title: i18n("Quick settings"),
                    summary: "",
                    iconName: "configure"
                })
            expanded: root.quickSettingsOpen || root.expandedStatusPage === "settings"
            onClicked: root.request("", this)
            onContextRequested: root.request("settings", this)
        }
    }
}

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Continuous split-action connection row. State follows the service.

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: root

    property string iconName: ""
    property string label: ""
    property string sublabel: ""
    property bool active: false
    // A request is in flight and the service has not echoed yet: the
    // underline goes purple (the state axis' "pending").
    property bool pending: false
    property bool available: true
    property string detailTitle: ""
    property Component detailContent: null
    property bool detailEnabled: true
    /// The bar panel this card drills into, by the bar-widget id that
    /// opens it ("network", "bluetooth", "audio"). Set instead of
    /// `detailContent` when the full view already exists as a panel: the
    /// chip on the bar and the card in here then open the SAME surface
    /// rather than two views of the same service drifting apart.
    property string detailPanelId: ""

    readonly property bool hasDetail: root.detailEnabled && (root.detailContent !== null || root.detailPanelId !== "")
    // Layout hint read by ControlCenter. Every rail spans the pane.
    // Whether this tile wants the grid's full width. A toggle is a card in
    // a column; a level (volume, brightness) reads better across the row,
    // because its underline IS its control and a longer line is a finer one.
    property bool spansRow: true
    property string controlGroup: "connections"
    /// Where this card sits on the shared field, 0..1. The host sets it from
    /// the card's position in the grid, so a row of cards steps along the
    /// spectrum instead of each one picking a colour.
    property real railT: 0.5

    signal toggled
    signal detailRequested

    implicitWidth: 320
    implicitHeight: Math.max(Appearance.rowHeight, root.sublabel ? 57 : 43)
    opacity: root.available ? 1 : StateLayer.disabled_content

    activeFocusOnTab: true

    Accessible.role: Accessible.Button
    Accessible.name: root.label
    Accessible.description: root.available ? root.sublabel : (root.sublabel.length > 0 ? qsTr("%1, unavailable").arg(root.sublabel) : qsTr("Unavailable"))
    Accessible.onPressAction: root._activate()

    Keys.onSpacePressed: event => root._activateFromKey(event)
    Keys.onReturnPressed: event => root._activateFromKey(event)
    Keys.onEnterPressed: event => root._activateFromKey(event)

    function _activate(): void {
        if (root.available)
            root.toggled();
    }

    function _activateFromKey(event: var): void {
        if (event.isAutoRepeat)
            return;
        if (!root.available)
            return;
        root.toggled();
        event.accepted = true;
    }

    Rectangle {
        anchors.fill: parent
        radius: Appearance.radius * 0.6
        color: root && root.active ? Qt.tint(Appearance.card, Qt.alpha(Appearance.accent, 0.12)) : Appearance.card
        border.width: 1
        border.color: root.activeFocus ? Appearance.text : Appearance.outline
    }

    Item {
        anchors.fill: parent
        anchors.rightMargin: root.hasDetail ? 44 : 0
        HoverHandler {
            id: hover
            enabled: root.available
            cursorShape: Qt.PointingHandCursor
        }
        TapHandler {
            enabled: root.available
            onTapped: root._activate()
        }
    }

    RowLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 13
        anchors.rightMargin: root.hasDetail ? 57 : 13
        spacing: 12
        ShellIcon {
            Layout.preferredWidth: 19
            Layout.preferredHeight: 19
            source: root.iconName
            isMask: true
            color: root && root.pending ? Appearance.at(0.66) : root && root.active ? Appearance.accent : Appearance.muted
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3
            Text {
                Accessible.ignored: true
                Layout.fillWidth: true
                text: root.label
                color: Appearance.text
                font.family: Tokens.font_family_ui
                font.pixelSize: Math.round((11) * Appearance.textScale)
                elide: Text.ElideRight
            }
            Text {
                Accessible.ignored: true
                Layout.fillWidth: true
                visible: root.sublabel !== ""
                text: root.sublabel
                color: Appearance.muted
                font.family: Tokens.font_family_ui
                font.pixelSize: Math.round((9) * Appearance.textScale)
                elide: Text.ElideRight
            }
        }
    }

    // One continuous surface with a separate, full-height details target.
    // The divider is inset; the end cap has no second rounded background.
    Item {
        id: chevron
        anchors.right: parent.right
        height: parent.height
        width: 44
        visible: root.hasDetail
        activeFocusOnTab: root.available
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("%1 details").arg(root.label)
        Accessible.onPressAction: if (root.available)
            root.detailRequested()
        Keys.onSpacePressed: event => {
            if (!event.isAutoRepeat && root.available)
                root.detailRequested();
            event.accepted = true;
        }
        Keys.onReturnPressed: event => {
            if (!event.isAutoRepeat && root.available)
                root.detailRequested();
            event.accepted = true;
        }
        Rectangle {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 1
            height: parent.height * 0.5
            color: Appearance.outline
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: 5
            radius: Math.max(3, Appearance.radius - 8)
            color: detailsHover.hovered ? Qt.alpha(Appearance.text, 0.07) : "transparent"
            border.width: chevron.activeFocus ? 1 : 0
            border.color: Appearance.text
        }
        ShellIcon {
            anchors.centerIn: parent
            width: 16
            height: 16
            source: root.LayoutMirroring.enabled ? "go-previous-symbolic" : "go-next-symbolic"
            isMask: true
            color: Appearance.text
        }
        HoverHandler {
            id: detailsHover
            enabled: root.available
            cursorShape: Qt.PointingHandCursor
        }
        TapHandler {
            enabled: root.available
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: root.detailRequested()
        }
    }
}

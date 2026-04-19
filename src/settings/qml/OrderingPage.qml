// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation
import org.plasmazones.common as QFZCommon

Flickable {
    id: root

    required property string headerText
    required property string infoText
    required property string emptyText
    required property string emptyExplanation
    required property string resetAccessibleName
    required property bool hasCustomOrder
    // Callbacks the parent page must provide
    required property var resolveOrder
    required property var moveItem
    required property var resetOrder
    // Zone preview source: "zones" uses model.zones, "previewZones" uses model.previewZones
    property string previewZonesKey: "zones"
    // Zone count key: "zoneCount" or "defaultMaxWindows"
    property string zoneCountKey: "zoneCount"
    // Whether to hide the badge when count is 0
    property bool hideZeroBadge: false
    property bool _rebuilding: false
    property bool _movingLocally: false

    function rebuildModel() {
        _rebuilding = true;
        orderModel.clear();
        let items = resolveOrder();
        for (let i = 0; i < items.length; i++) orderModel.append(items[i])
        _rebuilding = false;
    }

    // Move locally within the QML model and notify the controller without a full rebuild
    function moveLocal(from, to) {
        if (from < 0 || from >= orderModel.count || to < 0 || to >= orderModel.count || from === to)
            return ;

        _movingLocally = true;
        orderModel.move(from, to, 1);
        moveItem(from, to);
        _movingLocally = false;
    }

    function commitOrder() {
        let ids = [];
        for (let i = 0; i < orderModel.count; i++) ids.push(orderModel.get(i).id)
        return ids;
    }

    contentHeight: mainCol.implicitHeight
    clip: true
    Component.onCompleted: rebuildModel()

    ColumnLayout {
        id: mainCol

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            text: root.infoText
            visible: true
        }

        SettingsCard {
            headerText: root.headerText

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                // Manual layout with absolute positioning for drag animation
                Item {
                    id: orderContainer

                    readonly property real rowHeight: Kirigami.Units.gridUnit * 4
                    property int dragFromIndex: -1
                    property int dropTargetIndex: -1
                    property bool isDragging: false

                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(orderModel.count * rowHeight, Kirigami.Units.gridUnit * 10)
                    clip: true

                    ListModel {
                        id: orderModel
                    }

                    Repeater {
                        id: repeater

                        model: orderModel

                        delegate: Item {
                            id: delegateRoot

                            required property int index
                            required property var model
                            // Base position from index
                            readonly property real baseY: index * orderContainer.rowHeight
                            // Visual offset: shift other items to make room during drag
                            readonly property real visualOffset: {
                                if (!orderContainer.isDragging || index === orderContainer.dragFromIndex)
                                    return 0;

                                let from = orderContainer.dragFromIndex;
                                let to = orderContainer.dropTargetIndex;
                                if (from < 0 || to < 0)
                                    return 0;

                                // Items between from and to need to shift by one row
                                if (from < to) {
                                    // Dragging down: items between (from, to] shift up
                                    if (index > from && index <= to)
                                        return -orderContainer.rowHeight;

                                } else {
                                    // Dragging up: items between [to, from) shift down
                                    if (index >= to && index < from)
                                        return orderContainer.rowHeight;

                                }
                                return 0;
                            }

                            width: orderContainer.width
                            height: orderContainer.rowHeight
                            y: baseY + visualOffset
                            z: dragArea.drag.active ? 100 : 0

                            Rectangle {
                                id: delegateContent

                                property bool isDragging: dragArea.drag.active

                                width: parent.width
                                height: orderContainer.rowHeight
                                radius: Kirigami.Units.smallSpacing
                                border.width: isDragging ? 2 : 0
                                border.color: Kirigami.Theme.highlightColor
                                color: {
                                    if (isDragging)
                                        return Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.15);

                                    if (delegateHover.hovered)
                                        return Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.06);

                                    return "transparent";
                                }
                                // Scale up slightly when dragging
                                scale: isDragging ? 1.02 : 1

                                HoverHandler {
                                    id: delegateHover
                                }

                                MouseArea {
                                    id: dragArea

                                    anchors.fill: parent
                                    drag.target: delegateRoot
                                    drag.axis: Drag.YAxis
                                    drag.minimumY: 0
                                    drag.maximumY: (orderModel.count - 1) * orderContainer.rowHeight
                                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                                    onPressed: {
                                        orderContainer.dragFromIndex = delegateRoot.index;
                                        orderContainer.dropTargetIndex = delegateRoot.index;
                                        orderContainer.isDragging = true;
                                    }
                                    onReleased: {
                                        let from = orderContainer.dragFromIndex;
                                        let to = orderContainer.dropTargetIndex;
                                        orderContainer.isDragging = false;
                                        orderContainer.dragFromIndex = -1;
                                        orderContainer.dropTargetIndex = -1;
                                        // Reset position — the model move will reposition
                                        delegateRoot.y = Qt.binding(function() {
                                            return delegateRoot.baseY + delegateRoot.visualOffset;
                                        });
                                        if (from >= 0 && to >= 0 && from !== to && from < orderModel.count && to < orderModel.count)
                                            root.moveLocal(from, to);

                                    }
                                    onPositionChanged: {
                                        if (drag.active) {
                                            // Calculate target index from dragged item center
                                            let centerY = delegateRoot.y + orderContainer.rowHeight / 2;
                                            let targetIndex = Math.max(0, Math.min(orderModel.count - 1, Math.floor(centerY / orderContainer.rowHeight)));
                                            if (targetIndex !== orderContainer.dropTargetIndex)
                                                orderContainer.dropTargetIndex = targetIndex;

                                        }
                                    }
                                }

                                RowLayout {
                                    spacing: Kirigami.Units.largeSpacing

                                    anchors {
                                        fill: parent
                                        margins: Kirigami.Units.smallSpacing
                                        leftMargin: Kirigami.Units.largeSpacing
                                        rightMargin: Kirigami.Units.largeSpacing
                                    }

                                    // Drag handle
                                    Kirigami.Icon {
                                        source: "handle-sort"
                                        Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                                        Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
                                        opacity: delegateHover.hovered ? 0.7 : 0.3

                                        Behavior on opacity {
                                            PhosphorMotionAnimation {
                                                profile: "global"
                                            }

                                        }

                                    }

                                    // Position number
                                    Label {
                                        text: (delegateRoot.index + 1) + "."
                                        font.bold: true
                                        color: Kirigami.Theme.disabledTextColor
                                        Layout.preferredWidth: Kirigami.Units.gridUnit * 1.5
                                        horizontalAlignment: Text.AlignRight
                                    }

                                    // Preview thumbnail
                                    Rectangle {
                                        Layout.preferredWidth: Kirigami.Units.gridUnit * 6
                                        Layout.preferredHeight: Kirigami.Units.gridUnit * 3
                                        radius: Kirigami.Units.smallSpacing / 2
                                        color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2)
                                        border.color: delegateHover.hovered ? Kirigami.Theme.highlightColor : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.9)
                                        border.width: delegateHover.hovered ? 2 : 1

                                        QFZCommon.ZonePreview {
                                            anchors.fill: parent
                                            anchors.margins: Math.round(Kirigami.Units.smallSpacing * 0.75)
                                            zones: delegateRoot.model[root.previewZonesKey] || []
                                            isHovered: delegateHover.hovered
                                            showZoneNumbers: false
                                            minZoneSize: 2
                                        }

                                        Behavior on border.color {
                                            PhosphorMotionAnimation {
                                                profile: "global"
                                            }

                                        }

                                        Behavior on border.width {
                                            PhosphorMotionAnimation {
                                                profile: "global"
                                            }

                                        }

                                    }

                                    // Name + description
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2

                                        Label {
                                            text: delegateRoot.model.displayName
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }

                                        Label {
                                            text: delegateRoot.model.description || ""
                                            elide: Text.ElideRight
                                            font: Kirigami.Theme.smallFont
                                            color: Kirigami.Theme.disabledTextColor
                                            visible: text.length > 0
                                            Layout.fillWidth: true
                                        }

                                    }

                                    // Zone count badge
                                    Rectangle {
                                        readonly property int count: delegateRoot.model[root.zoneCountKey] || 0

                                        Layout.preferredWidth: badgeLabel.implicitWidth + Kirigami.Units.smallSpacing * 2
                                        Layout.preferredHeight: badgeLabel.implicitHeight + Kirigami.Units.smallSpacing
                                        radius: height / 2
                                        color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.15)
                                        visible: !root.hideZeroBadge || count > 0

                                        Label {
                                            id: badgeLabel

                                            anchors.centerIn: parent
                                            text: i18np("%n zone", "%n zones", parent.count)
                                            font: Kirigami.Theme.smallFont
                                            color: Kirigami.Theme.highlightColor
                                        }

                                    }

                                    // Move up
                                    ToolButton {
                                        icon.name: "arrow-up"
                                        icon.width: Kirigami.Units.iconSizes.smallMedium
                                        icon.height: Kirigami.Units.iconSizes.smallMedium
                                        enabled: delegateRoot.index > 0
                                        opacity: enabled ? 1 : 0.3
                                        onClicked: root.moveLocal(delegateRoot.index, delegateRoot.index - 1)
                                        ToolTip.visible: hovered
                                        ToolTip.text: i18n("Move up")
                                        Accessible.name: i18n("Move %1 up", delegateRoot.model.displayName)

                                        Behavior on opacity {
                                            PhosphorMotionAnimation {
                                                profile: "global"
                                            }

                                        }

                                    }

                                    // Move down
                                    ToolButton {
                                        icon.name: "arrow-down"
                                        icon.width: Kirigami.Units.iconSizes.smallMedium
                                        icon.height: Kirigami.Units.iconSizes.smallMedium
                                        enabled: delegateRoot.index < orderModel.count - 1
                                        opacity: enabled ? 1 : 0.3
                                        onClicked: root.moveLocal(delegateRoot.index, delegateRoot.index + 1)
                                        ToolTip.visible: hovered
                                        ToolTip.text: i18n("Move down")
                                        Accessible.name: i18n("Move %1 down", delegateRoot.model.displayName)

                                        Behavior on opacity {
                                            PhosphorMotionAnimation {
                                                profile: "global"
                                            }

                                        }

                                    }

                                }

                                Behavior on color {
                                    PhosphorMotionAnimation {
                                        profile: "global"
                                    }

                                }

                                Behavior on border.width {
                                    PhosphorMotionAnimation {
                                        profile: "global"
                                    }

                                }

                                // Subtle lift on hover
                                transform: Translate {
                                    y: delegateHover.hovered && !delegateContent.isDragging ? -1 : 0

                                    Behavior on y {
                                        PhosphorMotionAnimation {
                                            profile: "global"
                                        }

                                    }

                                }

                                Behavior on scale {
                                    PhosphorMotionAnimation {
                                        profile: "global"
                                    }

                                }

                            }

                            Behavior on y {
                                enabled: !dragArea.drag.active

                                NumberAnimation {
                                    duration: Kirigami.Units.longDuration
                                    easing.type: Easing.OutCubic
                                }

                            }

                        }

                    }

                    Kirigami.PlaceholderMessage {
                        anchors.centerIn: parent
                        visible: orderModel.count === 0
                        text: root.emptyText
                        explanation: root.emptyExplanation
                    }

                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    Item {
                        Layout.fillWidth: true
                    }

                    Button {
                        text: i18n("Reset to Default Order")
                        icon.name: "edit-reset"
                        enabled: root.hasCustomOrder
                        onClicked: root.resetOrder()
                        Accessible.name: root.resetAccessibleName
                    }

                }

            }

        }

    }

}

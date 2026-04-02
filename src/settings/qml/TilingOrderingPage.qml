// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.common as QFZCommon

Flickable {
    id: root

    readonly property var
    settingsBridge: TilingBridge {
    }

    readonly property bool hasCustomOrder: settingsController.settings.tilingAlgorithmOrder.length > 0

    function rebuildModel() {
        orderModel.clear();
        let items = settingsController.resolvedTilingOrder();
        for (let i = 0; i < items.length; i++) {
            let item = items[i];
            item.previewZones = settingsController.generateAlgorithmPreview(item.id, 4, 0.6, 1);
            orderModel.append(item);
        }
    }

    function commitOrder() {
        let ids = [];
        for (let i = 0; i < orderModel.count; i++) ids.push(orderModel.get(i).id)
        settingsController.settings.tilingAlgorithmOrder = ids;
    }

    contentHeight: mainCol.implicitHeight
    clip: true
    Component.onCompleted: rebuildModel()

    Connections {
        function onAvailableAlgorithmsChanged() {
            root.rebuildModel();
        }

        target: settingsController
    }

    Connections {
        function onTilingAlgorithmOrderChanged() {
            root.rebuildModel();
        }

        target: settingsController.settings
    }

    ColumnLayout {
        id: mainCol

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            text: i18n("Set the priority order for algorithms when cycling with keyboard shortcuts and in the zone selector popup. Drag rows or use the arrow buttons to reorder.")
            visible: true
        }

        SettingsCard {
            headerText: i18n("Tiling Algorithm Priority")

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

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
                            readonly property real baseY: index * orderContainer.rowHeight
                            readonly property real visualOffset: {
                                if (!orderContainer.isDragging || index === orderContainer.dragFromIndex)
                                    return 0;

                                let from = orderContainer.dragFromIndex;
                                let to = orderContainer.dropTargetIndex;
                                if (from < 0 || to < 0)
                                    return 0;

                                if (from < to) {
                                    if (index > from && index <= to)
                                        return -orderContainer.rowHeight;

                                } else {
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
                                        delegateRoot.y = Qt.binding(function() {
                                            return delegateRoot.baseY + delegateRoot.visualOffset;
                                        });
                                        if (from >= 0 && to >= 0 && from !== to) {
                                            orderModel.move(from, to, 1);
                                            root.commitOrder();
                                        }
                                    }
                                    onPositionChanged: {
                                        if (drag.active) {
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
                                            NumberAnimation {
                                                duration: 200
                                                easing.type: Easing.OutCubic
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

                                    // Algorithm preview (same style as the dropdown)
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
                                            zones: delegateRoot.model.previewZones || []
                                            isHovered: delegateHover.hovered
                                            showZoneNumbers: false
                                            minZoneSize: 2
                                        }

                                        Behavior on border.color {
                                            ColorAnimation {
                                                duration: 200
                                                easing.type: Easing.OutCubic
                                            }

                                        }

                                        Behavior on border.width {
                                            NumberAnimation {
                                                duration: 150
                                                easing.type: Easing.OutCubic
                                            }

                                        }

                                    }

                                    // Algorithm name + description
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2

                                        Label {
                                            text: delegateRoot.model.name
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
                                        readonly property int count: delegateRoot.model.defaultMaxWindows || 0

                                        Layout.preferredWidth: algoZoneLabel.implicitWidth + Kirigami.Units.smallSpacing * 2
                                        Layout.preferredHeight: algoZoneLabel.implicitHeight + Kirigami.Units.smallSpacing
                                        radius: height / 2
                                        color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.15)
                                        visible: count > 0

                                        Label {
                                            id: algoZoneLabel

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
                                        onClicked: settingsController.moveTilingAlgorithm(delegateRoot.index, delegateRoot.index - 1)
                                        ToolTip.visible: hovered
                                        ToolTip.text: i18n("Move up")
                                        Accessible.name: i18n("Move %1 up", delegateRoot.model.name)

                                        Behavior on opacity {
                                            NumberAnimation {
                                                duration: 200
                                                easing.type: Easing.OutCubic
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
                                        onClicked: settingsController.moveTilingAlgorithm(delegateRoot.index, delegateRoot.index + 1)
                                        ToolTip.visible: hovered
                                        ToolTip.text: i18n("Move down")
                                        Accessible.name: i18n("Move %1 down", delegateRoot.model.name)

                                        Behavior on opacity {
                                            NumberAnimation {
                                                duration: 200
                                                easing.type: Easing.OutCubic
                                            }

                                        }

                                    }

                                }

                                Behavior on color {
                                    ColorAnimation {
                                        duration: 200
                                        easing.type: Easing.OutCubic
                                    }

                                }

                                Behavior on border.width {
                                    NumberAnimation {
                                        duration: 150
                                        easing.type: Easing.OutCubic
                                    }

                                }

                                transform: Translate {
                                    y: delegateHover.hovered && !delegateContent.isDragging ? -1 : 0

                                    Behavior on y {
                                        NumberAnimation {
                                            duration: 200
                                            easing.type: Easing.OutCubic
                                        }

                                    }

                                }

                                Behavior on scale {
                                    NumberAnimation {
                                        duration: 200
                                        easing.type: Easing.OutCubic
                                    }

                                }

                            }

                            Behavior on y {
                                enabled: !dragArea.drag.active

                                NumberAnimation {
                                    duration: 300
                                    easing.type: Easing.OutCubic
                                }

                            }

                        }

                    }

                    Kirigami.PlaceholderMessage {
                        anchors.centerIn: parent
                        visible: orderModel.count === 0
                        text: i18n("No algorithms available")
                        explanation: i18n("Algorithms are registered by the daemon.")
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
                        onClicked: settingsController.resetTilingOrder()
                        Accessible.name: i18n("Reset algorithm order to default")
                    }

                }

            }

        }

    }

}

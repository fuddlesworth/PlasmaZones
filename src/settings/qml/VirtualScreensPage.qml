// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Settings page for virtual screen configuration.
 *
 * Allows splitting a physical monitor into multiple virtual screens
 * with preset layouts or custom split ratios. Changes are staged via
 * settingsController and flushed to the daemon when the global Apply
 * button is clicked.
 */
Flickable {
    id: root

    // ── Internal state ───────────────────────────────────────────────────
    property string _selectedScreen: ""
    property var _pendingScreens: [] // Local editable state
    property var _savedScreens: [] // What is actually applied in daemon
    property int _screenWidth: 1920 // Actual pixel width of selected screen
    property int _screenHeight: 1080 // Actual pixel height of selected screen

    function _refreshConfig() {
        if (_selectedScreen === "")
            return ;

        // Check for staged config first
        if (settingsController.hasUnsavedVirtualScreenConfig(_selectedScreen))
            _pendingScreens = settingsController.getStagedVirtualScreenConfig(_selectedScreen);
        else
            _pendingScreens = settingsController.getVirtualScreenConfig(_selectedScreen);
        _savedScreens = settingsController.getVirtualScreenConfig(_selectedScreen);
    }

    function _deepCopy(arr) {
        return JSON.parse(JSON.stringify(arr));
    }

    function _stageCurrentConfig() {
        if (_selectedScreen === "" || _pendingScreens.length === 0)
            return ;

        settingsController.stageVirtualScreenConfig(_selectedScreen, _pendingScreens);
    }

    function _loadPreset(ratios, names) {
        var screens = [];
        var xPos = 0;
        for (var i = 0; i < ratios.length; i++) {
            var w = ratios[i] / 100;
            screens.push({
                "x": xPos,
                "y": 0,
                "width": w,
                "height": 1,
                "displayName": names[i] || ""
            });
            xPos += w;
        }
        _pendingScreens = screens;
        _stageCurrentConfig();
    }

    function _updateScreenGeometry() {
        var screens = settingsController.screens;
        for (var i = 0; i < screens.length; i++) {
            if (screens[i].name === _selectedScreen) {
                _screenWidth = screens[i].width || 1920;
                _screenHeight = screens[i].height || 1080;
                return ;
            }
        }
        _screenWidth = 1920;
        _screenHeight = 1080;
    }

    // Redistribute splits equally for a given count
    function _redistributeEqual(count) {
        if (count < 1)
            count = 1;

        if (count > 5)
            count = 5;

        var screens = [];
        var w = 1 / count;
        for (var i = 0; i < count; i++) {
            var existingName = "";
            if (i < _pendingScreens.length)
                existingName = _pendingScreens[i].displayName || "";

            screens.push({
                "x": i * w,
                "y": 0,
                "width": w,
                "height": 1,
                "displayName": existingName
            });
        }
        _pendingScreens = screens;
        _stageCurrentConfig();
    }

    // Clamp divider move: ensure both adjacent regions have at least 10%
    function _moveDivider(dividerIndex, newFraction) {
        if (dividerIndex < 0 || dividerIndex >= _pendingScreens.length - 1)
            return ;

        var screens = _deepCopy(_pendingScreens);
        var leftIdx = dividerIndex;
        var rightIdx = dividerIndex + 1;
        var leftStart = screens[leftIdx].x;
        var rightEnd = screens[rightIdx].x + screens[rightIdx].width;
        var minW = 0.1; // 10% minimum
        var newDivPos = Math.max(leftStart + minW, Math.min(rightEnd - minW, newFraction));
        screens[leftIdx].width = newDivPos - leftStart;
        screens[rightIdx].x = newDivPos;
        screens[rightIdx].width = rightEnd - newDivPos;
        // Normalize: ensure total width is exactly 1.0
        var total = 0;
        for (var k = 0; k < screens.length - 1; k++) {
            total += screens[k].width;
        }
        screens[screens.length - 1].width = 1 - total;
        // Also fix the x position of the last screen
        screens[screens.length - 1].x = total;
        _pendingScreens = screens;
        _stageCurrentConfig();
    }

    contentHeight: content.implicitHeight
    clip: true
    Component.onCompleted: {
        // Auto-select primary monitor, fallback to first
        if (!_selectedScreen && settingsController.screens.length > 0) {
            var screens = settingsController.screens;
            for (var i = 0; i < screens.length; i++) {
                if (screens[i].isPrimary) {
                    _selectedScreen = screens[i].name || "";
                    return ;
                }
            }
            _selectedScreen = screens[0].name || "";
        }
    }
    on_SelectedScreenChanged: {
        _updateScreenGeometry();
        _refreshConfig();
    }

    Connections {
        function onScreensChanged() {
            root._updateScreenGeometry();
            root._refreshConfig();
        }

        target: settingsController
    }

    // Refresh when global discard resets needsSave to false
    Connections {
        function onNeedsSaveChanged() {
            if (!settingsController.needsSave)
                root._refreshConfig();

        }

        target: settingsController
    }

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            text: i18n("Split a physical monitor into multiple virtual screens. Each virtual screen gets its own zone layout and acts as an independent display.")
            visible: true
        }

        // ═══════════════════════════════════════════════════════════════
        // MONITOR SELECTOR (visual icon bar)
        // ═══════════════════════════════════════════════════════════════
        MonitorSelectorSection {
            Layout.fillWidth: true
            appSettings: settingsController
            showAllMonitors: false
            physicalOnly: true
            selectedScreenName: root._selectedScreen
            onSelectedScreenNameChanged: {
                root._selectedScreen = selectedScreenName;
            }
        }

        // ═══════════════════════════════════════════════════════════════
        // VISUAL PREVIEW with draggable dividers
        // ═══════════════════════════════════════════════════════════════
        SettingsCard {
            headerText: i18n("Preview")

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                // Split direction indicator
                Label {
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    text: i18n("Horizontal Split")
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                }

                Rectangle {
                    id: previewRect

                    Layout.fillWidth: true
                    Layout.maximumWidth: Kirigami.Units.gridUnit * 30
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredHeight: {
                        var ratio = root._screenHeight / root._screenWidth;
                        return Math.min(width * ratio, Kirigami.Units.gridUnit * 10);
                    }
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    Layout.bottomMargin: Kirigami.Units.largeSpacing
                    color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.5)
                    border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.3)
                    border.width: 1
                    radius: Kirigami.Units.smallSpacing

                    // "No subdivisions" label when empty
                    Label {
                        anchors.centerIn: parent
                        visible: root._pendingScreens.length === 0
                        text: i18n("No subdivisions (full screen)")
                        color: Kirigami.Theme.disabledTextColor
                        font.italic: true
                    }

                    // Virtual screen region rectangles
                    Repeater {
                        model: root._pendingScreens

                        Rectangle {
                            required property var modelData
                            required property int index

                            x: modelData.x * previewRect.width + 1
                            y: modelData.y * previewRect.height + 1
                            width: modelData.width * previewRect.width - 2
                            height: modelData.height * previewRect.height - 2
                            color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.15)
                            border.color: Kirigami.Theme.highlightColor
                            border.width: 2
                            radius: Kirigami.Units.smallSpacing / 2

                            ColumnLayout {
                                anchors.centerIn: parent
                                spacing: 2

                                Label {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: modelData.displayName || i18n("Screen %1", index + 1)
                                    font.weight: Font.DemiBold
                                    font.pixelSize: Math.max(10, Math.min(14, parent.parent.width / 8))
                                    color: Kirigami.Theme.textColor
                                    elide: Text.ElideRight
                                    maximumLineCount: 1
                                }

                                Label {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: Math.round(modelData.width * 100) + "%"
                                    font.pixelSize: Math.max(9, Math.min(12, parent.parent.width / 10))
                                    color: Kirigami.Theme.disabledTextColor
                                }

                            }

                        }

                    }

                    // Draggable divider handles between regions
                    Repeater {
                        model: root._pendingScreens.length > 1 ? root._pendingScreens.length - 1 : 0

                        Item {
                            id: dividerHandle

                            required property int index
                            readonly property real dividerX: {
                                if (index < root._pendingScreens.length - 1)
                                    return (root._pendingScreens[index].x + root._pendingScreens[index].width) * previewRect.width;

                                return 0;
                            }

                            x: dividerX - 3
                            y: 0
                            width: 7
                            height: previewRect.height

                            // Visual divider line
                            Rectangle {
                                anchors.centerIn: parent
                                width: dividerDragArea.containsMouse || dividerDragArea.pressed ? 3 : 1
                                height: parent.height - 4
                                radius: 1
                                color: dividerDragArea.containsMouse || dividerDragArea.pressed ? Kirigami.Theme.highlightColor : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.5)

                                Behavior on width {
                                    NumberAnimation {
                                        duration: 100
                                    }

                                }

                                Behavior on color {
                                    ColorAnimation {
                                        duration: 100
                                    }

                                }

                            }

                            // Drag grip indicator
                            Rectangle {
                                anchors.centerIn: parent
                                width: 12
                                height: 24
                                radius: 4
                                color: dividerDragArea.containsMouse || dividerDragArea.pressed ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.3) : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.1)
                                border.width: 1
                                border.color: dividerDragArea.containsMouse || dividerDragArea.pressed ? Kirigami.Theme.highlightColor : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2)
                                visible: previewRect.height > 40

                                // Grip dots
                                Column {
                                    anchors.centerIn: parent
                                    spacing: 3

                                    Repeater {
                                        model: 3

                                        Rectangle {
                                            width: 2
                                            height: 2
                                            radius: 1
                                            color: Kirigami.Theme.textColor
                                            opacity: 0.5
                                        }

                                    }

                                }

                            }

                            MouseArea {
                                id: dividerDragArea

                                property real dragStartX: 0
                                property real dragStartFraction: 0

                                anchors.fill: parent
                                anchors.margins: -4
                                cursorShape: Qt.SplitHCursor
                                hoverEnabled: true
                                onPressed: function(mouse) {
                                    dragStartX = mouse.x + dividerHandle.x;
                                    dragStartFraction = dividerHandle.dividerX / previewRect.width;
                                }
                                onPositionChanged: function(mouse) {
                                    if (!pressed)
                                        return ;

                                    var globalX = mouse.x + dividerHandle.x;
                                    var deltaFraction = (globalX - dragStartX) / previewRect.width;
                                    var newFraction = dragStartFraction + deltaFraction;
                                    root._moveDivider(dividerHandle.index, newFraction);
                                }
                            }

                        }

                    }

                }

            }

        }

        // ═══════════════════════════════════════════════════════════════
        // PRESETS (populate local state, do NOT apply)
        // ═══════════════════════════════════════════════════════════════
        SettingsCard {
            headerText: i18n("Presets")

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                GridLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    Layout.bottomMargin: Kirigami.Units.largeSpacing
                    columns: 2
                    columnSpacing: Kirigami.Units.smallSpacing
                    rowSpacing: Kirigami.Units.smallSpacing

                    Button {
                        Layout.fillWidth: true
                        text: i18n("50 / 50")
                        enabled: root._selectedScreen !== ""
                        onClicked: root._loadPreset([50, 50], [i18n("Left"), i18n("Right")])
                    }

                    Button {
                        Layout.fillWidth: true
                        text: i18n("60 / 40")
                        enabled: root._selectedScreen !== ""
                        onClicked: root._loadPreset([60, 40], [i18n("Main"), i18n("Side")])
                    }

                    Button {
                        Layout.fillWidth: true
                        text: i18n("33 / 33 / 33")
                        enabled: root._selectedScreen !== ""
                        onClicked: root._loadPreset([33.34, 33.33, 33.33], [i18n("Left"), i18n("Center"), i18n("Right")])
                    }

                    Button {
                        Layout.fillWidth: true
                        text: i18n("40 / 20 / 40")
                        enabled: root._selectedScreen !== ""
                        onClicked: root._loadPreset([40, 20, 40], [i18n("Left"), i18n("Center"), i18n("Right")])
                    }

                }

            }

        }

        // ═══════════════════════════════════════════════════════════════
        // CUSTOM SPLIT EDITOR
        // ═══════════════════════════════════════════════════════════════
        SettingsCard {
            headerText: i18n("Custom Split")

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                // Number of columns
                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Number of splits:")
                    }

                    SpinBox {
                        id: splitCountSpinBox

                        from: 1
                        to: 5
                        value: root._pendingScreens.length > 0 ? root._pendingScreens.length : 1
                        editable: true
                        enabled: root._selectedScreen !== ""
                        onValueModified: {
                            root._redistributeEqual(value);
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    Button {
                        text: i18n("Equalize")
                        icon.name: "distribute-horizontal-equal"
                        flat: true
                        enabled: root._selectedScreen !== "" && root._pendingScreens.length > 1
                        onClicked: root._redistributeEqual(root._pendingScreens.length)
                        ToolTip.text: i18n("Reset all splits to equal widths")
                        ToolTip.visible: hovered
                    }

                }

                // Per-split name and width editing
                Repeater {
                    model: root._pendingScreens

                    RowLayout {
                        required property var modelData
                        required property int index

                        Layout.fillWidth: true
                        Layout.leftMargin: Kirigami.Units.largeSpacing
                        Layout.rightMargin: Kirigami.Units.largeSpacing
                        spacing: Kirigami.Units.smallSpacing

                        Label {
                            text: (index + 1) + "."
                            Layout.preferredWidth: Kirigami.Units.gridUnit * 1.5
                            horizontalAlignment: Text.AlignRight
                            color: Kirigami.Theme.disabledTextColor
                        }

                        TextField {
                            Layout.fillWidth: true
                            text: modelData.displayName || ""
                            placeholderText: i18n("Screen %1", index + 1)
                            onEditingFinished: {
                                var screens = root._deepCopy(root._pendingScreens);
                                if (index >= 0 && index < screens.length) {
                                    screens[index].displayName = text;
                                    root._pendingScreens = screens;
                                    root._stageCurrentConfig();
                                }
                            }
                        }

                        Label {
                            text: Math.round(modelData.width * 100) + "%"
                            Layout.preferredWidth: Kirigami.Units.gridUnit * 3
                            horizontalAlignment: Text.AlignRight
                            color: Kirigami.Theme.disabledTextColor
                            font.family: "monospace"
                        }

                        Label {
                            text: Math.round(modelData.width * root._screenWidth) + "px"
                            Layout.preferredWidth: Kirigami.Units.gridUnit * 4
                            horizontalAlignment: Text.AlignRight
                            color: Kirigami.Theme.disabledTextColor
                            font: Kirigami.Theme.smallFont
                        }

                    }

                }

                // Bottom margin
                Item {
                    Layout.preferredHeight: Kirigami.Units.smallSpacing
                }

            }

        }

        // ═══════════════════════════════════════════════════════════════
        // REMOVE SUBDIVISIONS
        // ═══════════════════════════════════════════════════════════════
        Button {
            text: i18n("Remove Subdivisions")
            icon.name: "edit-delete"
            enabled: root._selectedScreen !== "" && (root._pendingScreens.length > 0 || root._savedScreens.length > 0)
            onClicked: {
                settingsController.stageVirtualScreenRemoval(root._selectedScreen);
                root._pendingScreens = [];
            }
        }

    }

}

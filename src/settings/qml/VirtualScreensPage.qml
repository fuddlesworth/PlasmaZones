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
    // UX cap — C++ maximum is ConfigDefaults::maxVirtualScreensPerPhysical() (10).
    // Capped lower here for usability: more than 5 splits is impractical on most ultrawides.
    readonly property int _maxVirtualScreens: 5
    property string _selectedScreen: ""
    // Array of {x: real, y: real, width: real, height: real, displayName: string} — staged virtual screen definitions
    property var _pendingScreens: []
    // Array of {x: real, y: real, width: real, height: real, displayName: string} — last-saved virtual screen definitions
    property var _savedScreens: []
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
        // First pass: exact name match (physical screen entry)
        for (var i = 0; i < screens.length; i++) {
            if (screens[i].name === _selectedScreen) {
                var w = screens[i].width;
                var h = screens[i].height;
                if (w && h) {
                    _screenWidth = w;
                    _screenHeight = h;
                    return ;
                }
                break;
            }
        }
        // Second pass: if width/height weren't found (e.g. physicalOnly dedup
        // deleted them), reconstruct from virtual screen children.
        // Children are horizontal sub-regions so summing their widths gives
        // the physical width; the physical height equals any child's height
        // (all children span the full vertical extent).
        var prefix = _selectedScreen + "/vs:";
        var totalW = 0;
        var maxH = 0;
        for (var j = 0; j < screens.length; j++) {
            var name = screens[j].name || "";
            if (name.indexOf(prefix) === 0 && screens[j].width && screens[j].height) {
                totalW += screens[j].width;
                if (screens[j].height > maxH)
                    maxH = screens[j].height;

            }
        }
        if (totalW > 0 && maxH > 0) {
            _screenWidth = totalW;
            _screenHeight = maxH;
            return ;
        }
        _screenWidth = 1920;
        _screenHeight = 1080;
    }

    // Redistribute splits equally for a given count
    function _redistributeEqual(count) {
        if (count <= 1) {
            settingsController.stageVirtualScreenRemoval(_selectedScreen);
            _pendingScreens = [];
            return ;
        }
        if (count > _maxVirtualScreens)
            count = _maxVirtualScreens;

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

    // Clamp divider move: ensure all regions have at least 5% width
    function _moveDivider(dividerIndex, newFraction) {
        if (dividerIndex < 0 || dividerIndex >= _pendingScreens.length - 1)
            return ;

        var screens = _deepCopy(_pendingScreens);
        var leftIdx = dividerIndex;
        var rightIdx = dividerIndex + 1;
        var leftStart = screens[leftIdx].x;
        var rightEnd = screens[rightIdx].x + screens[rightIdx].width;
        var minW = 0.05; // 5% minimum
        // Clamp the new divider position
        var minX = leftStart + minW;
        var maxX = rightEnd - minW;
        var newDivPos = Math.max(minX, Math.min(maxX, newFraction));
        // Update left screen width
        screens[leftIdx].width = newDivPos - leftStart;
        // Update right screen x and width
        screens[rightIdx].x = newDivPos;
        screens[rightIdx].width = rightEnd - newDivPos;
        // Cascade x positions for all screens after rightIdx
        for (var k = rightIdx + 1; k < screens.length; k++) {
            screens[k].x = screens[k - 1].x + screens[k - 1].width;
        }
        // Normalize: ensure last screen extends to 1.0
        var total = 0;
        for (var m = 0; m < screens.length - 1; m++) {
            total += screens[m].width;
        }
        var lastIdx = screens.length - 1;
        screens[lastIdx].x = total;
        screens[lastIdx].width = Math.max(minW, 1 - total);
        // Validate ALL screens against minimum width — reject the move if any violates
        for (var j = 0; j < screens.length; j++) {
            if (screens[j].width < minW)
                return ;

        }
        _pendingScreens = screens;
        _stageCurrentConfig();
    }

    // Check if current config matches a preset (by ratio count and approximate widths)
    function _matchesPreset(ratios) {
        if (_pendingScreens.length !== ratios.length)
            return false;

        for (var i = 0; i < ratios.length; i++) {
            if (Math.abs(_pendingScreens[i].width - ratios[i] / 100) > 0.01)
                return false;

        }
        return true;
    }

    // Strip "/vs:N" suffix to get physical screen ID (MonitorSelectorSection
    // uses physicalOnly:true which deduplicates to physical IDs)
    function _toPhysicalId(name) {
        var vsIdx = name.indexOf("/vs:");
        return vsIdx >= 0 ? name.substring(0, vsIdx) : name;
    }

    function _autoSelectScreen() {
        var screens = settingsController.screens;
        for (var i = 0; i < screens.length; i++) {
            if (screens[i].isPrimary) {
                _selectedScreen = _toPhysicalId(screens[i].name || "");
                return ;
            }
        }
        if (screens.length > 0)
            _selectedScreen = _toPhysicalId(screens[0].name || "");

    }

    contentHeight: content.implicitHeight
    clip: true
    Component.onCompleted: {
        if (!_selectedScreen && settingsController.screens.length > 0)
            _autoSelectScreen();

    }
    on_SelectedScreenChanged: {
        _updateScreenGeometry();
        _refreshConfig();
    }

    Connections {
        function onScreensChanged() {
            root._updateScreenGeometry();
            root._refreshConfig();
            if (root._selectedScreen === "" && settingsController.screens.length > 0)
                root._autoSelectScreen();

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

        // Monitor selector (visual icon bar)
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

        // Visual preview with draggable dividers
        SettingsCard {
            headerText: i18n("Preview")

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                // Resolution and split count
                Label {
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    text: {
                        let res = root._screenWidth + " \u00d7 " + root._screenHeight;
                        let count = root._pendingScreens.length;
                        if (count > 1)
                            return res + " · " + i18n("%1-Way Split", count);
                        else if (count === 1)
                            return res + " · " + i18n("Single Region");
                        return res;
                    }
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                }

                VirtualScreenPreview {
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
                    pendingScreens: root._pendingScreens
                    screenWidth: root._screenWidth
                    screenHeight: root._screenHeight
                    onDividerMoved: function(dividerIndex, newFraction) {
                        root._moveDivider(dividerIndex, newFraction);
                    }
                }

            }

        }

        // Presets (populate local state, do NOT apply)
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
                        highlighted: root._matchesPreset([50, 50])
                        onClicked: root._loadPreset([50, 50], [i18n("Left"), i18n("Right")])
                        Accessible.name: i18n("Preset: %1", text)
                    }

                    Button {
                        Layout.fillWidth: true
                        text: i18n("60 / 40")
                        enabled: root._selectedScreen !== ""
                        highlighted: root._matchesPreset([60, 40])
                        onClicked: root._loadPreset([60, 40], [i18n("Main"), i18n("Side")])
                        Accessible.name: i18n("Preset: %1", text)
                    }

                    Button {
                        Layout.fillWidth: true
                        text: i18n("33 / 33 / 33")
                        enabled: root._selectedScreen !== ""
                        highlighted: root._matchesPreset([33.3, 33.4, 33.3])
                        onClicked: root._loadPreset([33.3, 33.4, 33.3], [i18n("Left"), i18n("Center"), i18n("Right")])
                        Accessible.name: i18n("Preset: %1", text)
                    }

                    Button {
                        Layout.fillWidth: true
                        text: i18n("40 / 20 / 40")
                        enabled: root._selectedScreen !== ""
                        highlighted: root._matchesPreset([40, 20, 40])
                        onClicked: root._loadPreset([40, 20, 40], [i18n("Left"), i18n("Center"), i18n("Right")])
                        Accessible.name: i18n("Preset: %1", text)
                    }

                }

            }

        }

        // Custom split editor
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
                        to: root._maxVirtualScreens
                        // No value: binding — set imperatively to avoid binding breakage
                        editable: true
                        enabled: root._selectedScreen !== ""
                        Component.onCompleted: value = root._pendingScreens.length > 1 ? root._pendingScreens.length : 1
                        onValueModified: {
                            if (value <= 1) {
                                // Treat 1 as "no split" — clear pending and stage removal
                                settingsController.stageVirtualScreenRemoval(root._selectedScreen);
                                root._pendingScreens = [];
                            } else {
                                root._redistributeEqual(value);
                            }
                        }
                        Accessible.name: i18n("Number of virtual screens")
                    }

                    Connections {
                        function onPendingScreensChanged() {
                            splitCountSpinBox.value = root._pendingScreens.length > 1 ? root._pendingScreens.length : 1;
                        }

                        target: root
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
                        Accessible.name: i18n("Equalize virtual screen sizes")
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
                            Accessible.name: i18n("Display name for virtual screen %1", index + 1)
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
                            font: Kirigami.Theme.smallFont
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

        // Actions
        SettingsCard {
            headerText: i18n("Actions")

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Button {
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    Layout.bottomMargin: Kirigami.Units.largeSpacing
                    text: i18n("Remove Subdivisions")
                    icon.name: "edit-delete"
                    palette.buttonText: Kirigami.Theme.negativeTextColor
                    enabled: root._selectedScreen !== "" && (root._pendingScreens.length > 0 || root._savedScreens.length > 0)
                    onClicked: {
                        settingsController.stageVirtualScreenRemoval(root._selectedScreen);
                        root._pendingScreens = [];
                        root._savedScreens = [];
                    }
                    ToolTip.text: i18n("Remove all virtual screen subdivisions from this monitor")
                    ToolTip.visible: hovered
                    Accessible.name: i18n("Remove virtual screen subdivisions")
                }

            }

        }

    }

}

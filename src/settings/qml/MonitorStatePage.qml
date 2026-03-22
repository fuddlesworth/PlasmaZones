// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.common as QFZCommon

/**
 * @brief Monitor State dashboard — current mode and layout per monitor.
 *
 * Uses the visual monitor selector bar to pick a monitor, then shows
 * a layout preview with mode toggle and layout/algorithm selector.
 */
Flickable {
    id: root

    property var _layouts: settingsController.layouts
    // Bridge for LayoutComboBox — exposes only what it accesses.
    // The `layouts` property binding auto-generates a `layoutsChanged` signal,
    // which LayoutComboBox's Connections target listens for.
    readonly property QtObject
    _layoutBridge: QtObject {
        readonly property var layouts: settingsController.layouts
        readonly property string defaultLayoutId: appSettings.defaultLayoutId
    }

    property var _screenStates: []
    property string _selectedScreen: ""
    property int _revision: 0

    function _refresh() {
        _screenStates = settingsController.getScreenStates();
        _revision++;
    }

    function _currentState() {
        var target = _selectedScreen;
        // If no screen selected, use first screen
        if (!target && _screenStates.length > 0)
            target = _screenStates[0].screenName || "";

        for (var i = 0; i < _screenStates.length; i++) {
            if (_screenStates[i].screenName === target)
                return _screenStates[i];

        }
        return _screenStates.length > 0 ? _screenStates[0] : null;
    }

    function _findLayout(layoutId) {
        if (!layoutId || !_layouts)
            return null;

        for (var i = 0; i < _layouts.length; i++) {
            if (_layouts[i].id === layoutId)
                return _layouts[i];

        }
        return null;
    }

    function _getZones(layoutId) {
        var layout = _findLayout(layoutId);
        return layout ? (layout.zones || []) : [];
    }

    // Stage the current local state for the selected screen (flushed on Apply)
    function _stageCurrentState() {
        if (!_selectedScreen)
            return ;

        var snapping = stateView.localMode === 0 ? stateView.localLayoutId : "";
        var tiling = stateView.localMode === 1 ? stateView.localAlgorithmId : "";
        // Only stage when there's a meaningful value for the active mode
        if (stateView.localMode === 0 && !snapping)
            return ;

        if (stateView.localMode === 1 && !tiling)
            return ;

        settingsController.stageAssignmentEntry(_selectedScreen, stateView.localMode, snapping, tiling ? ("autotile:" + tiling) : "");
    }

    contentHeight: content.implicitHeight
    clip: true
    Component.onCompleted: {
        _refresh();
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

    Connections {
        function onScreensChanged() {
            root._refresh();
        }

        function onLayoutsChanged() {
            root._layouts = settingsController.layouts;
            root._refresh();
        }

        function onVirtualDesktopsChanged() {
            root._refresh();
        }

        function onActivitiesChanged() {
            root._refresh();
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
            text: i18n("View and change the active mode and layout for each monitor.")
            visible: true
        }

        // Monitor selector bar (no "All Monitors" — always show a specific monitor)
        MonitorSelectorSection {
            Layout.fillWidth: true
            appSettings: settingsController
            showAllMonitors: false
            selectedScreenName: root._selectedScreen
            onSelectedScreenNameChanged: {
                root._selectedScreen = selectedScreenName;
            }
        }

        // Daemon offline / no screens message
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Warning
            text: i18n("Unable to retrieve monitor state. Make sure the daemon is running.")
            visible: stateView.screenState === null
        }

        // Current state for selected monitor
        ColumnLayout {
            id: stateView

            property var screenState: {
                void root._revision;
                void root._selectedScreen;
                return root._currentState();
            }
            // Local state — initialized from D-Bus, changed by user
            property int localMode: 0
            property string localLayoutId: ""
            property string localAlgorithmId: ""
            property bool isTiling: localMode === 1
            // Resolved layout object for LayoutThumbnail
            property var currentLayout: root._findLayout(localLayoutId)

            Layout.alignment: Qt.AlignHCenter
            spacing: Kirigami.Units.largeSpacing
            visible: screenState !== null
            onScreenStateChanged: {
                if (!screenState)
                    return ;

                var staged = settingsController.getStagedAssignment(root._selectedScreen);
                if (Object.keys(staged).length > 0) {
                    // Restore from staged state
                    localMode = staged.mode !== undefined ? staged.mode : (screenState.mode || 0);
                    localLayoutId = staged.layoutId !== undefined ? staged.layoutId : (screenState.layoutId || "");
                    localAlgorithmId = staged.algorithmId !== undefined ? staged.algorithmId : (screenState.algorithmId || "");
                } else {
                    // No staged changes — use daemon state
                    localMode = screenState.mode || 0;
                    localLayoutId = screenState.layoutId || "";
                    localAlgorithmId = screenState.algorithmId || "";
                }
            }

            // Layout preview (snapping)
            LayoutThumbnail {
                Layout.alignment: Qt.AlignHCenter
                visible: !stateView.isTiling
                layout: stateView.currentLayout || ({
                    "name": i18n("Default"),
                    "zones": root._getZones(stateView.localLayoutId)
                })
                isSelected: true
                baseHeight: Kirigami.Units.gridUnit * 14
                maxThumbnailWidth: Kirigami.Units.gridUnit * 32
                Accessible.name: {
                    var l = stateView.currentLayout;
                    return l ? i18n("Snapping layout preview: %1", l.name) : i18n("Snapping layout preview");
                }
            }

            // Algorithm preview (tiling)
            LayoutThumbnail {
                Layout.alignment: Qt.AlignHCenter
                visible: stateView.isTiling
                layout: {
                    var algoId = "autotile:" + stateView.localAlgorithmId;
                    var found = root._findLayout(algoId);
                    if (found)
                        return found;

                    return {
                        "name": stateView.localAlgorithmId || i18n("Default"),
                        "zones": []
                    };
                }
                isSelected: true
                baseHeight: Kirigami.Units.gridUnit * 14
                maxThumbnailWidth: Kirigami.Units.gridUnit * 32
                Accessible.name: {
                    var algoId = "autotile:" + stateView.localAlgorithmId;
                    var found = root._findLayout(algoId);
                    return found ? i18n("Tiling algorithm preview: %1", found.name) : i18n("Tiling algorithm preview");
                }
            }

            // Mode toggle (below preview)
            SettingsButtonGroup {
                Layout.alignment: Qt.AlignHCenter
                model: [i18n("Snapping"), i18n("Tiling")]
                currentIndex: stateView.localMode
                onIndexChanged: function(idx) {
                    stateView.localMode = idx;
                    root._stageCurrentState();
                }
            }

            // Layout selector (snapping)
            LayoutComboBox {
                Layout.alignment: Qt.AlignHCenter
                visible: !stateView.isTiling
                appSettings: root._layoutBridge
                currentLayoutId: stateView.localLayoutId
                layoutFilter: 0
                noneText: i18n("Default")
                showPreview: true
                onActivated: function(idx) {
                    var entry = model[idx];
                    stateView.localLayoutId = entry ? (entry.value || "") : "";
                    root._stageCurrentState();
                }
            }

            // Algorithm selector (tiling)
            LayoutComboBox {
                Layout.alignment: Qt.AlignHCenter
                visible: stateView.isTiling
                appSettings: root._layoutBridge
                currentLayoutId: "autotile:" + stateView.localAlgorithmId
                layoutFilter: 1
                noneText: i18n("Default")
                showPreview: true
                onActivated: function(idx) {
                    var entry = model[idx];
                    var id = entry ? (entry.value || "") : "";
                    if (id.startsWith("autotile:"))
                        stateView.localAlgorithmId = id.substring(9);
                    else
                        stateView.localAlgorithmId = id;
                    root._stageCurrentState();
                }
            }

        }

    }

}

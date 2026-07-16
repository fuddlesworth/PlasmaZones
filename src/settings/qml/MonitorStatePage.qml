// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Monitor State dashboard — current mode and layout per monitor.
 *
 * Uses the spatial DisplayMap to pick a monitor, then shows a layout
 * preview with mode toggle and layout/algorithm selector.
 */
SettingsFlickable {
    id: root

    property var _layouts: settingsController.layouts
    // Bridge for LayoutComboBox — exposes only what it accesses.
    // The `layouts` property binding auto-generates a `layoutsChanged` signal,
    // which LayoutComboBox's Connections target listens for.
    readonly property QtObject _layoutBridge: QtObject {
        readonly property var layouts: settingsController.layouts
        readonly property string defaultLayoutId: appSettings.defaultLayoutId
        readonly property string defaultAutotileAlgorithm: appSettings.defaultAutotileAlgorithm
        // LayoutComboBox's preview CategoryBadge reads `autoAssignAllLayouts` for
        // the global-auto-assign indicator; expose it so the Monitor State
        // dropdowns light it like the rest of the app.
        readonly property bool autoAssignAllLayouts: appSettings.autoAssignAllLayouts
    }

    property var _screenStates: []
    property string _selectedScreen: ""
    property int _revision: 0
    // Aspect ratio of the currently selected screen (for layout preview).
    // Falls back to the first screen if none is selected.
    readonly property real _selectedScreenAspectRatio: {
        var screens = settingsController.screens;
        var target = _selectedScreen;
        if (!target && screens.length > 0)
            target = screens[0].name || "";

        for (var i = 0; i < screens.length; i++) {
            if (screens[i].name === target) {
                var w = screens[i].width || 0;
                var h = screens[i].height || 0;
                if (w > 0 && h > 0)
                    return w / h;

                break;
            }
        }
        return 0;
    }

    function _refresh() {
        _screenStates = settingsController.getScreenStates();
        _revision++;
    }

    function _currentState() {
        var target = _selectedScreen;
        // If no screen selected, use first screen
        if (!target && _screenStates.length > 0)
            target = _screenStates[0].screenId || "";

        for (var i = 0; i < _screenStates.length; i++) {
            if (_screenStates[i].screenId === target)
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

    // Stage the current local state for the selected screen (flushed on Apply).
    // Uses setAssignmentEntry targeting the exact (screen, desktop, activity)
    // context from getScreenStates — most specific context wins.
    function _stageCurrentState() {
        if (!_selectedScreen)
            return;

        var state = stateView.screenState;
        if (!state)
            return;

        var desktop = state.virtualDesktop || 0;
        var activity = state.activity || "";
        var snapping = "";
        var tiling = "";
        if (stateView.localMode === 1) {
            var algoId = stateView.localAlgorithmId || state.algorithmId;
            if (!algoId)
                return;

            tiling = "autotile:" + algoId;
        } else {
            var layoutId = stateView.localLayoutId || state.layoutId;
            if (!layoutId)
                return;

            snapping = layoutId;
        }
        settingsController.stageAssignmentEntry(_selectedScreen, desktop, activity, stateView.localMode, snapping, tiling);
    }

    contentHeight: content.implicitHeight
    clip: true
    Component.onCompleted: {
        _refresh();
        if (!_selectedScreen && settingsController.screens.length > 0)
            _autoSelectScreen();
    }

    // Auto-select primary monitor, fallback to first.
    function _autoSelectScreen() {
        var screens = settingsController.screens || [];
        for (var i = 0; i < screens.length; i++) {
            if (screens[i].isPrimary) {
                _selectedScreen = screens[i].name || "";
                return;
            }
        }
        if (screens.length > 0)
            _selectedScreen = screens[0].name || "";
    }

    // True if `id` is still a connected output (physical-id aware, so a
    // physically-present screen with virtual children still matches).
    function _screenStillPresent(id) {
        var arr = settingsController.screens || [];
        for (var i = 0; i < arr.length; i++) {
            var nm = arr[i].name || "";
            if (nm === id || settingsController.physicalScreenId(nm) === id)
                return true;
        }
        return false;
    }

    Connections {
        target: settingsController

        function onScreensChanged() {
            // Drop a selection whose output was unplugged, then re-pick.
            if (root._selectedScreen !== "" && !root._screenStillPresent(root._selectedScreen))
                root._selectedScreen = "";
            if (root._selectedScreen === "" && settingsController.screens.length > 0)
                root._autoSelectScreen();
            root._refresh();
        }

        function onLayoutsChanged() {
            // _layouts is bound to settingsController.layouts and refreshes on
            // its own layoutsChanged; only the dependent view needs a nudge.
            root._refresh();
        }

        function onVirtualDesktopsChanged() {
            root._refresh();
        }

        function onActivitiesChanged() {
            root._refresh();
        }

        function onScreenLayoutChanged() {
            root._refresh();
        }
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

        // Monitor picker (spatial map; always a specific monitor, no "All")
        DisplayMap {
            Layout.fillWidth: true
            appSettings: settingsController
            showAll: false
            physicalOnly: false
            selectedScreenName: root._selectedScreen
            onScreenPicked: name => root._selectedScreen = name
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
                    return;

                var desktop = screenState.virtualDesktop || 0;
                var activity = screenState.activity || "";
                var staged = settingsController.getStagedAssignment(root._selectedScreen, desktop, activity);
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
                // Fallback stands in for a layout the local list doesn't carry,
                // so there are no zones to draw. The daemon still reports the
                // resolved name, so show that rather than nothing.
                layout: stateView.currentLayout || ({
                        "displayName": (stateView.screenState && stateView.screenState.layoutName) || i18n("Default"),
                        "zones": []
                    })
                isSelected: true
                baseHeight: Kirigami.Units.gridUnit * 14
                maxThumbnailWidth: Kirigami.Units.gridUnit * 32
                screenAspectRatio: root._selectedScreenAspectRatio
                Accessible.name: {
                    var l = stateView.currentLayout;
                    return l ? i18n("Snapping layout preview: %1", l.displayName) : i18n("Snapping layout preview");
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

                    // getScreenStates reports the algorithm's display name, so
                    // prefer it over the raw id ("bsp") the local list missed.
                    return {
                        "displayName": (stateView.screenState && stateView.screenState.algorithmName) || stateView.localAlgorithmId || i18n("Default"),
                        "zones": []
                    };
                }
                isSelected: true
                baseHeight: Kirigami.Units.gridUnit * 14
                maxThumbnailWidth: Kirigami.Units.gridUnit * 32
                screenAspectRatio: root._selectedScreenAspectRatio
                Accessible.name: {
                    var algoId = "autotile:" + stateView.localAlgorithmId;
                    var found = root._findLayout(algoId);
                    return found ? i18n("Tiling algorithm preview: %1", found.displayName) : i18n("Tiling algorithm preview");
                }
            }

            // Mode toggle (below preview)
            SettingsButtonGroup {
                Layout.alignment: Qt.AlignHCenter
                model: [i18n("Snapping"), i18n("Tiling")]
                currentIndex: stateView.localMode
                onIndexChanged: function (idx) {
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
                onActivated: function (idx) {
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
                onActivated: function (idx) {
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

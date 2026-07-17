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
        // No selection yet — fall back to the first reported state.
        if (!target)
            return _screenStates.length > 0 ? _screenStates[0] : null;

        for (var i = 0; i < _screenStates.length; i++) {
            if (_screenStates[i].screenId === target)
                return _screenStates[i];
        }
        // The selected screen has no reported state. Return null so the
        // "Unable to retrieve monitor state" warning shows, rather than
        // displaying (and staging against) another monitor's context.
        return null;
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
            if (stateView.localAlgorithmCleared) {
                // The user explicitly picked "Default": stage an assignment
                // clear so an earlier explicit pick in this session and any
                // daemon-side explicit assignment are reverted on Apply,
                // instead of pinning the currently-resolved algorithm via
                // the fallback below.
                settingsController.stageAssignmentClear(_selectedScreen, desktop, activity);
                return;
            }
            // The || fallback serves the mode-toggle path, which stages the
            // currently-resolved algorithm when the user has not picked one.
            var algoId = stateView.localAlgorithmId || state.algorithmId;
            if (!algoId) {
                // Nothing resolved to pin. Unstage any stale staged entry
                // for this context (e.g. an opposite-mode pick from earlier
                // in the session) so Apply cannot commit it while the UI
                // shows Default. A true unstage, not a staged clear — a
                // staged clear is pushed on Apply and would wipe a
                // pre-existing daemon-side assignment the user never touched.
                if (Object.keys(settingsController.getStagedAssignment(_selectedScreen, desktop, activity)).length > 0)
                    settingsController.removeStagedAssignment(_selectedScreen, desktop, activity);
                return;
            }

            tiling = "autotile:" + algoId;
        } else {
            if (stateView.localLayoutCleared) {
                // The user explicitly picked "Default": stage an assignment
                // clear so an earlier explicit pick in this session and any
                // daemon-side explicit assignment are reverted on Apply,
                // instead of pinning the currently-resolved layout via the
                // fallback below.
                settingsController.stageAssignmentClear(_selectedScreen, desktop, activity);
                return;
            }
            // The || fallback serves the mode-toggle path, which stages the
            // currently-resolved layout when the user has not picked one.
            var layoutId = stateView.localLayoutId || state.layoutId;
            if (!layoutId) {
                // Nothing resolved to pin. Unstage any stale staged entry
                // for this context (e.g. an opposite-mode pick from earlier
                // in the session) so Apply cannot commit it while the UI
                // shows Default. A true unstage, not a staged clear — a
                // staged clear is pushed on Apply and would wipe a
                // pre-existing daemon-side assignment the user never touched.
                if (Object.keys(settingsController.getStagedAssignment(_selectedScreen, desktop, activity)).length > 0)
                    settingsController.removeStagedAssignment(_selectedScreen, desktop, activity);
                return;
            }

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
            // True after the user explicitly picks "Default" in the layout
            // selector. Tracked separately from localLayoutId because the
            // combo reports Default as an empty value, which is otherwise
            // indistinguishable from the not-yet-touched state.
            property bool localLayoutCleared: false
            // Same tracking for the algorithm selector's "Default" pick: the
            // combo reports it as an empty value, indistinguishable from the
            // not-yet-touched state without this flag.
            property bool localAlgorithmCleared: false
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
                // Selection context changed — drop any pending explicit
                // "Default" pick; the state below re-initializes from
                // staged or daemon values.
                localLayoutCleared = false;
                localAlgorithmCleared = false;
                var staged = settingsController.getStagedAssignment(root._selectedScreen, desktop, activity);
                if (staged.fullCleared) {
                    // A staged full clear means "Default" is pending for this
                    // context: show Default rather than re-reading the
                    // daemon's still-resolved explicit values.
                    localMode = screenState.mode || 0;
                    localLayoutId = "";
                    localAlgorithmId = "";
                    localLayoutCleared = true;
                    localAlgorithmCleared = true;
                } else if (Object.keys(staged).length > 0) {
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
                    // A mode toggle is an explicit re-pin: the user is asking
                    // for this mode on this screen, so any earlier "Default"
                    // pick no longer applies. Clear both flags so
                    // _stageCurrentState stages the currently-resolved value
                    // instead of silently dropping the mode change.
                    stateView.localLayoutCleared = false;
                    stateView.localAlgorithmCleared = false;
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
                    var id = entry ? (entry.value || "") : "";
                    stateView.localLayoutId = id;
                    stateView.localLayoutCleared = (id === "");
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
                    stateView.localAlgorithmCleared = (id === "");
                    root._stageCurrentState();
                }
            }
        }
    }
}

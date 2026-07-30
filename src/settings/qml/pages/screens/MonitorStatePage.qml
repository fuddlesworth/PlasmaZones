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

    // Bridge for LayoutComboBox — exposes only what it accesses.
    // The `layouts` property binding auto-generates a `layoutsChanged` signal,
    // which LayoutComboBox's Connections target listens for.
    readonly property QtObject _layoutBridge: QtObject {
        readonly property string defaultAutotileAlgorithm: appSettings.defaultAutotileAlgorithm
        readonly property var layouts: settingsController.layouts
        readonly property string defaultLayoutId: appSettings.defaultLayoutId
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
        if (!layoutId || !_layoutBridge.layouts)
            return null;

        for (var i = 0; i < _layoutBridge.layouts.length; i++) {
            if (_layoutBridge.layouts[i].id === layoutId)
                return _layoutBridge.layouts[i];
        }
        return null;
    }

    // Resolve what one slot (layout or algorithm) carries into a staged entry.
    // Lossless mode toggling: every staged entry carries the SIBLING mode's
    // pick too. The daemon rebuilds the whole context rule from the entry, so
    // staging an empty sibling field would drop a stored layout/algorithm for
    // good — pin to Scrolling and back and the zone layout would be gone. An
    // explicit "Default" pick (@p cleared) deliberately carries empty so the
    // sibling keeps following the global default.
    // Only a TOUCHED slot carries its local value (@p localId). An untouched
    // slot falls back to the daemon's assignment (@p resolvedId) and carries
    // it only when the daemon marked it explicit (@p explicitFlag), so a pure
    // mode switch never freezes a cascade default into an explicit
    // assignment. The local ids cannot stand in for that test: they are
    // pre-filled from the RESOLVED state, so they always look like a pick. A
    // daemon too old to report the marker leaves it undefined, and there the
    // existing assignment is preserved rather than silently dropped.
    function carrySibling(cleared, touched, localId, explicitFlag, resolvedId) {
        if (cleared)
            return "";

        if (touched)
            return localId;

        if (explicitFlag === undefined || explicitFlag)
            return resolvedId || "";

        return "";
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
        // See carrySibling for the rule both slots follow.
        var siblingSnapping = root.carrySibling(stateView.localLayoutCleared, stateView.localLayoutTouched, stateView.localLayoutId, state.layoutIdExplicit, state.layoutId);
        var siblingAlgo = root.carrySibling(stateView.localAlgorithmCleared, stateView.localAlgorithmTouched, stateView.localAlgorithmId, state.algorithmIdExplicit, state.algorithmId);
        var siblingTiling = siblingAlgo ? "autotile:" + siblingAlgo : "";
        if (stateView.isScrolling) {
            // Scrolling has neither a zone layout nor a tiling algorithm of
            // its own: the entry carries the mode plus both preserved
            // sibling fields.
            settingsController.stageAssignmentEntry(_selectedScreen, desktop, activity, stateView.localMode, siblingSnapping, siblingTiling);
            return;
        }
        if (stateView.localMode === 1) {
            // An explicit "Default" pick clears the algorithm slot. Otherwise
            // stage the user's pick, else the currently-resolved algorithm.
            var algoId = stateView.localAlgorithmCleared ? "" : (stateView.localAlgorithmId || state.algorithmId || "");
            if (!algoId) {
                // Nothing to pin — either the user picked "Default", or
                // nothing resolved (fresh config, or the context suppresses
                // the default). Stage a MODE-ONLY entry, exactly like the
                // Snapping and Scrolling branches. The wire is mode=1 with
                // an EMPTY algorithm id (the adaptor only validates
                // non-empty ids), so the switch commits, the slot is cleared
                // without touching the mode pin or the sibling, the
                // algorithm keeps FOLLOWING the global default (never frozen
                // to today's value), and the combo honestly keeps showing
                // "Default".
                settingsController.stageAssignmentEntry(_selectedScreen, desktop, activity, stateView.localMode, siblingSnapping, "");
                return;
            }

            tiling = "autotile:" + algoId;
            snapping = siblingSnapping;
        } else {
            // An explicit "Default" pick clears the layout slot. The ||
            // fallback serves the mode-toggle path, which stages the
            // currently-resolved layout when the user has not picked one.
            var layoutId = stateView.localLayoutCleared ? "" : (stateView.localLayoutId || state.layoutId || "");
            if (!layoutId) {
                // Nothing to pin — either the user picked "Default", or
                // nothing resolved (the context suppresses the default
                // layout, so state.layoutId is empty). The MODE change must
                // still commit — leaving from Scrolling used to fall into an
                // unstage here and silently drop the switch while the button
                // group showed Snapping. Stage a mode-only entry; it clears
                // the layout slot while keeping the mode pin and the
                // sibling, and the daemon accepts a bare mode exactly as it
                // does for Scrolling above.
                settingsController.stageAssignmentEntry(_selectedScreen, desktop, activity, stateView.localMode, "", siblingTiling);
                return;
            }

            snapping = layoutId;
            tiling = siblingTiling;
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
            // _layoutBridge.layouts is bound to settingsController.layouts and refreshes on
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
            // Kirigami.InlineMessage defaults to visible: false.
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
            // True once this session has actually assigned the slot — a
            // selector pick, a toggle into the slot's mode, or a restored
            // staged entry. localLayoutId / localAlgorithmId are pre-filled
            // from the RESOLVED state, so they can never answer "did the
            // user set this?" on their own; the sibling carry in
            // _stageCurrentState needs that answer to avoid freezing a
            // cascade default into an explicit assignment.
            property bool localLayoutTouched: false
            property bool localAlgorithmTouched: false
            property bool isTiling: localMode === 1
            // The scrolling engine is mode 2. It picks neither a layout nor an
            // algorithm, so the preview, the selectors and the staged entry all
            // branch on this instead of treating "not tiling" as snapping.
            property bool isScrolling: localMode === 2
            property bool isSnapping: localMode === 0
            // Resolved layout object for LayoutThumbnail
            property var currentLayout: root._findLayout(localLayoutId)
            // Live strip zones for the scrolling preview, refreshed with the
            // selection context. Empty when the screen is not scrolling right
            // now (mode staged but not applied, no windows, daemon down).
            property var scrollingStripZones: []
            // Representative endless strip: a full column mid-view with a
            // clipped column at each edge, so the sketch reads as a window
            // onto a longer strip rather than a fixed zone layout.
            readonly property var scrollingFallbackZones: [
                {
                    "zoneNumber": 1,
                    "id": "0",
                    "name": "",
                    "useCustomColors": false,
                    "relativeGeometry": {
                        "x": 0,
                        "y": 0,
                        "width": 0.1,
                        "height": 1
                    }
                },
                {
                    "zoneNumber": 2,
                    "id": "1",
                    "name": "",
                    "useCustomColors": false,
                    "relativeGeometry": {
                        "x": 0.115,
                        "y": 0,
                        "width": 0.5,
                        "height": 1
                    }
                },
                {
                    "zoneNumber": 3,
                    "id": "2",
                    "name": "",
                    "useCustomColors": false,
                    "relativeGeometry": {
                        "x": 0.63,
                        "y": 0,
                        "width": 0.37,
                        "height": 1
                    }
                }
            ]

            // Fetch the live strip for the current selection. The daemon's
            // strip is briefly empty while a mode flip's re-announce batch is
            // being adopted (the OSD defers around the same window); a one-shot
            // read landing there returned [] and left the fallback sketch up
            // for good. When the daemon says the screen IS scrolling but the
            // strip came back empty, re-read once after a settle beat.
            function refreshScrollingStrip() {
                if (!screenState)
                    return;
                scrollingStripZones = settingsController.getScrollingStripPreview(screenState.screenId || "");
                if (scrollingStripZones.length === 0 && (screenState.mode || 0) === 2)
                    stripSettleRetry.restart();
            }

            Timer {
                id: stripSettleRetry
                interval: 400
                repeat: false
                onTriggered: {
                    if (stateView.screenState)
                        stateView.scrollingStripZones = settingsController.getScrollingStripPreview(stateView.screenState.screenId || "");
                }
            }

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
                localLayoutTouched = false;
                localAlgorithmTouched = false;
                // Refresh the live strip preview with the selection context
                // (cheap D-Bus read; [] when not scrolling). Keyed on the
                // STATE's own screen id — before the user clicks a monitor,
                // _selectedScreen is still empty while the shown state is the
                // first reported one, and fetching with the empty id left the
                // sketch up despite a live strip.
                refreshScrollingStrip();
                var staged = settingsController.getStagedAssignment(root._selectedScreen, desktop, activity);
                if (Object.keys(staged).length > 0) {
                    // A staged entry carries the WHOLE context rule, so every
                    // slot in it is authoritative: a missing id is a pending
                    // slot clear, not an absent opinion. Re-reading the
                    // daemon's resolved value for such a slot would hide the
                    // clear and re-carry the value on the next stage.
                    localMode = staged.mode !== undefined ? staged.mode : (screenState.mode || 0);
                    localLayoutId = staged.layoutId !== undefined ? staged.layoutId : "";
                    localAlgorithmId = staged.algorithmId !== undefined ? staged.algorithmId : "";
                    localLayoutCleared = staged.layoutId === undefined;
                    localAlgorithmCleared = staged.algorithmId === undefined;
                    localLayoutTouched = staged.layoutId !== undefined;
                    localAlgorithmTouched = staged.algorithmId !== undefined;
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
                visible: stateView.isSnapping
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

            // Strip preview (scrolling): the live strip when the screen is
            // scrolling right now, else a representative endless-strip
            // sketch (clipped columns at both edges suggest continuation).
            LayoutThumbnail {
                Layout.alignment: Qt.AlignHCenter
                visible: stateView.isScrolling
                // Re-read the live strip when the preview surfaces (a mode
                // pick flips visibility without a screen-state change).
                onVisibleChanged: {
                    if (visible)
                        stateView.refreshScrollingStrip();
                }
                // category 1 renders the "Dynamic" badge (a live strip
                // snapshot is generated, not editable). Zone numbers are the
                // 1-based VISIBLE column slots the Snap-to-Zone digits
                // target, so they label exactly what is on screen.
                layout: ({
                        "displayName": i18nc("tiling mode name", "Scrolling"),
                        "category": 1,
                        "zones": stateView.scrollingStripZones.length > 0 ? stateView.scrollingStripZones : stateView.scrollingFallbackZones
                    })
                isSelected: true
                baseHeight: Kirigami.Units.gridUnit * 14
                maxThumbnailWidth: Kirigami.Units.gridUnit * 32
                screenAspectRatio: root._selectedScreenAspectRatio
                Accessible.name: i18n("Scrolling strip preview")
            }

            // Scrolling picks neither a layout nor an algorithm, so a short
            // explanation stands in for the selector.
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                Layout.maximumWidth: Kirigami.Units.gridUnit * 32
                type: Kirigami.MessageType.Information
                text: i18n("Scrolling mode arranges windows in resizable columns on an endless strip. It does not use a zone layout.")
                visible: stateView.isScrolling
            }

            // Mode toggle (below preview)
            SettingsButtonGroup {
                Layout.alignment: Qt.AlignHCenter
                Accessible.name: i18n("Placement mode")
                // Deliberately NOT gated on appSettings.autotileEnabled: an
                // assignment is durable state, and hiding the Tiling button
                // while a screen is already assigned Tiling would make that
                // state unrepresentable here. With the feature disabled the
                // router downgrades the screen to Snapping until it is
                // re-enabled; the assignment itself is preserved.
                model: [i18nc("tiling mode name", "Snapping"), i18nc("tiling mode name", "Tiling"), i18nc("tiling mode name", "Scrolling")]
                currentIndex: stateView.localMode
                onIndexChanged: function (idx) {
                    stateView.localMode = idx;
                    // A mode toggle is an explicit re-pin FOR THE ENTERED
                    // MODE only: its earlier "Default" pick no longer
                    // applies, so clear that flag and stage the resolved
                    // value. The SIBLING mode's cleared flag survives — a
                    // pending "Default" on the mode being left is a
                    // deliberate pick the toggle must not silently re-pin.
                    if (idx === 0) {
                        stateView.localLayoutCleared = false;
                        stateView.localLayoutTouched = true;
                    } else if (idx === 1) {
                        stateView.localAlgorithmCleared = false;
                        stateView.localAlgorithmTouched = true;
                    }
                    root._stageCurrentState();
                }
            }

            // Layout selector (snapping)
            LayoutComboBox {
                Layout.alignment: Qt.AlignHCenter
                visible: stateView.isSnapping
                Accessible.name: i18n("Snapping layout")
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
                    stateView.localLayoutTouched = true;
                    root._stageCurrentState();
                }
            }

            // Algorithm selector (tiling)
            LayoutComboBox {
                Layout.alignment: Qt.AlignHCenter
                visible: stateView.isTiling
                Accessible.name: i18n("Tiling algorithm")
                appSettings: root._layoutBridge
                currentLayoutId: stateView.localAlgorithmId ? "autotile:" + stateView.localAlgorithmId : ""
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
                    stateView.localAlgorithmTouched = true;
                    root._stageCurrentState();
                }
            }
        }
    }
}

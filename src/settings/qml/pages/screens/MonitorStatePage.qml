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
    // Prefix the layout list uses to namespace tiling algorithms apart from
    // zone layouts. Named so the id surgery below never restates its length.
    readonly property string _autotilePrefix: "autotile:"
    // Aspect ratio of the currently selected screen (for layout preview).
    // With no selection yet, fall back to the SHOWN state's own screen — the
    // same object _currentState returns and _stageCurrentState writes against.
    // Falling back to screens[0] instead let the preview take its shape from a
    // different monitor than the one whose state the page is displaying,
    // because the two lists are ordered independently.
    readonly property real _selectedScreenAspectRatio: {
        void _revision;
        void _screenStates;
        var screens = settingsController.screens;
        var target = _selectedScreen;
        if (!target) {
            var state = _currentState();
            target = state ? (state.screenId || "") : "";
        }

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
        // Same physical-id tolerance _screenStillPresent applies, so a
        // selection naming a physically-present output still resolves to the
        // state reported for one of its virtual children. Exact matches win,
        // hence the second pass.
        for (var j = 0; j < _screenStates.length; j++) {
            var sid = _screenStates[j].screenId || "";
            if (sid && settingsController.physicalScreenId(sid) === target)
                return _screenStates[j];
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
    function _carrySibling(cleared, touched, localId, explicitFlag, resolvedId) {
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
        var state = stateView.screenState;
        if (!state)
            return;

        // Key the write off the STATE, exactly as the staged-entry read does
        // (the read at onScreenStateChanged uses screenState.screenId).
        // _selectedScreen must NOT win here: when the state resolved through
        // the physical-id fallback pass, the selection holds a physical id
        // while the state holds the virtual child id, and an entry staged
        // under the selection would never be read back. Deriving both keys
        // from the same object makes read and write agree by construction.
        var target = state.screenId || "";
        if (!target)
            return;

        var desktop = state.virtualDesktop || 0;
        var activity = state.activity || "";
        var snapping = "";
        var tiling = "";
        // See _carrySibling for the rule both slots follow.
        var siblingSnapping = root._carrySibling(stateView.localLayoutCleared, stateView.localLayoutTouched, stateView.localLayoutId, state.layoutIdExplicit, state.layoutId);
        var siblingAlgo = root._carrySibling(stateView.localAlgorithmCleared, stateView.localAlgorithmTouched, stateView.localAlgorithmId, state.algorithmIdExplicit, state.algorithmId);
        var siblingTiling = siblingAlgo ? root._autotilePrefix + siblingAlgo : "";
        if (stateView.isScrolling) {
            // Scrolling has neither a zone layout nor a tiling algorithm of
            // its own: the entry carries the mode plus both preserved
            // sibling fields.
            settingsController.stageAssignmentEntry(target, desktop, activity, stateView.localMode, siblingSnapping, siblingTiling);
            return;
        }
        if (stateView.isTiling) {
            // An explicit "Default" pick clears the algorithm slot. Otherwise
            // stage the user's pick, else the currently-resolved algorithm.
            // _carrySibling's rule, for the same reason as the layout slot
            // below: an untouched algorithm carries the resolved value only
            // when the daemon marked it explicit, so a pure mode toggle cannot
            // freeze the global default algorithm onto this screen.
            var algoId = root._carrySibling(stateView.localAlgorithmCleared, stateView.localAlgorithmTouched, stateView.localAlgorithmId, state.algorithmIdExplicit, state.algorithmId);
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
                settingsController.stageAssignmentEntry(target, desktop, activity, stateView.localMode, siblingSnapping, "");
                return;
            }

            tiling = root._autotilePrefix + algoId;
            snapping = siblingSnapping;
        } else {
            // Same rule the SIBLING slot follows (see _carrySibling): an
            // untouched slot carries the daemon's resolved value only when the
            // daemon marked it EXPLICIT. Falling back to state.layoutId
            // unconditionally froze a cascade default into an explicit
            // assignment — toggle Snapping to Scrolling and back, hit Apply,
            // and the screen now pins today's global default and stops
            // following it. The entered mode is not exempt from that rule.
            var layoutId = root._carrySibling(stateView.localLayoutCleared, stateView.localLayoutTouched, stateView.localLayoutId, state.layoutIdExplicit, state.layoutId);
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
                settingsController.stageAssignmentEntry(target, desktop, activity, stateView.localMode, "", siblingTiling);
                return;
            }

            snapping = layoutId;
            tiling = siblingTiling;
        }
        settingsController.stageAssignmentEntry(target, desktop, activity, stateView.localMode, snapping, tiling);
    }

    contentHeight: content.implicitHeight
    clip: true
    // Pick the monitor BEFORE the first state read. _autoSelectScreen reads
    // only settingsController.screens, so it needs nothing from _refresh, and
    // selecting afterwards moved the shown state a second time — a second
    // blocking round of the staged-entry and strip-preview reads at startup.
    Component.onCompleted: {
        if (!_selectedScreen && settingsController.screens.length > 0)
            _autoSelectScreen();

        _refresh();
    }

    // The recovery half of the visibility gates below: any event a hidden
    // cached page skipped (a global Discard, a dirty-set flip) is caught up
    // in one re-read when the page comes back.
    onVisibleChanged: {
        if (visible)
            _refresh();
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

        // A global Discard or a factory reset drops the staged entries without
        // touching this page's local mode/layout state, so the discarded pick
        // stayed on screen and the touched flags carried it straight back into
        // the next staged write. Re-reading the states re-fires
        // onScreenStateChanged, which re-initializes both the local values and
        // the flags from whatever survived. Same handler VirtualScreensPage
        // uses for the same reason.
        function onDirtyPagesChanged() {
            // Visibility-gated like every other blocking read on this page:
            // a hidden cached page re-reads on its own visible transition.
            if (!root.visible)
                return;
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

            // The two `void` reads are the binding's dependency declaration:
            // _currentState() is a plain function, so QML records no
            // dependency on the properties it reads. Touching _revision and
            // _selectedScreen here is what makes the binding re-run when the
            // state list is refreshed or the user picks another monitor.
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
            // One preview box shape for all three thumbnails, and the width the
            // explainer below them is clamped to, so the column reads as a
            // single stack instead of three independently sized cards.
            readonly property real _previewHeight: Kirigami.Units.gridUnit * 14
            readonly property real _previewMaxWidth: Kirigami.Units.gridUnit * 32
            // Settle beat after an event-driven read came back empty, and the
            // slow beat the live preview polls on. Named so the comments below
            // can talk about them without restating the numbers.
            readonly property int _stripSettleIntervalMs: 400
            readonly property int _stripLiveIntervalMs: 2000
            // Live strip zones for the scrolling preview, refreshed with the
            // selection context. Empty when the screen is not scrolling right
            // now (mode staged but not applied, no windows, daemon down).
            // Deliberately retained while the page is hidden rather than
            // cleared: the thumbnail's onVisibleChanged re-reads synchronously
            // before the preview is rendered again, so a stale array is never
            // the thing on screen, and dropping it would only cost an extra
            // blocking read on every hide.
            property var scrollingStripZones: []
            // Representative endless strip: a full column mid-view with a
            // clipped column at each edge, so the sketch reads as a window
            // onto a longer strip rather than a fixed zone layout.
            readonly property var scrollingFallbackZones: [
                {
                    "id": "strip:fallback:0",
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
                    "id": "strip:fallback:1",
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
                    "id": "strip:fallback:2",
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
            // allowRetry MUST be false when called FROM the settle timer. The
            // retry is a one-shot settle beat, and a re-arm from inside its own
            // handler is an unbounded loop: a scrolling screen with no windows
            // legitimately reports an empty strip forever, and the page object
            // outlives its visibility (the page host keeps visited pages alive
            // and merely hides them), so nothing would ever stop it. That is a
            // blocking D-Bus round trip every _stripSettleIntervalMs on a page
            // nobody is looking at. Every call site passes the flag
            // explicitly; there is no default, so no site can drift into the
            // retrying behaviour by omission.
            function refreshScrollingStrip(allowRetry) {
                if (!screenState)
                    return;
                // The read is a BLOCKING D-Bus round trip, so it must not run
                // for a page the user cannot see. The host keeps visited pages
                // alive and merely hides them, and screenState changes on every
                // hotplug / desktop / activity event, so without this a stack
                // of cached Monitors pages would each block the GUI thread on
                // events that change nothing they are showing. The live timer
                // is the SINGLE re-read path when the page comes back: it
                // carries triggeredOnStart, so its running transition (which
                // includes both visibility and the mode) fires one read
                // immediately rather than leaving the preview stale for a
                // whole beat.
                if (!stateView.visible)
                    return;
                scrollingStripZones = settingsController.getScrollingStripPreview(screenState.screenId || "");
                // The settle beat covers the case where the mode just flipped
                // and the re-announce batch briefly reports an empty strip: one
                // _stripSettleIntervalMs shot beats waiting out the live beat.
                // It cannot compound — the handler passes allowRetry=false, so
                // a retry never arms another retry, and restart() on a one-shot
                // Timer replaces any pending fire rather than stacking one.
                // Gated on isScrolling as well, matching the live timer: with
                // Snapping staged locally the thumbnail is hidden, and arming a
                // blocking read for it is exactly the poll that gate exists to
                // prevent.
                if (allowRetry && stateView.isScrolling && scrollingStripZones.length === 0 && (screenState.mode || 0) === 2)
                    stripSettleRetry.restart();
            }

            Timer {
                id: stripSettleRetry
                interval: stateView._stripSettleIntervalMs
                repeat: false
                onTriggered: stateView.refreshScrollingStrip(false)
            }

            // The strip is live state: the user can open a window, widen a
            // column, or tab two together while this page is up, and the
            // daemon publishes no per-strip-change signal to subscribe to.
            // So the preview re-reads on a slow beat, and only while it is
            // actually on screen showing a scrolling monitor. The read is a
            // single cheap D-Bus call that returns [] for any other screen.
            Timer {
                id: stripLiveRefresh
                interval: stateView._stripLiveIntervalMs
                repeat: true
                // Gated on isScrolling, not just the daemon's mode: with the
                // daemon scrolling and Snapping staged locally the preview
                // thumbnail is hidden, and polling for something nobody is
                // looking at is the one thing a blocking call must not do.
                running: stateView.visible && stateView.isScrolling && stateView.screenState !== null && (stateView.screenState.mode || 0) === 2
                // Fire on the running transition too, so coming back to a
                // hidden-then-shown page (or picking Scrolling in the mode
                // toggle) repaints from one fresh read. This is deliberately
                // the ONLY event-driven re-read: a second path meant two
                // blocking reads in the same frame on every such transition.
                triggeredOnStart: true
                // allowRetry=false: the settle beat exists for EVENT-driven
                // reads (a mode flip whose re-announce batch briefly reports an
                // empty strip). This timer already re-reads on its own beat, so
                // letting each tick arm a settle shot just raises the
                // blocking-call rate for as long as the strip is legitimately
                // empty.
                onTriggered: stateView.refreshScrollingStrip(false)
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
                // sketch up despite a live strip. allowRetry=true: this is the
                // event-driven read the settle beat exists for.
                refreshScrollingStrip(true);
                // Keyed on the STATE's own screen id, for the same reason the
                // strip refresh above is: before the user clicks a monitor,
                // _selectedScreen is still empty while the state being shown
                // is the first reported one, so reading the staged entry for
                // the empty id would show the wrong monitor's pending edit.
                var staged = settingsController.getStagedAssignment(screenState.screenId || "", desktop, activity);
                if (Object.keys(staged).length > 0) {
                    // A staged entry carries the WHOLE context rule, so every
                    // slot in it is authoritative: a missing id is a pending
                    // slot clear, not an absent opinion. Re-reading the
                    // daemon's resolved value for such a slot would hide the
                    // clear and re-carry the value on the next stage.
                    localMode = staged.mode !== undefined ? staged.mode : (screenState.mode || 0);
                    localLayoutId = staged.layoutId !== undefined ? staged.layoutId : "";
                    localAlgorithmId = staged.algorithmId !== undefined ? staged.algorithmId : "";
                    // A staged empty id collapses to key ABSENCE on the way in,
                    // so an absent key on a staged entry is the echo of a
                    // staged clear. getStagedAssignment's declaration in
                    // settingscontroller.h is the authoritative statement.
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
                id: snappingPreview

                Layout.alignment: Qt.AlignHCenter
                visible: stateView.isSnapping
                layout: {
                    if (stateView.currentLayout)
                        return stateView.currentLayout;

                    // A staged CLEAR means "Default", which resolves to the
                    // global default layout — the same layout the selector's
                    // own Default row previews. Drawing an empty box here left
                    // the big preview blank while the row right below it showed
                    // the zones. Gated on the cleared flag, not on any empty
                    // id: the daemon also reports an EMPTY layoutId for a
                    // context whose active layout is suppressed, and that
                    // screen must keep showing as unassigned rather than have
                    // the preview reinstate the default the daemon withheld.
                    if (stateView.localLayoutCleared) {
                        var fallback = root._findLayout(root._layoutBridge.defaultLayoutId);
                        if (fallback)
                            return fallback;
                    }
                    // The local list does not carry this layout, so there are
                    // no zones to draw. The daemon still reports the resolved
                    // name, so show that rather than nothing.
                    return {
                        "displayName": (stateView.screenState && stateView.screenState.layoutName) || i18n("Default"),
                        "zones": []
                    };
                }
                isSelected: true
                globalAutoAssign: root._layoutBridge.autoAssignAllLayouts
                baseHeight: stateView._previewHeight
                maxThumbnailWidth: stateView._previewMaxWidth
                screenAspectRatio: root._selectedScreenAspectRatio
                Accessible.name: {
                    var name = snappingPreview.layout ? (snappingPreview.layout.displayName || "") : "";
                    return name ? i18nc("accessible name of the layout preview; %1 is the layout name", "Snapping layout preview, %1", name) : i18nc("accessible name of the layout preview when no layout name is known", "Snapping layout preview");
                }
            }

            // Algorithm preview (tiling)
            LayoutThumbnail {
                id: tilingPreview

                Layout.alignment: Qt.AlignHCenter
                visible: stateView.isTiling
                layout: {
                    var found = root._findLayout(root._autotilePrefix + stateView.localAlgorithmId);
                    if (found)
                        return found;

                    // Same reasoning as the snapping preview: no local pick
                    // means "Default", which resolves to the global default
                    // algorithm the selector's Default row already previews.
                    if (!stateView.localAlgorithmId) {
                        var fallback = root._findLayout(root._autotilePrefix + root._layoutBridge.defaultAutotileAlgorithm);
                        if (fallback)
                            return fallback;
                    }
                    // getScreenStates reports the algorithm's display name, so
                    // prefer it over the raw id ("bsp") the local list missed.
                    return {
                        "displayName": (stateView.screenState && stateView.screenState.algorithmName) || stateView.localAlgorithmId || i18n("Default"),
                        "zones": []
                    };
                }
                isSelected: true
                globalAutoAssign: root._layoutBridge.autoAssignAllLayouts
                baseHeight: stateView._previewHeight
                maxThumbnailWidth: stateView._previewMaxWidth
                screenAspectRatio: root._selectedScreenAspectRatio
                Accessible.name: {
                    var name = tilingPreview.layout ? (tilingPreview.layout.displayName || "") : "";
                    return name ? i18nc("accessible name of the tiling preview; %1 is the algorithm name", "Tiling algorithm preview, %1", name) : i18nc("accessible name of the tiling preview when no algorithm name is known", "Tiling algorithm preview");
                }
            }

            // Strip preview (scrolling): the live strip when the screen is
            // scrolling right now, else a representative endless-strip
            // sketch (clipped columns at both edges suggest continuation).
            LayoutThumbnail {
                Layout.alignment: Qt.AlignHCenter
                visible: stateView.isScrolling
                // No onVisibleChanged re-read here: the live timer's
                // triggeredOnStart covers the same visibility and mode
                // transitions (its running binding includes both), and two
                // paths meant two blocking reads in the same frame.
                // category 1 renders the "Dynamic" badge (a live strip
                // snapshot is generated, not editable). Tiles are numbered
                // sequentially in strip order so every visible window gets
                // its own distinct label. Only the first nine of those numbers
                // are reachable from the keyboard: there are nine Snap to Zone
                // digit shortcuts, so a tenth visible tile is drawn with a
                // label no key can address.
                //
                // No id on this literal, deliberately. Every consumer of the
                // object is right here — LayoutCard reads displayName,
                // category and zones — and a stable id would only invite code
                // elsewhere to treat this throwaway snapshot as a real layout.
                layout: ({
                        "displayName": i18nc("tiling mode name", "Scrolling"),
                        "category": 1,
                        // The sketch is unnumbered, matching the daemon's OSD
                        // card: its shapes stand for "a strip", not tiles, so
                        // a labelled 1/2/3 would offer digit targets that do
                        // not exist. A real strip labels every tile.
                        "zoneNumberDisplay": stateView.scrollingStripZones.length > 0 ? "all" : "none",
                        "zones": stateView.scrollingStripZones.length > 0 ? stateView.scrollingStripZones : stateView.scrollingFallbackZones
                    })
                isSelected: true
                globalAutoAssign: root._layoutBridge.autoAssignAllLayouts
                baseHeight: stateView._previewHeight
                maxThumbnailWidth: stateView._previewMaxWidth
                screenAspectRatio: root._selectedScreenAspectRatio
                Accessible.name: stateView.scrollingStripZones.length > 0 ? i18np("Scrolling strip preview with %n window", "Scrolling strip preview with %n windows", stateView.scrollingStripZones.length) : i18nc("accessible name of the placeholder strip preview shown when the screen is not scrolling yet", "Scrolling strip preview, example strip")
            }

            // Scrolling picks neither a layout nor an algorithm, so a short
            // explanation stands in for the selector.
            Kirigami.InlineMessage {
                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: stateView._previewMaxWidth
                type: Kirigami.MessageType.Information
                text: i18n("Scrolling mode arranges windows in resizable columns on an endless strip. It does not use a zone layout. The numbers show the order of the windows on screen, and Snap to Zone uses them.")
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
                    // Entering a mode drops that mode's pending "Default"
                    // pick (the flag, not the combo's own pick — a Default
                    // chosen IN the combo also set touched, which survives
                    // and still carries the clear through _carrySibling's
                    // touched arm). The SIBLING mode's cleared flag survives:
                    // a pending Default on the mode being left is a
                    // deliberate pick the toggle must not silently re-pin.
                    //
                    // touched is deliberately NOT set here. A mode toggle is
                    // not a slot pick, and _carrySibling's touched arm returns
                    // localId — which is pre-filled from the RESOLVED value —
                    // so setting it made every toggle stage the cascade
                    // default as an explicit assignment. Left untouched, the
                    // slot falls to the explicitFlag arm: it carries only
                    // what the daemon marked explicit, so a pure
                    // Snapping-Scrolling-Snapping round trip leaves a
                    // default-following screen following the default.
                    if (idx === 0) {
                        stateView.localLayoutCleared = false;
                    } else if (idx === 1) {
                        stateView.localAlgorithmCleared = false;
                    }
                    root._stageCurrentState();
                }
            }

            // Layout selector (snapping)
            LayoutComboBox {
                id: snappingSelector

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

            // An id the local layout list does not carry leaves the selector on
            // no row at all (currentIndex -1, an empty field), which reads as if
            // the monitor had no layout while the preview above names one. Say
            // what is actually assigned instead of showing a blank.
            Kirigami.InlineMessage {
                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: stateView._previewMaxWidth
                type: Kirigami.MessageType.Information
                text: i18n("This monitor uses %1, which is not in your layout list.", (stateView.screenState && stateView.screenState.layoutName) || stateView.localLayoutId)
                // count > 1: while a layout fetch is in flight the model is
                // just the Default row and every id resolves to -1, which is
                // not a missing layout and must not raise this alarm.
                visible: stateView.isSnapping && snappingSelector.currentIndex === -1 && snappingSelector.count > 1
            }

            // Algorithm selector (tiling)
            LayoutComboBox {
                id: tilingSelector

                Layout.alignment: Qt.AlignHCenter
                visible: stateView.isTiling
                Accessible.name: i18n("Tiling algorithm")
                appSettings: root._layoutBridge
                currentLayoutId: stateView.localAlgorithmId ? root._autotilePrefix + stateView.localAlgorithmId : ""
                layoutFilter: 1
                noneText: i18n("Default")
                showPreview: true
                onActivated: function (idx) {
                    var entry = model[idx];
                    var id = entry ? (entry.value || "") : "";
                    if (id.startsWith(root._autotilePrefix))
                        stateView.localAlgorithmId = id.substring(root._autotilePrefix.length);
                    else
                        stateView.localAlgorithmId = id;
                    stateView.localAlgorithmCleared = (id === "");
                    stateView.localAlgorithmTouched = true;
                    root._stageCurrentState();
                }
            }

            // Same blank-selector case as the snapping hint above.
            Kirigami.InlineMessage {
                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: stateView._previewMaxWidth
                type: Kirigami.MessageType.Information
                text: i18n("This monitor uses %1, which is not in your algorithm list.", (stateView.screenState && stateView.screenState.algorithmName) || stateView.localAlgorithmId)
                // Same in-flight-fetch guard as the snapping hint above.
                visible: stateView.isTiling && tilingSelector.currentIndex === -1 && tilingSelector.count > 1
            }
        }
    }
}

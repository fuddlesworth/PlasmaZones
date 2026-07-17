// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.common as QFZCommon
import org.phosphor.animation

/**
 * @brief Settings page for virtual screen configuration.
 *
 * Allows splitting a physical monitor into multiple virtual screens
 * using a columns-by-rows grid model. Supports horizontal-only splits,
 * vertical-only splits, and full grids (e.g. 2x2). Changes are staged
 * via settingsController and flushed to the daemon when Apply is clicked.
 */
SettingsFlickable {
    id: root

    // ── Internal state ───────────────────────────────────────────────────
    // UX cap — C++ maximum is ConfigDefaults::maxVirtualScreensPerPhysical() (10).
    readonly property int _maxVirtualScreens: 10
    property string _selectedScreen: ""
    // Array of {x, y, width, height, displayName} — staged virtual screen definitions (row-major order)
    property var _pendingScreens: []
    // Array of {x, y, width, height, displayName} — last-saved virtual screen definitions
    property var _savedScreens: []
    // Sentinel physical resolution used when no real geometry can be inferred.
    readonly property int _fallbackScreenWidth: 1920
    readonly property int _fallbackScreenHeight: 1080
    property int _screenWidth: _fallbackScreenWidth
    property int _screenHeight: _fallbackScreenHeight
    // Grid dimensions inferred from pending screens
    property int _columns: 1
    property int _rows: 1
    // True when the pending screens form a strict row-major grid. The divider
    // math (_moveColumnDivider/_moveRowDivider) indexes cells as r*cols+c, so
    // dividers are only created/usable when this holds; non-grid configs
    // (e.g. hand-edited JSON) would otherwise be corrupted by a drag.
    property bool _isGrid: true
    // Flips true one tick after the first geometry resolution so the preview
    // box doesn't animate the initial fallback(16:9)→real-aspect jump on page
    // open; monitor switches afterwards still animate.
    property bool _geometrySettled: false

    function _refreshConfig() {
        if (_selectedScreen === "")
            return;

        if (settingsController.hasUnsavedVirtualScreenConfig(_selectedScreen))
            _pendingScreens = settingsController.getStagedVirtualScreenConfig(_selectedScreen);
        else
            _pendingScreens = settingsController.getVirtualScreenConfig(_selectedScreen);
        _savedScreens = settingsController.getVirtualScreenConfig(_selectedScreen);
        _detectGrid();
    }

    function _deepCopy(arr) {
        return JSON.parse(JSON.stringify(arr));
    }

    function _stageCurrentConfig() {
        if (_selectedScreen === "" || _pendingScreens.length === 0)
            return;

        settingsController.stageVirtualScreenConfig(_selectedScreen, _pendingScreens);
    }

    // Detect grid dimensions from a flat screens array by extracting unique
    // x-start and y-start positions. Pure helper: returns {cols, rows, isGrid}
    // where isGrid reports whether the array really is a row-major cols×rows
    // grid (cell count matches, and each cell sits at its expected row/column
    // start in row-major order). Non-grid configs get cols=length, rows=1 so
    // counts stay meaningful, but isGrid=false gates the divider machinery.
    function _detectGridOf(screensArray) {
        if (screensArray.length <= 1)
            return {
                "cols": 1,
                "rows": 1,
                "isGrid": true
            };
        var tol = 0.01;
        var xStarts = [];
        var yStarts = [];
        for (var i = 0; i < screensArray.length; i++) {
            var s = screensArray[i];
            var foundX = false;
            for (var j = 0; j < xStarts.length; j++) {
                if (Math.abs(xStarts[j] - s.x) < tol) {
                    foundX = true;
                    break;
                }
            }
            if (!foundX)
                xStarts.push(s.x);

            var foundY = false;
            for (var k = 0; k < yStarts.length; k++) {
                if (Math.abs(yStarts[k] - s.y) < tol) {
                    foundY = true;
                    break;
                }
            }
            if (!foundY)
                yStarts.push(s.y);
        }
        var detectedCols = xStarts.length;
        var detectedRows = yStarts.length;
        if (detectedCols * detectedRows === screensArray.length) {
            // Count matches — cheap geometry confirmation: in row-major order
            // cell i must start at (sorted xStarts[i % cols], yStarts[i / cols]).
            xStarts.sort(function (a, b) {
                return a - b;
            });
            yStarts.sort(function (a, b) {
                return a - b;
            });
            var rowMajor = true;
            for (var m = 0; m < screensArray.length; m++) {
                var expX = xStarts[m % detectedCols];
                var expY = yStarts[Math.floor(m / detectedCols)];
                if (Math.abs(screensArray[m].x - expX) >= tol || Math.abs(screensArray[m].y - expY) >= tol) {
                    rowMajor = false;
                    break;
                }
            }
            return {
                "cols": detectedCols,
                "rows": detectedRows,
                "isGrid": rowMajor
            };
        }
        return {
            "cols": screensArray.length,
            "rows": 1,
            "isGrid": false
        };
    }

    // Detect grid dimensions of the staged (pending) config into
    // _columns/_rows, and grid validity into _isGrid.
    function _detectGrid() {
        var grid = _detectGridOf(_pendingScreens);
        _columns = grid.cols;
        _rows = grid.rows;
        _isGrid = grid.isGrid;
    }

    // Build horizontal split regions from percentage ratios.
    function _horizontalRegions(ratios, names) {
        var regions = [];
        var xPos = 0;
        for (var i = 0; i < ratios.length; i++) {
            var w = (i === ratios.length - 1) ? (1 - xPos) : (ratios[i] / 100);
            regions.push({
                "x": xPos,
                "y": 0,
                "width": w,
                "height": 1,
                "displayName": names[i] || ""
            });
            xPos += w;
        }
        return regions;
    }

    // Build a uniform grid of regions (row-major order).
    function _gridRegions(cols, rows, names) {
        var regions = [];
        var cw = 1 / cols;
        var rh = 1 / rows;
        for (var r = 0; r < rows; r++) {
            for (var c = 0; c < cols; c++) {
                var idx = r * cols + c;
                regions.push({
                    "x": c * cw,
                    "y": r * rh,
                    "width": (c === cols - 1) ? (1 - c * cw) : cw,
                    "height": (r === rows - 1) ? (1 - r * rh) : rh,
                    "displayName": (names && idx < names.length) ? names[idx] : ""
                });
            }
        }
        return regions;
    }

    // Load a preset by replacing pending screens with the given regions.
    function _loadPreset(regions) {
        _pendingScreens = regions;
        _detectGrid();
        _stageCurrentConfig();
    }

    function _updateScreenGeometry() {
        // Mark geometry settled on the next tick the first time we resolve, so
        // the initial fallback→real transition lands without animating (the
        // Behaviors below gate on _geometrySettled). Scheduled here, before the
        // assignments, so it survives the function's several early returns.
        if (!root._geometrySettled)
            Qt.callLater(function () {
                root._geometrySettled = true;
            });

        var screens = settingsController.screens;
        // First pass: exact name match (physical screen entry)
        for (var i = 0; i < screens.length; i++) {
            if (screens[i].name === _selectedScreen) {
                var w = screens[i].width;
                var h = screens[i].height;
                if (w && h) {
                    _screenWidth = w;
                    _screenHeight = h;
                    return;
                }
                break;
            }
        }
        // Second pass: reconstruct from virtual screen children.
        // Group children by pixel height to identify rows, then take one
        // row's total width as the physical width and sum distinct row
        // heights for the physical height.
        var prefix = _selectedScreen + "/vs:";
        var children = [];
        for (var j = 0; j < screens.length; j++) {
            var name = screens[j].name || "";
            if (name.indexOf(prefix) === 0 && screens[j].width && screens[j].height)
                children.push({
                    "w": screens[j].width,
                    "h": screens[j].height
                });
        }
        if (children.length === 0) {
            _screenWidth = _fallbackScreenWidth;
            _screenHeight = _fallbackScreenHeight;
            return;
        }
        // Group by pixel height (2px tolerance) to separate rows
        var rowHeights = [];
        var rowWidths = [];
        for (var ci = 0; ci < children.length; ci++) {
            var ch = children[ci].h;
            var found = false;
            for (var ri = 0; ri < rowHeights.length; ri++) {
                if (Math.abs(rowHeights[ri] - ch) < 2) {
                    rowWidths[ri] += children[ci].w;
                    found = true;
                    break;
                }
            }
            if (!found) {
                rowHeights.push(ch);
                rowWidths.push(children[ci].w);
            }
        }
        // Uniform grid fallback: when all cells share one height (e.g. 2x2),
        // height-grouping collapses every row into a single group, so
        // rowWidths[0] is the summed width of ALL cells = rows × (one row's
        // full width). Dividing by the row count recovers one physical row's
        // width; multiplying the shared cell height by it recovers the full
        // height. The /vs: children reflect the APPLIED config, so derive the
        // grid from _savedScreens (which mirrors it) rather than _rows/_columns,
        // which track the staged config and may differ mid-edit.
        // Accepted transient: between a save and the daemon republishing its
        // screens, _savedScreens already holds the new config while the /vs:
        // children still reflect the old one, so the reconstructed geometry can
        // be briefly wrong. Display-only and self-correcting on the next
        // screensChanged — deliberately not fixed (audit 2026-07 concurred).
        var appliedGrid = _detectGridOf(_savedScreens);
        if (rowHeights.length === 1 && appliedGrid.rows > 1 && appliedGrid.cols > 0) {
            _screenWidth = Math.round(rowWidths[0] / appliedGrid.rows);
            _screenHeight = rowHeights[0] * appliedGrid.rows;
            return;
        }
        // Physical width: any row's total width (should be the same for all rows)
        var maxRowW = 0;
        for (var rw = 0; rw < rowWidths.length; rw++) {
            if (rowWidths[rw] > maxRowW)
                maxRowW = rowWidths[rw];
        }
        // Physical height: sum of all distinct row heights
        var totalH = 0;
        for (var rhi = 0; rhi < rowHeights.length; rhi++) {
            totalH += rowHeights[rhi];
        }
        if (maxRowW > 0 && totalH > 0) {
            _screenWidth = maxRowW;
            _screenHeight = totalH;
        } else {
            _screenWidth = _fallbackScreenWidth;
            _screenHeight = _fallbackScreenHeight;
        }
    }

    // Redistribute to equal grid cells at given dimensions.
    function _redistributeGrid(cols, rows) {
        if (cols <= 0 || rows <= 0)
            return;

        var total = cols * rows;
        if (total <= 1) {
            settingsController.stageVirtualScreenRemoval(_selectedScreen);
            root._refreshConfig();
            return;
        }
        if (total > _maxVirtualScreens)
            return;

        var screens = [];
        var cw = 1 / cols;
        var rh = 1 / rows;
        for (var r = 0; r < rows; r++) {
            for (var c = 0; c < cols; c++) {
                var idx = r * cols + c;
                var existingName = "";
                if (idx < _pendingScreens.length)
                    existingName = _pendingScreens[idx].displayName || "";

                screens.push({
                    "x": c * cw,
                    "y": r * rh,
                    "width": (c === cols - 1) ? (1 - c * cw) : cw,
                    "height": (r === rows - 1) ? (1 - r * rh) : rh,
                    "displayName": existingName
                });
            }
        }
        _pendingScreens = screens;
        _columns = cols;
        _rows = rows;
        // _gridRegions-style output above is row-major by construction.
        _isGrid = true;
        _stageCurrentConfig();
    }

    // Move a column divider (vertical boundary between adjacent columns).
    // Adjusts x/width of all cells straddling that column boundary.
    // Requires a strict row-major grid (r*cols+c indexing below).
    function _moveColumnDivider(colIndex, newXFraction) {
        if (!_isGrid)
            return;
        if (colIndex < 0 || colIndex >= _columns - 1)
            return;

        var screens = _deepCopy(_pendingScreens);
        var minW = 0.05;
        // Use first row to read column positions
        var leftStart = screens[colIndex].x;
        var rightEnd = screens[colIndex + 1].x + screens[colIndex + 1].width;
        var minX = leftStart + minW;
        var maxX = rightEnd - minW;
        // Degenerate pair: the two columns together are narrower than
        // 2*minW, so no divider position satisfies both minimums —
        // clamping to [minX, maxX] would pin to the wrong edge.
        if (minX > maxX)
            return;
        var newDivPos = Math.max(minX, Math.min(maxX, newXFraction));
        var newLeftWidth = newDivPos - leftStart;
        var newRightWidth = rightEnd - newDivPos;
        // Update all cells in columns colIndex and colIndex+1 across all rows
        for (var r = 0; r < _rows; r++) {
            var leftIdx = r * _columns + colIndex;
            var rightIdx = r * _columns + colIndex + 1;
            screens[leftIdx].width = newLeftWidth;
            screens[rightIdx].x = newDivPos;
            screens[rightIdx].width = newRightWidth;
        }
        // The clamp bounds newDivPos to [minX, maxX], so both moved cells keep
        // width >= minW and unmoved cells are untouched — no post-mutation
        // re-check is needed (a float-edge one would spuriously reject a
        // boundary drag).
        _pendingScreens = screens;
        _stageCurrentConfig();
    }

    // Move a row divider (horizontal boundary between adjacent rows).
    // Adjusts y/height of all cells straddling that row boundary.
    // Requires a strict row-major grid (r*cols+c indexing below).
    function _moveRowDivider(rowIndex, newYFraction) {
        if (!_isGrid)
            return;
        if (rowIndex < 0 || rowIndex >= _rows - 1)
            return;

        var screens = _deepCopy(_pendingScreens);
        var minH = 0.05;
        // Use first column to read row positions
        var topStart = screens[rowIndex * _columns].y;
        var bottomEnd = screens[(rowIndex + 1) * _columns].y + screens[(rowIndex + 1) * _columns].height;
        var minY = topStart + minH;
        var maxY = bottomEnd - minH;
        // Degenerate pair: see _moveColumnDivider — no valid divider
        // position when the pair is shorter than 2*minH.
        if (minY > maxY)
            return;
        var newDivPos = Math.max(minY, Math.min(maxY, newYFraction));
        var newTopHeight = newDivPos - topStart;
        var newBottomHeight = bottomEnd - newDivPos;
        // Update all cells in rows rowIndex and rowIndex+1 across all columns
        for (var c = 0; c < _columns; c++) {
            var topIdx = rowIndex * _columns + c;
            var bottomIdx = (rowIndex + 1) * _columns + c;
            screens[topIdx].height = newTopHeight;
            screens[bottomIdx].y = newDivPos;
            screens[bottomIdx].height = newBottomHeight;
        }
        // The clamp bounds newDivPos to [minY, maxY], so both moved cells keep
        // height >= minH and unmoved cells are untouched — no post-mutation
        // re-check is needed.
        _pendingScreens = screens;
        _stageCurrentConfig();
    }

    // Check if current config matches a preset (comparing regions).
    function _matchesPreset(regions) {
        if (_pendingScreens.length !== regions.length)
            return false;

        for (var i = 0; i < regions.length; i++) {
            if (Math.abs(_pendingScreens[i].x - regions[i].x) > 0.01)
                return false;

            if (Math.abs(_pendingScreens[i].y - regions[i].y) > 0.01)
                return false;

            if (Math.abs(_pendingScreens[i].width - regions[i].width) > 0.01)
                return false;

            if (Math.abs(_pendingScreens[i].height - regions[i].height) > 0.01)
                return false;
        }
        return true;
    }

    // Strip "/vs:N" suffix to get physical screen ID
    function _toPhysicalId(name) {
        // Canonical "/vs:" collapse lives in C++ (PhosphorIdentity); don't
        // re-spell the separator here.
        return settingsController.physicalScreenId(name);
    }

    function _autoSelectScreen() {
        var screens = settingsController.screens;
        for (var i = 0; i < screens.length; i++) {
            if (screens[i].isPrimary) {
                _selectedScreen = _toPhysicalId(screens[i].name || "");
                return;
            }
        }
        if (screens.length > 0)
            _selectedScreen = _toPhysicalId(screens[0].name || "");
    }

    // True if `physId` is still a connected physical output.
    function _screenStillPresent(physId) {
        var arr = settingsController.screens || [];
        for (var i = 0; i < arr.length; i++) {
            if (root._toPhysicalId(arr[i].name || "") === physId)
                return true;
        }
        return false;
    }

    contentHeight: content.implicitHeight
    clip: true
    Component.onCompleted: {
        if (!_selectedScreen && settingsController.screens.length > 0)
            _autoSelectScreen();
    }
    on_SelectedScreenChanged: {
        // Config first: geometry reconstruction's uniform-grid fallback derives
        // its grid via _detectGridOf(_savedScreens), which _refreshConfig just
        // loaded for this screen.
        _refreshConfig();
        _updateScreenGeometry();
    }

    Connections {
        function onScreensChanged() {
            // Drop a selection whose physical output was unplugged, then re-pick.
            // Changing _selectedScreen fires on_SelectedScreenChanged, which
            // already refreshes geometry + config — so only refresh explicitly
            // here when the selection did NOT change (e.g. a non-selected output
            // hot-plugged), avoiding a redundant double refresh.
            var before = root._selectedScreen;
            if (before !== "" && !root._screenStillPresent(before))
                root._selectedScreen = "";
            if (root._selectedScreen === "" && settingsController.screens.length > 0)
                root._autoSelectScreen();
            if (root._selectedScreen === before) {
                // Config first: geometry reconstruction reads the grid
                // _refreshConfig just loaded (uniform-grid fallback).
                root._refreshConfig();
                root._updateScreenGeometry();
            }
        }

        target: settingsController
    }

    // Re-read the selected screen's config whenever the dirty set changes.
    // Covers a global discard (needsSave → false), a global save, AND the
    // per-page Reset/Discard of this page (which revert the staged VS configs
    // and emit dirtyPagesChanged even when other pages keep the global flag
    // dirty). Unconditional is safe: every editor mutation stages immediately
    // (_stageCurrentConfig), so _refreshConfig always reads back the staged
    // value that mirrors _pendingScreens — never clobbering an in-progress edit.
    Connections {
        function onDirtyPagesChanged() {
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

        // Monitor picker (spatial map; physical outputs only, no "All")
        DisplayMap {
            Layout.fillWidth: true
            appSettings: settingsController
            showAll: false
            physicalOnly: true
            selectedScreenName: root._selectedScreen
            onScreenPicked: name => root._selectedScreen = name
        }

        // Visual preview with draggable dividers
        SettingsCard {
            headerText: i18n("Preview")

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                // Resolution and split info
                Label {
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    text: {
                        let orient = root._screenHeight > root._screenWidth ? i18nc("@label screen orientation", "Portrait") : i18nc("@label screen orientation", "Landscape");
                        let res = i18nc("@info screen resolution in pixels", "%1\u00d7%2 px", root._screenWidth, root._screenHeight) + " \u00b7 " + orient;
                        let count = root._pendingScreens.length;
                        if (count > 1) {
                            if (root._rows > 1)
                                return res + " \u00b7 " + i18n("%1\u00d7%2 Grid (%3 screens)", root._columns, root._rows, count);

                            return res + " \u00b7 " + i18n("%1-Way Split", count);
                        } else if (count === 1) {
                            return res + " \u00b7 " + i18n("Single Region");
                        }
                        return res;
                    }
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                }

                VirtualScreenPreview {
                    id: previewRect

                    // Fit the screen's real pixel dimensions inside the available
                    // box so the preview is portrait for portrait monitors and
                    // landscape for landscape ones — not a fixed horizontal
                    // rectangle. A single uniform scale drives both width and
                    // height off the true monitor size (bounded by the card's
                    // content width and a max height, smaller bound wins), so the
                    // whole screen always fits; the box is then centred.
                    readonly property real _availWidth: Math.min(parent.width - Kirigami.Units.largeSpacing * 2, Kirigami.Units.gridUnit * 30)
                    readonly property real _maxHeight: Kirigami.Units.gridUnit * 16
                    readonly property real _fitScale: (root._screenWidth > 0 && root._screenHeight > 0) ? Math.min(_availWidth / root._screenWidth, _maxHeight / root._screenHeight) : 0

                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: _fitScale > 0 ? root._screenWidth * _fitScale : _availWidth
                    Layout.preferredHeight: _fitScale > 0 ? root._screenHeight * _fitScale : _availWidth * 9 / 16
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    Layout.bottomMargin: Kirigami.Units.largeSpacing
                    pendingScreens: root._pendingScreens
                    screenWidth: root._screenWidth
                    screenHeight: root._screenHeight
                    columns: root._columns
                    rows: root._rows
                    isGrid: root._isGrid
                    onColumnDividerMoved: function (colIndex, newFraction) {
                        root._moveColumnDivider(colIndex, newFraction);
                    }
                    onRowDividerMoved: function (rowIndex, newFraction) {
                        root._moveRowDivider(rowIndex, newFraction);
                    }

                    // Smoothly morph the box when switching monitors or changing
                    // the split — width and height animate independently so a
                    // landscape→portrait change eases into the new shape.
                    Behavior on Layout.preferredWidth {
                        enabled: root._geometrySettled
                        PhosphorMotionAnimation {
                            profile: "widget.hover"
                            durationOverride: Kirigami.Units.longDuration
                        }
                    }

                    Behavior on Layout.preferredHeight {
                        enabled: root._geometrySettled
                        PhosphorMotionAnimation {
                            profile: "widget.hover"
                            durationOverride: Kirigami.Units.longDuration
                        }
                    }
                }
            }
        }

        // Presets
        SettingsCard {
            headerText: i18n("Presets")
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                // Preset card grid. Each card shows a ZonePreview thumbnail + label
                // and applies the split via _loadPreset(); the active preset is
                // highlighted via _matchesPreset(). One flat, data-driven grid
                // replaces the old two groups of plain text buttons. The
                // builders (_horizontalRegions / _gridRegions) and matcher are
                // reused unchanged — this is purely the presentation.
                GridLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    Layout.bottomMargin: Kirigami.Units.largeSpacing
                    columns: 4
                    uniformCellWidths: true
                    columnSpacing: Kirigami.Units.smallSpacing
                    rowSpacing: Kirigami.Units.smallSpacing

                    Repeater {
                        model: [
                            {
                                "label": i18n("50% / 50%"),
                                "detail": i18n("Horizontal"),
                                "regions": root._horizontalRegions([50, 50], [i18n("Left"), i18n("Right")])
                            },
                            {
                                "label": i18n("60% / 40%"),
                                "detail": i18n("Horizontal"),
                                "regions": root._horizontalRegions([60, 40], [i18n("Main"), i18n("Side")])
                            },
                            {
                                "label": i18n("33% / 33% / 33%"),
                                "detail": i18n("Horizontal"),
                                "regions": root._horizontalRegions([33.3, 33.4, 33.3], [i18n("Left"), i18n("Center"), i18n("Right")])
                            },
                            {
                                "label": i18n("40% / 20% / 40%"),
                                "detail": i18n("Horizontal"),
                                "regions": root._horizontalRegions([40, 20, 40], [i18n("Left"), i18n("Center"), i18n("Right")])
                            },
                            {
                                "label": i18n("50% / 50%"),
                                "detail": i18n("Vertical"),
                                "regions": root._gridRegions(1, 2, [i18n("Top"), i18n("Bottom")])
                            },
                            {
                                "label": i18n("50% / 50%"),
                                "detail": i18n("Grid"),
                                "regions": root._gridRegions(2, 2, [i18n("Top-Left"), i18n("Top-Right"), i18n("Bottom-Left"), i18n("Bottom-Right")])
                            },
                            {
                                "label": i18n("33% / 33% / 33%"),
                                "detail": i18n("Grid"),
                                "regions": root._gridRegions(3, 2, [i18n("Top-Left"), i18n("Top-Center"), i18n("Top-Right"), i18n("Bottom-Left"), i18n("Bottom-Center"), i18n("Bottom-Right")])
                            },
                            {
                                "label": i18n("60% / 40%"),
                                "detail": i18n("Grid"),
                                "regions": [
                                    {
                                        "x": 0,
                                        "y": 0,
                                        "width": 0.6,
                                        "height": 0.5,
                                        "displayName": i18n("Top-Main")
                                    },
                                    {
                                        "x": 0.6,
                                        "y": 0,
                                        "width": 0.4,
                                        "height": 0.5,
                                        "displayName": i18n("Top-Side")
                                    },
                                    {
                                        "x": 0,
                                        "y": 0.5,
                                        "width": 0.6,
                                        "height": 0.5,
                                        "displayName": i18n("Bottom-Main")
                                    },
                                    {
                                        "x": 0.6,
                                        "y": 0.5,
                                        "width": 0.4,
                                        "height": 0.5,
                                        "displayName": i18n("Bottom-Side")
                                    }
                                ]
                            }
                        ]

                        delegate: ItemDelegate {
                            id: presetCard

                            required property var modelData
                            required property int index

                            readonly property bool active: root._matchesPreset(presetCard.modelData.regions)

                            // Inner padding (card border -> content) applied as a
                            // margin inside contentItem below, not via the Control's
                            // padding: the org.kde.desktop ItemDelegate style overrides
                            // per-side padding, so it would be ignored and the content
                            // hugged the border. Control padding zeroed.
                            readonly property real _cardPad: Kirigami.Units.largeSpacing

                            Layout.fillWidth: true
                            enabled: root._selectedScreen !== ""
                            hoverEnabled: true
                            padding: 0
                            Accessible.name: i18n("Preset: %1 %2", presetCard.modelData.label, presetCard.modelData.detail)
                            // Deep-copy the preset's regions so divider drags mutate
                            // _pendingScreens, not the shared model entry.
                            onClicked: root._loadPreset(presetCard.modelData.regions.map(function (r) {
                                return {
                                    "x": r.x,
                                    "y": r.y,
                                    "width": r.width,
                                    "height": r.height,
                                    "displayName": r.displayName
                                };
                            }))

                            // Selection highlight follows the layout-grid convention
                            // (LayoutGridDelegate): a subtle accent wash + accent
                            // border, not a full-opacity fill.
                            background: Rectangle {
                                radius: Kirigami.Units.smallSpacing * 1.5
                                color: presetCard.active ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.15) : (presetCard.hovered ? Qt.tint(Kirigami.Theme.backgroundColor, Qt.alpha(Kirigami.Theme.hoverColor, 0.1)) : Kirigami.Theme.backgroundColor)
                                border.width: 1
                                border.color: presetCard.active ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.5) : (presetCard.hovered ? Kirigami.Theme.hoverColor : Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast))
                            }

                            contentItem: Item {
                                implicitWidth: presetRow.implicitWidth + presetCard._cardPad * 2
                                implicitHeight: presetRow.implicitHeight + presetCard._cardPad * 2

                                RowLayout {
                                    id: presetRow

                                    anchors.fill: parent
                                    anchors.margins: presetCard._cardPad
                                    spacing: Kirigami.Units.largeSpacing

                                    // Preview thumbnail (left): fixed 16:9 box using the
                                    // shared ZonePreview, drawn as an inset well (thumbnail
                                    // altBg on presetCard bg on SettingsCard altBg) with an
                                    // accent border that thickens when active. Zone
                                    // numbers off — the split shape is what matters here.
                                    Rectangle {
                                        Layout.preferredHeight: Kirigami.Units.gridUnit * 3
                                        Layout.preferredWidth: Kirigami.Units.gridUnit * 3 * 16 / 9
                                        Layout.alignment: Qt.AlignVCenter
                                        radius: Kirigami.Units.smallSpacing
                                        color: Kirigami.Theme.alternateBackgroundColor
                                        border.width: presetCard.active ? 3 : 1
                                        border.color: presetCard.active ? Kirigami.Theme.highlightColor : Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)

                                        QFZCommon.ZonePreview {
                                            anchors.fill: parent
                                            anchors.margins: Kirigami.Units.smallSpacing
                                            zones: presetCard.modelData.regions
                                            isActive: presetCard.active
                                            zonePadding: Math.round(Kirigami.Units.smallSpacing / 2)
                                            edgeGap: Math.round(Kirigami.Units.smallSpacing / 2)
                                            minZoneSize: 6
                                            showZoneNumbers: false
                                        }
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Layout.alignment: Qt.AlignVCenter
                                        spacing: 0

                                        Label {
                                            Layout.fillWidth: true
                                            text: presetCard.modelData.label
                                            font.weight: Font.Medium
                                            elide: Text.ElideRight
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: presetCard.modelData.detail
                                            font: Kirigami.Theme.smallFont
                                            color: Kirigami.Theme.disabledTextColor
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Custom split editor
        SettingsCard {
            headerText: i18n("Custom Split")
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                // Columns and Rows spinboxes
                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    spacing: Kirigami.Units.largeSpacing

                    RowLayout {
                        spacing: Kirigami.Units.smallSpacing

                        Label {
                            text: i18n("Columns:")
                        }

                        SpinBox {
                            id: columnsSpinBox

                            from: 1
                            to: Math.max(1, Math.floor(root._maxVirtualScreens / Math.max(1, root._rows)))
                            editable: true
                            enabled: root._selectedScreen !== ""
                            Component.onCompleted: value = root._columns
                            onValueModified: {
                                var newCols = value;
                                var maxRows = Math.max(1, Math.floor(root._maxVirtualScreens / newCols));
                                var newRows = Math.min(root._rows, maxRows);
                                // _redistributeGrid handles the <=1 case itself
                                // (stages removal + refreshes).
                                root._redistributeGrid(newCols, newRows);
                            }
                            Accessible.name: i18n("Number of columns")
                        }
                    }

                    RowLayout {
                        spacing: Kirigami.Units.smallSpacing

                        Label {
                            text: i18n("Rows:")
                        }

                        SpinBox {
                            id: rowsSpinBox

                            from: 1
                            to: Math.max(1, Math.floor(root._maxVirtualScreens / Math.max(1, root._columns)))
                            editable: true
                            enabled: root._selectedScreen !== ""
                            Component.onCompleted: value = root._rows
                            onValueModified: {
                                var newRows = value;
                                var maxCols = Math.max(1, Math.floor(root._maxVirtualScreens / newRows));
                                var newCols = Math.min(root._columns, maxCols);
                                // _redistributeGrid handles the <=1 case itself
                                // (stages removal + refreshes).
                                root._redistributeGrid(newCols, newRows);
                            }
                            Accessible.name: i18n("Number of rows")
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
                        onClicked: root._redistributeGrid(root._columns, root._rows)
                        ToolTip.text: i18n("Reset all splits to equal sizes")
                        ToolTip.visible: hovered
                        Accessible.name: i18n("Equalize virtual screen sizes")
                    }
                }

                // Sync spinboxes when grid dimensions change
                Connections {
                    function on_ColumnsChanged() {
                        columnsSpinBox.value = root._columns;
                    }

                    function on_RowsChanged() {
                        rowsSpinBox.value = root._rows;
                    }

                    target: root
                }

                // Per-split name and dimensions
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
                                // editingFinished fires on any focus-out; skip the
                                // copy/stage when the text didn't actually change so
                                // merely tabbing through fields can't dirty the page.
                                if (text === (modelData.displayName || ""))
                                    return;
                                var screens = root._deepCopy(root._pendingScreens);
                                if (index >= 0 && index < screens.length) {
                                    screens[index].displayName = text;
                                    root._pendingScreens = screens;
                                    root._stageCurrentConfig();
                                }
                            }
                        }

                        Label {
                            text: {
                                var wp = Math.round(modelData.width * 100);
                                if (root._rows > 1)
                                    return i18nc("@info virtual screen size as width by height percentages", "%1% \u00d7 %2%", wp, Math.round(modelData.height * 100));

                                return i18nc("@info virtual screen width as a percentage", "%1%", wp);
                            }
                            Layout.preferredWidth: root._rows > 1 ? Kirigami.Units.gridUnit * 5 : Kirigami.Units.gridUnit * 3
                            horizontalAlignment: Text.AlignRight
                            color: Kirigami.Theme.disabledTextColor
                            font: Kirigami.Theme.smallFont
                        }

                        Label {
                            text: {
                                var wpx = Math.round(modelData.width * root._screenWidth);
                                if (root._rows > 1)
                                    return i18nc("@info screen resolution in pixels", "%1\u00d7%2 px", wpx, Math.round(modelData.height * root._screenHeight));

                                return i18nc("@info width in pixels", "%1 px", wpx);
                            }
                            Layout.preferredWidth: root._rows > 1 ? Kirigami.Units.gridUnit * 5.5 : Kirigami.Units.gridUnit * 4
                            horizontalAlignment: Text.AlignRight
                            color: Kirigami.Theme.disabledTextColor
                            font: Kirigami.Theme.smallFont
                        }
                    }
                }

                SettingsSeparator {}

                Button {
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    Layout.bottomMargin: Kirigami.Units.smallSpacing
                    text: i18n("Remove Subdivisions")
                    icon.name: "edit-delete"
                    palette.buttonText: Kirigami.Theme.negativeTextColor
                    enabled: root._selectedScreen !== "" && (root._pendingScreens.length > 0 || root._savedScreens.length > 0)
                    onClicked: {
                        settingsController.stageVirtualScreenRemoval(root._selectedScreen);
                        // Read the staged removal back through the single
                        // reconciliation path (pending/saved/grid all come from
                        // _refreshConfig). Explicit call, not the
                        // onDirtyPagesChanged connection: staging emits
                        // dirtyPagesChanged only on the clean→dirty transition,
                        // so it stays silent when the page is already dirty.
                        root._refreshConfig();
                    }
                    ToolTip.text: i18n("Remove all virtual screen subdivisions from this monitor")
                    ToolTip.visible: hovered
                    Accessible.name: i18n("Remove virtual screen subdivisions")
                }
            }
        }
    }
}

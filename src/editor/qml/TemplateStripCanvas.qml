// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "ThemeHelpers.js" as Theme
import org.kde.kirigami as Kirigami
import org.phosphor.animation

/**
 * @brief Visual strip canvas for scrolling-template editing
 *
 * Renders the template's seed blueprint as full-cross-extent column bands, the
 * way the scrolling engine lays the first windows out. A column's width is a
 * fraction of the screen ALONG the strip, so the canvas's main extent IS the
 * screen: bands whose summed widths pass the far edge scroll, exactly like the
 * real strip. Drag the divider after a column to resize it (strip semantics:
 * neighbours keep their width and shift, nothing redistributes), click a
 * column to select it, and use its action buttons to reorder, flip stacked or
 * tabbed, or remove. Adding columns lives in the ControlBar.
 *
 * The canvas transposes with the target screen's strip axis
 * (controller.templatePreviewVertical): on a vertical strip the bands stack
 * down the canvas and scroll on y. The template data is unchanged either way —
 * a column width is a fraction along the strip whichever way the strip runs —
 * so this is a rendering and input concern only, and every geometry expression
 * below reads through mainExtent / verticalAxis rather than naming x or y.
 */
Item {
    id: stripCanvas

    required property var editorController
    // The key-handling surface (EditorWindow's drawingArea). Every pointer
    // interaction on the strip hands focus back to it, the same contract
    // CanvasMouseHandler keeps for the zone canvas — without it, focus left
    // in a panel text field silently kills every keyboard column operation.
    required property Item keyFocusTarget
    // The scrolling-template sub-model (editorController.scrollingTemplate)
    readonly property var templateModel: editorController ? editorController.scrollingTemplate : null

    // Selection is index-based on purpose: blueprint columns are an ordered
    // list without identities, unlike zones.
    property int selectedColumn: -1

    // Which way the strip runs on the target screen, and the canvas extent a
    // column fraction is measured against. Every axis-dependent expression in
    // this file goes through these two, so the transposition is one binding
    // and not a scattered set of x/y swaps.
    readonly property bool verticalAxis: editorController ? editorController.templatePreviewVertical : false
    readonly property real mainExtent: verticalAxis ? height : width

    readonly property var columns: templateModel ? templateModel.columns : []
    readonly property int columnCount: columns ? columns.length : 0
    readonly property var constants: templateModel ? templateModel.scrollingConstants() : ({})
    readonly property real minFraction: constants.proportionMin || 0.05
    readonly property real maxFraction: constants.proportionMax || 1
    // Gap between bands. A full grid unit on purpose: the width divider
    // lives inside this gap the way snapping's zone dividers live inside the
    // zone padding, and a smallSpacing gap leaves it too thin to grab (the
    // neighbouring band would occlude half its extended hit area too).
    readonly property real bandSpacing: Kirigami.Units.gridUnit
    readonly property color frameBorderColor: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)

    // Tracks the previous count so an append can be told apart from a
    // removal or reload; -1 means "next change is a reload, never a grow".
    property int _lastColumnCount: -1
    // Set when an append should scroll the tail into view; consumed by the
    // content-size handler, which fires after the Grid has repolished, so the
    // scroll target is not one band stale. An append that never moves the
    // content size needs no scroll (the strip fits), so there is no
    // tick-timed fallback on purpose.
    property bool _revealTailPending: false

    Component.onCompleted: _lastColumnCount = columnCount

    // A different template was loaded over this canvas (the controller emits
    // layoutIdChanged BEFORE the model reset precisely so this runs first):
    // rewind the viewport, clear the selection — a band index picked in the
    // old template means nothing in the new one — and make sure the count
    // change is not mistaken for an append.
    Connections {
        function onLayoutIdChanged() {
            stripCanvas._lastColumnCount = -1;
            stripCanvas.selectedColumn = -1;
            // Both, unconditionally: the axis can flip under a loaded
            // template, and a stale offset on the now-cross axis would leave
            // the strip scrolled sideways out of view.
            stripFlickable.contentX = 0;
            stripFlickable.contentY = 0;
            // Re-stamp after the model reset lands (this handler runs BEFORE
            // it by design): if the new template happens to carry the same
            // column count, onColumnCountChanged never fires and the -1
            // would otherwise latch, swallowing the next append's
            // select-and-reveal.
            Qt.callLater(function () {
                stripCanvas._lastColumnCount = stripCanvas.columnCount;
            });
        }

        target: stripCanvas.editorController
        enabled: stripCanvas.editorController !== null
    }

    // Keep the selection inside the list when columns are removed or the
    // template is reloaded underneath us, and reveal a column appended while
    // the strip's tail is scrolled off screen (the ControlBar's Add Column
    // lands at the end, which can lie past the far edge).
    onColumnCountChanged: {
        if (selectedColumn >= columnCount)
            selectedColumn = columnCount - 1;
        if (_lastColumnCount >= 0 && columnCount > _lastColumnCount) {
            // Select the appended column so both add affordances (the
            // ControlBar button and the empty-state action) behave the same.
            // A grow is ASSUMED to be an append: undoing a middle-column
            // removal also lands here and selects/reveals the tail, which is
            // accepted — the model does not expose the inserted index.
            selectedColumn = columnCount - 1;
            // consumeTailReveal consumes this once the Grid has repolished.
            // An append that does NOT move the main content size needs no
            // scroll at all (the strip fits, the offset is already clamped to
            // 0), so no tick-timed fallback exists — the one residual is a
            // flag from such an append being consumed by a LATER overflow
            // crossing, which the non-grow retire below bounds.
            _revealTailPending = true;
        } else {
            // Any non-grow change retires a stale reveal so a removal or
            // reload cannot inherit an earlier append's tail scroll.
            _revealTailPending = false;
        }
        _lastColumnCount = columnCount;
    }

    function selectColumn(index) {
        selectedColumn = (index >= 0 && index < columnCount) ? index : -1;
    }

    // The strip's scroll offset, whichever axis it runs on. Reading and
    // writing it through one pair keeps revealColumn and the tail reveal
    // axis-blind.
    function _mainOffset() {
        return stripCanvas.verticalAxis ? stripFlickable.contentY : stripFlickable.contentX;
    }

    function _setMainOffset(value) {
        if (stripCanvas.verticalAxis)
            stripFlickable.contentY = value;
        else
            stripFlickable.contentX = value;
    }

    // Keep the selected band inside the viewport: keyboard selection and
    // resize can walk or grow a column past the visible edge on an
    // overflowing strip. Band pitch in content space is fraction * the
    // canvas's main extent (each band gives up one gap, the Grid spacing gives
    // it back).
    function revealColumn(index) {
        if (index < 0 || index >= columnCount)
            return;

        const viewport = stripCanvas.verticalAxis ? stripFlickable.height : stripFlickable.width;
        const content = stripCanvas.verticalAxis ? stripFlickable.contentHeight : stripFlickable.contentWidth;
        let low = 0;
        for (let i = 0; i < index; i++)
            low += columns[i].width * stripCanvas.mainExtent;
        const high = low + columns[index].width * stripCanvas.mainExtent;
        const offset = stripCanvas._mainOffset();
        if (low < offset)
            stripCanvas._setMainOffset(Math.max(0, low));
        else if (high > offset + viewport)
            stripCanvas._setMainOffset(Math.max(0, Math.min(high - viewport, content - viewport)));
    }

    // An axis flip re-lays the whole strip, so an offset measured on the old
    // main axis means nothing on the new one. Rewind both rather than try to
    // carry a position across: the band under the cursor is not the band under
    // it after a transposition.
    onVerticalAxisChanged: {
        stripFlickable.contentX = 0;
        stripFlickable.contentY = 0;
        _revealTailPending = false;
    }

    // Keyboard handling, called from EditorWindow's canvas key handler while
    // template mode is active. The arrows ALONG the strip select, Shift+them
    // resize, Ctrl+them reorder, Delete removes. Only keys that actually act
    // are accepted; an arrow with nothing selected (or an empty strip) falls
    // through instead of being swallowed as a no-op — which is also what the
    // cross-axis arrows do, deliberately: a column stack has no keyboard verb
    // here, so those keys stay available to whatever else wants them rather
    // than being eaten by the axis that happens to be drawn.
    function handleKeyPress(event) {
        if (!templateModel)
            return false;

        const lowKey = verticalAxis ? Qt.Key_Up : Qt.Key_Left;
        const highKey = verticalAxis ? Qt.Key_Down : Qt.Key_Right;
        const step = constants.keyboardResizeStep || 0.01;
        if (event.key === lowKey || event.key === highKey) {
            const dir = event.key === highKey ? 1 : -1;
            if (event.modifiers & Qt.ShiftModifier) {
                if (selectedColumn >= 0) {
                    event.accepted = true;
                    // One undo entry per held key: the first press opens a
                    // gesture, autorepeat merges into it.
                    if (!event.isAutoRepeat)
                        templateModel.beginWidthDrag();
                    const current = columns[selectedColumn].width;
                    templateModel.setColumnWidth(selectedColumn, current + dir * step, true);
                    revealColumn(selectedColumn);
                    return true;
                }
            } else if (event.modifiers & Qt.ControlModifier) {
                if (selectedColumn >= 0) {
                    const target = selectedColumn + dir;
                    if (target >= 0 && target < columnCount) {
                        event.accepted = true;
                        templateModel.moveColumn(selectedColumn, target);
                        selectedColumn = target;
                        revealColumn(target);
                        return true;
                    }
                }
            } else if (columnCount > 0) {
                event.accepted = true;
                selectedColumn = Math.max(0, Math.min(columnCount - 1, selectedColumn < 0 ? 0 : selectedColumn + dir));
                revealColumn(selectedColumn);
                return true;
            }
            return false;
        }
        if (event.key === Qt.Key_Delete && selectedColumn >= 0) {
            event.accepted = true;
            templateModel.removeColumn(selectedColumn);
            return true;
        }
        return false;
    }

    // ─── Empty state ─────────────────────────────────────────────────────
    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        // Floored at zero: a canvas narrower than the inset would otherwise
        // hand the message a negative width.
        width: Math.max(0, parent.width - Kirigami.Units.gridUnit * 8)
        visible: stripCanvas.columnCount === 0
        icon.name: stripCanvas.verticalAxis ? "view-split-top-bottom" : "view-split-left-right"
        text: i18nc("@info:placeholder", "This template starts no columns")
        explanation: i18nc("@info:placeholder", "The first windows you open form the starting columns, in order along the strip. Later windows use the default width from the panel. A template without starting columns only sets the width presets.")

        helpfulAction: Kirigami.Action {
            text: i18nc("@action:button", "Add Column")
            icon.name: "list-add"
            enabled: stripCanvas.templateModel !== null
            onTriggered: {
                stripCanvas.templateModel.addColumn();
                stripCanvas.selectedColumn = 0;
            }
        }
    }

    // ─── Strip ───────────────────────────────────────────────────────────
    Flickable {
        id: stripFlickable

        anchors.fill: parent
        visible: stripCanvas.columnCount > 0
        // Content along the strip is the bands plus the last column's divider
        // gap; adding columns lives in the ControlBar, so a strip that exactly
        // fills the screen has nothing to scroll (bands give up one gap of
        // width, so the sum still lands exactly on the viewport extent).
        // Across the strip the content is the viewport, so that axis never
        // scrolls whichever way the strip runs.
        contentWidth: stripCanvas.verticalAxis ? width : Math.max(stripGrid.width + stripCanvas.bandSpacing, width)
        contentHeight: stripCanvas.verticalAxis ? Math.max(stripGrid.height + stripCanvas.bandSpacing, height) : height
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        // Only genuine overflow (summed widths past 100%) scrolls; the bar
        // appears just then so the excess is discoverable. An appended
        // column's reveal waits for this signal so it scrolls against the
        // repolished content size, not the stale one. Both handlers exist
        // because either can be the main axis, and each ignores the call when
        // it is currently the CROSS one — a cross-axis content change is just
        // the viewport resizing and must not consume a pending reveal.
        function consumeTailReveal(fromVerticalAxis) {
            if (!stripCanvas._revealTailPending || fromVerticalAxis !== stripCanvas.verticalAxis)
                return;

            stripCanvas._revealTailPending = false;
            const viewport = stripCanvas.verticalAxis ? height : width;
            const content = stripCanvas.verticalAxis ? contentHeight : contentWidth;
            stripCanvas._setMainOffset(Math.max(0, content - viewport));
        }

        onContentWidthChanged: consumeTailReveal(false)
        onContentHeightChanged: consumeTailReveal(true)
        ScrollBar.horizontal: ScrollBar {
            policy: stripCanvas.verticalAxis ? ScrollBar.AlwaysOff : ScrollBar.AsNeeded
        }
        ScrollBar.vertical: ScrollBar {
            policy: stripCanvas.verticalAxis ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }

        // Deselect on a click that lands between or past the bands, and hand
        // key focus back to the drawing area (the CanvasMouseHandler
        // contract, applied to the mode that replaced it).
        TapHandler {
            onTapped: {
                stripCanvas.selectedColumn = -1;
                stripCanvas.keyFocusTarget.forceActiveFocus();
            }
        }

        // Grid rather than Row/Column so one positioner serves both axes: a
        // single row lays the bands along x, a single column lays them along
        // y. Both dimensions are given explicitly — Grid's defaults would
        // otherwise wrap the bands into a real grid.
        Grid {
            id: stripGrid

            readonly property int bandCount: Math.max(1, stripCanvas.columnCount)

            rows: stripCanvas.verticalAxis ? bandCount : 1
            columns: stripCanvas.verticalAxis ? 1 : bandCount
            // Cross extent only: the main extent stays implicit so the
            // positioner sizes itself to the bands, which is what the
            // Flickable's content size is measured from.
            width: stripCanvas.verticalAxis ? stripFlickable.width : implicitWidth
            height: stripCanvas.verticalAxis ? implicitHeight : stripFlickable.height
            spacing: stripCanvas.bandSpacing

            Repeater {
                id: bandsRepeater

                model: stripCanvas.columns

                delegate: Rectangle {
                    id: band

                    required property var modelData
                    required property int index

                    readonly property bool isSelected: stripCanvas.selectedColumn === index
                    readonly property bool isTabbed: modelData.display === 1
                    readonly property real widthFraction: modelData.width
                    // The zone-canvas invariant, in miniature: NOTHING writes
                    // the C++ model between press and release of a width
                    // drag. The columns property is a QVariantList, so any
                    // model write resets the Repeater and destroys every
                    // delegate — including the MouseArea holding the grab.
                    // During a drag the width renders from dragFraction,
                    // written locally per move; the single model commit
                    // happens on release, after the grab has ended.
                    property bool resizing: false
                    property real dragFraction: 0
                    readonly property real displayFraction: resizing ? dragFraction : widthFraction

                    // An axis flip re-bases every coordinate the gesture is
                    // measured in: startMain was captured along the OLD main
                    // axis and the next move would read the new one, so the
                    // delta would span two bases. Discard the gesture instead,
                    // the same thing a cancelled grab does. No UI path reaches
                    // this today (the axis re-resolves on a target-screen
                    // change or a settings reload, and the drag holds the
                    // grab), so this is a guard, not a live fix.
                    Connections {
                        function onVerticalAxisChanged() {
                            band.resizing = false;
                        }

                        target: stripCanvas
                    }

                    // Extent along the strip. No floor: the rendered edge must
                    // stay exactly at fraction * the canvas's main extent or
                    // the drag math and the screen-edge marker both lie. A
                    // 5%-minimum band on a short canvas renders small and
                    // clips its caption; that is the honest rendering.
                    readonly property real mainSize: Math.max(displayFraction * stripCanvas.mainExtent - stripCanvas.bandSpacing, 0)

                    // NO clip here: the drag handle is a child positioned
                    // OUTSIDE the band's bounds (in the gap after it), and a
                    // clipping band would erase it and its hit area. The
                    // caption clips inside the content column instead.
                    width: stripCanvas.verticalAxis ? stripGrid.width : mainSize
                    height: stripCanvas.verticalAxis ? mainSize : stripGrid.height
                    radius: Kirigami.Units.smallSpacing * Theme.radiusMultiplier
                    color: isSelected ? Theme.withAlpha(Kirigami.Theme.highlightColor, 0.25) : Theme.withAlpha(Kirigami.Theme.alternateBackgroundColor, 0.6)
                    border.width: isSelected ? 2 : 1
                    border.color: isSelected ? Kirigami.Theme.highlightColor : stripCanvas.frameBorderColor

                    Accessible.role: Accessible.Button
                    // One msgid with the display mode substituted, so
                    // translators translate the sentence once; selection is
                    // exposed as checked state (the TopBar screen-button
                    // precedent) because every action and key operates on it.
                    Accessible.name: i18nc("@info accessible column summary; %3 is Stacked or Tabbed", "Column %1, %2% wide, %3", index + 1, Math.round(band.displayFraction * 100), band.isTabbed ? i18nc("@info column display", "Tabbed") : i18nc("@info column display", "Stacked"))
                    Accessible.checkable: true
                    Accessible.checked: band.isSelected
                    Accessible.onPressAction: {
                        stripCanvas.selectColumn(band.index);
                        stripCanvas.keyFocusTarget.forceActiveFocus();
                    }

                    MouseArea {
                        id: bandArea

                        anchors.fill: parent
                        hoverEnabled: true
                        // Selection on CLICK, not press: on an overflowing
                        // strip the Flickable may turn a press-and-move into
                        // a pan, and a pan must not also change the
                        // selection.
                        onClicked: {
                            stripCanvas.selectColumn(band.index);
                            stripCanvas.keyFocusTarget.forceActiveFocus();
                        }
                    }

                    // Display-mode sketch: tabbed columns show one window with
                    // a segmented tab bar, stacked columns show two windows
                    // divided along the CROSS axis. The whole sketch depicts
                    // the within-column stack, so it runs across the strip and
                    // transposes with it — on a vertical strip the tab bar is
                    // a rail down one side and the two windows sit side by
                    // side.
                    GridLayout {
                        anchors.fill: parent
                        anchors.margins: Kirigami.Units.largeSpacing
                        // Column count alone drives the transposition: this is
                        // Layouts, not the Grid positioner above, and its
                        // default LeftToRight flow reads only `columns`.
                        columns: stripCanvas.verticalAxis ? 3 : 1
                        rowSpacing: Kirigami.Units.smallSpacing
                        columnSpacing: Kirigami.Units.smallSpacing
                        // The caption must not overflow a narrow band now
                        // that the width floor is gone; the clip lives here
                        // so the out-of-bounds drag handle stays untouched.
                        clip: true

                        GridLayout {
                            Layout.fillWidth: !stripCanvas.verticalAxis
                            Layout.fillHeight: stripCanvas.verticalAxis
                            visible: band.isTabbed
                            // Column count alone, as above.
                            columns: stripCanvas.verticalAxis ? 1 : 3
                            rowSpacing: Kirigami.Units.smallSpacing / 2
                            columnSpacing: Kirigami.Units.smallSpacing / 2

                            Repeater {
                                model: 3

                                Rectangle {
                                    required property int index

                                    readonly property real pillThickness: Kirigami.Units.smallSpacing * 2

                                    Layout.fillWidth: !stripCanvas.verticalAxis
                                    Layout.fillHeight: stripCanvas.verticalAxis
                                    Layout.preferredWidth: stripCanvas.verticalAxis ? pillThickness : -1
                                    Layout.preferredHeight: stripCanvas.verticalAxis ? -1 : pillThickness
                                    radius: Math.min(width, height) / 2
                                    color: Theme.withAlpha(Kirigami.Theme.textColor, index === 0 ? 0.5 : 0.2)
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: Kirigami.Units.smallSpacing
                            color: Theme.withAlpha(Kirigami.Theme.textColor, 0.08)
                            border.width: 1
                            border.color: Theme.withAlpha(Kirigami.Theme.textColor, 0.15)

                            ColumnLayout {
                                anchors.centerIn: parent
                                spacing: 0

                                Label {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: i18nc("@info column width percentage", "%1%", Math.round(band.displayFraction * 100))
                                    font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.6
                                    font.weight: Font.DemiBold
                                }

                                Label {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: i18nc("@info column caption", "Column %1", band.index + 1)
                                    color: Kirigami.Theme.disabledTextColor
                                }

                                Label {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: band.isTabbed ? i18nc("@info column display", "Tabbed") : i18nc("@info column display", "Stacked")
                                    color: Kirigami.Theme.disabledTextColor
                                    font: Kirigami.Theme.smallFont
                                }
                            }
                        }

                        // The second stacked window, a quarter of the column's
                        // CROSS extent, so it divides the stack the same way
                        // whichever axis that stack runs on.
                        Rectangle {
                            Layout.fillWidth: !stripCanvas.verticalAxis
                            Layout.fillHeight: stripCanvas.verticalAxis
                            Layout.preferredWidth: stripCanvas.verticalAxis ? band.width * 0.25 : -1
                            Layout.preferredHeight: stripCanvas.verticalAxis ? -1 : band.height * 0.25
                            visible: !band.isTabbed
                            radius: Kirigami.Units.smallSpacing
                            color: Theme.withAlpha(Kirigami.Theme.textColor, 0.08)
                            border.width: 1
                            border.color: Theme.withAlpha(Kirigami.Theme.textColor, 0.15)
                        }
                    }

                    // Column action buttons: reorder, flip display, remove.
                    // Same component, size, spacing, placement, and
                    // hover-or-selected reveal as the zone canvas's
                    // ActionButtons so the two modes read as one editor.
                    Row {
                        id: bandActions

                        readonly property real buttonSize: Kirigami.Units.gridUnit * 2.5
                        // Hide rather than overflow when the band is too small
                        // to hold the row, mirroring ActionButtons' fit gate.
                        // The row stays a horizontal Row on both axes (it is
                        // chrome, not a picture of the layout), so BOTH band
                        // dimensions are checked — on a vertical strip the
                        // band is wide but can be short.
                        readonly property real requiredWidth: 4 * buttonSize + 3 * spacing + 2 * anchors.margins
                        readonly property real requiredHeight: buttonSize + 2 * anchors.margins

                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: Kirigami.Units.smallSpacing
                        spacing: Kirigami.Units.smallSpacing
                        // The buttons are hover items, so the moment the
                        // cursor reaches one of them bandArea.containsMouse
                        // drops and the row would hide under the cursor on an
                        // unselected band — the same class the zone canvas
                        // fixed by ORing its buttons' hover back in
                        // (EditorZone.anyButtonHovered). A HoverHandler on the
                        // row observes without taking the grab, so one term
                        // covers all four buttons and their spacing gaps.
                        visible: (band.isSelected || bandArea.containsMouse || bandActionsHover.hovered) && band.width >= requiredWidth && band.height >= requiredHeight
                        z: 2

                        HoverHandler {
                            id: bandActionsHover
                        }

                        ZoneActionButton {
                            buttonSize: bandActions.buttonSize
                            iconSource: stripCanvas.verticalAxis ? "arrow-up" : "arrow-left"
                            enabled: band.index > 0
                            opacity: enabled ? 1 : 0.4
                            accessibleName: i18nc("@action:button", "Move column toward the strip start")
                            accessibleDescription: i18nc("@info:tooltip", "Swap this column with the previous one")
                            tooltipText: i18nc("@tooltip", "Move toward start")
                            onClicked: {
                                // Hoist before the move: the model write
                                // rebuilds the delegates, so band.index must
                                // not be read after it.
                                const from = band.index;
                                stripCanvas.templateModel.moveColumn(from, from - 1);
                                stripCanvas.selectedColumn = from - 1;
                            }
                        }

                        ZoneActionButton {
                            buttonSize: bandActions.buttonSize
                            iconSource: band.isTabbed ? "view-list-details" : "tab-new"
                            accessibleName: band.isTabbed ? i18nc("@action:button", "Show windows stacked") : i18nc("@action:button", "Show windows as tabs")
                            accessibleDescription: i18nc("@info:tooltip", "Switch this column between stacked windows and tabs")
                            tooltipText: band.isTabbed ? i18nc("@tooltip", "Show windows stacked") : i18nc("@tooltip", "Show windows as tabs")
                            onClicked: stripCanvas.templateModel.setColumnDisplay(band.index, band.isTabbed ? 0 : 1)
                        }

                        ZoneActionButton {
                            buttonSize: bandActions.buttonSize
                            iconSource: stripCanvas.verticalAxis ? "arrow-down" : "arrow-right"
                            enabled: band.index < stripCanvas.columnCount - 1
                            opacity: enabled ? 1 : 0.4
                            accessibleName: i18nc("@action:button", "Move column toward the strip end")
                            accessibleDescription: i18nc("@info:tooltip", "Swap this column with the next one")
                            tooltipText: i18nc("@tooltip", "Move toward end")
                            onClicked: {
                                // Same hoist rationale as move-toward-start.
                                const from = band.index;
                                stripCanvas.templateModel.moveColumn(from, from + 1);
                                stripCanvas.selectedColumn = from + 1;
                            }
                        }

                        ZoneActionButton {
                            buttonSize: bandActions.buttonSize
                            iconSource: "edit-delete"
                            useNegativeColor: true
                            accessibleName: i18nc("@action:button", "Remove column")
                            accessibleDescription: i18nc("@info:tooltip", "Remove this column from the template")
                            tooltipText: i18nc("@tooltip", "Remove column")
                            onClicked: stripCanvas.templateModel.removeColumn(band.index)
                        }
                    }

                    // Width drag handle, drawn in the gap after this column
                    // the way DividerHandle sits between zones: a rounded bar
                    // spanning the column's cross extent, with grip dots and a
                    // centre line, subtle at rest and highlighted on
                    // hover/drag. Strip semantics still apply — only this
                    // column resizes, later columns shift along the strip —
                    // which is why every column gets one, the last included.
                    //
                    // "After this column" is along the STRIP, so the handle
                    // sits past the band's far main edge and lies across the
                    // cross one; both anchor pairs flip together with the
                    // axis.
                    Rectangle {
                        id: dragHandle

                        // The handle fills the gap it lives in, which is a
                        // full grid unit (see bandSpacing).
                        readonly property real handleThickness: stripCanvas.bandSpacing
                        readonly property bool isDragging: band.resizing

                        anchors.horizontalCenter: stripCanvas.verticalAxis ? parent.horizontalCenter : parent.right
                        anchors.horizontalCenterOffset: stripCanvas.verticalAxis ? 0 : stripCanvas.bandSpacing / 2
                        anchors.verticalCenter: stripCanvas.verticalAxis ? parent.bottom : parent.verticalCenter
                        anchors.verticalCenterOffset: stripCanvas.verticalAxis ? stripCanvas.bandSpacing / 2 : 0
                        // Floored at zero: the cross extent is the band's, and
                        // a band narrower than one gap would otherwise give
                        // the handle a negative size.
                        readonly property real handleSpan: Math.max(0, (stripCanvas.verticalAxis ? parent.width : parent.height) - stripCanvas.bandSpacing)

                        width: stripCanvas.verticalAxis ? handleSpan : handleThickness
                        height: stripCanvas.verticalAxis ? handleThickness : handleSpan
                        radius: Math.min(width, height) / 2
                        color: (handleArea.containsMouse || dragHandle.isDragging) ? Theme.withAlpha(Kirigami.Theme.highlightColor, dragHandle.isDragging ? 0.4 : 0.25) : Theme.withAlpha(Kirigami.Theme.backgroundColor, 0.3)
                        border.color: (handleArea.containsMouse || dragHandle.isDragging) ? Kirigami.Theme.highlightColor : stripCanvas.frameBorderColor
                        border.width: dragHandle.isDragging ? 2 : (handleArea.containsMouse ? 1 : 0)
                        z: 3

                        Behavior on color {
                            PhosphorMotionAnimation {
                                profile: "widget.hover"
                                durationOverride: Kirigami.Units.longDuration
                            }
                        }

                        Behavior on border.color {
                            PhosphorMotionAnimation {
                                profile: "widget.hover"
                                durationOverride: Kirigami.Units.longDuration
                            }
                        }

                        // Grip dots, the DividerHandle pattern.
                        Item {
                            id: gripPattern

                            readonly property real dotSize: Kirigami.Units.smallSpacing * 0.75
                            readonly property real dotSpacing: dotSize * 2.5
                            // Dots run along the handle's LONG side, which is
                            // the band's cross extent — height on a horizontal
                            // strip, width on a vertical one.
                            readonly property real dotRun: stripCanvas.verticalAxis ? width : height
                            readonly property int dotCount: Math.max(3, Math.min(7, Math.floor(dotRun / dotSpacing)))

                            anchors.fill: parent
                            anchors.margins: Kirigami.Units.smallSpacing

                            Repeater {
                                model: gripPattern.dotCount

                                Rectangle {
                                    required property int index

                                    readonly property real alongOffset: index * gripPattern.dotSpacing + (gripPattern.dotRun - (gripPattern.dotCount - 1) * gripPattern.dotSpacing) / 2 - gripPattern.dotSize / 2

                                    x: stripCanvas.verticalAxis ? alongOffset : gripPattern.width / 2 - gripPattern.dotSize / 2
                                    y: stripCanvas.verticalAxis ? gripPattern.height / 2 - gripPattern.dotSize / 2 : alongOffset
                                    width: gripPattern.dotSize
                                    height: gripPattern.dotSize
                                    radius: gripPattern.dotSize / 2
                                    color: (handleArea.containsMouse || dragHandle.isDragging) ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor
                                    opacity: (handleArea.containsMouse || dragHandle.isDragging) ? 0.9 : 0.5

                                    Behavior on opacity {
                                        PhosphorMotionAnimation {
                                            profile: "widget.hover"
                                        }
                                    }

                                    Behavior on color {
                                        PhosphorMotionAnimation {
                                            profile: "widget.hover"
                                        }
                                    }
                                }
                            }
                        }

                        // Centre line indicator, matching DividerHandle. It
                        // runs along the handle, so it is thin across and long
                        // down the handle's own long side.
                        Rectangle {
                            anchors.centerIn: parent
                            width: stripCanvas.verticalAxis ? parent.width * 0.6 : 2
                            height: stripCanvas.verticalAxis ? 2 : parent.height * 0.6
                            radius: Math.round(Kirigami.Units.smallSpacing / 4)
                            color: (handleArea.containsMouse || dragHandle.isDragging) ? Kirigami.Theme.highlightColor : stripCanvas.frameBorderColor
                            opacity: (handleArea.containsMouse || dragHandle.isDragging) ? 0.8 : 0.4

                            Behavior on opacity {
                                PhosphorMotionAnimation {
                                    profile: "widget.hover"
                                }
                            }
                        }

                        MouseArea {
                            id: handleArea

                            // The gesture's coordinate ALONG the strip, in
                            // content space.
                            property real startMain: 0
                            property real startFraction: 0

                            function mainCoordOf(mouse) {
                                const p = mapToItem(stripGrid, mouse.x, mouse.y);
                                return stripCanvas.verticalAxis ? p.y : p.x;
                            }

                            anchors.fill: parent
                            anchors.margins: -Kirigami.Units.smallSpacing
                            hoverEnabled: true
                            cursorShape: stripCanvas.verticalAxis ? Qt.SizeVerCursor : Qt.SizeHorCursor
                            preventStealing: true
                            Accessible.role: Accessible.Slider
                            Accessible.name: i18nc("@action:button", "Column width divider")
                            Accessible.description: i18nc("@info:tooltip", "Drag along the strip to resize this column")
                            // Coordinates are captured in CONTENT space
                            // (stripGrid), not viewport space: shrinking a
                            // late column can make the Flickable re-clamp its
                            // scroll offset, which moves the viewport under
                            // the cursor and would corrupt a viewport-space
                            // delta.
                            onPressed: mouse => {
                                stripCanvas.selectColumn(band.index);
                                stripCanvas.keyFocusTarget.forceActiveFocus();
                                startMain = mainCoordOf(mouse);
                                startFraction = band.widthFraction;
                                band.dragFraction = startFraction;
                                band.resizing = true;
                            }
                            onPositionChanged: mouse => {
                                if (!band.resizing)
                                    return;

                                const nowMain = mainCoordOf(mouse);
                                const fraction = startFraction + (nowMain - startMain) / stripCanvas.mainExtent;
                                band.dragFraction = Math.max(stripCanvas.minFraction, Math.min(stripCanvas.maxFraction, fraction));
                            }
                            // The one model write of the gesture, after the
                            // grab has ended — see the invariant note on the
                            // band's drag properties. Synchronous on purpose,
                            // unlike PresetChipEditor's deferred commits:
                            // onReleased runs after the grab ended and Qt
                            // releases delegates via deleteLater, whereas the
                            // chip commits fire INSIDE active focus/accepted
                            // signal emissions, which is the UAF shape. A
                            // CANCELLED grab (window deactivation stealing
                            // it) deliberately commits nothing: the band
                            // snaps back to its stored width, the same
                            // discard a cancelled zone drag performs.
                            onReleased: {
                                if (!band.resizing)
                                    return;

                                band.resizing = false;
                                stripCanvas.templateModel.setColumnWidth(band.index, band.dragFraction, false);
                            }
                            onCanceled: band.resizing = false
                        }
                    }
                }
            }
        }
    }

    // Screen-edge marker: everything past this line starts off screen, the way
    // the real strip scrolls columns past the monitor's edge. In content space
    // 100% of the screen lands at mainExtent - bandSpacing (each band gives up
    // one gap), and the caption sits on the NEAR side of the line so it stays
    // on the canvas instead of under the property panel.
    Rectangle {
        id: screenEdgeMarker

        // Position ALONG the strip, in viewport space. Sits OUTSIDE the
        // clipped Flickable, so it must hide itself once scrolling would carry
        // it past the canvas's near edge — the offset goes negative there and
        // it would otherwise paint over the inset margins.
        readonly property real mainPos: stripCanvas.mainExtent - stripCanvas.bandSpacing - stripCanvas._mainOffset() - 1
        readonly property real mainContent: stripCanvas.verticalAxis ? stripFlickable.contentHeight : stripFlickable.contentWidth
        readonly property real mainViewport: stripCanvas.verticalAxis ? stripFlickable.height : stripFlickable.width

        x: stripCanvas.verticalAxis ? 0 : mainPos
        y: stripCanvas.verticalAxis ? mainPos : 0
        width: stripCanvas.verticalAxis ? parent.width : 2
        height: stripCanvas.verticalAxis ? 2 : parent.height
        visible: stripFlickable.visible && mainContent > mainViewport && mainPos >= 0
        color: Theme.withAlpha(Kirigami.Theme.negativeTextColor, 0.5)

        Label {
            anchors.top: stripCanvas.verticalAxis ? undefined : parent.top
            anchors.bottom: stripCanvas.verticalAxis ? parent.top : undefined
            anchors.left: stripCanvas.verticalAxis ? parent.left : undefined
            anchors.right: stripCanvas.verticalAxis ? undefined : parent.left
            anchors.margins: Kirigami.Units.smallSpacing
            // The caption hangs on the NEAR side of the line, so it needs its
            // own gate: for a small positive marker offset it would otherwise
            // paint past the canvas edge into the inset margins.
            visible: screenEdgeMarker.mainPos >= (stripCanvas.verticalAxis ? implicitHeight : implicitWidth) + Kirigami.Units.smallSpacing
            text: i18nc("@info marker caption", "Screen edge")
            color: Kirigami.Theme.negativeTextColor
            font: Kirigami.Theme.smallFont
        }
    }
}

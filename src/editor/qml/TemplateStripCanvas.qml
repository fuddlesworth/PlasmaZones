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
 * Renders the template's seed blueprint as full-height column bands, the way
 * the scrolling engine lays the first windows out. Column widths are
 * fractions of the screen width, so the canvas width IS the screen: bands
 * whose summed widths pass the right edge scroll horizontally, exactly like
 * the real strip. Drag the divider after a column to resize it (strip
 * semantics: neighbours keep their width and shift, nothing redistributes),
 * click a column to select it, and use its action buttons to reorder, flip
 * stacked or tabbed, or remove. Adding columns lives in the ControlBar.
 */
Item {
    id: stripCanvas

    required property var editorController
    // The scrolling-template sub-model (editorController.scrollingTemplate)
    readonly property var templateModel: editorController ? editorController.scrollingTemplate : null

    // Selection is index-based on purpose: blueprint columns are an ordered
    // list without identities, unlike zones.
    property int selectedColumn: -1

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
    // Set when an append should scroll the tail into view; consumed by
    // onContentWidthChanged (which fires after the Row has repolished, so
    // the scroll target is not one band stale) with a callLater fallback
    // for appends that do not change contentWidth.
    property bool _revealTailPending: false

    Component.onCompleted: _lastColumnCount = columnCount

    // A different template was loaded over this canvas: rewind the viewport
    // and make sure the count change is not mistaken for an append.
    Connections {
        function onLayoutIdChanged() {
            stripCanvas._lastColumnCount = -1;
            stripFlickable.contentX = 0;
        }

        target: stripCanvas.editorController
        enabled: stripCanvas.editorController !== null
    }

    // Keep the selection inside the list when columns are removed or the
    // template is reloaded underneath us, and reveal a column appended while
    // the strip's tail is scrolled off screen (the ControlBar's Add Column
    // lands at the end, which can lie past the right edge).
    onColumnCountChanged: {
        if (selectedColumn >= columnCount)
            selectedColumn = columnCount - 1;
        if (_lastColumnCount >= 0 && columnCount > _lastColumnCount) {
            _revealTailPending = true;
            Qt.callLater(function () {
                if (stripCanvas._revealTailPending) {
                    stripCanvas._revealTailPending = false;
                    stripFlickable.contentX = Math.max(0, stripFlickable.contentWidth - stripFlickable.width);
                }
            });
        }
        _lastColumnCount = columnCount;
    }

    function selectColumn(index) {
        selectedColumn = (index >= 0 && index < columnCount) ? index : -1;
    }

    // Keyboard handling, called from EditorWindow's canvas key handler while
    // template mode is active. Left/Right select, Shift+Left/Right resize,
    // Ctrl+Left/Right reorder, Delete removes.
    function handleKeyPress(event) {
        if (!templateModel)
            return false;

        const step = 0.01;
        if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
            const dir = event.key === Qt.Key_Right ? 1 : -1;
            event.accepted = true;
            if (event.modifiers & Qt.ShiftModifier) {
                if (selectedColumn >= 0) {
                    // One undo entry per held key: the first press opens a
                    // gesture, autorepeat merges into it.
                    if (!event.isAutoRepeat)
                        templateModel.beginWidthDrag();
                    const current = columns[selectedColumn].width;
                    templateModel.setColumnWidth(selectedColumn, current + dir * step, true);
                }
            } else if (event.modifiers & Qt.ControlModifier) {
                if (selectedColumn >= 0) {
                    const target = selectedColumn + dir;
                    if (target >= 0 && target < columnCount) {
                        templateModel.moveColumn(selectedColumn, target);
                        selectedColumn = target;
                    }
                }
            } else {
                if (columnCount > 0)
                    selectedColumn = Math.max(0, Math.min(columnCount - 1, selectedColumn < 0 ? 0 : selectedColumn + dir));
            }
            return true;
        }
        if (event.key === Qt.Key_Delete && selectedColumn >= 0) {
            event.accepted = true;
            templateModel.removeColumn(selectedColumn);
            return true;
        }
        event.accepted = false;
        return false;
    }

    // ─── Empty state ─────────────────────────────────────────────────────
    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        width: parent.width - Kirigami.Units.gridUnit * 8
        visible: stripCanvas.columnCount === 0
        icon.name: "view-split-left-right"
        text: i18n("This template starts no columns")
        explanation: i18n("The first windows you open form the starting columns, left to right. Later windows use the default width from the panel. A template without starting columns only sets the width presets.")

        helpfulAction: Kirigami.Action {
            text: i18n("Add Column")
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
        // Content is the bands plus the last column's divider gap; adding
        // columns lives in the ControlBar, so a strip that exactly fills the
        // screen has nothing to scroll (bands give up one gap of width, so
        // the sum still lands exactly on the viewport width).
        contentWidth: Math.max(stripRow.width + stripCanvas.bandSpacing, width)
        contentHeight: height
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        // Only genuine overflow (summed widths past 100%) scrolls; the bar
        // appears just then so the excess is discoverable. An appended
        // column's reveal waits for this signal so it scrolls against the
        // repolished width, not the stale one.
        onContentWidthChanged: {
            if (stripCanvas._revealTailPending) {
                stripCanvas._revealTailPending = false;
                contentX = Math.max(0, contentWidth - width);
            }
        }
        ScrollBar.horizontal: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        // Deselect on a click that lands between or past the bands.
        TapHandler {
            onTapped: stripCanvas.selectedColumn = -1
        }

        Row {
            id: stripRow

            height: parent.height
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

                    width: Math.max(displayFraction * stripCanvas.width - stripCanvas.bandSpacing, Kirigami.Units.gridUnit * 3)
                    height: parent.height
                    radius: Kirigami.Units.smallSpacing * Theme.radiusMultiplier
                    color: isSelected ? Theme.withAlpha(Kirigami.Theme.highlightColor, 0.25) : Theme.withAlpha(Kirigami.Theme.alternateBackgroundColor, 0.6)
                    border.width: isSelected ? 2 : 1
                    border.color: isSelected ? Kirigami.Theme.highlightColor : stripCanvas.frameBorderColor

                    Accessible.role: Accessible.Button
                    Accessible.name: band.isTabbed ? i18n("Column %1, %2% wide, tabbed", index + 1, Math.round(band.displayFraction * 100)) : i18n("Column %1, %2% wide, stacked", index + 1, Math.round(band.displayFraction * 100))

                    MouseArea {
                        id: bandArea

                        anchors.fill: parent
                        hoverEnabled: true
                        // Selection on CLICK, not press: on an overflowing
                        // strip the Flickable may turn a press-and-move into
                        // a pan, and a pan must not also change the
                        // selection.
                        onClicked: stripCanvas.selectColumn(band.index)
                    }

                    // Display-mode sketch: tabbed columns show one window with
                    // a segmented tab bar, stacked columns show two windows on
                    // top of each other.
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Kirigami.Units.largeSpacing
                        spacing: Kirigami.Units.smallSpacing

                        RowLayout {
                            Layout.fillWidth: true
                            visible: band.isTabbed
                            spacing: Kirigami.Units.smallSpacing / 2

                            Repeater {
                                model: 3

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: Kirigami.Units.smallSpacing * 2
                                    radius: height / 2
                                    color: Theme.withAlpha(Kirigami.Theme.textColor, index === 0 ? 0.5 : 0.2)

                                    required property int index
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
                                    text: i18n("%1%", Math.round(band.displayFraction * 100))
                                    font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.6
                                    font.weight: Font.DemiBold
                                }

                                Label {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: i18n("Column %1", band.index + 1)
                                    color: Kirigami.Theme.disabledTextColor
                                }

                                Label {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: band.isTabbed ? i18n("Tabbed") : i18n("Stacked")
                                    color: Kirigami.Theme.disabledTextColor
                                    font: Kirigami.Theme.smallFont
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: band.height * 0.25
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
                        // Hide rather than overflow when the band is too
                        // narrow, mirroring ActionButtons' fit gate.
                        readonly property real requiredWidth: 4 * buttonSize + 3 * spacing + 2 * anchors.margins

                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: Kirigami.Units.smallSpacing
                        spacing: Kirigami.Units.smallSpacing
                        visible: (band.isSelected || bandArea.containsMouse) && band.width >= requiredWidth
                        z: 2

                        ZoneActionButton {
                            buttonSize: bandActions.buttonSize
                            iconSource: "arrow-left"
                            enabled: band.index > 0
                            opacity: enabled ? 1 : 0.4
                            accessibleName: i18nc("@action:button", "Move column left")
                            accessibleDescription: i18nc("@info:tooltip", "Swap this column with the one to its left")
                            tooltipText: i18nc("@tooltip", "Move left")
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
                            iconSource: "arrow-right"
                            enabled: band.index < stripCanvas.columnCount - 1
                            opacity: enabled ? 1 : 0.4
                            accessibleName: i18nc("@action:button", "Move column right")
                            accessibleDescription: i18nc("@info:tooltip", "Swap this column with the one to its right")
                            tooltipText: i18nc("@tooltip", "Move right")
                            onClicked: {
                                // Same hoist rationale as move-left.
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
                    // the way DividerHandle sits between zones: full-height
                    // rounded bar with grip dots and a centre line, subtle at
                    // rest and highlighted on hover/drag. Strip semantics
                    // still apply — only this column resizes, later columns
                    // shift along the strip — which is why every column gets
                    // one, the last included.
                    Rectangle {
                        id: dragHandle

                        readonly property real handleThickness: Math.max(stripCanvas.bandSpacing, Kirigami.Units.smallSpacing)
                        readonly property bool isDragging: band.resizing

                        anchors.horizontalCenter: parent.right
                        anchors.horizontalCenterOffset: stripCanvas.bandSpacing / 2
                        anchors.verticalCenter: parent.verticalCenter
                        width: handleThickness
                        height: parent.height - stripCanvas.bandSpacing
                        radius: width / 2
                        color: (handleArea.containsMouse || dragHandle.isDragging) ? Qt.alpha(Kirigami.Theme.highlightColor, dragHandle.isDragging ? 0.4 : 0.25) : Qt.alpha(Kirigami.Theme.backgroundColor, 0.3)
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
                            readonly property int dotCount: Math.max(3, Math.min(7, Math.floor(height / dotSpacing)))

                            anchors.fill: parent
                            anchors.margins: Kirigami.Units.smallSpacing

                            Repeater {
                                model: gripPattern.dotCount

                                Rectangle {
                                    required property int index

                                    x: gripPattern.width / 2 - gripPattern.dotSize / 2
                                    y: index * gripPattern.dotSpacing + (gripPattern.height - (gripPattern.dotCount - 1) * gripPattern.dotSpacing) / 2 - gripPattern.dotSize / 2
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

                        // Centre line indicator, matching DividerHandle.
                        Rectangle {
                            anchors.centerIn: parent
                            width: 2
                            height: parent.height * 0.6
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

                            property real startX: 0
                            property real startFraction: 0

                            anchors.fill: parent
                            anchors.margins: -Kirigami.Units.smallSpacing
                            hoverEnabled: true
                            cursorShape: Qt.SizeHorCursor
                            preventStealing: true
                            Accessible.role: Accessible.Slider
                            Accessible.name: i18nc("@action:button", "Column width divider")
                            Accessible.description: i18nc("@info:tooltip", "Drag horizontally to resize this column")
                            // Coordinates are captured in CONTENT space
                            // (stripRow), not viewport space: shrinking a
                            // late column can make the Flickable re-clamp
                            // contentX, which moves the viewport under the
                            // cursor and would corrupt a viewport-space
                            // delta.
                            onPressed: mouse => {
                                stripCanvas.selectColumn(band.index);
                                startX = mapToItem(stripRow, mouse.x, mouse.y).x;
                                startFraction = band.widthFraction;
                                band.dragFraction = startFraction;
                                band.resizing = true;
                            }
                            onPositionChanged: mouse => {
                                if (!band.resizing)
                                    return;

                                const nowX = mapToItem(stripRow, mouse.x, mouse.y).x;
                                const fraction = startFraction + (nowX - startX) / stripCanvas.width;
                                band.dragFraction = Math.max(stripCanvas.minFraction, Math.min(stripCanvas.maxFraction, fraction));
                            }
                            // The one model write of the gesture, after the
                            // grab has ended — see the invariant note on the
                            // band's drag properties.
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

    // Screen-edge marker: everything right of this line starts off screen,
    // the way the real strip scrolls columns past the monitor's edge. In
    // content space 100% of the screen lands at width - bandSpacing (each
    // band gives up one gap), and the caption sits LEFT of the line so it
    // stays on the canvas instead of under the property panel.
    Rectangle {
        x: stripCanvas.width - stripCanvas.bandSpacing - stripFlickable.contentX - 1
        width: 2
        height: parent.height
        visible: stripFlickable.visible && stripFlickable.contentWidth > stripFlickable.width
        color: Theme.withAlpha(Kirigami.Theme.negativeTextColor, 0.5)

        Label {
            anchors.top: parent.top
            anchors.right: parent.left
            anchors.margins: Kirigami.Units.smallSpacing
            text: i18n("Screen edge")
            color: Kirigami.Theme.negativeTextColor
            font: Kirigami.Theme.smallFont
        }
    }
}

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.plasmazones.common as QFZCommon

/**
 * Layout Picker Overlay - Interactive layout browser with keyboard navigation
 *
 * Full-screen transparent window with a centered card showing all available layouts.
 * Selecting a layout switches to it and resnaps all windows.
 *
 * Keyboard: Arrow keys move selection, Enter confirms, Escape dismisses.
 * Mouse: Click a layout to select, click outside to dismiss.
 */
Window {
    id: root

    // Layout data (array of layout objects with id, name, zones, category, autoAssign)
    property var layouts: []
    property string activeLayoutId: ""

    // Screen info for aspect ratio
    property real screenAspectRatio: 16 / 9
    readonly property real safeAspectRatio: Math.max(0.5, Math.min(4.0, screenAspectRatio))

    // Theme colors
    property color backgroundColor: Kirigami.Theme.backgroundColor
    property color textColor: Kirigami.Theme.textColor
    property color highlightColor: Kirigami.Theme.highlightColor

    // Font properties for zone number labels
    property string fontFamily: ""
    property real fontSizeScale: 1.0
    property int fontWeight: Font.Bold
    property bool fontItalic: false
    property bool fontUnderline: false
    property bool fontStrikeout: false

    // Current keyboard selection index — binding is intentionally broken on first
    // keyboard/mouse interaction; the picker is recreated each time so this is safe.
    property int selectedIndex: {
        for (var i = 0; i < layouts.length; i++) {
            if (layouts[i].id === activeLayoutId)
                return i;
        }
        return 0;
    }

    // Grid dimensions
    readonly property int layoutCount: layouts.length
    readonly property int gridColumns: Math.min(layoutCount, Math.max(3, Math.min(5, Math.ceil(Math.sqrt(layoutCount * 1.5)))))
    readonly property int gridRows: gridColumns > 0 ? Math.ceil(layoutCount / gridColumns) : 0

    // Layout constants — match ZoneSelectorLayout (zoneselectorlayout.h)
    QtObject {
        id: metrics

        // Container chrome
        readonly property int containerPadding: 36
        readonly property int paddingSide: 18
        readonly property int containerRadius: 12
        readonly property int indicatorSpacing: 18
        readonly property real backdropAlpha: 0.3

        // Card preview
        readonly property int previewWidth: 160

        // Show/hide animation
        readonly property int showDuration: Kirigami.Units.shortDuration
        readonly property int hideDuration: Math.round(Kirigami.Units.shortDuration * 0.8)
        readonly property real showScaleFrom: 0.9
        readonly property real hideScaleTo: 0.95
        readonly property real showOvershoot: 1.1
    }

    // Card dimensions
    readonly property int previewWidth: metrics.previewWidth
    readonly property int previewHeight: Math.round(previewWidth / safeAspectRatio)
    readonly property int cardWidth: previewWidth + metrics.paddingSide * 2
    readonly property int cardHeight: previewHeight + metrics.containerPadding + metrics.paddingSide
    readonly property int cardSpacing: metrics.indicatorSpacing

    // Signals
    signal layoutSelected(string layoutId)
    signal dismissed()

    // Window configuration
    flags: Qt.FramelessWindowHint | Qt.Tool
    color: "transparent"
    visible: false

    // Show with animation
    function show() {
        showAnimation.stop();
        hideAnimation.stop();
        contentWrapper.opacity = 0;
        container.scale = metrics.showScaleFrom;
        root.visible = true;
        showAnimation.start();
        root.requestActivate();
    }

    // Hide with animation
    function hide() {
        showAnimation.stop();
        if (root.visible) {
            hideAnimation.start();
        }
    }

    // Show animation
    ParallelAnimation {
        id: showAnimation

        NumberAnimation {
            target: contentWrapper
            property: "opacity"
            from: 0; to: 1
            duration: metrics.showDuration
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: container
            property: "scale"
            from: metrics.showScaleFrom; to: 1
            duration: metrics.showDuration
            easing.type: Easing.OutBack
            easing.overshoot: metrics.showOvershoot
        }
    }

    // Hide animation
    SequentialAnimation {
        id: hideAnimation

        ParallelAnimation {
            NumberAnimation {
                target: contentWrapper
                property: "opacity"
                to: 0
                duration: metrics.hideDuration
                easing.type: Easing.InCubic
            }
            NumberAnimation {
                target: container
                property: "scale"
                to: metrics.hideScaleTo
                duration: metrics.hideDuration
                easing.type: Easing.InCubic
            }
        }
        ScriptAction {
            script: {
                root.visible = false;
                root.dismissed();
            }
        }
    }

    // Keyboard handling (Escape is handled by C++ eventFilter for reliable Wayland support)
    Shortcut { sequence: "Return"; onActivated: confirmSelection() }
    Shortcut { sequence: "Enter"; onActivated: confirmSelection() }
    Shortcut { sequence: "Left"; onActivated: moveSelection(-1, 0) }
    Shortcut { sequence: "Right"; onActivated: moveSelection(1, 0) }
    Shortcut { sequence: "Up"; onActivated: moveSelection(0, -1) }
    Shortcut { sequence: "Down"; onActivated: moveSelection(0, 1) }

    function moveSelection(dx, dy) {
        if (layoutCount === 0) return;
        var col = selectedIndex % gridColumns;
        var row = Math.floor(selectedIndex / gridColumns);
        col = (col + dx + gridColumns) % gridColumns;
        row = (row + dy + gridRows) % gridRows;
        var newIndex = row * gridColumns + col;
        if (newIndex >= layoutCount) {
            // Clamp to last valid item in the target row
            var lastColInRow = Math.min(gridColumns, layoutCount - row * gridColumns) - 1;
            newIndex = row * gridColumns + Math.min(col, lastColInRow);
        }
        selectedIndex = Math.max(0, Math.min(layoutCount - 1, newIndex));
    }

    function confirmSelection() {
        if (selectedIndex >= 0 && selectedIndex < layoutCount) {
            var layout = layouts[selectedIndex];
            root.layoutSelected(layout.id);
        }
    }

    // Content wrapper for opacity animation (avoid Wayland setOpacity warning)
    Item {
        id: contentWrapper
        anchors.fill: parent
        opacity: 0

        // Backdrop - semi-transparent dim, click outside to close
        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(0, 0, 0, metrics.backdropAlpha)
            MouseArea {
                anchors.fill: parent
                onClicked: root.hide()
            }
        }

        // Main container card
        QFZCommon.PopupFrame {
            id: container

            anchors.centerIn: parent
            width: gridView.width + metrics.containerPadding
            // top padding + title + gap below title + grid + bottom padding
            height: titleLabel.height + gridView.height + metrics.paddingSide * 3
            backgroundColor: root.backgroundColor
            textColor: root.textColor
            containerRadius: metrics.containerRadius

            // Absorb clicks inside container to prevent backdrop dismiss
            MouseArea {
                anchors.fill: parent
                onClicked: function(mouse) { mouse.accepted = true; }
            }

            // Title
            Label {
                id: titleLabel
                anchors.top: parent.top
                anchors.topMargin: metrics.paddingSide
                anchors.horizontalCenter: parent.horizontalCenter
                text: i18n("Choose Layout")
                font.pixelSize: Kirigami.Theme.defaultFont.pixelSize * 1.4
                font.weight: Font.DemiBold
                color: root.textColor
            }

            // Layout grid
            Grid {
                id: gridView

                anchors.top: titleLabel.bottom
                anchors.topMargin: metrics.paddingSide
                anchors.horizontalCenter: parent.horizontalCenter
                columns: root.gridColumns
                spacing: root.cardSpacing

                Repeater {
                    model: root.layouts

                    Item {
                        id: layoutCard

                        property var layoutData: modelData
                        property bool isSelected: index === root.selectedIndex
                        property bool isActive: layoutData.id === root.activeLayoutId
                        property bool isHovered: cardMouse.containsMouse

                        width: root.cardWidth
                        height: root.cardHeight

                        Accessible.role: Accessible.Button
                        Accessible.name: layoutData.name || ""
                        Accessible.focusable: true

                        QFZCommon.LayoutCard {
                            anchors.fill: parent
                            layoutData: layoutCard.layoutData
                            isActive: layoutCard.isActive
                            isSelected: layoutCard.isSelected
                            isHovered: layoutCard.isHovered
                            previewWidth: root.previewWidth
                            previewHeight: root.previewHeight

                            // Layout picker features
                            showCardBackground: true
                            interactive: false

                            // Theme
                            highlightColor: root.highlightColor
                            textColor: root.textColor
                            backgroundColor: root.backgroundColor

                            // Font
                            fontFamily: root.fontFamily
                            fontSizeScale: root.fontSizeScale
                            fontWeight: root.fontWeight
                            fontItalic: root.fontItalic
                            fontUnderline: root.fontUnderline
                            fontStrikeout: root.fontStrikeout

                            activeOpacity: 0.6
                            animationDuration: Kirigami.Units.shortDuration
                        }

                        MouseArea {
                            id: cardMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                root.selectedIndex = index;
                                root.confirmSelection();
                            }
                            onEntered: root.selectedIndex = index
                        }
                    }
                }
            }
        }
    }
}

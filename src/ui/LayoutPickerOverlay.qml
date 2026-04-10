// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import PlasmaZones 1.0
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
    readonly property real safeAspectRatio: Math.max(0.5, Math.min(4, screenAspectRatio))
    // Theme colors
    property color backgroundColor: Kirigami.Theme.backgroundColor
    property color textColor: Kirigami.Theme.textColor
    property color highlightColor: Kirigami.Theme.highlightColor
    // Zone appearance (set from C++ settings for consistency with zone selector)
    property color inactiveColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.4)
    property color borderColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.9)
    property real activeOpacity: 0.5
    property real inactiveOpacity: 0.3
    // Font properties for zone number labels
    property string fontFamily: ""
    property real fontSizeScale: 1
    property int fontWeight: Font.Bold
    property bool fontItalic: false
    property bool fontUnderline: false
    property bool fontStrikeout: false
    property bool locked: false
    // Animation profile properties (set from C++ before show)
    property int animInDuration: 150
    property string animInStyle: "scalein"
    property real animInStyleParam: 0.9
    property int animOutDuration: 120
    property string animOutStyle: "scalein"
    property real animOutStyleParam: 0.95
    // Shader source URLs (set from C++ via AnimationShaderRegistry)
    property url animInShaderSource: ""
    property var animInShaderParams: ({
    })
    property url animOutShaderSource: ""
    property var animOutShaderParams: ({
    })
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
    // Card dimensions
    readonly property int previewWidth: metrics.previewWidth
    readonly property int previewHeight: Math.round(previewWidth / safeAspectRatio)
    readonly property int cardWidth: previewWidth + metrics.paddingSide * 2
    readonly property int cardHeight: previewHeight + metrics.containerPadding + metrics.paddingSide
    readonly property int cardSpacing: metrics.indicatorSpacing

    // Signals
    signal layoutSelected(string layoutId)
    signal dismissed()

    // Show with animation (style-adaptive, supports shader transitions)
    function show() {
        showAnimation.stop();
        hideAnimation.stop();
        shaderShowAnim.stop();
        shaderHideAnim.stop();
        if (animInShaderSource.toString() !== "") {
            // ── Shader transition path ──
            contentWrapper.opacity = 1;
            container.scale = 1;
            contentWrapper.layer.enabled = true;
            shaderTransition.shaderSource = animInShaderSource;
            shaderTransition.direction = 0;
            shaderTransition.duration = animInDuration;
            shaderTransition.styleParam = animInStyleParam;
            shaderTransition.shaderParams = animInShaderParams || {
            };
            shaderTransition.progress = 0;
            shaderTransition.visible = true;
            root.visible = true;
            shaderShowAnim.duration = animInDuration;
            shaderShowAnim.start();
        } else {
            // ── Built-in QML animation path ──
            contentWrapper.opacity = 0;
            container.scale = (animInStyle === "scalein") ? animInStyleParam : 1;
            container.y = 0;
            showOpacityAnim.duration = animInDuration;
            showTransformAnim.duration = (animInStyle === "fadein" || animInStyle === "none") ? 0 : animInDuration;
            if (animInStyle === "scalein") {
                showTransformAnim.target = container;
                showTransformAnim.property = "scale";
                showTransformAnim.from = animInStyleParam;
                showTransformAnim.to = 1;
                showTransformAnim.easing.type = Easing.OutBack;
                showTransformAnim.easing.overshoot = 1.1;
            } else if (animInStyle === "slideup") {
                showTransformAnim.target = container;
                showTransformAnim.property = "y";
                showTransformAnim.from = animInStyleParam;
                showTransformAnim.to = 0;
                showTransformAnim.easing.type = Easing.OutCubic;
                showTransformAnim.easing.overshoot = 0;
            } else {
                showTransformAnim.duration = 0;
            }
            root.visible = true;
            showAnimation.start();
        }
        root.requestActivate();
    }

    // Hide with animation (style-adaptive, supports shader transitions)
    function hide() {
        showAnimation.stop();
        shaderShowAnim.stop();
        if (!root.visible)
            return ;

        if (animOutShaderSource.toString() !== "") {
            // ── Shader transition path ──
            contentWrapper.opacity = 1;
            contentWrapper.layer.enabled = true;
            shaderTransition.shaderSource = animOutShaderSource;
            shaderTransition.direction = 1;
            shaderTransition.duration = animOutDuration;
            shaderTransition.styleParam = animOutStyleParam;
            shaderTransition.shaderParams = animOutShaderParams || {
            };
            shaderTransition.progress = 1;
            shaderTransition.visible = true;
            shaderHideAnim.duration = animOutDuration;
            shaderHideAnim.start();
        } else {
            // ── Built-in QML animation path ──
            hideOpacityAnim.duration = animOutDuration;
            hideTransformAnim.duration = (animOutStyle === "fadein" || animOutStyle === "none") ? 0 : animOutDuration;
            if (animOutStyle === "scalein") {
                hideTransformAnim.target = container;
                hideTransformAnim.property = "scale";
                hideTransformAnim.to = animOutStyleParam;
                hideTransformAnim.easing.type = Easing.InCubic;
            } else if (animOutStyle === "slideup") {
                hideTransformAnim.target = container;
                hideTransformAnim.property = "y";
                hideTransformAnim.to = animOutStyleParam;
                hideTransformAnim.easing.type = Easing.InCubic;
            } else {
                hideTransformAnim.duration = 0;
            }
            hideAnimation.start();
        }
    }

    function moveSelection(dx, dy) {
        if (layoutCount === 0 || root.locked)
            return ;

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
        if (root.locked)
            return ;

        if (selectedIndex >= 0 && selectedIndex < layoutCount) {
            var layout = layouts[selectedIndex];
            root.layoutSelected(layout.id);
        }
    }

    // Window configuration
    flags: Qt.FramelessWindowHint | Qt.Tool
    color: "transparent"
    visible: false

    // Layout constants — match ZoneSelectorLayout (zoneselectorlayout.h)
    QtObject {
        id: metrics

        // Container chrome
        readonly property int containerPadding: Kirigami.Units.gridUnit * 2
        readonly property int paddingSide: Kirigami.Units.gridUnit
        readonly property int containerRadius: Kirigami.Units.largeSpacing * 2
        readonly property int indicatorSpacing: Kirigami.Units.gridUnit
        // Card preview
        readonly property int previewWidth: 160
    }

    // Show animation (configured dynamically in show())
    ParallelAnimation {
        id: showAnimation

        NumberAnimation {
            id: showOpacityAnim

            target: contentWrapper
            property: "opacity"
            from: 0
            to: 1
            duration: root.animInDuration
            easing.type: Easing.OutCubic
        }

        NumberAnimation {
            id: showTransformAnim

            target: container
            property: "scale"
            from: root.animInStyleParam
            to: 1
            duration: root.animInDuration
            easing.type: Easing.OutBack
            easing.overshoot: 1.1
        }

    }

    // Hide animation (configured dynamically in hide())
    SequentialAnimation {
        id: hideAnimation

        ParallelAnimation {
            NumberAnimation {
                id: hideOpacityAnim

                target: contentWrapper
                property: "opacity"
                to: 0
                duration: root.animOutDuration
                easing.type: Easing.InCubic
            }

            NumberAnimation {
                id: hideTransformAnim

                target: container
                property: "scale"
                to: root.animOutStyleParam
                duration: root.animOutDuration
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
    Shortcut {
        sequence: "Return"
        onActivated: confirmSelection()
    }

    Shortcut {
        sequence: "Enter"
        onActivated: confirmSelection()
    }

    Shortcut {
        sequence: "Left"
        onActivated: moveSelection(-1, 0)
    }

    Shortcut {
        sequence: "Right"
        onActivated: moveSelection(1, 0)
    }

    Shortcut {
        sequence: "Up"
        onActivated: moveSelection(0, -1)
    }

    Shortcut {
        sequence: "Down"
        onActivated: moveSelection(0, 1)
    }

    // ── Shader transition animations ──
    NumberAnimation {
        id: shaderShowAnim

        target: shaderTransition
        property: "progress"
        from: 0
        to: 1
        duration: root.animInDuration
        easing.type: Easing.OutCubic
        onFinished: {
            contentWrapper.layer.enabled = false;
            shaderTransition.visible = false;
        }
    }

    SequentialAnimation {
        id: shaderHideAnim

        NumberAnimation {
            target: shaderTransition
            property: "progress"
            from: 1
            to: 0
            duration: root.animOutDuration
            easing.type: Easing.InCubic
        }

        ScriptAction {
            script: {
                contentWrapper.layer.enabled = false;
                shaderTransition.visible = false;
                root.visible = false;
                root.dismissed();
            }
        }

    }

    AnimationShaderItem {
        id: shaderTransition

        anchors.fill: contentWrapper
        source: contentWrapper
        visible: false
        progress: 0
    }

    // Content wrapper for opacity animation (avoid Wayland setOpacity warning)
    Item {
        id: contentWrapper

        anchors.fill: parent
        opacity: 0

        // Backdrop — click outside to dismiss (transparent, no dim)
        MouseArea {
            anchors.fill: parent
            onClicked: root.hide()
            Accessible.name: i18n("Dismiss layout picker")
            Accessible.role: Accessible.Button
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
                onClicked: function(mouse) {
                    mouse.accepted = true;
                }
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
                            showMasterDot: layoutCard.layoutData.isAutotile === true && layoutCard.layoutData.supportsMasterCount === true
                            producesOverlappingZones: layoutCard.layoutData.producesOverlappingZones === true
                            zoneNumberDisplay: layoutCard.layoutData.zoneNumberDisplay || "all"
                            previewWidth: root.previewWidth
                            previewHeight: root.previewHeight
                            // Layout picker features
                            showCardBackground: true
                            interactive: false
                            // Zone appearance (consistent with zone selector)
                            zonePadding: 1
                            edgeGap: 1
                            minZoneSize: 8
                            zoneHighlightColor: root.highlightColor
                            zoneInactiveColor: root.inactiveColor
                            zoneBorderColor: root.borderColor
                            activeOpacity: root.activeOpacity
                            inactiveOpacity: root.inactiveOpacity
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
                            animationDuration: Kirigami.Units.shortDuration
                        }

                        // Lock overlay for non-active layouts — absorbs all mouse events
                        Rectangle {
                            anchors.fill: parent
                            visible: root.locked && !layoutCard.isActive
                            z: 100
                            color: Qt.rgba(0, 0, 0, 0.5)
                            radius: Kirigami.Units.largeSpacing

                            Kirigami.Icon {
                                anchors.centerIn: parent
                                source: "object-locked"
                                width: Math.min(parent.width, parent.height) * 0.3
                                height: width
                                color: "white"
                            }

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.ForbiddenCursor
                                onClicked: function(mouse) {
                                    mouse.accepted = true;
                                }
                                onPressed: function(mouse) {
                                    mouse.accepted = true;
                                }
                            }

                        }

                        MouseArea {
                            id: cardMouse

                            anchors.fill: parent
                            hoverEnabled: true
                            enabled: !(root.locked && !layoutCard.isActive)
                            cursorShape: root.locked && !layoutCard.isActive ? Qt.ForbiddenCursor : Qt.PointingHandCursor
                            onClicked: {
                                if (root.locked)
                                    return ;

                                root.selectedIndex = index;
                                root.confirmSelection();
                            }
                            onEntered: {
                                if (root.locked && !layoutCard.isActive)
                                    return ;

                                root.selectedIndex = index;
                            }
                        }

                    }

                }

            }

        }

    }

}

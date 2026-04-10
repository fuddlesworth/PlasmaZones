// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import PlasmaZones 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Window
import org.kde.kirigami as Kirigami

/**
 * Snap Assist Overlay - Aero Snap style window picker
 *
 * Displays empty zones with candidate window cards positioned inside each zone.
 * Each zone shows all available candidates (unsnapped including floated windows).
 * User clicks a candidate in a zone to snap that window to that zone.
 */
Window {
    // contentWrapper

    id: root

    property var emptyZones: []
    property var candidates: []
    property int screenWidth: 1920
    property int screenHeight: 1080
    // Zone appearance defaults (set from C++ when available; fallback matches ZoneOverlay)
    property color highlightColor: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.7)
    property color inactiveColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.4)
    property color borderColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.9)
    property real activeOpacity: 0.5
    property real inactiveOpacity: 0.3
    property int borderWidth: Kirigami.Units.smallSpacing
    property int borderRadius: Kirigami.Units.gridUnit
    // Layout constants (extracted from magic numbers for maintainability)
    readonly property real cardScaleBase: 0.35
    readonly property real cardWidthMultiplier: 2.2
    readonly property real minCardWidth: 100
    readonly property real minIconSize: 24
    readonly property real iconSizeRatio: 0.6
    readonly property real zoneSizeRefForFont: 200
    readonly property real minFontScale: 0.4
    readonly property real minFontPx: 8
    // Animation profile properties (set from C++ before show)
    property int animInDuration: 150
    property string animInStyle: "scalein"
    property real animInStyleParam: 0.9
    property int animOutDuration: 120
    property string animOutStyle: "scalein"
    property real animOutStyleParam: 0.95
    property url animInShaderSource: ""
    property var animInShaderParams: ({
    })
    property url animOutShaderSource: ""
    property var animOutShaderParams: ({
    })

    signal windowSelected(string windowId, string zoneId, string geometryJson)
    signal dismissed()

    // Show with animation
    function show() {
        showAnimation.stop();
        hideAnimation.stop();
        shaderShowAnim.stop();
        shaderHideAnim.stop();
        if (animInShaderSource.toString() !== "") {
            contentWrapper.opacity = 1;
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
            contentWrapper.opacity = 0;
            root.visible = true;
            showAnimation.start();
        }
        root.requestActivate();
    }

    // Hide with animation
    function hide() {
        showAnimation.stop();
        shaderShowAnim.stop();
        if (!root.visible)
            return ;

        if (animOutShaderSource.toString() !== "") {
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
            hideAnimation.start();
        }
    }

    flags: Qt.FramelessWindowHint | Qt.Tool
    color: "transparent"
    visible: false

    // Dismiss on Escape
    Shortcut {
        sequence: "Escape"
        onActivated: root.hide()
    }

    // Show animation (fade in)
    NumberAnimation {
        id: showAnimation

        target: contentWrapper
        property: "opacity"
        from: 0
        to: 1
        duration: root.animInDuration
        easing.type: Easing.OutCubic
    }

    // Hide animation (fade out)
    SequentialAnimation {
        id: hideAnimation

        NumberAnimation {
            target: contentWrapper
            property: "opacity"
            to: 0
            duration: root.animOutDuration
            easing.type: Easing.InCubic
        }

        ScriptAction {
            script: {
                root.visible = false;
                root.dismissed();
            }
        }

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

    // Content wrapper for animation
    Item {
        id: contentWrapper

        anchors.fill: parent

        // Backdrop - semi-transparent dim, click outside to close
        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(0, 0, 0, 0.25)

            MouseArea {
                anchors.fill: parent
                onClicked: root.hide()
                Accessible.name: i18n("Dismiss snap assist overlay")
                Accessible.role: Accessible.Button
            }

        }

        // Each zone shows all candidates; user picks any window to snap to any zone
        Repeater {
            model: root.emptyZones

            Item {
                id: zoneContainer

                property var zone: modelData

                x: zone ? zone.x : 0
                y: zone ? zone.y : 0
                width: zone ? zone.width : 0
                height: zone ? zone.height : 0
                visible: zone && zone.zoneId && root.candidates.length > 0

                // Zone background - matches main overlay colors/borders including zone overrides
                Rectangle {
                    id: zoneBg

                    // useCustomColors: zone override; else root (settings)
                    readonly property bool useCustom: zone && (zone.useCustomColors === true || zone.useCustomColors === 1)
                    readonly property color fillColor: useCustom && zone.inactiveColor ? zone.inactiveColor : root.inactiveColor
                    readonly property real fillOpacity: useCustom && zone.inactiveOpacity !== undefined ? zone.inactiveOpacity : root.inactiveOpacity
                    readonly property color strokeColor: useCustom && zone.borderColor ? zone.borderColor : root.borderColor
                    readonly property int strokeWidth: useCustom && zone.borderWidth !== undefined ? zone.borderWidth : root.borderWidth
                    readonly property int cornerRadius: useCustom && zone.borderRadius !== undefined ? zone.borderRadius : root.borderRadius

                    anchors.fill: parent
                    radius: zoneBg.cornerRadius
                    color: Qt.rgba(zoneBg.fillColor.r, zoneBg.fillColor.g, zoneBg.fillColor.b, zoneBg.fillOpacity)
                    border.color: zoneBg.strokeColor
                    border.width: zoneBg.strokeWidth
                }

                // Grid of candidate cards inside zone - centered, scaled like zone numbers
                Flow {
                    id: candidateFlow

                    readonly property real zoneSize: Math.min(zoneContainer.width, zoneContainer.height) || 1
                    readonly property real cardScale: root.cardScaleBase / Math.max(1, Math.sqrt(root.candidates.length))
                    readonly property real cardBaseSize: zoneSize * cardScale
                    readonly property real iconSize: Math.max(root.minIconSize, cardBaseSize * root.iconSizeRatio)
                    readonly property real cardWidth: Math.max(root.minCardWidth, cardBaseSize * root.cardWidthMultiplier)
                    // Scale font like ZoneItem zone name: base on theme, scale with zone size
                    readonly property real fontPixelSize: {
                        var baseSize = Kirigami.Theme.defaultFont.pixelSize;
                        var scaleFactor = zoneSize / root.zoneSizeRefForFont;
                        var scaledSize = baseSize * Math.max(root.minFontScale, Math.min(1, scaleFactor));
                        return Math.max(root.minFontPx, Math.round(scaledSize));
                    }
                    readonly property real flowWidth: zoneContainer.width - Kirigami.Units.smallSpacing * 2
                    readonly property real cardTotalWidth: cardWidth + Kirigami.Units.smallSpacing * 2
                    readonly property real flowSpacing: Math.max(2, Math.min(8, zoneSize * 0.02))
                    readonly property int itemsPerRow: Math.max(1, Math.floor((flowWidth + flowSpacing) / (cardTotalWidth + flowSpacing)))
                    readonly property real contentWidth: {
                        var n = root.candidates.length;
                        if (n <= 0)
                            return 0;

                        var perRow = itemsPerRow;
                        if (n <= perRow)
                            return n * cardTotalWidth + (n - 1) * flowSpacing;

                        return perRow * cardTotalWidth + (perRow - 1) * flowSpacing;
                    }
                    readonly property real centerPadding: Math.max(0, (flowWidth - contentWidth) / 2)

                    width: flowWidth
                    leftPadding: centerPadding
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: Math.max(Kirigami.Units.smallSpacing, (zoneContainer.height - candidateFlow.implicitHeight) / 2)
                    spacing: flowSpacing

                    Repeater {
                        model: root.candidates

                        Item {
                            id: candidateCard

                            property var candidate: modelData
                            property bool hovered: cardMouse.containsMouse

                            width: candidateFlow.cardWidth + Kirigami.Units.smallSpacing * 2
                            height: cardContent.height + Kirigami.Units.smallSpacing * 2

                            Rectangle {
                                anchors.fill: parent
                                radius: Math.max(2, candidateFlow.zoneSize * 0.01)
                                color: candidateCard.hovered ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.2) : Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.5)
                                border.color: candidateCard.hovered ? Kirigami.Theme.highlightColor : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2)
                                border.width: candidateCard.hovered ? 2 : 1

                                Behavior on color {
                                    ColorAnimation {
                                        duration: 150
                                    }

                                }

                                Behavior on border.color {
                                    ColorAnimation {
                                        duration: 150
                                    }

                                }

                            }

                            Row {
                                id: cardContent

                                anchors.centerIn: parent
                                width: candidateFlow.cardWidth
                                spacing: Kirigami.Units.smallSpacing

                                Item {
                                    width: candidateFlow.iconSize
                                    height: width

                                    Image {
                                        anchors.fill: parent
                                        visible: !!(candidate && candidate.thumbnail)
                                        fillMode: Image.PreserveAspectFit
                                        source: (candidate && candidate.thumbnail) ? candidate.thumbnail : ""
                                        cache: false
                                    }

                                    Image {
                                        anchors.fill: parent
                                        visible: !(candidate && candidate.thumbnail) && !!(candidate && candidate.iconPng)
                                        fillMode: Image.PreserveAspectFit
                                        source: (candidate && candidate.iconPng) ? candidate.iconPng : ""
                                        cache: false
                                    }

                                    Kirigami.Icon {
                                        anchors.fill: parent
                                        visible: !(candidate && candidate.thumbnail) && !(candidate && candidate.iconPng)
                                        source: candidate ? (candidate.icon || "application-x-executable") : "application-x-executable"
                                    }

                                }

                                Label {
                                    width: parent.width - candidateFlow.iconSize - Kirigami.Units.smallSpacing
                                    anchors.verticalCenter: parent.verticalCenter
                                    horizontalAlignment: Text.AlignLeft
                                    verticalAlignment: Text.AlignVCenter
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                    text: candidate ? (candidate.caption || "") : ""
                                    font.pixelSize: candidateFlow.fontPixelSize
                                    color: Kirigami.Theme.textColor
                                }

                            }

                            MouseArea {
                                // Don't close here — C++ manages the window lifecycle.
                                // During continuation, showSnapAssist() reuses this window
                                // with updated data. When all zones are filled, C++ calls
                                // hideSnapAssist() which destroys the window.

                                id: cardMouse

                                anchors.fill: parent
                                hoverEnabled: root.visible
                                cursorShape: Qt.PointingHandCursor
                                Accessible.name: candidate && candidate.caption ? i18n("Snap %1 to this zone", candidate.caption) : i18n("Snap window to this zone")
                                // ToolTip disabled: Breeze ToolTip causes binding loop on contentWidth
                                // when used inside Repeater+Flow. Accessible.name provides screen reader info.
                                onClicked: {
                                    const wId = candidate ? candidate.windowId : "";
                                    const zoneId = zoneContainer.zone ? (zoneContainer.zone.zoneId || "") : "";
                                    if (!zoneContainer.zone || !wId || !zoneId) {
                                        root.hide();
                                        return ;
                                    }
                                    const z = zoneContainer.zone;
                                    const geo = z && z.x !== undefined && z.y !== undefined ? JSON.stringify({
                                        "x": z.x,
                                        "y": z.y,
                                        "width": z.width,
                                        "height": z.height
                                    }) : "{}";
                                    root.windowSelected(wId, zoneId, geo);
                                }
                            }

                        }

                    }

                }

            }

        }

    }

}

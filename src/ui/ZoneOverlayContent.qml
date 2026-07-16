// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import org.phosphor.animation
import org.plasmazones.common as QFZCommon

/**
 * Zone overlay content body — Item version of the legacy ZoneOverlay.qml,
 * hosted inside the unified PassiveOverlayShell's mainOverlay slot when
 * the screen is using the rectangle-based (non-shader) overlay path.
 */
Item {
    id: root

    property var zones: []
    property string highlightedZoneId: ""
    property var highlightedZoneIds: []
    property bool showNumbers: true
    property var previewZones: []
    property color highlightColor: QFZCommon.ZoneColorDefaults.activeZoneColor
    property color inactiveColor: QFZCommon.ZoneColorDefaults.inactiveZoneColor
    property color borderColor: QFZCommon.ZoneColorDefaults.zoneBorderColor
    property color labelFontColor: Kirigami.Theme.textColor
    property string fontFamily: ""
    property real fontSizeScale: 1
    property int fontWeight: Font.Bold
    property bool fontItalic: false
    property bool fontUnderline: false
    property bool fontStrikeout: false
    property real activeOpacity: 0.5
    property real inactiveOpacity: 0.3
    property int borderWidth: Kirigami.Units.smallSpacing
    property int borderRadius: Kirigami.Units.gridUnit
    property bool _idled: false

    function flash() {
        flashAnimation.start();
    }

    function highlightZone(zoneId) {
        highlightedZoneId = zoneId || "";
        highlightedZoneIds = [];
    }

    function highlightZones(zoneIds) {
        highlightedZoneIds = zoneIds || [];
        highlightedZoneId = "";
    }

    function clearHighlight() {
        highlightedZoneId = "";
        highlightedZoneIds = [];
    }

    function hasCustomColors(zoneData) {
        var v = zoneData.useCustomColors;
        return v === true || v === 1 || (typeof v === "string" && v.toLowerCase() === "true");
    }

    function isZoneHighlighted(zoneData) {
        if (zoneData.isHighlighted)
            return true;

        if (!zoneData.id)
            return false;

        if (zoneData.id === highlightedZoneId)
            return true;

        for (var i = 0; i < highlightedZoneIds.length; i++) {
            if (highlightedZoneIds[i] === zoneData.id)
                return true;
        }
        return false;
    }

    anchors.fill: parent

    Item {
        id: content

        anchors.fill: parent
        visible: !root._idled

        Rectangle {
            id: debugBg

            anchors.fill: parent
            color: "transparent"
            visible: root.zones.length === 0

            Text {
                anchors.centerIn: parent
                text: i18n("PlasmaZones Overlay Active\n(No zones defined)")
                color: Kirigami.Theme.disabledTextColor
                font.pixelSize: Kirigami.Theme.defaultFont.pixelSize * 1.5
                horizontalAlignment: Text.AlignHCenter
                visible: parent.visible
            }
        }

        Repeater {
            model: root.zones

            delegate: Loader {
                required property var modelData
                required property int index

                x: modelData.x
                y: modelData.y
                width: modelData.width
                height: modelData.height
                sourceComponent: (modelData.overlayDisplayMode === 1) ? layoutPreviewComponent : zoneRectComponent
            }
        }

        Component {
            id: zoneRectComponent

            ZoneItem {
                property var modelData: parent ? parent.modelData : ({})
                property int zoneIndex: parent ? parent.index : 0
                property bool useCustom: root.hasCustomColors(modelData)

                anchors.fill: parent
                zoneNumber: modelData.zoneNumber || (zoneIndex + 1)
                zoneName: modelData.name || ""
                isHighlighted: root.isZoneHighlighted(modelData)
                isMultiZone: {
                    if (root.highlightedZoneIds.length <= 1)
                        return false;

                    if (!modelData.id)
                        return false;

                    for (var i = 0; i < root.highlightedZoneIds.length; i++) {
                        if (root.highlightedZoneIds[i] === modelData.id)
                            return true;
                    }
                    return false;
                }
                showNumber: root.showNumbers
                highlightColor: (useCustom && modelData.highlightColor) ? modelData.highlightColor : root.highlightColor
                inactiveColor: (useCustom && modelData.inactiveColor) ? modelData.inactiveColor : root.inactiveColor
                borderColor: (useCustom && modelData.borderColor) ? modelData.borderColor : root.borderColor
                labelFontColor: root.labelFontColor
                fontFamily: root.fontFamily
                fontSizeScale: root.fontSizeScale
                fontWeight: root.fontWeight
                fontItalic: root.fontItalic
                fontUnderline: root.fontUnderline
                fontStrikeout: root.fontStrikeout
                activeOpacity: (useCustom && modelData.activeOpacity !== undefined) ? modelData.activeOpacity : root.activeOpacity
                inactiveOpacity: (useCustom && modelData.inactiveOpacity !== undefined) ? modelData.inactiveOpacity : root.inactiveOpacity
                borderWidth: (useCustom && modelData.borderWidth !== undefined) ? modelData.borderWidth : root.borderWidth
                borderRadius: (useCustom && modelData.borderRadius !== undefined) ? modelData.borderRadius : root.borderRadius
            }
        }

        Component {
            id: layoutPreviewComponent

            Item {
                property var modelData: parent ? parent.modelData : ({})
                property int zoneIndex: parent ? parent.index : 0

                anchors.fill: parent

                Item {
                    id: previewContainer

                    property real previewWidth: Math.min(parent.width * 0.6, 200)
                    property real screenAspect: (root.width > 0 && root.height > 0) ? (root.width / root.height) : (16 / 9)
                    property real previewHeight: previewWidth / screenAspect

                    anchors.centerIn: parent
                    width: previewWidth
                    height: previewHeight

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -Kirigami.Units.smallSpacing
                        radius: Kirigami.Units.smallSpacing * 1.5
                        color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.85)
                        border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
                        border.width: 1
                    }

                    QFZCommon.ZonePreview {
                        anchors.fill: parent
                        zones: root.previewZones
                        selectedZoneIndex: zoneIndex
                        showZoneNumbers: root.showNumbers
                        zonePadding: 1
                        edgeGap: 1
                        minZoneSize: 8
                        activeOpacity: root.activeOpacity
                        inactiveOpacity: root.inactiveOpacity
                        highlightColor: root.highlightColor
                        inactiveColor: root.inactiveColor
                        borderColor: root.borderColor
                        labelFontColor: root.labelFontColor
                        fontFamily: root.fontFamily
                        fontSizeScale: root.fontSizeScale
                        fontWeight: root.fontWeight
                        fontItalic: root.fontItalic
                        fontUnderline: root.fontUnderline
                        fontStrikeout: root.fontStrikeout
                    }
                }
            }
        }

        Rectangle {
            id: flashOverlay

            anchors.fill: parent
            color: root.highlightColor
            opacity: 0
            visible: opacity > 0

            SequentialAnimation {
                id: flashAnimation

                PhosphorMotionAnimation {
                    target: flashOverlay
                    properties: "opacity"
                    from: 0.3
                    to: 0
                    profile: "widget.zoneOverlayFlash"
                }
            }
        }
    }
}

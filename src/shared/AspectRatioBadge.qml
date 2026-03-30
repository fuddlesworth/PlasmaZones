// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

/**
 * Aspect ratio class badge for layout cards.
 *
 * Shows a compact label indicating the monitor type a layout is designed for:
 * "16:9", "21:9", "32:9", "9:16", or hidden for "any" (universal layouts).
 *
 * Uses an icon-like shorthand ratio rather than the raw class string
 * so the badge stays compact in tight layout card UIs.
 */
Rectangle {
    id: root

    // aspectRatioClass string from C++ (via layoutData): "any", "standard", "ultrawide", "super-ultrawide", "portrait"
    property string aspectRatioClass: "any"
    readonly property real heightScale: 0.9
    readonly property real backgroundOpacity: 0.15
    readonly property real textOpacity: 0.7
    readonly property real fontScale: 0.75
    readonly property string badgeText: {
        switch (aspectRatioClass) {
        case "standard":
            return "16:9";
        case "ultrawide":
            return "21:9";
        case "super-ultrawide":
            return "32:9";
        case "portrait":
            return "9:16";
        default:
            return "";
        }
    }
    readonly property color badgeColor: {
        switch (aspectRatioClass) {
        case "standard":
            return Kirigami.Theme.textColor;
        case "ultrawide":
            return Kirigami.Theme.positiveTextColor;
        case "super-ultrawide":
            return Kirigami.Theme.neutralTextColor;
        case "portrait":
            return Kirigami.Theme.activeTextColor;
        default:
            return Kirigami.Theme.textColor;
        }
    }

    // Hide badge for universal layouts — no point labelling "works everywhere"
    visible: aspectRatioClass !== "any" && aspectRatioClass !== ""
    implicitWidth: badgeLabel.implicitWidth + Kirigami.Units.smallSpacing * 1.5
    implicitHeight: Kirigami.Units.gridUnit * heightScale
    radius: Kirigami.Units.smallSpacing / 2
    color: Qt.rgba(badgeColor.r, badgeColor.g, badgeColor.b, backgroundOpacity)

    Label {
        id: badgeLabel

        anchors.centerIn: parent
        text: root.badgeText
        font.pixelSize: Kirigami.Theme.smallFont.pixelSize * root.fontScale
        font.weight: Font.Medium
        color: root.badgeColor
        opacity: root.textOpacity
    }

}

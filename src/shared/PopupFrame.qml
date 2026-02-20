// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Effects
import org.kde.kirigami as Kirigami

/**
 * @brief Shared popup frame providing consistent shadow, background, and border.
 *
 * Used by ZoneSelectorWindow and LayoutPickerOverlay to share container chrome.
 * Children are injected via the default property alias into the internal frame.
 *
 * MouseArea is NOT included — each parent provides its own dismiss/absorb logic.
 */
Item {
    id: root

    property color backgroundColor: Kirigami.Theme.backgroundColor
    property color textColor: Kirigami.Theme.textColor
    property real containerRadius: Kirigami.Units.smallSpacing * 3

    default property alias contentData: frame.data

    QtObject {
        id: style

        readonly property real backgroundAlpha: 0.95
        readonly property real borderAlpha: 0.2
        readonly property real shadowAlpha: 0.38
    }

    MultiEffect {
        source: frame
        anchors.fill: frame
        shadowEnabled: true
        shadowColor: Qt.rgba(0, 0, 0, style.shadowAlpha)
        shadowBlur: 1
        shadowVerticalOffset: 2
        shadowHorizontalOffset: 0
    }

    Rectangle {
        id: frame

        anchors.fill: parent
        color: Qt.rgba(root.backgroundColor.r, root.backgroundColor.g, root.backgroundColor.b, style.backgroundAlpha)
        radius: root.containerRadius
        border.color: Qt.rgba(root.textColor.r, root.textColor.g, root.textColor.b, style.borderAlpha)
        border.width: 1
    }
}

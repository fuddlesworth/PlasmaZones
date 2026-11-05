// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami

// Scrolling → Strip Selector. The edge-triggered strip popup shown while
// dragging a window on a scrolling screen: enabled, trigger distance,
// on-screen position and preview size. Reuses the snapping zone selector's
// bounds bridge — the min/max/preset constants are UI bounds, not values
// bound to either config group.
SettingsFlickable {
    id: root

    readonly property var settingsBridge: settingsController.snappingZoneSelectorPage
    readonly property int sliderValueLabelWidth: Kirigami.Units.gridUnit * 3
    readonly property int zoneSelectorTriggerMin: root.settingsBridge.triggerDistanceMin
    readonly property int zoneSelectorTriggerMax: root.settingsBridge.triggerDistanceMax
    readonly property int zoneSelectorPreviewWidthMin: root.settingsBridge.previewWidthMin
    readonly property int zoneSelectorPreviewWidthMax: root.settingsBridge.previewWidthMax
    readonly property int zoneSelectorPreviewSmall: root.settingsBridge.previewWidthSmall
    readonly property int zoneSelectorPreviewMedium: root.settingsBridge.previewWidthMedium
    readonly property int zoneSelectorPreviewLarge: root.settingsBridge.previewWidthLarge
    readonly property int zoneSelectorPreviewHeightMin: root.settingsBridge.previewHeightMin
    readonly property int zoneSelectorPreviewHeightMax: root.settingsBridge.previewHeightMax
    readonly property real screenAspectRatio: Screen.width > 0 && Screen.height > 0 ? (Screen.width / Screen.height) : (16 / 9)

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        ScrollingZoneSelectorSection {
            Layout.fillWidth: true
            appSettings: settingsController.settings
            controller: settingsController
            constants: root
            screenAspectRatio: root.screenAspectRatio
        }
    }
}

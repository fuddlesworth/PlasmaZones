// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami

// Snapping → Zone Selector. The edge-triggered layout-picker popup, all in one
// page: behavior (enable, trigger distance) and appearance (position,
// arrangement, preview size) as sections via ZoneSelectorSection in "all" mode.
// The per-screen popup overrides are scoped by the header scope chip on each of
// ZoneSelectorSection's per-monitor cards.
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
    readonly property int zoneSelectorGridColumnsMin: root.settingsBridge.gridColumnsMin
    readonly property int zoneSelectorGridColumnsMax: root.settingsBridge.gridColumnsMax
    readonly property int zoneSelectorMaxRowsMin: root.settingsBridge.maxRowsMin
    readonly property int zoneSelectorMaxRowsMax: root.settingsBridge.maxRowsMax
    readonly property real screenAspectRatio: Screen.width > 0 && Screen.height > 0 ? (Screen.width / Screen.height) : (16 / 9)

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        ZoneSelectorSection {
            Layout.fillWidth: true
            mode: "all"
            appSettings: settingsController.settings
            controller: settingsController
            constants: root
            screenAspectRatio: root.screenAspectRatio
        }
    }
}

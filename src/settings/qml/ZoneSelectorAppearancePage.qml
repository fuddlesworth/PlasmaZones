// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami

// Appearance → Daemon Surfaces → Zone Selector. The popup's appearance facets:
// on-screen position, layout arrangement and preview size. Its behavior (enable
// + trigger distance) lives under Snapping → Zone Selector; both embed the same
// ZoneSelectorSection (so the per-screen helper and effective* resolution stay
// single-sourced) and select a facet subset via `mode`. Bounds wiring mirrors
// SnappingZoneSelectorPage — the same controller bridge feeds both.
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
            mode: "appearance"
            appSettings: settingsController.settings
            controller: settingsController
            constants: root
            screenAspectRatio: root.screenAspectRatio
        }
    }
}

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Appearance → Daemon Surfaces → OSDs. The on-screen-display surface styling.
// Only the visual style lives here; the "which OSD shows" toggles and the
// drag-overlay style stay in General → On-Screen Display as behavior.
SettingsFlickable {
    id: root

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("On-Screen Display")
            searchAnchor: "osd"
            scopeLabel: i18n("Global")
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("OSD style")
                    searchAnchor: "osdStyle"
                    description: i18n("Visual style of on-screen notifications")

                    WideComboBox {
                        // Styling has no effect while every OSD is off, so the
                        // control follows the General toggles' combined state.
                        readonly property bool anyOsdEnabled: appSettings.showOsdOnLayoutSwitch || appSettings.showOsdOnDesktopSwitch || appSettings.showNavigationOsd

                        Accessible.name: i18n("OSD style")
                        enabled: anyOsdEnabled
                        currentIndex: Math.max(0, Math.min(appSettings.osdStyle, 2))
                        model: [i18n("None"), i18n("Text only"), i18n("Visual preview")]
                        onActivated: index => {
                            appSettings.osdStyle = index;
                        }
                    }
                }
            }
        }
    }
}

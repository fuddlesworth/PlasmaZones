// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Shared per-monitor section divider + monitor selector card
 *
 * Combines the section divider ("Per-Monitor Settings — ...") and the monitor
 * selection card (ScreenComboBox + info/reset row) used identically in
 * AutotilingTab and ZoneSelectorSection.
 *
 * Includes a hot-unplug handler: when ScreenComboBox internally resets its
 * index (e.g. a monitor disappears), the Connections block detects the
 * currentValue change and syncs selectedScreenName back to the parent.
 */
ColumnLayout {
    id: root

    required property var appSettings
    property bool featureEnabled: true
    property string selectedScreenName: ""
    property bool hasOverrides: false
    readonly property bool isPerScreen: selectedScreenName !== ""

    signal resetClicked()

    visible: appSettings.screens.length > 1
    spacing: Kirigami.Units.largeSpacing

    // Section divider
    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        Label {
            text: root.isPerScreen ? i18n("Per-Monitor Settings — %1", root.selectedScreenName) : i18n("Per-Monitor Settings — All Monitors")
            opacity: 0.6
            font.pixelSize: Kirigami.Theme.smallFont.pixelSize
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

    }

    // Monitor selector card
    Item {
        Layout.fillWidth: true
        implicitHeight: monitorSelectorCard.implicitHeight

        Kirigami.Card {
            id: monitorSelectorCard

            anchors.fill: parent
            enabled: root.featureEnabled

            header: Kirigami.Heading {
                level: 3
                text: i18n("Monitor")
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                ScreenComboBox {
                    id: monitorCombo

                    Layout.fillWidth: true
                    appSettings: root.appSettings
                    noneText: i18n("All Monitors (Default)")
                    Accessible.name: i18n("Monitor selection")
                    onActivated: {
                        root.selectedScreenName = currentScreenName;
                    }
                }

                // Sync selectedScreenName on hot-unplug: when ScreenComboBox
                // internally resets (screen disappears), currentValue changes
                // and we propagate that to the parent.
                Connections {
                    function onCurrentValueChanged() {
                        if (monitorCombo.currentScreenName !== root.selectedScreenName)
                            root.selectedScreenName = monitorCombo.currentScreenName;

                    }

                    target: monitorCombo
                }

                // Per-screen info/reset row
                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.smallSpacing
                    Layout.topMargin: Kirigami.Units.smallSpacing * 2
                    visible: root.isPerScreen

                    Kirigami.InlineMessage {
                        Layout.fillWidth: true
                        type: root.hasOverrides ? Kirigami.MessageType.Positive : Kirigami.MessageType.Information
                        text: root.hasOverrides ? i18n("Custom settings for this monitor") : i18n("Using default settings (editing below will create an override)")
                        visible: true
                    }

                    Button {
                        text: i18n("Reset to defaults")
                        icon.name: "edit-clear"
                        visible: root.hasOverrides
                        onClicked: root.resetClicked()
                    }

                }

            }

        }

    }

}

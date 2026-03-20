// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    contentHeight: content.implicitHeight

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // --- Mode Selection ---
        Kirigami.Card {
            Layout.fillWidth: true

            header: Kirigami.Heading {
                text: i18n("Mode Selection")
                level: 2
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Active mode:")
                    }

                    Label {
                        text: kcm.autotileEnabled ? i18n("Autotiling") : i18n("Manual Snapping")
                        font.bold: true
                    }

                }

                CheckBox {
                    text: i18n("Enable autotiling")
                    checked: kcm.autotileEnabled
                    onToggled: kcm.autotileEnabled = checked
                }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: i18n("Snapping mode uses predefined zone layouts. Autotiling mode arranges windows automatically using a tiling algorithm. Each screen can use either mode independently.")
                    opacity: 0.7
                }

            }

        }

        // --- Default Layout ---
        Kirigami.Card {
            Layout.fillWidth: true

            header: Kirigami.Heading {
                text: i18n("Default Layout")
                level: 2
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Current default:")
                    }

                    Label {
                        text: kcm.defaultLayoutId || i18n("(none)")
                        font.bold: true
                    }

                }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: i18n("The default layout is used as a fallback when no explicit assignment exists for a screen, desktop, or activity.")
                    opacity: 0.7
                }

            }

        }

        // --- Connected Screens ---
        Kirigami.Card {
            Layout.fillWidth: true

            header: Kirigami.Heading {
                text: i18n("Connected Screens")
                level: 2
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: 0

                Label {
                    visible: settingsController.screens.length === 0
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.gridUnit
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: i18n("No screens detected. Start the PlasmaZones daemon to see connected monitors.")
                    opacity: 0.7
                }

                Repeater {
                    id: screenRepeater

                    model: settingsController.screens

                    delegate: ItemDelegate {
                        id: screenDelegate

                        required property var modelData
                        required property int index
                        readonly property string screenName: modelData.name || ""
                        readonly property bool isPrimary: modelData.isPrimary === true
                        readonly property string resolution: modelData.resolution || ""
                        readonly property string manufacturer: modelData.manufacturer || ""
                        readonly property string screenModel: modelData.model || ""
                        readonly property bool isDisabled: settingsController.isMonitorDisabled(screenName)

                        Layout.fillWidth: true
                        width: parent ? parent.width : 0

                        Kirigami.Separator {
                            anchors.bottom: parent.bottom
                            anchors.left: parent.left
                            anchors.right: parent.right
                            visible: screenDelegate.index < screenRepeater.count - 1
                        }

                        contentItem: RowLayout {
                            spacing: Kirigami.Units.smallSpacing

                            Kirigami.Icon {
                                source: screenDelegate.isPrimary ? "starred-symbolic" : "monitor"
                                implicitWidth: Kirigami.Units.iconSizes.smallMedium
                                implicitHeight: Kirigami.Units.iconSizes.smallMedium
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0

                                Label {
                                    Layout.fillWidth: true
                                    text: {
                                        let parts = [];
                                        if (screenDelegate.manufacturer)
                                            parts.push(screenDelegate.manufacturer);

                                        if (screenDelegate.screenModel)
                                            parts.push(screenDelegate.screenModel);

                                        if (parts.length === 0)
                                            parts.push(screenDelegate.screenName);

                                        return parts.join(" ");
                                    }
                                    font.bold: screenDelegate.isPrimary
                                    elide: Text.ElideRight
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: {
                                        let parts = [screenDelegate.screenName];
                                        if (screenDelegate.resolution)
                                            parts.push(screenDelegate.resolution);

                                        if (screenDelegate.isPrimary)
                                            parts.push(i18n("Primary"));

                                        if (screenDelegate.isDisabled)
                                            parts.push(i18n("Disabled"));

                                        return parts.join(" \u00b7 ");
                                    }
                                    font: Kirigami.Theme.smallFont
                                    opacity: 0.7
                                    elide: Text.ElideRight
                                }

                            }

                            CheckBox {
                                text: i18n("Enabled")
                                checked: !screenDelegate.isDisabled
                                onToggled: settingsController.setMonitorDisabled(screenDelegate.screenName, !checked)
                            }

                        }

                    }

                }

            }

        }

        // --- Advanced Assignments ---
        Kirigami.Card {
            Layout.fillWidth: true

            header: Kirigami.Heading {
                text: i18n("Advanced Assignments")
                level: 2
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: true
                    type: Kirigami.MessageType.Information
                    text: i18n("Per-screen layout assignments, per-desktop overrides, per-activity overrides, and app-to-zone rules are managed in KDE System Settings for the full interactive experience.")
                }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: i18n("You can also edit assignments directly in the config file:")
                    opacity: 0.7
                }

                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    wrapMode: Text.WordWrap
                    font.family: "monospace"
                    text: "[Assignment:HDMI-A-1]\nMode=0\nSnappingLayout=halves\nTilingAlgorithm=bsp\n\n[Assignment:DP-1:Desktop:2]\nMode=1\nTilingAlgorithm=master-stack"
                    opacity: 0.7
                }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: i18n("Resolution order: Desktop/Activity assignment > Screen assignment > Default layout.")
                    topPadding: Kirigami.Units.smallSpacing
                    opacity: 0.7
                }

            }

        }

    }

}

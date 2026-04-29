// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    readonly property var animBridge: settingsController.animationPage
    property var shaderList: root.animBridge.availableTransitionEffects

    contentHeight: content.implicitHeight
    clip: true

    Connections {
        target: root.animBridge
        function onEffectsChanged() {
            root.shaderList = root.animBridge.availableTransitionEffects;
        }
    }

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Installed Transition Effects")
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Label {
                    visible: root.shaderList.length === 0
                    Layout.fillWidth: true
                    text: i18n("No transition effects available. Drop shader packs into ~/.local/share/plasmazones/animations/")
                    wrapMode: Text.WordWrap
                    color: Kirigami.Theme.disabledTextColor
                    font.italic: true
                }

                Repeater {
                    model: root.shaderList

                    delegate: ColumnLayout {
                        id: shaderDelegate

                        required property var modelData
                        required property int index

                        Layout.fillWidth: true
                        spacing: 0

                        SettingsSeparator {
                            visible: shaderDelegate.index > 0
                        }

                        SettingsRow {
                            title: shaderDelegate.modelData.name ?? ""
                            description: shaderDelegate.modelData.description ?? ""

                            RowLayout {
                                spacing: Kirigami.Units.smallSpacing

                                Label {
                                    text: shaderDelegate.modelData.category ?? ""
                                    font: Kirigami.Theme.smallFont
                                    color: Kirigami.Theme.disabledTextColor
                                }

                                Label {
                                    visible: (shaderDelegate.modelData.parameterCount ?? 0) > 0
                                    text: i18n("%1 param(s)", shaderDelegate.modelData.parameterCount)
                                    font: Kirigami.Theme.smallFont
                                    color: Kirigami.Theme.disabledTextColor
                                }

                                Label {
                                    text: shaderDelegate.modelData.isUserEffect ? i18n("User") : i18n("System")
                                    font: Kirigami.Theme.smallFont
                                    color: shaderDelegate.modelData.isUserEffect ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.disabledTextColor
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

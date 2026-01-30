// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Exclusions tab - Window filtering, excluded applications, and window classes
 */
ScrollView {
    id: root

    required property var kcm
    required property QtObject constants

    clip: true
    contentWidth: availableWidth

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            text: i18n("Windows from excluded applications or with excluded window classes will not snap to zones.")
            visible: true
        }

        // Window type and size exclusions
        Kirigami.Card {
            Layout.fillWidth: true

            header: Kirigami.Heading {
                level: 3
                text: i18n("Window Filtering")
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing
                Layout.margins: Kirigami.Units.smallSpacing

                CheckBox {
                    text: i18n("Exclude transient windows (dialogs, utilities, tooltips)")
                    checked: kcm.excludeTransientWindows
                    onToggled: kcm.excludeTransientWindows = checked
                }

                Label {
                    text: i18n("Minimum window size for snapping:")
                    Layout.topMargin: Kirigami.Units.smallSpacing
                }

                RowLayout {
                    spacing: Kirigami.Units.largeSpacing

                    RowLayout {
                        Label {
                            text: i18n("Width:")
                        }
                        SpinBox {
                            from: 0
                            to: 1000
                            stepSize: 10
                            value: kcm.minimumWindowWidth
                            onValueModified: kcm.minimumWindowWidth = value

                            textFromValue: function(value) {
                                return value === 0 ? i18n("Disabled") : value + " px"
                            }
                        }
                    }

                    RowLayout {
                        Label {
                            text: i18n("Height:")
                        }
                        SpinBox {
                            from: 0
                            to: 1000
                            stepSize: 10
                            value: kcm.minimumWindowHeight
                            onValueModified: kcm.minimumWindowHeight = value

                            textFromValue: function(value) {
                                return value === 0 ? i18n("Disabled") : value + " px"
                            }
                        }
                    }
                }

                Label {
                    text: i18n("Windows smaller than these dimensions will not snap to zones. Set to 0 to disable.")
                    font.italic: true
                    opacity: 0.7
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }

        // Excluded applications
        Kirigami.Card {
            Layout.fillWidth: true
            Layout.fillHeight: true

            header: Kirigami.Heading {
                level: 3
                text: i18n("Excluded Applications")
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.smallSpacing

                    TextField {
                        id: excludedAppField
                        Layout.fillWidth: true
                        placeholderText: i18n("Application name (e.g., firefox, konsole)")

                        onAccepted: {
                            if (text.length > 0) {
                                kcm.addExcludedApp(text)
                                text = ""
                            }
                        }
                    }

                    Button {
                        text: i18n("Add")
                        icon.name: "list-add"
                        enabled: excludedAppField.text.length > 0
                        onClicked: {
                            kcm.addExcludedApp(excludedAppField.text)
                            excludedAppField.text = ""
                        }
                    }
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: Kirigami.Units.smallSpacing
                    clip: true
                    model: kcm.excludedApplications

                    delegate: ItemDelegate {
                        width: ListView.view.width
                        required property string modelData
                        required property int index

                        contentItem: RowLayout {
                            Kirigami.Icon {
                                source: "application-x-executable"
                                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                                Layout.alignment: Qt.AlignVCenter
                            }

                            Label {
                                text: modelData
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                            }

                            ToolButton {
                                icon.name: "edit-delete"
                                onClicked: kcm.removeExcludedApp(index)
                            }
                        }
                    }

                    Kirigami.PlaceholderMessage {
                        anchors.centerIn: parent
                        width: parent.width - Kirigami.Units.gridUnit * 4
                        visible: parent.count === 0
                        text: i18n("No excluded applications")
                        explanation: i18n("Add application names above to exclude them from zone snapping")
                    }
                }
            }
        }

        // Excluded window classes
        Kirigami.Card {
            Layout.fillWidth: true
            Layout.fillHeight: true

            header: Kirigami.Heading {
                level: 3
                text: i18n("Excluded Window Classes")
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.smallSpacing

                    TextField {
                        id: excludedClassField
                        Layout.fillWidth: true
                        placeholderText: i18n("Window class (use xprop to find)")

                        onAccepted: {
                            if (text.length > 0) {
                                kcm.addExcludedWindowClass(text)
                                text = ""
                            }
                        }
                    }

                    Button {
                        text: i18n("Add")
                        icon.name: "list-add"
                        enabled: excludedClassField.text.length > 0
                        onClicked: {
                            kcm.addExcludedWindowClass(excludedClassField.text)
                            excludedClassField.text = ""
                        }
                    }
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: Kirigami.Units.smallSpacing
                    clip: true
                    model: kcm.excludedWindowClasses

                    delegate: ItemDelegate {
                        width: ListView.view.width
                        required property string modelData
                        required property int index

                        contentItem: RowLayout {
                            Kirigami.Icon {
                                source: "window"
                                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                                Layout.alignment: Qt.AlignVCenter
                            }

                            Label {
                                text: modelData
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                                font: Kirigami.Theme.fixedWidthFont
                            }

                            ToolButton {
                                icon.name: "edit-delete"
                                onClicked: kcm.removeExcludedWindowClass(index)
                            }
                        }
                    }

                    Kirigami.PlaceholderMessage {
                        anchors.centerIn: parent
                        width: parent.width - Kirigami.Units.gridUnit * 4
                        visible: parent.count === 0
                        text: i18n("No excluded window classes")
                        explanation: i18n("Use 'xprop | grep WM_CLASS' to find window classes")
                    }
                }
            }
        }
    }
}

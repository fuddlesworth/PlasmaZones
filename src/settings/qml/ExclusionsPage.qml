// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    contentHeight: content.implicitHeight
    clip: true

    WindowPickerDialog {
        id: windowPickerDialog

        appSettings: settingsController
        onPicked: (value) => {
            if (forApps) {
                appSettings.addExcludedApplication(value);
            } else {
                appSettings.addExcludedWindowClass(value);
            }
        }
    }

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // Info message at top level, matching original placement
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            text: i18n("Windows from excluded applications or with excluded window classes will be ignored by both snapping and autotiling.")
            visible: true
        }

        // --- Window Filtering ---
        Item {
            Layout.fillWidth: true
            implicitHeight: filteringCard.implicitHeight

            SettingsCard {
                id: filteringCard

                anchors.fill: parent
                headerText: i18n("Window Filtering")
                collapsible: true

                contentItem: Kirigami.FormLayout {
                    CheckBox {
                        Kirigami.FormData.label: i18n("Transient:")
                        text: i18n("Exclude transient windows (dialogs, popups, toolbars)")
                        checked: appSettings.excludeTransientWindows
                        onToggled: appSettings.excludeTransientWindows = checked
                    }

                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                        Kirigami.FormData.label: i18n("Minimum Window Size")
                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Dimensions:")
                        spacing: Kirigami.Units.largeSpacing

                        RowLayout {
                            Label {
                                text: i18n("W:")
                            }

                            SpinBox {
                                from: 0
                                to: 1000
                                stepSize: 10
                                value: appSettings.minimumWindowWidth
                                onValueModified: appSettings.minimumWindowWidth = value
                                textFromValue: function(value) {
                                    return value === 0 ? i18n("Disabled") : value + " px";
                                }
                                Accessible.name: i18n("Minimum window width")
                            }

                        }

                        RowLayout {
                            Label {
                                text: i18n("H:")
                            }

                            SpinBox {
                                from: 0
                                to: 1000
                                stepSize: 10
                                value: appSettings.minimumWindowHeight
                                onValueModified: appSettings.minimumWindowHeight = value
                                textFromValue: function(value) {
                                    return value === 0 ? i18n("Disabled") : value + " px";
                                }
                                Accessible.name: i18n("Minimum window height")
                            }

                        }

                    }

                    Label {
                        text: i18n("Windows smaller than these dimensions will be excluded. Set to 0 to disable.")
                        font.italic: true
                        opacity: 0.7
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                }

            }

        }

        // --- Excluded Applications ---
        Item {
            Layout.fillWidth: true
            implicitHeight: appsCard.implicitHeight

            ExclusionListCard {
                id: appsCard

                anchors.fill: parent
                title: i18n("Excluded Applications")
                placeholderText: i18n("Application name (e.g., firefox, konsole)")
                emptyTitle: i18n("No excluded applications")
                emptyExplanation: i18n("Add application names above to exclude them from snapping and autotiling")
                iconSource: "application-x-executable"
                model: appSettings.excludedApplications
                useMonospaceFont: false
                showPickButton: true
                onAddRequested: (text) => appSettings.addExcludedApplication(text)
                onRemoveRequested: (index) => appSettings.removeExcludedApplicationAt(index)
                onPickRequested: windowPickerDialog.openForApps()
            }

        }

        // --- Excluded Window Classes ---
        Item {
            Layout.fillWidth: true
            implicitHeight: classesCard.implicitHeight

            ExclusionListCard {
                id: classesCard

                anchors.fill: parent
                title: i18n("Excluded Window Classes")
                placeholderText: i18n("Window class (e.g., org.kde.dolphin)")
                emptyTitle: i18n("No excluded window classes")
                emptyExplanation: i18n("Add window classes above to exclude them from snapping and autotiling")
                iconSource: "window"
                model: appSettings.excludedWindowClasses
                useMonospaceFont: true
                showPickButton: true
                onAddRequested: (text) => appSettings.addExcludedWindowClass(text)
                onRemoveRequested: (index) => appSettings.removeExcludedWindowClassAt(index)
                onPickRequested: windowPickerDialog.openForClasses()
            }

        }

    }

}

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
                appsCard.refreshModel();
            } else {
                appSettings.addExcludedWindowClass(value);
                classesCard.refreshModel();
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

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    SettingsRow {
                        title: i18n("Exclude transient windows")
                        description: i18n("Skip dialogs, popups, and toolbars for snapping and tiling")

                        SettingsSwitch {
                            checked: appSettings.excludeTransientWindows
                            accessibleName: i18n("Exclude transient windows")
                            onToggled: appSettings.excludeTransientWindows = checked
                        }

                    }

                    SettingsSeparator {
                    }

                    SettingsRow {
                        title: i18n("Minimum window width")
                        description: appSettings.minimumWindowWidth === 0 ? i18n("Disabled — no width threshold") : i18n("Windows narrower than this are excluded")

                        SettingsSpinBox {
                            from: 0
                            to: 1000
                            stepSize: 10
                            value: appSettings.minimumWindowWidth
                            unitText: ""
                            Accessible.name: i18n("Minimum window width")
                            onValueModified: (value) => {
                                return appSettings.minimumWindowWidth = value;
                            }
                            textFromValue: function(value) {
                                return value === 0 ? i18n("Off") : value + " px";
                            }
                        }

                    }

                    SettingsSeparator {
                    }

                    SettingsRow {
                        title: i18n("Minimum window height")
                        description: appSettings.minimumWindowHeight === 0 ? i18n("Disabled — no height threshold") : i18n("Windows shorter than this are excluded")

                        SettingsSpinBox {
                            from: 0
                            to: 1000
                            stepSize: 10
                            value: appSettings.minimumWindowHeight
                            unitText: ""
                            Accessible.name: i18n("Minimum window height")
                            onValueModified: (value) => {
                                return appSettings.minimumWindowHeight = value;
                            }
                            textFromValue: function(value) {
                                return value === 0 ? i18n("Off") : value + " px";
                            }
                        }

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
                onAddRequested: (text) => {
                    return appSettings.addExcludedApplication(text);
                }
                onRemoveRequested: (index) => {
                    return appSettings.removeExcludedApplicationAt(index);
                }
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
                onAddRequested: (text) => {
                    return appSettings.addExcludedWindowClass(text);
                }
                onRemoveRequested: (index) => {
                    return appSettings.removeExcludedWindowClassAt(index);
                }
                onPickRequested: windowPickerDialog.openForClasses()
            }

        }

    }

}

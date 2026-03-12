// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kcmutils as KCMUtils
import org.kde.kirigami as Kirigami

KCMUtils.SimpleKCM {
    id: root

    // Capture the context property so child components can access it via root.kcmModule
    readonly property var kcmModule: kcm

    WindowPickerDialog {
        id: windowPickerDialog

        kcm: root.kcmModule
        onPicked: (value) => {
            if (forApps)
                root.kcmModule.addExcludedApp(value);
            else
                root.kcmModule.addExcludedWindowClass(value);
        }
    }

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            text: i18n("Windows from excluded applications or with excluded window classes will be ignored by both snapping and autotiling.")
            visible: true
        }

        // Window type and size exclusions
        Item {
            Layout.fillWidth: true
            implicitHeight: filteringCard.implicitHeight

            Kirigami.Card {
                id: filteringCard

                anchors.fill: parent

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Window Filtering")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing
                    Layout.margins: Kirigami.Units.smallSpacing

                    CheckBox {
                        text: i18n("Exclude transient windows (dialogs, popups, toolbars)")
                        checked: kcm.excludeTransientWindows
                        onToggled: kcm.excludeTransientWindows = checked
                    }

                    Label {
                        text: i18n("Minimum window size for zone management:")
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
                                    return value === 0 ? i18n("Disabled") : value + " px";
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
                                    return value === 0 ? i18n("Disabled") : value + " px";
                                }
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

        // Excluded applications
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
                model: kcm.excludedApplications
                useMonospaceFont: false
                showPickButton: true
                onAddRequested: (text) => {
                    return kcm.addExcludedApp(text);
                }
                onRemoveRequested: (index) => {
                    return kcm.removeExcludedApp(index);
                }
                onPickRequested: windowPickerDialog.openForApps()
            }

        }

        // Excluded window classes
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
                model: kcm.excludedWindowClasses
                useMonospaceFont: false
                showPickButton: true
                onAddRequested: (text) => {
                    return kcm.addExcludedWindowClass(text);
                }
                onRemoveRequested: (index) => {
                    return kcm.removeExcludedWindowClass(index);
                }
                onPickRequested: windowPickerDialog.openForClasses()
            }

        }

    }

}

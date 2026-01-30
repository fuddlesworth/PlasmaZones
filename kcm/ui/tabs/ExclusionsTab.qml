// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Exclusions tab - Window filtering, excluded applications, and window classes
 *
 * Refactored to use ExclusionListCard for DRY compliance.
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

        // Excluded applications - using reusable component
        ExclusionListCard {
            Layout.fillWidth: true
            Layout.fillHeight: true

            title: i18n("Excluded Applications")
            placeholderText: i18n("Application name (e.g., firefox, konsole)")
            emptyTitle: i18n("No excluded applications")
            emptyExplanation: i18n("Add application names above to exclude them from zone snapping")
            iconSource: "application-x-executable"
            model: kcm.excludedApplications
            useMonospaceFont: false

            onAddRequested: (text) => kcm.addExcludedApp(text)
            onRemoveRequested: (index) => kcm.removeExcludedApp(index)
        }

        // Excluded window classes - using reusable component
        ExclusionListCard {
            Layout.fillWidth: true
            Layout.fillHeight: true

            title: i18n("Excluded Window Classes")
            placeholderText: i18n("Window class (use xprop to find)")
            emptyTitle: i18n("No excluded window classes")
            emptyExplanation: i18n("Use 'xprop | grep WM_CLASS' to find window classes")
            iconSource: "window"
            model: kcm.excludedWindowClasses
            useMonospaceFont: true

            onAddRequested: (text) => kcm.addExcludedWindowClass(text)
            onRemoveRequested: (index) => kcm.removeExcludedWindowClass(index)
        }
    }
}

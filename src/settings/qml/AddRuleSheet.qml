// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Guided "Add rule" subject chooser.
 *
 * The common case ("assign a layout to a monitor") stays a two-click flow:
 * pick a subject (Monitor / Application / Activity / Custom), and the sheet
 * asks the controller for a pre-shaped empty rule, then hands it to the
 * RuleEditorSheet. Priority is never surfaced here — it is derived for the
 * specialized subjects and only exposed in the advanced editor.
 */
Kirigami.OverlaySheet {
    id: sheet

    /// The WindowRuleController.
    required property var controller

    /// Emitted with a fresh rule JSON map once the user picks a subject. The
    /// page opens the RuleEditorSheet on this.
    signal subjectChosen(var ruleJson)

    function _choose(subject) {
        var rule = sheet.controller.newEmptyRule(subject);
        sheet.close();
        sheet.subjectChosen(rule);
    }

    title: i18n("Add Window Rule")

    ColumnLayout {
        implicitWidth: Kirigami.Units.gridUnit * 26
        spacing: Kirigami.Units.smallSpacing

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: i18n("What should this rule be about?")
        }

        ItemDelegate {
            Layout.fillWidth: true
            icon.name: "monitor"
            text: i18n("Monitor")
            Accessible.name: i18n("Create a rule that assigns a layout to a monitor")
            onClicked: sheet._choose("monitor")

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                Kirigami.Icon {
                    source: "monitor"
                    Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                    Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    Label {
                        text: i18n("Monitor")
                        font.bold: true
                    }

                    Label {
                        text: i18n("Assign a layout or engine to a monitor context.")
                        opacity: 0.7
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                }

            }

        }

        ItemDelegate {
            Layout.fillWidth: true
            Accessible.name: i18n("Create a rule that targets an application")
            onClicked: sheet._choose("application")

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                Kirigami.Icon {
                    source: "application-x-executable"
                    Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                    Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    Label {
                        text: i18n("Application")
                        font.bold: true
                    }

                    Label {
                        text: i18n("Float, exclude or tweak windows of a specific app.")
                        opacity: 0.7
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                }

            }

        }

        ItemDelegate {
            Layout.fillWidth: true
            Accessible.name: i18n("Create a rule that targets an activity")
            onClicked: sheet._choose("activity")

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                Kirigami.Icon {
                    source: "activities"
                    Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                    Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    Label {
                        text: i18n("Activity")
                        font.bold: true
                    }

                    Label {
                        text: i18n("Assign a layout or engine to a Plasma activity.")
                        opacity: 0.7
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                }

            }

        }

        ItemDelegate {
            Layout.fillWidth: true
            Accessible.name: i18n("Create a custom rule with a composite match expression")
            onClicked: sheet._choose("custom")

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                Kirigami.Icon {
                    source: "configure"
                    Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                    Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    Label {
                        text: i18n("Custom…")
                        font.bold: true
                    }

                    Label {
                        text: i18n("Build a composite AND / OR / NOT match expression.")
                        opacity: 0.7
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                }

            }

        }

    }

}

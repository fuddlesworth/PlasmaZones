// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Visibility settings dialog
 *
 * Per-context layout visibility (Tier 2): which screens, virtual desktops,
 * and activities this layout should appear on in the zone selector.
 * Empty lists = visible everywhere (opt-in).
 */
Kirigami.Dialog {
    id: root

    required property var editorController

    title: i18nc("@title:window", "Zone Selector Visibility")
    standardButtons: Kirigami.Dialog.NoButton

    footer: Item {
        implicitHeight: footerLayout.implicitHeight + Kirigami.Units.largeSpacing * 2
        implicitWidth: footerLayout.implicitWidth

        RowLayout {
            id: footerLayout
            anchors.fill: parent
            anchors.margins: Kirigami.Units.largeSpacing

            Item { Layout.fillWidth: true }

            Button {
                text: i18nc("@action:button", "Apply")
                icon.name: "dialog-ok-apply"
                onClicked: root.close()
            }
        }
    }
    preferredWidth: Kirigami.Units.gridUnit * 18
    padding: Kirigami.Units.largeSpacing

    ColumnLayout {
        spacing: Kirigami.Units.mediumSpacing

        // Description
        Label {
            text: i18nc("@info", "Choose where this layout appears in the zone selector popup. All checked means visible everywhere.")
            wrapMode: Text.WordWrap
            opacity: 0.7
            Layout.fillWidth: true
            Layout.maximumWidth: root.preferredWidth - root.padding * 2
        }

        Kirigami.Separator {
            Layout.fillWidth: true
            visible: screensSection.visible || desktopsSection.visible || activitiesSection.visible
        }

        // ─── Screens section ─────────────────────────────
        ColumnLayout {
            id: screensSection
            visible: root.editorController && root.editorController.availableScreenIds.length > 1
            spacing: Kirigami.Units.smallSpacing
            Layout.fillWidth: true

            SectionHeader {
                title: i18nc("@title:group", "Screens")
                icon: "monitor"
            }

            Repeater {
                id: screensRepeater
                model: root.editorController ? root.editorController.availableScreenIds : []

                CheckBox {
                    required property string modelData
                    required property int index

                    text: root.editorController ? root.editorController.screenDisplayName(modelData) : modelData
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    checked: isChecked()

                    function isChecked() {
                        if (!root.editorController) return true
                        var list = root.editorController.allowedScreens
                        return list.length === 0 || list.indexOf(modelData) >= 0
                    }

                    onClicked: {
                        if (root.editorController)
                            root.editorController.toggleScreenAllowed(modelData)
                    }

                    Connections {
                        target: root.editorController
                        function onAllowedScreensChanged() {
                            checked = isChecked()
                        }
                    }
                }
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
            visible: screensSection.visible && desktopsSection.visible
        }

        // ─── Virtual Desktops section ────────────────────
        ColumnLayout {
            id: desktopsSection
            visible: root.editorController && root.editorController.virtualDesktopCount > 1
            spacing: Kirigami.Units.smallSpacing
            Layout.fillWidth: true

            SectionHeader {
                title: i18nc("@title:group", "Virtual Desktops")
                icon: "virtual-desktops"
            }

            Repeater {
                id: desktopsRepeater
                model: root.editorController && root.editorController.virtualDesktopCount > 1 ? root.editorController.virtualDesktopCount : 0

                CheckBox {
                    required property int index
                    readonly property int desktop: index + 1

                    text: root.editorController.virtualDesktopNames[index] || (i18nc("@label", "Desktop %1", desktop))
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    checked: isChecked()

                    function isChecked() {
                        if (!root.editorController) return true
                        var list = root.editorController.allowedDesktops
                        return list.length === 0 || list.indexOf(Number(desktop)) >= 0
                    }

                    onClicked: {
                        if (root.editorController)
                            root.editorController.toggleDesktopAllowed(desktop)
                    }

                    Connections {
                        target: root.editorController
                        function onAllowedDesktopsChanged() {
                            checked = isChecked()
                        }
                    }
                }
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
            visible: activitiesSection.visible && (screensSection.visible || desktopsSection.visible)
        }

        // ─── Activities section ───────────────────────────
        ColumnLayout {
            id: activitiesSection
            visible: root.editorController && root.editorController.activitiesAvailable
            spacing: Kirigami.Units.smallSpacing
            Layout.fillWidth: true

            SectionHeader {
                title: i18nc("@title:group", "Activities")
                icon: "activities"
            }

            Repeater {
                id: activitiesRepeater
                model: root.editorController && root.editorController.activitiesAvailable ? root.editorController.availableActivities : []

                CheckBox {
                    required property var modelData
                    required property int index
                    readonly property string activityId: modelData.id || ""

                    text: modelData.name || modelData.id
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    checked: isChecked()

                    function isChecked() {
                        if (!root.editorController) return true
                        var list = root.editorController.allowedActivities
                        return list.length === 0 || list.indexOf(activityId) >= 0
                    }

                    onClicked: {
                        if (root.editorController)
                            root.editorController.toggleActivityAllowed(activityId)
                    }

                    Connections {
                        target: root.editorController
                        function onAllowedActivitiesChanged() {
                            checked = isChecked()
                        }
                    }
                }
            }
        }
    }

}

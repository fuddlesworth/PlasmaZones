// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import ".."

/**
 * @brief Monitor assignments card - Assign layouts to monitors and virtual desktops
 */
Kirigami.Card {
    id: root

    required property var kcm
    required property QtObject constants

    header: Kirigami.Heading {
        level: 3
        text: i18n("Monitor Assignments")
        padding: Kirigami.Units.smallSpacing
    }

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        ListView {
            id: monitorListView
            Layout.fillWidth: true
            Layout.preferredHeight: count > 0 ? contentHeight : Kirigami.Units.gridUnit * 4
            Layout.margins: Kirigami.Units.smallSpacing
            clip: true
            model: root.kcm.screens
            interactive: false

            Accessible.name: i18n("Monitor list")
            Accessible.role: Accessible.List

            delegate: Item {
                id: monitorDelegate
                width: ListView.view.width
                height: monitorContent.implicitHeight
                required property var modelData
                required property int index

                property bool expanded: false
                property string screenName: modelData.name || ""

                ColumnLayout {
                    id: monitorContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.leftMargin: Kirigami.Units.smallSpacing
                    anchors.rightMargin: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.smallSpacing

                    // Top spacer
                    Item { height: Kirigami.Units.smallSpacing }

                    // Header row - always visible
                    RowLayout {
                        id: monitorHeaderRow
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        Kirigami.Icon {
                            source: "video-display"
                            Layout.preferredWidth: Kirigami.Units.iconSizes.small
                            Layout.preferredHeight: Kirigami.Units.iconSizes.small
                            Layout.alignment: Qt.AlignTop
                        }

                        ColumnLayout {
                            Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                            spacing: 0

                            Label {
                                id: monitorNameLabel
                                text: modelData.name || i18n("Unknown Monitor")
                                font.bold: true
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Label {
                                text: {
                                    let info = modelData.resolution || ""
                                    if (modelData.isPrimary) {
                                        info += (info ? " • " : "") + i18n("Primary")
                                    }
                                    return info
                                }
                                opacity: root.constants.labelSecondaryOpacity
                                font.pointSize: Kirigami.Theme.smallFont.pointSize
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }

                        // "All Desktops" label
                        Label {
                            text: i18n("All Desktops:")
                            Layout.alignment: Qt.AlignVCenter
                        }

                        LayoutComboBox {
                            id: screenLayoutCombo
                            Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                            kcm: root.kcm
                            noneText: i18n("Default")

                            function updateFromAssignment() {
                                currentLayoutId = root.kcm.getLayoutForScreen(monitorDelegate.screenName) || ""
                            }

                            Component.onCompleted: updateFromAssignment()

                            Connections {
                                target: root.kcm
                                function onScreenAssignmentsChanged() {
                                    screenLayoutCombo.updateFromAssignment()
                                }
                            }

                            onActivated: {
                                let selectedValue = model[currentIndex].value
                                if (selectedValue === "") {
                                    root.kcm.clearScreenAssignment(monitorDelegate.screenName)
                                } else {
                                    root.kcm.assignLayoutToScreen(monitorDelegate.screenName, selectedValue)
                                }
                            }
                        }

                        ToolButton {
                            icon.name: "edit-clear"
                            onClicked: {
                                root.kcm.clearScreenAssignment(monitorDelegate.screenName)
                                screenLayoutCombo.clearSelection()
                            }
                            ToolTip.visible: hovered
                            ToolTip.text: i18n("Clear assignment")
                            Accessible.name: i18n("Clear assignment for %1", monitorDelegate.screenName)
                        }

                        Item { Layout.fillWidth: true }

                        // Expand button - only show if multiple virtual desktops
                        ToolButton {
                            visible: root.kcm.virtualDesktopCount > 1
                            icon.name: monitorDelegate.expanded ? "go-up" : "go-down"
                            text: monitorDelegate.expanded ? "" : i18n("Per-desktop")
                            display: AbstractButton.TextBesideIcon
                            onClicked: monitorDelegate.expanded = !monitorDelegate.expanded
                            ToolTip.visible: hovered
                            ToolTip.text: monitorDelegate.expanded ?
                                i18n("Hide per-desktop assignments") :
                                i18n("Show per-desktop assignments")
                        }
                    }

                    // Disable PlasmaZones on this monitor
                    CheckBox {
                        Layout.fillWidth: true
                        text: i18n("Disable PlasmaZones on this monitor")
                        checked: root.kcm.disabledMonitors.indexOf(monitorDelegate.screenName) >= 0
                        onToggled: root.kcm.setMonitorDisabled(monitorDelegate.screenName, checked)
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("When enabled, the zone overlay and zone picker will not appear on this monitor, and windows will not snap to zones here.")
                    }

                    // Per-desktop assignments section - expandable
                    ColumnLayout {
                        id: perDesktopSection
                        Layout.fillWidth: true
                        Layout.leftMargin: Kirigami.Units.gridUnit * 2
                        visible: monitorDelegate.expanded && root.kcm.virtualDesktopCount > 1
                        spacing: Kirigami.Units.smallSpacing

                        Kirigami.Separator {
                            Layout.fillWidth: true
                        }

                        Label {
                            text: i18n("Per-Desktop Overrides")
                            font.bold: true
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                            opacity: 0.8
                        }

                        Label {
                            text: i18n("Override the default layout for specific virtual desktops")
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                            opacity: root.constants.labelSecondaryOpacity
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        // Per-desktop combo boxes
                        Repeater {
                            model: root.kcm.virtualDesktopCount

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing

                                required property int index

                                property int desktopNumber: index + 1
                                property string desktopName: root.kcm.virtualDesktopNames[index] || i18n("Desktop %1", desktopNumber)

                                Kirigami.Icon {
                                    source: "preferences-desktop-virtual"
                                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                    Layout.preferredHeight: Kirigami.Units.iconSizes.small
                                    Layout.alignment: Qt.AlignVCenter
                                }

                                Label {
                                    text: desktopName
                                    Layout.preferredWidth: Kirigami.Units.gridUnit * 8
                                    Layout.alignment: Qt.AlignVCenter
                                    elide: Text.ElideRight
                                }

                                LayoutComboBox {
                                    id: desktopLayoutCombo
                                    Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                                    kcm: root.kcm
                                    noneText: i18n("Use default")

                                    property int desktopNum: desktopNumber

                                    function updateFromAssignment() {
                                        let hasExplicit = root.kcm.hasExplicitAssignmentForScreenDesktop(monitorDelegate.screenName, desktopNum)
                                        if (!hasExplicit) {
                                            currentLayoutId = ""
                                            return
                                        }
                                        currentLayoutId = root.kcm.getLayoutForScreenDesktop(monitorDelegate.screenName, desktopNum) || ""
                                    }

                                    Component.onCompleted: updateFromAssignment()

                                    Connections {
                                        target: root.kcm
                                        function onScreenAssignmentsChanged() {
                                            desktopLayoutCombo.updateFromAssignment()
                                        }
                                    }

                                    onActivated: {
                                        let selectedValue = model[currentIndex].value
                                        if (selectedValue === "") {
                                            root.kcm.clearScreenDesktopAssignment(monitorDelegate.screenName, desktopNum)
                                        } else {
                                            root.kcm.assignLayoutToScreenDesktop(monitorDelegate.screenName, desktopNum, selectedValue)
                                        }
                                    }
                                }

                                ToolButton {
                                    icon.name: "edit-clear"
                                    onClicked: {
                                        root.kcm.clearScreenDesktopAssignment(monitorDelegate.screenName, desktopLayoutCombo.desktopNum)
                                        desktopLayoutCombo.clearSelection()
                                    }
                                    ToolTip.visible: hovered
                                    ToolTip.text: i18n("Clear assignment for this desktop")
                                }

                                Item { Layout.fillWidth: true }
                            }
                        }
                    }

                    // Bottom spacer
                    Item { height: Kirigami.Units.smallSpacing }
                }
            }

            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - Kirigami.Units.gridUnit * 4
                visible: parent.count === 0
                text: i18n("No monitors detected")
                explanation: i18n("Make sure the PlasmaZones daemon is running")
            }
        }

        // Info message when only one virtual desktop
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            visible: root.kcm.virtualDesktopCount <= 1 && root.kcm.screens.length > 0
            type: Kirigami.MessageType.Information
            text: i18n("Per-desktop layout assignments are available when using multiple virtual desktops. Add more desktops in System Settings → Virtual Desktops.")
        }
    }
}

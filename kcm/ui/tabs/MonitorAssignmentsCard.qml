// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Monitor assignments card - Assign layouts to monitors and virtual desktops
 *
 * Refactored to use AssignmentRow component, eliminating duplicated patterns.
 */
Kirigami.Card {
    id: root

    required property var kcm
    required property QtObject constants
    // 0 = snapping (zone layouts), 1 = tiling (autotile algorithms)
    property int viewMode: 0

    header: Kirigami.Heading {
        level: 3
        text: root.viewMode === 1 ? i18n("Monitor Tiling Assignments") : i18n("Monitor Assignments")
        padding: Kirigami.Units.smallSpacing
    }

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        ListView {
            id: monitorListView

            Layout.fillWidth: true
            // Use model length for reactive height calculation (count may lag on first load)
            Layout.preferredHeight: (root.kcm.screens && root.kcm.screens.length > 0) ? contentHeight : Kirigami.Units.gridUnit * 4
            Layout.margins: Kirigami.Units.smallSpacing
            clip: true
            model: root.kcm.screens
            interactive: false
            Accessible.name: i18n("Monitor list")
            Accessible.role: Accessible.List

            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - Kirigami.Units.gridUnit * 4
                visible: parent.count === 0
                text: i18n("No monitors detected")
                explanation: i18n("Make sure the PlasmaZones daemon is running")
            }

            delegate: Item {
                id: monitorDelegate

                required property var modelData
                required property int index
                property bool expanded: false
                property string screenName: modelData.name || ""
                // Revision counter — incremented when assignment data changes,
                // forcing currentLayoutId bindings to re-evaluate without
                // breaking the binding (imperative assignment breaks bindings).
                property int _assignmentRevision: 0

                width: ListView.view.width
                height: monitorContent.implicitHeight

                ColumnLayout {
                    id: monitorContent

                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.smallSpacing

                    Item {
                        height: Kirigami.Units.smallSpacing
                    }

                    // Monitor header with assignment
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        // Monitor info
                        Kirigami.Icon {
                            source: "video-display"
                            Layout.preferredWidth: Kirigami.Units.iconSizes.small
                            Layout.preferredHeight: Kirigami.Units.iconSizes.small
                            Layout.alignment: Qt.AlignTop
                        }

                        ColumnLayout {
                            Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                            spacing: 0

                            Label {
                                text: {
                                    let name = modelData.name || i18n("Unknown Monitor");
                                    let mfr = modelData.manufacturer || "";
                                    let mdl = modelData.model || "";
                                    let parts = [mfr, mdl].filter(function(s) {
                                        return s !== "";
                                    });
                                    let displayInfo = parts.join(" ");
                                    return displayInfo ? name + " — " + displayInfo : name;
                                }
                                font.bold: true
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Label {
                                text: {
                                    let info = modelData.resolution || "";
                                    if (modelData.isPrimary)
                                        info += (info ? " • " : "") + i18n("Primary");

                                    return info;
                                }
                                opacity: root.constants.labelSecondaryOpacity
                                font.pointSize: Kirigami.Theme.smallFont.pointSize
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                        }

                        Label {
                            text: root.viewMode === 1 ? i18n("Algorithm:") : i18n("All Desktops:")
                            Layout.alignment: Qt.AlignVCenter
                        }

                        // Screen-level assignment using LayoutComboBox directly
                        // (AssignmentRow adds icon+label which we already have)
                        LayoutComboBox {
                            id: screenLayoutCombo

                            Layout.preferredWidth: Kirigami.Units.gridUnit * 16
                            kcm: root.kcm
                            noneText: i18n("Default")
                            showPreview: root.viewMode === 0
                            layoutFilter: root.viewMode === 1 ? 1 : 0
                            currentLayoutId: {
                                void (monitorDelegate._assignmentRevision); // force re-evaluate on data change
                                return root.viewMode === 1 ? (root.kcm.getTilingLayoutForScreen(monitorDelegate.screenName) || "") : (root.kcm.getLayoutForScreen(monitorDelegate.screenName) || "");
                            }
                            onActivated: {
                                let selectedValue = model[currentIndex].value;
                                if (root.viewMode === 1) {
                                    if (selectedValue === "")
                                        root.kcm.clearTilingScreenAssignment(monitorDelegate.screenName);
                                    else
                                        root.kcm.assignTilingLayoutToScreen(monitorDelegate.screenName, selectedValue);
                                } else {
                                    if (selectedValue === "")
                                        root.kcm.clearScreenAssignment(monitorDelegate.screenName);
                                    else
                                        root.kcm.assignLayoutToScreen(monitorDelegate.screenName, selectedValue);
                                }
                            }

                            Connections {
                                function onScreenAssignmentsChanged() {
                                    monitorDelegate._assignmentRevision++;
                                }

                                function onTilingScreenAssignmentsChanged() {
                                    monitorDelegate._assignmentRevision++;
                                }

                                function onTilingDesktopAssignmentsChanged() {
                                    monitorDelegate._assignmentRevision++;
                                }

                                target: root.kcm
                            }

                        }

                        ToolButton {
                            icon.name: "edit-clear"
                            onClicked: {
                                if (root.viewMode === 1)
                                    root.kcm.clearTilingScreenAssignment(monitorDelegate.screenName);
                                else
                                    root.kcm.clearScreenAssignment(monitorDelegate.screenName);
                                screenLayoutCombo.clearSelection();
                            }
                            ToolTip.visible: hovered
                            ToolTip.text: i18n("Clear assignment")
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        ToolButton {
                            visible: root.kcm.virtualDesktopCount > 1
                            icon.name: monitorDelegate.expanded ? "go-up" : "go-down"
                            text: monitorDelegate.expanded ? "" : i18n("Per-desktop")
                            display: AbstractButton.TextBesideIcon
                            onClicked: monitorDelegate.expanded = !monitorDelegate.expanded
                            ToolTip.visible: hovered
                            ToolTip.text: monitorDelegate.expanded ? i18n("Hide per-desktop assignments") : i18n("Show per-desktop assignments")
                        }

                    }

                    // Monitor disable option
                    CheckBox {
                        id: disableCheck

                        Layout.fillWidth: true
                        text: i18n("Disable PlasmaZones on this monitor")
                        checked: root.kcm.isMonitorDisabled(monitorDelegate.screenName)
                        onToggled: root.kcm.setMonitorDisabled(monitorDelegate.screenName, checked)
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("When enabled, zones will not appear on this monitor")

                        Connections {
                            function onDisabledMonitorsChanged() {
                                disableCheck.checked = root.kcm.isMonitorDisabled(monitorDelegate.screenName);
                            }

                            target: root.kcm
                        }

                    }

                    // Per-desktop section (expandable)
                    ColumnLayout {
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

                        // Per-desktop assignments using AssignmentRow
                        Repeater {
                            model: root.kcm.virtualDesktopCount

                            AssignmentRow {
                                id: desktopRow

                                required property int index
                                property int desktopNumber: index + 1
                                property string desktopName: root.kcm.virtualDesktopNames[index] || i18n("Desktop %1", desktopNumber)
                                // Per-desktop "Default" resolves to monitor's layout (or global if monitor has none)
                                property string monitorLayout: {
                                    void (monitorDelegate._assignmentRevision);
                                    return root.viewMode === 1 ? (root.kcm.getTilingLayoutForScreen(monitorDelegate.screenName) || "") : (root.kcm.getLayoutForScreen(monitorDelegate.screenName) || "");
                                }

                                Layout.fillWidth: true
                                kcm: root.kcm
                                iconSource: "preferences-desktop-virtual"
                                labelText: desktopName
                                layoutFilter: root.viewMode === 1 ? 1 : 0
                                showPreview: root.viewMode === 0
                                noneText: i18n("Use default")
                                resolvedDefaultId: monitorLayout !== "" ? monitorLayout : (root.kcm.defaultLayoutId || "")
                                currentLayoutId: {
                                    void (monitorDelegate._assignmentRevision);
                                    if (root.viewMode === 1) {
                                        let hasExplicit = root.kcm.hasExplicitTilingAssignmentForScreenDesktop(monitorDelegate.screenName, desktopNumber);
                                        return hasExplicit ? (root.kcm.getTilingLayoutForScreenDesktop(monitorDelegate.screenName, desktopNumber) || "") : "";
                                    } else {
                                        let hasExplicit = root.kcm.hasExplicitAssignmentForScreenDesktop(monitorDelegate.screenName, desktopNumber);
                                        return hasExplicit ? (root.kcm.getSnappingLayoutForScreenDesktop(monitorDelegate.screenName, desktopNumber) || "") : "";
                                    }
                                }
                                onAssignmentSelected: (layoutId) => {
                                    if (root.viewMode === 1)
                                        root.kcm.assignTilingLayoutToScreenDesktop(monitorDelegate.screenName, desktopNumber, layoutId);
                                    else
                                        root.kcm.assignLayoutToScreenDesktop(monitorDelegate.screenName, desktopNumber, layoutId);
                                }
                                onAssignmentCleared: {
                                    if (root.viewMode === 1)
                                        root.kcm.clearTilingScreenDesktopAssignment(monitorDelegate.screenName, desktopNumber);
                                    else
                                        root.kcm.clearScreenDesktopAssignment(monitorDelegate.screenName, desktopNumber);
                                }
                            }

                        }

                    }

                    Item {
                        height: Kirigami.Units.smallSpacing
                    }

                }

            }

        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            visible: root.kcm.virtualDesktopCount <= 1 && root.kcm.screens.length > 0
            type: Kirigami.MessageType.Information
            text: i18n("Per-desktop assignments are available with multiple virtual desktops.")
        }

    }

}

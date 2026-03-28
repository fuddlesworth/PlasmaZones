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
SettingsCard {
    id: root

    required property var appSettings
    // 0 = snapping (zone layouts), 1 = tiling (autotile algorithms)
    property int viewMode: 0
    // Revision counter — incremented when lock state changes externally,
    // forcing lock-dependent bindings to re-evaluate.
    property int _lockRevision: 0

    headerText: root.viewMode === 1 ? i18n("Monitor Tiling Assignments") : i18n("Monitor Assignments")
    collapsible: true

    Connections {
        function onLockedScreensChanged() {
            root._lockRevision++;
        }

        target: root.appSettings
    }

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        ListView {
            id: monitorListView

            Layout.fillWidth: true
            // Use model length for reactive height calculation (count may lag on first load)
            Layout.preferredHeight: (root.appSettings.screens && root.appSettings.screens.length > 0) ? contentHeight : Kirigami.Units.gridUnit * 4
            Layout.margins: Kirigami.Units.smallSpacing
            clip: true
            model: root.appSettings.screens
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
                                font.weight: Font.DemiBold
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
                                opacity: 0.7
                                font: Kirigami.Theme.smallFont
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
                            enabled: {
                                void (monitorDelegate._assignmentRevision);
                                void (root._lockRevision);
                                return !root.appSettings.isScreenLocked(monitorDelegate.screenName, root.viewMode);
                            }
                            appSettings: root.appSettings
                            noneText: i18n("Default")
                            showPreview: true
                            layoutFilter: root.viewMode === 1 ? 1 : 0
                            currentLayoutId: {
                                void (monitorDelegate._assignmentRevision); // force re-evaluate on data change
                                return root.viewMode === 1 ? (root.appSettings.getTilingLayoutForScreen(monitorDelegate.screenName) || "") : (root.appSettings.getLayoutForScreen(monitorDelegate.screenName) || "");
                            }
                            onActivated: {
                                let selectedValue = model[currentIndex].value;
                                if (root.viewMode === 1) {
                                    if (selectedValue === "")
                                        root.appSettings.clearTilingScreenAssignment(monitorDelegate.screenName);
                                    else
                                        root.appSettings.assignTilingLayoutToScreen(monitorDelegate.screenName, selectedValue);
                                } else {
                                    if (selectedValue === "")
                                        root.appSettings.clearScreenAssignment(monitorDelegate.screenName);
                                    else
                                        root.appSettings.assignLayoutToScreen(monitorDelegate.screenName, selectedValue);
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

                                function onLockedScreensChanged() {
                                    monitorDelegate._assignmentRevision++;
                                }

                                target: root.appSettings
                            }

                        }

                        ToolButton {
                            icon.name: "edit-clear"
                            enabled: {
                                void (monitorDelegate._assignmentRevision);
                                void (root._lockRevision);
                                return !root.appSettings.isScreenLocked(monitorDelegate.screenName, root.viewMode);
                            }
                            onClicked: {
                                if (root.viewMode === 1)
                                    root.appSettings.clearTilingScreenAssignment(monitorDelegate.screenName);
                                else
                                    root.appSettings.clearScreenAssignment(monitorDelegate.screenName);
                                screenLayoutCombo.clearSelection();
                            }
                            ToolTip.visible: hovered
                            ToolTip.text: i18n("Clear assignment")
                        }

                        ToolButton {
                            icon.name: {
                                void (monitorDelegate._assignmentRevision);
                                void (root._lockRevision);
                                return root.appSettings.isScreenLocked(monitorDelegate.screenName, root.viewMode) ? "object-locked" : "object-unlocked";
                            }
                            opacity: {
                                void (monitorDelegate._assignmentRevision);
                                void (root._lockRevision);
                                return root.appSettings.isScreenLocked(monitorDelegate.screenName, root.viewMode) ? 1 : 0.4;
                            }
                            display: ToolButton.IconOnly
                            ToolTip.text: {
                                void (monitorDelegate._assignmentRevision);
                                void (root._lockRevision);
                                return root.appSettings.isScreenLocked(monitorDelegate.screenName, root.viewMode) ? i18n("Unlock layout for this monitor") : i18n("Lock layout for this monitor");
                            }
                            ToolTip.visible: hovered
                            onClicked: root.appSettings.toggleScreenLock(monitorDelegate.screenName, root.viewMode)
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        ToolButton {
                            visible: root.appSettings.virtualDesktopCount > 1
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
                        checked: root.appSettings.isMonitorDisabled(monitorDelegate.screenName)
                        onToggled: root.appSettings.setMonitorDisabled(monitorDelegate.screenName, checked)
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("When enabled, zones will not appear on this monitor")

                        Connections {
                            function onDisabledMonitorsChanged() {
                                disableCheck.checked = root.appSettings.isMonitorDisabled(monitorDelegate.screenName);
                            }

                            target: root.appSettings
                        }

                    }

                    // Per-desktop section (expandable)
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: Kirigami.Units.gridUnit * 2
                        visible: monitorDelegate.expanded && root.appSettings.virtualDesktopCount > 1
                        spacing: Kirigami.Units.smallSpacing

                        Kirigami.Separator {
                            Layout.fillWidth: true
                        }

                        Label {
                            text: i18n("Per-Desktop Overrides")
                            font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                            font.weight: Font.DemiBold
                            opacity: 0.7
                        }

                        // Per-desktop assignments using AssignmentRow
                        Repeater {
                            model: root.appSettings.virtualDesktopCount

                            ColumnLayout {
                                id: desktopRowContainer

                                required property int index
                                property int desktopNumber: index + 1
                                property string desktopName: root.appSettings.virtualDesktopNames[index] || i18n("Desktop %1", desktopNumber)

                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Kirigami.Units.smallSpacing

                                    AssignmentRow {
                                        id: desktopRow

                                        // Per-desktop "Default" resolves to monitor's layout (or global if monitor has none)
                                        property string monitorLayout: {
                                            void (monitorDelegate._assignmentRevision);
                                            return root.viewMode === 1 ? (root.appSettings.getTilingLayoutForScreen(monitorDelegate.screenName) || "") : (root.appSettings.getLayoutForScreen(monitorDelegate.screenName) || "");
                                        }

                                        Layout.fillWidth: true
                                        enabled: {
                                            void (monitorDelegate._assignmentRevision);
                                            void (root._lockRevision);
                                            return !root.appSettings.isContextLocked(monitorDelegate.screenName, desktopRowContainer.desktopNumber, "", root.viewMode);
                                        }
                                        appSettings: root.appSettings
                                        iconSource: "preferences-desktop-virtual"
                                        labelText: desktopRowContainer.desktopName
                                        layoutFilter: root.viewMode === 1 ? 1 : 0
                                        showPreview: true
                                        noneText: i18n("Use default")
                                        resolvedDefaultId: monitorLayout !== "" ? monitorLayout : (root.appSettings.defaultLayoutId || "")
                                        currentLayoutId: {
                                            void (monitorDelegate._assignmentRevision);
                                            if (root.viewMode === 1) {
                                                let hasExplicit = root.appSettings.hasExplicitTilingAssignmentForScreenDesktop(monitorDelegate.screenName, desktopRowContainer.desktopNumber);
                                                return hasExplicit ? (root.appSettings.getTilingLayoutForScreenDesktop(monitorDelegate.screenName, desktopRowContainer.desktopNumber) || "") : "";
                                            } else {
                                                let hasExplicit = root.appSettings.hasExplicitAssignmentForScreenDesktop(monitorDelegate.screenName, desktopRowContainer.desktopNumber);
                                                return hasExplicit ? (root.appSettings.getSnappingLayoutForScreenDesktop(monitorDelegate.screenName, desktopRowContainer.desktopNumber) || "") : "";
                                            }
                                        }
                                        onAssignmentSelected: (layoutId) => {
                                            if (root.viewMode === 1)
                                                root.appSettings.assignTilingLayoutToScreenDesktop(monitorDelegate.screenName, desktopRowContainer.desktopNumber, layoutId);
                                            else
                                                root.appSettings.assignLayoutToScreenDesktop(monitorDelegate.screenName, desktopRowContainer.desktopNumber, layoutId);
                                        }
                                        onAssignmentCleared: {
                                            if (root.viewMode === 1)
                                                root.appSettings.clearTilingScreenDesktopAssignment(monitorDelegate.screenName, desktopRowContainer.desktopNumber);
                                            else
                                                root.appSettings.clearScreenDesktopAssignment(monitorDelegate.screenName, desktopRowContainer.desktopNumber);
                                        }
                                    }

                                    ToolButton {
                                        icon.name: {
                                            void (monitorDelegate._assignmentRevision);
                                            void (root._lockRevision);
                                            return root.appSettings.isContextLocked(monitorDelegate.screenName, desktopRowContainer.desktopNumber, "", root.viewMode) ? "object-locked" : "object-unlocked";
                                        }
                                        opacity: {
                                            void (monitorDelegate._assignmentRevision);
                                            void (root._lockRevision);
                                            return root.appSettings.isContextLocked(monitorDelegate.screenName, desktopRowContainer.desktopNumber, "", root.viewMode) ? 1 : 0.4;
                                        }
                                        display: ToolButton.IconOnly
                                        ToolTip.text: {
                                            void (monitorDelegate._assignmentRevision);
                                            void (root._lockRevision);
                                            return root.appSettings.isContextLocked(monitorDelegate.screenName, desktopRowContainer.desktopNumber, "", root.viewMode) ? i18n("Unlock layout for %1", desktopRowContainer.desktopName) : i18n("Lock layout for %1", desktopRowContainer.desktopName);
                                        }
                                        ToolTip.visible: hovered
                                        onClicked: root.appSettings.toggleContextLock(monitorDelegate.screenName, desktopRowContainer.desktopNumber, "", root.viewMode)
                                    }

                                }

                                CheckBox {
                                    id: desktopDisableCheck

                                    Layout.fillWidth: true
                                    Layout.leftMargin: Kirigami.Units.gridUnit
                                    text: i18n("Disable PlasmaZones on this desktop")
                                    checked: root.appSettings.isDesktopDisabled(monitorDelegate.screenName, desktopRowContainer.desktopNumber)
                                    onToggled: root.appSettings.setDesktopDisabled(monitorDelegate.screenName, desktopRowContainer.desktopNumber, checked)
                                    ToolTip.visible: hovered
                                    ToolTip.text: i18n("When enabled, zones will not appear on %1 for this monitor", desktopRowContainer.desktopName)

                                    Connections {
                                        function onDisabledDesktopsChanged() {
                                            desktopDisableCheck.checked = root.appSettings.isDesktopDisabled(monitorDelegate.screenName, desktopDisableCheck.parent.desktopNumber);
                                        }

                                        target: root.appSettings
                                    }

                                }

                            }

                        }

                        Kirigami.InlineMessage {
                            function allDesktopsDisabledOnScreen() {
                                let count = root.appSettings.virtualDesktopCount;
                                if (count <= 1)
                                    return false;

                                for (let i = 1; i <= count; i++) {
                                    if (!root.appSettings.isDesktopDisabled(monitorDelegate.screenName, i))
                                        return false;

                                }
                                return true;
                            }

                            Layout.fillWidth: true
                            visible: allDesktopsDisabledOnScreen()
                            type: Kirigami.MessageType.Warning
                            text: i18n("All desktops are disabled on this monitor.")

                            Connections {
                                function onDisabledDesktopsChanged() {
                                    parent.visible = parent.allDesktopsDisabledOnScreen();
                                }

                                target: root.appSettings
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
            visible: root.appSettings.virtualDesktopCount <= 1 && root.appSettings.screens.length > 0
            type: Kirigami.MessageType.Information
            text: i18n("Per-desktop assignments are available with multiple virtual desktops.")
        }

    }

}

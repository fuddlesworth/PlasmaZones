// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @brief Main KCM settings UI with tabbed interface
 *
 * Pages:
 * - Layouts: View, create, edit, import/export layouts
 * - Editor: Keyboard shortcuts and snapping settings
 * - Assignments: Monitor, activity, and quick layout assignments
 * - Zones: Appearance and behavior settings (merged for consistency)
 * - Display: Zone selector popup and OSD settings
 * - Exclusions: Apps and windows to exclude from snapping
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore
import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCM
import "tabs"

KCM.AbstractKCM {
    id: root

    title: i18n("PlasmaZones")
    framedView: false

    // Capture the kcm context property as a regular property for passing to child components
    // Context properties don't automatically propagate to imported QML modules
    readonly property var kcmModule: kcm

    // Screen aspect ratio for locked preview calculations
    readonly property real screenAspectRatio: Screen.width > 0 && Screen.height > 0
        ? (Screen.width / Screen.height)
        : (16.0 / 9.0)

    // Effective preview dimensions (matches overlayservice.cpp auto-sizing logic)
    // Auto mode: width = clamp(screenWidth/10, 120, 280), height = width/aspectRatio
    // Manual mode: use settings values
    readonly property int effectivePreviewWidth: kcm.zoneSelectorSizeMode === 0
        ? Math.max(120, Math.min(280, Math.round(Screen.width / 10)))
        : kcm.zoneSelectorPreviewWidth
    readonly property int effectivePreviewHeight: kcm.zoneSelectorSizeMode === 0
        ? Math.round(effectivePreviewWidth / screenAspectRatio)
        : kcm.zoneSelectorPreviewHeight

    // Constants to eliminate magic numbers
    QtObject {
        id: constants

        // Layout dimensions
        readonly property int layoutListMinHeight: 150
        readonly property int sliderPreferredWidth: 200
        readonly property int sliderValueLabelWidth: 40
        readonly property int colorButtonSize: 32

        // Opacity values
        readonly property real labelSecondaryOpacity: 0.7

        // Slider ranges
        readonly property int opacitySliderMax: 100
        readonly property int borderWidthMax: 10
        readonly property int borderRadiusMax: 50
        readonly property int paddingMax: 100
        readonly property int thresholdMax: 50
        readonly property int zoneSelectorTriggerMax: 200
        readonly property int zoneSelectorPreviewWidthMin: 80
        readonly property int zoneSelectorPreviewWidthMax: 400
        readonly property int zoneSelectorPreviewHeightMin: 60
        readonly property int zoneSelectorPreviewHeightMax: 300
        readonly property int zoneSelectorGridColumnsMax: 10

        // Quick layout shortcuts
        readonly property int quickLayoutSlotCount: 9

        // Layout type ratios (matching C++ Defaults)
        readonly property real priorityGridMainRatio: 0.67
        readonly property real priorityGridSecondaryRatio: 0.33
        readonly property real focusSideRatio: 0.15
        readonly property real focusMainRatio: 0.70

    }

    header: ColumnLayout {
        width: parent.width
        spacing: 0

        // Master enable/disable row
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.largeSpacing

            Label {
                text: i18n("Enable PlasmaZones")
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            Label {
                text: kcm.daemonRunning ? i18n("Running") : i18n("Stopped")
                opacity: 0.7
            }

            Switch {
                id: daemonEnabledSwitch
                checked: kcm.daemonEnabled
                onToggled: kcm.daemonEnabled = checked
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
            visible: kcm.daemonEnabled
        }

        TabBar {
            id: tabBar
            Layout.fillWidth: true
            visible: kcm.daemonEnabled

            TabButton {
                text: i18n("Layouts")
                icon.name: "view-grid-symbolic"
            }
            TabButton {
                text: i18n("Editor")
                icon.name: "document-edit"
            }
            TabButton {
                text: i18n("Assignments")
                icon.name: "view-list-details"
            }
            TabButton {
                text: i18n("Zones")
                icon.name: "view-split-left-right"
            }
            TabButton {
                text: i18n("Display")
                icon.name: "select-rectangular"
            }
            TabButton {
                text: i18n("Exclusions")
                icon.name: "dialog-cancel-symbolic"
            }
        }
    }

    // Placeholder when daemon is disabled
    Item {
        anchors.fill: parent
        visible: !kcm.daemonEnabled

        ColumnLayout {
            anchors.centerIn: parent
            spacing: Kirigami.Units.largeSpacing

            Kirigami.Icon {
                source: "plasmazones"
                Layout.preferredWidth: Kirigami.Units.iconSizes.huge
                Layout.preferredHeight: Kirigami.Units.iconSizes.huge
                Layout.alignment: Qt.AlignHCenter
                opacity: 0.5
            }

            Label {
                text: i18n("PlasmaZones is disabled")
                font.bold: true
                Layout.alignment: Qt.AlignHCenter
            }

            Label {
                text: i18n("Turn on PlasmaZones to manage window layouts.")
                opacity: 0.7
                Layout.alignment: Qt.AlignHCenter
            }
        }
    }

    StackLayout {
        id: stackLayout
        anchors.fill: parent
        anchors.margins: Kirigami.Units.largeSpacing
        currentIndex: tabBar.currentIndex
        visible: kcm.daemonEnabled

        // TAB 1: LAYOUTS
        // Pass kcmModule (captured context property) to child components
        LayoutsTab {
            id: layoutsTab
            kcm: root.kcmModule
            constants: constants

            onRequestDeleteLayout: function(layout) {
                deleteConfirmDialog.layoutToDelete = layout
                deleteConfirmDialog.open()
            }
            onRequestImportLayout: importDialog.open()
            onRequestExportLayout: function(layoutId) {
                exportDialog.layoutId = layoutId
                exportDialog.open()
            }
        }

        // TAB 2: EDITOR
        EditorTab {
            kcm: root.kcmModule
            constants: constants
        }

        // TAB 3: ASSIGNMENTS
        AssignmentsTab {
            kcm: root.kcmModule
            constants: constants
        }

        // TAB 4: ZONES (merged Appearance + Behavior)
        ZonesTab {
            id: zonesTab
            kcm: root.kcmModule
            constants: constants
            isCurrentTab: stackLayout.currentIndex === 3

            onRequestHighlightColorDialog: {
                highlightColorDialog.selectedColor = kcm.highlightColor
                highlightColorDialog.open()
            }
            onRequestInactiveColorDialog: {
                inactiveColorDialog.selectedColor = kcm.inactiveColor
                inactiveColorDialog.open()
            }
            onRequestBorderColorDialog: {
                borderColorDialog.selectedColor = kcm.borderColor
                borderColorDialog.open()
            }
            onRequestColorFileDialog: colorFileDialog.open()
        }

        // TAB 5: DISPLAY (Zone Selector)
        DisplayTab {
            kcm: root.kcmModule
            constants: constants
            screenAspectRatio: root.screenAspectRatio
            isCurrentTab: stackLayout.currentIndex === 4
        }

        // TAB 6: EXCLUSIONS
        ExclusionsTab {
            kcm: root.kcmModule
            constants: constants
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    // DIALOGS
    // ═══════════════════════════════════════════════════════════════════════

    // Color dialogs
    ColorDialog {
        id: highlightColorDialog
        title: i18n("Choose Highlight Color")
        onAccepted: kcm.highlightColor = selectedColor
    }

    ColorDialog {
        id: inactiveColorDialog
        title: i18n("Choose Inactive Zone Color")
        onAccepted: kcm.inactiveColor = selectedColor
    }

    ColorDialog {
        id: borderColorDialog
        title: i18n("Choose Border Color")
        onAccepted: kcm.borderColor = selectedColor
    }

    // File dialogs
    FileDialog {
        id: importDialog
        title: i18n("Import Layout")
        nameFilters: ["JSON files (*.json)", "All files (*)"]
        fileMode: FileDialog.OpenFile
        onAccepted: kcm.importLayout(selectedFile.toString().replace(/^file:\/\/+/, "/"))
    }

    FileDialog {
        id: exportDialog
        title: i18n("Export Layout")
        nameFilters: ["JSON files (*.json)"]
        fileMode: FileDialog.SaveFile
        property string layoutId: ""
        onAccepted: kcm.exportLayout(layoutId, selectedFile.toString().replace(/^file:\/\/+/, "/"))
    }

    FileDialog {
        id: colorFileDialog
        title: i18n("Import Colors from File")
        nameFilters: ["JSON files (*.json)", "All files (*)"]
        fileMode: FileDialog.OpenFile
        onAccepted: kcm.loadColorsFromFile(selectedFile.toString().replace(/^file:\/\/+/, "/"))
    }

    // Delete confirmation dialog
    Kirigami.PromptDialog {
        id: deleteConfirmDialog
        title: i18n("Delete Layout")
        subtitle: i18n("Are you sure you want to delete '%1'?", layoutToDelete?.name ?? "")
        standardButtons: Kirigami.Dialog.Yes | Kirigami.Dialog.No
        preferredWidth: Math.min(Kirigami.Units.gridUnit * 30, parent.width * 0.8)
        property var layoutToDelete: null

        onAccepted: {
            if (layoutToDelete) {
                kcm.deleteLayout(layoutToDelete.id)
                layoutToDelete = null
            }
        }
        onRejected: layoutToDelete = null
    }

    // Layout editor launcher dialog
    Kirigami.Dialog {
        id: layoutEditorSheet
        title: i18n("Edit Layout")
        standardButtons: Kirigami.Dialog.Close
        preferredWidth: Math.min(Kirigami.Units.gridUnit * 25, parent.width * 0.8)

        property string layoutId: ""
        property string layoutName: ""

        ColumnLayout {
            spacing: Kirigami.Units.largeSpacing
            width: parent.width

            Label {
                Layout.fillWidth: true
                text: i18n("Use the visual editor to create and modify zone layouts.")
                wrapMode: Text.WordWrap
            }

            Button {
                Layout.fillWidth: true
                text: i18n("Open Layout Editor")
                icon.name: "document-edit"
                onClicked: {
                    kcm.openEditor()
                    layoutEditorSheet.close()
                }
            }

            Kirigami.Separator {
                Layout.fillWidth: true
            }

            Label {
                Layout.fillWidth: true
                text: i18n("You can also edit layout JSON files directly:")
                wrapMode: Text.WordWrap
                opacity: 0.7
            }

            Button {
                text: i18n("Open Layouts Folder")
                icon.name: "folder-open"
                onClicked: {
                    // Use KIO to open folder (better integration with KDE)
                    Qt.openUrlExternally("file://" + StandardPaths.writableLocation(StandardPaths.StandardLocation.GenericDataLocation) + "/plasmazones/layouts")
                }
            }
        }
    }
}

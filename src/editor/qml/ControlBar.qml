// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Bottom control bar component for the layout editor
 *
 * Contains zone creation, template selection, snapping controls, and save/cancel buttons.
 * Organized into logical groups for better UX.
 * Follows KDE HIG and Kirigami best practices.
 */
ToolBar {
    id: controlBar

    required property var editorController
    required property var confirmCloseDialog
    required property var editorWindow
    // Expose addZoneButton for access from parent
    property alias addZoneButton: addZoneButtonItem

    height: Kirigami.Units.gridUnit * 5 // Use theme spacing (40px - better visual balance)
    z: 100

    RowLayout {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.smallSpacing // Use theme spacing (4px)
        spacing: Kirigami.Units.mediumSpacing // Use theme spacing (8px - between major sections)

        // ═══════════════════════════════════════════════════════════════
        // ZONE CREATION SECTION
        // ═══════════════════════════════════════════════════════════════
        RowLayout {
            id: zoneCreationSection

            spacing: Kirigami.Units.gridUnit // Use theme spacing (8px - within section)

            // Add Zone button (exposed via alias)
            Button {
                id: addZoneButtonItem

                text: i18nc("@action:button", "Add Zone")
                icon.name: "list-add"
                enabled: editorController !== null && editorController !== undefined
                Accessible.name: text
                Accessible.description: i18nc("@info", "Click to add a new zone to the layout")
                ToolTip.visible: hovered
                ToolTip.text: i18nc("@tooltip", "Add a new zone to the layout. The zone will be created centered on the canvas.")
                onClicked: {
                    if (editorController) {
                        // Create a zone centered on the canvas with default size (25% x 25%)
                        var relW = 0.25;
                        // 25% width
                        var relH = 0.25;
                        // 25% height
                        var relX = 0.5 - (relW / 2);
                        // Center horizontally
                        var relY = 0.5 - (relH / 2);
                        // Center vertically
                        editorController.addZone(relX, relY, relW, relH);
                    }
                }
            }

            // Templates dropdown
            ComboBox {
                id: templateCombo

                // Template model with preview information
                property var templateModel: [{
                    "text": i18nc("@item:inmenu", "Apply Template..."),
                    "value": "",
                    "templateType": "",
                    "columns": 0,
                    "rows": 0,
                    "group": ""
                }, {
                    "text": i18nc("@item:inmenu", "Grid 2×2"),
                    "value": "grid:2:2",
                    "templateType": "grid",
                    "columns": 2,
                    "rows": 2,
                    "group": i18nc("@title:group", "Grid Layouts")
                }, {
                    "text": i18nc("@item:inmenu", "Grid 3×2"),
                    "value": "grid:3:2",
                    "templateType": "grid",
                    "columns": 3,
                    "rows": 2,
                    "group": i18nc("@title:group", "Grid Layouts")
                }, {
                    "text": i18nc("@item:inmenu", "Columns 2"),
                    "value": "columns:2:1",
                    "templateType": "columns",
                    "columns": 2,
                    "rows": 0,
                    "group": i18nc("@title:group", "Column Layouts")
                }, {
                    "text": i18nc("@item:inmenu", "Columns 3"),
                    "value": "columns:3:1",
                    "templateType": "columns",
                    "columns": 3,
                    "rows": 0,
                    "group": i18nc("@title:group", "Column Layouts")
                }, {
                    "text": i18nc("@item:inmenu", "Rows 2"),
                    "value": "rows:1:2",
                    "templateType": "rows",
                    "columns": 0,
                    "rows": 2,
                    "group": i18nc("@title:group", "Row Layouts")
                }, {
                    "text": i18nc("@item:inmenu", "Priority Grid"),
                    "value": "priority:0:0",
                    "templateType": "priority",
                    "columns": 0,
                    "rows": 0,
                    "group": i18nc("@title:group", "Special Layouts")
                }, {
                    "text": i18nc("@item:inmenu", "Focus"),
                    "value": "focus:0:0",
                    "templateType": "focus",
                    "columns": 0,
                    "rows": 0,
                    "group": i18nc("@title:group", "Special Layouts")
                }]

                Layout.preferredWidth: 200
                Accessible.name: i18nc("@label", "Layout templates")
                Accessible.description: i18nc("@info", "Apply a predefined layout template")
                model: templateModel
                textRole: "text"
                valueRole: "value"
                onActivated: {
                    if (currentValue !== "" && editorController) {
                        var parts = currentValue.split(":");
                        editorController.applyTemplate(parts[0], parseInt(parts[1], 10), parseInt(parts[2], 10));
                        currentIndex = 0; // Reset to "Apply Template..."
                    }
                }

                // Custom delegate with visual previews
                delegate: ItemDelegate {
                    required property var modelData
                    required property int index

                    width: templateCombo.width

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.smallSpacing

                        // Visual preview
                        TemplatePreview {
                            visible: modelData.templateType && modelData.templateType !== ""
                            templateType: modelData.templateType || ""
                            columns: modelData.columns || 2
                            rows: modelData.rows || 2
                            Layout.preferredWidth: 60
                            Layout.preferredHeight: 40
                            Layout.maximumWidth: 60
                            Layout.maximumHeight: 40
                            Layout.alignment: Qt.AlignVCenter
                        }

                        // Text label
                        Label {
                            text: modelData.text || ""
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                        }

                    }

                }

            }

        }

        // Visual separator
        Kirigami.Separator {
            Layout.fillHeight: true
            Layout.preferredWidth: 1
        }

        // ═══════════════════════════════════════════════════════════════
        // SNAPPING CONTROLS SECTION
        // ═══════════════════════════════════════════════════════════════
        RowLayout {
            id: snappingSection

            spacing: Kirigami.Units.gridUnit // Use theme spacing (8px - within section)

            // Edge snapping toggle - first in snapping section
            Button {
                id: edgeSnapButton

                text: i18nc("@action:button", "Snap to Edges")
                icon.name: "snap-bounding-box-center"
                checkable: true
                enabled: editorController !== null && editorController !== undefined
                // Bind checked state directly to edgeSnappingEnabled property
                checked: editorController ? (editorController.edgeSnappingEnabled || false) : false
                Accessible.name: text
                Accessible.description: i18nc("@info", "Toggle edge snapping. Zones will align with edges of other zones when enabled.")
                ToolTip.visible: hovered
                ToolTip.text: checked ? i18nc("@tooltip", "Edge snapping enabled. Zones align to edges of other zones. Click to disable.") : i18nc("@tooltip", "Edge snapping disabled. Click to enable. Zones will align to edges of other zones.")
                onToggled: {
                    if (editorController)
                        editorController.edgeSnappingEnabled = checked;

                }
            }

            // Grid snapping toggle
            Button {
                id: gridSnapButton

                icon.name: "view-grid"
                text: i18nc("@action:button", "Snap to Grid")
                checkable: true
                enabled: editorController !== null && editorController !== undefined
                // Bind checked state directly to gridSnappingEnabled property
                checked: editorController ? (editorController.gridSnappingEnabled || false) : false
                Accessible.name: text
                Accessible.description: i18nc("@info", "Toggle grid snapping. Zones will align to a grid when enabled. Use the dropdowns to change horizontal and vertical grid sizes.")
                ToolTip.visible: hovered
                ToolTip.text: checked ? i18nc("@tooltip", "Grid snapping enabled (H:%1% V:%2%). Zones align to grid lines. Click to disable.", editorController ? Math.round((editorController.snapIntervalX || 0.1) * 100) : 10, editorController ? Math.round((editorController.snapIntervalY || 0.1) * 100) : 10) : i18nc("@tooltip", "Grid snapping disabled. Click to enable. Zones will align to grid lines.")
                onToggled: {
                    if (editorController)
                        editorController.gridSnappingEnabled = checked;

                }
            }

            // Grid interval sliders - only visible when grid snapping is enabled
            RowLayout {
                id: gridIntervalControls

                spacing: Kirigami.Units.smallSpacing
                visible: editorController ? (editorController.gridSnappingEnabled || false) : false
                enabled: editorController !== null && editorController !== undefined

                Label {
                    text: i18nc("@label", "H:")
                    Accessible.name: i18nc("@label", "Horizontal grid interval")
                }

                // Horizontal grid interval slider
                Slider {
                    id: gridIntervalXSlider

                    Layout.preferredWidth: 80
                    from: 0.01
                    to: 0.5
                    stepSize: 0.01
                    value: editorController ? (editorController.snapIntervalX || 0.1) : 0.1
                    Accessible.name: i18nc("@label", "Horizontal grid interval")
                    Accessible.description: i18nc("@info", "Adjust horizontal grid interval (1% to 50%)")
                    onMoved: {
                        if (editorController) {
                            editorController.snapIntervalX = value;
                            // Ensure grid snapping is enabled when changing interval
                            if (!editorController.gridSnappingEnabled)
                                editorController.gridSnappingEnabled = true;

                        }
                    }

                    // Update when snapIntervalX changes externally
                    Connections {
                        function onSnapIntervalXChanged() {
                            if (!gridIntervalXSlider.pressed)
                                gridIntervalXSlider.value = editorController.snapIntervalX || 0.1;

                        }

                        target: editorController
                        enabled: editorController !== null
                    }

                }

                Label {
                    text: Math.round(gridIntervalXSlider.value * 100) + "%"
                    Layout.preferredWidth: 32
                    horizontalAlignment: Text.AlignRight
                    Accessible.name: i18nc("@info", "Horizontal interval: %1%", Math.round(gridIntervalXSlider.value * 100))
                }

                Label {
                    text: i18nc("@label", "V:")
                    Layout.leftMargin: Kirigami.Units.smallSpacing
                    Accessible.name: i18nc("@label", "Vertical grid interval")
                }

                // Vertical grid interval slider
                Slider {
                    id: gridIntervalYSlider

                    Layout.preferredWidth: 80
                    from: 0.01
                    to: 0.5
                    stepSize: 0.01
                    value: editorController ? (editorController.snapIntervalY || 0.1) : 0.1
                    Accessible.name: i18nc("@label", "Vertical grid interval")
                    Accessible.description: i18nc("@info", "Adjust vertical grid interval (1% to 50%)")
                    onMoved: {
                        if (editorController) {
                            editorController.snapIntervalY = value;
                            // Ensure grid snapping is enabled when changing interval
                            if (!editorController.gridSnappingEnabled)
                                editorController.gridSnappingEnabled = true;

                        }
                    }

                    // Update when snapIntervalY changes externally
                    Connections {
                        function onSnapIntervalYChanged() {
                            if (!gridIntervalYSlider.pressed)
                                gridIntervalYSlider.value = editorController.snapIntervalY || 0.1;

                        }

                        target: editorController
                        enabled: editorController !== null
                    }

                }

                Label {
                    text: Math.round(gridIntervalYSlider.value * 100) + "%"
                    Layout.preferredWidth: 32
                    horizontalAlignment: Text.AlignRight
                    Accessible.name: i18nc("@info", "Vertical interval: %1%", Math.round(gridIntervalYSlider.value * 100))
                }

            }

            // Grid overlay visibility toggle - positioned after grid size controls
            Button {
                id: gridOverlayButton

                icon.name: "view-grid-symbolic"
                text: i18nc("@action:button", "Show Grid")
                checkable: true
                enabled: editorController !== null && editorController !== undefined && (editorController.gridSnappingEnabled || false)
                // Bind checked state directly to gridOverlayVisible property
                checked: editorController ? (editorController.gridOverlayVisible || false) : false
                Accessible.name: text
                Accessible.description: i18nc("@info", "Toggle grid overlay visibility. Shows or hides the visual grid lines on the canvas.")
                ToolTip.visible: hovered
                ToolTip.text: checked ? i18nc("@tooltip", "Grid overlay visible. Click to hide grid lines.") : i18nc("@tooltip", "Grid overlay hidden. Click to show grid lines.")
                onToggled: {
                    if (editorController)
                        editorController.gridOverlayVisible = checked;

                }
            }

        }

        Item {
            Layout.fillWidth: true
        }

        // ═══════════════════════════════════════════════════════════════
        // STATUS SECTION - Unsaved changes indicator
        // ═══════════════════════════════════════════════════════════════
        RowLayout {
            id: statusSection

            spacing: Kirigami.Units.smallSpacing
            visible: editorController ? (editorController.hasUnsavedChanges || false) : false

            Kirigami.Icon {
                source: "document-save"
                width: Kirigami.Units.iconSizes.smallMedium
                height: Kirigami.Units.iconSizes.smallMedium
                color: Kirigami.Theme.negativeTextColor
            }

            Label {
                id: unsavedIndicator

                text: i18nc("@info", "Unsaved changes")
                color: Kirigami.Theme.negativeTextColor
                font.weight: Font.Medium
                Accessible.name: text
                Accessible.role: Accessible.AlertMessage
            }

        }

        // Visual separator
        Kirigami.Separator {
            Layout.fillHeight: true
            Layout.preferredWidth: 1
        }

        // ═══════════════════════════════════════════════════════════════
        // ACTION BUTTONS SECTION
        // ═══════════════════════════════════════════════════════════════
        RowLayout {
            id: actionButtonsSection

            spacing: Kirigami.Units.gridUnit // Use theme spacing (8px - between buttons)

            // Cancel button
            Button {
                text: i18nc("@action:button", "Cancel")
                Accessible.name: text
                Accessible.description: i18nc("@info", "Discard changes and close editor")
                onClicked: {
                    if (editorController && editorController.hasUnsavedChanges)
                        confirmCloseDialog.open();
                    else
                        editorWindow.close();
                }
            }

            // Save button
            Button {
                id: saveButton

                text: i18nc("@action:button", "Save")
                icon.name: "document-save"
                highlighted: true
                enabled: editorController ? (editorController.hasUnsavedChanges || false) : false
                Accessible.name: text
                Accessible.description: i18nc("@info", "Save layout and close editor")
                onClicked: {
                    if (editorController)
                        editorController.saveLayout();

                    editorWindow.close();
                }
            }

        }

    }

}

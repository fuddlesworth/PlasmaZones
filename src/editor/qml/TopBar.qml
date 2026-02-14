// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Top bar component for the layout editor
 *
 * Contains monitor selector, layout name editor, and action buttons.
 * Follows KDE HIG and Kirigami best practices.
 */
ToolBar {
    id: topBar

    required property var editorController
    required property var availableScreens
    required property var confirmCloseDialog
    required property var helpDialog
    required property var shaderDialog
    required property var visibilityDialog
    required property var layoutSettingsDialog
    required property var importDialog
    required property var exportDialog
    required property var editorWindow
    required property bool fullscreenMode

    signal fullscreenToggled()

    height: Kirigami.Units.gridUnit * 5 // Use theme spacing (40px - better visual balance)
    z: 100

    RowLayout {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.smallSpacing // Use theme spacing (4px)
        spacing: Kirigami.Units.mediumSpacing // Use theme spacing (8px - between groups)

        // ═══════════════════════════════════════════════════════════════
        // SCREEN SELECTOR SECTION
        // ═══════════════════════════════════════════════════════════════
        RowLayout {
            id: screenSelectorSection

            spacing: Kirigami.Units.smallSpacing // Use theme spacing (4px - within section)

            Repeater {
                model: availableScreens || []

                delegate: Button {
                    text: modelData ? (modelData.name || "") : ""
                    highlighted: editorController && modelData && modelData.name === editorController.targetScreen
                    enabled: editorController !== null
                    Accessible.name: modelData ? modelData.name : ""
                    Accessible.description: i18nc("@info", "Select screen for layout editing")
                    onClicked: {
                        if (editorController && modelData)
                            editorController.targetScreen = modelData.name;

                    }
                }

            }

        }

        // Visual separator
        Kirigami.Separator {
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            visible: screenSelectorSection.children.length > 0
        }

        // ═══════════════════════════════════════════════════════════════
        // LAYOUT NAME SECTION
        // ═══════════════════════════════════════════════════════════════
        RowLayout {
            id: layoutNameSection

            spacing: Kirigami.Units.smallSpacing // Use theme spacing (4px - between label and field)
            // Initialize on component creation with delay to ensure editorController is ready
            Component.onCompleted: {
                Qt.callLater(function() {
                    if (editorController && editorController.layoutName)
                        layoutNameField.text = editorController.layoutName;

                });
            }

            // Layout name label
            Label {
                text: i18nc("@label", "Layout:")
                color: Kirigami.Theme.disabledTextColor
            }

            // Layout name field with integrated character counter
            TextField {
                id: layoutNameField

                readonly property int maxLength: 40
                readonly property int currentLength: text ? text.length : 0
                readonly property bool showCounter: currentLength > maxLength * 0.8 || currentLength > maxLength

                Layout.preferredWidth: 200
                enabled: editorController !== null && editorController !== undefined
                Accessible.name: i18nc("@label", "Layout name")
                Accessible.description: i18nc("@info", "Enter name for the layout")
                // Add right padding when counter is visible to prevent text overlap
                rightPadding: (showCounter || activeFocus) ? 50 : 0
                onEditingFinished: {
                    if (editorController && text !== editorController.layoutName)
                        editorController.layoutName = text;

                }

                background: Rectangle {
                    color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)
                    radius: Kirigami.Units.smallSpacing // Use theme spacing

                    // Character counter overlay (right-aligned inside field)
                    Label {
                        anchors.right: parent.right
                        anchors.rightMargin: Kirigami.Units.smallSpacing
                        anchors.verticalCenter: parent.verticalCenter
                        visible: layoutNameField.showCounter || layoutNameField.activeFocus
                        text: i18nc("@info", "%1/%2", layoutNameField.currentLength, layoutNameField.maxLength)
                        color: layoutNameField.currentLength > layoutNameField.maxLength ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.disabledTextColor
                        font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                        opacity: layoutNameField.activeFocus ? 1 : 0.6
                        Accessible.name: i18nc("@info", "Character count: %1 of %2", layoutNameField.currentLength, layoutNameField.maxLength)
                        Accessible.description: i18nc("@info", "Shows how many characters are used in the layout name")
                    }

                }

            }

            // Explicitly connect to layoutNameChanged signal for reliable updates
            Connections {
                function onLayoutNameChanged() {
                    if (!layoutNameField.activeFocus && editorController)
                        layoutNameField.text = editorController.layoutName || "";

                }

                target: editorController
                enabled: editorController !== null && editorController !== undefined
            }

        }

        Item {
            Layout.fillWidth: true
        }

        // Visual separator
        Kirigami.Separator {
            Layout.fillHeight: true
            Layout.preferredWidth: 1
        }

        // ═══════════════════════════════════════════════════════════════
        // UNDO/REDO BUTTONS SECTION
        // ═══════════════════════════════════════════════════════════════
        RowLayout {
            id: undoRedoSection

            // Helper property to safely access undoController
            property var undoController: editorController ? editorController.undoController : null
            property bool canUndo: undoController ? undoController.canUndo : false
            property bool canRedo: undoController ? undoController.canRedo : false
            property string undoText: undoController ? undoController.undoText : ""
            property string redoText: undoController ? undoController.redoText : ""

            spacing: Kirigami.Units.smallSpacing // Use theme spacing (4px - between buttons)
            visible: editorController && undoController !== null

            // Reactive updates when undoController properties change
            Connections {
                function onCanUndoChanged() {
                    undoRedoSection.canUndo = undoRedoSection.undoController ? undoRedoSection.undoController.canUndo : false;
                }

                function onCanRedoChanged() {
                    undoRedoSection.canRedo = undoRedoSection.undoController ? undoRedoSection.undoController.canRedo : false;
                }

                function onUndoTextChanged() {
                    undoRedoSection.undoText = undoRedoSection.undoController ? undoRedoSection.undoController.undoText : "";
                }

                function onRedoTextChanged() {
                    undoRedoSection.redoText = undoRedoSection.undoController ? undoRedoSection.undoController.redoText : "";
                }

                target: undoRedoSection.undoController
                enabled: undoRedoSection.undoController !== null
            }

            // Undo button
            ToolButton {
                id: undoButton

                icon.name: "edit-undo"
                enabled: undoRedoSection.canUndo
                onClicked: {
                    if (undoRedoSection.undoController)
                        undoRedoSection.undoController.undo();

                }
                ToolTip.text: undoRedoSection.canUndo ? i18nc("@action:tooltip", "Undo: %1", undoRedoSection.undoText) : i18nc("@action:tooltip", "Undo")
                ToolTip.visible: hovered
                Accessible.name: i18nc("@action", "Undo")
                Accessible.description: ToolTip.text
                Accessible.role: Accessible.Button
            }

            // Redo button
            ToolButton {
                id: redoButton

                icon.name: "edit-redo"
                enabled: undoRedoSection.canRedo
                onClicked: {
                    if (undoRedoSection.undoController)
                        undoRedoSection.undoController.redo();

                }
                ToolTip.text: undoRedoSection.canRedo ? i18nc("@action:tooltip", "Redo: %1", undoRedoSection.redoText) : i18nc("@action:tooltip", "Redo")
                ToolTip.visible: hovered
                Accessible.name: i18nc("@action", "Redo")
                Accessible.description: ToolTip.text
                Accessible.role: Accessible.Button
            }

            // Visual separator
            Kirigami.Separator {
                Layout.fillHeight: true
                Layout.preferredWidth: 1
            }

        }

        // ═══════════════════════════════════════════════════════════════
        // LAYOUT SETTINGS BUTTON (Per-layout gap overrides)
        // ═══════════════════════════════════════════════════════════════
        ToolButton {
            id: layoutSettingsButton

            icon.name: "configure"
            enabled: editorController !== null && editorController !== undefined
            onClicked: topBar.layoutSettingsDialog.open()
            ToolTip.text: i18nc("@tooltip", "Layout-specific settings (gaps)")
            ToolTip.visible: hovered
            Accessible.name: i18nc("@action", "Layout Settings")
            Accessible.description: i18nc("@info", "Configure per-layout gap overrides")
        }

        // Visual separator
        Kirigami.Separator {
            Layout.fillHeight: true
            Layout.preferredWidth: 1
        }

        // ═══════════════════════════════════════════════════════════════
        // VISIBILITY SETTINGS BUTTON (Tier 2 per-context filtering)
        // ═══════════════════════════════════════════════════════════════
        ToolButton {
            id: visibilityButton

            icon.name: "view-filter"
            enabled: editorController !== null && editorController !== undefined
            onClicked: topBar.visibilityDialog.open()
            ToolTip.text: i18nc("@tooltip", "Layout visibility (per monitor/desktop/activity)")
            ToolTip.visible: hovered
            Accessible.name: i18nc("@action", "Layout Visibility")
            Accessible.description: i18nc("@info", "Configure where this layout appears in the zone selector")
        }

        // Visual separator
        Kirigami.Separator {
            Layout.fillHeight: true
            Layout.preferredWidth: 1
        }

        // ═══════════════════════════════════════════════════════════════
        // SHADER SETTINGS BUTTON
        // ═══════════════════════════════════════════════════════════════
        ToolButton {
            id: shaderButton

            icon.name: "adjustlevels"
            enabled: editorController !== null && editorController.shadersEnabled
            visible: editorController !== null && editorController.shadersEnabled
            onClicked: topBar.shaderDialog.open()
            ToolTip.text: i18nc("@tooltip", "Shader effect settings")
            ToolTip.visible: hovered
            Accessible.name: i18nc("@action", "Shader Settings")
            Accessible.description: i18nc("@info", "Configure visual shader effects for zones")
        }

        // Visual separator (only show if shader button is visible)
        Kirigami.Separator {
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            visible: shaderButton.visible
        }

        // ═══════════════════════════════════════════════════════════════
        // ACTION BUTTONS SECTION
        // ═══════════════════════════════════════════════════════════════
        RowLayout {
            id: actionButtonsSection

            spacing: Kirigami.Units.smallSpacing // Use theme spacing (4px - between buttons)

            // Import button
            ToolButton {
                icon.name: "document-import"
                enabled: editorController !== null && editorController !== undefined
                onClicked: importDialog.open()
                ToolTip.text: i18nc("@tooltip", "Import layout from file")
                ToolTip.visible: hovered
                Accessible.name: i18nc("@action", "Import Layout")
                Accessible.description: i18nc("@info", "Import a layout from a JSON file")
            }

            // Export button
            ToolButton {
                icon.name: "document-export"
                enabled: editorController !== null && editorController !== undefined && editorController.layoutId !== ""
                onClicked: exportDialog.open()
                ToolTip.text: i18nc("@tooltip", "Export layout to file")
                ToolTip.visible: hovered
                Accessible.name: i18nc("@action", "Export Layout")
                Accessible.description: i18nc("@info", "Export the current layout to a JSON file")
            }

            // Visual separator
            Kirigami.Separator {
                Layout.fillHeight: true
                Layout.preferredWidth: 1
            }

            // Fullscreen toggle button
            ToolButton {
                icon.name: topBar.fullscreenMode ? "view-restore" : "view-fullscreen"
                onClicked: topBar.fullscreenToggled()
                ToolTip.text: topBar.fullscreenMode ? i18nc("@tooltip", "Exit fullscreen mode (F11)") : i18nc("@tooltip", "Enter fullscreen mode (F11)")
                ToolTip.visible: hovered
                ToolTip.delay: Kirigami.Units.toolTipDelay
                Accessible.name: topBar.fullscreenMode ? i18nc("@action", "Exit Fullscreen") : i18nc("@action", "Fullscreen")
                Accessible.description: i18nc("@info", "Toggle fullscreen editing mode")
            }

            // Help button
            ToolButton {
                icon.name: "help-hint"
                onClicked: helpDialog.open()
                ToolTip.text: i18nc("@tooltip", "Quick reference guide (F1)")
                ToolTip.visible: hovered
                Accessible.name: i18nc("@action", "Help")
                Accessible.description: i18nc("@info", "Open quick reference guide")
            }

            // Close button
            ToolButton {
                icon.name: "window-close"
                onClicked: {
                    if (editorController && editorController.hasUnsavedChanges)
                        confirmCloseDialog.open();
                    else
                        editorWindow.close();
                }
                ToolTip.text: i18nc("@tooltip", "Close editor")
                ToolTip.visible: hovered
                Accessible.name: i18nc("@action", "Close")
                Accessible.description: i18nc("@info", "Close the layout editor")
            }

        }

    }

}

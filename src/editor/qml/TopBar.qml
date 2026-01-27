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
                    text: modelData ? modelData.name : ""
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
            onClicked: layoutSettingsPopup.open()
            ToolTip.text: i18nc("@tooltip", "Layout-specific settings (gaps)")
            ToolTip.visible: hovered
            Accessible.name: i18nc("@action", "Layout Settings")
            Accessible.description: i18nc("@info", "Configure per-layout gap overrides")

            Popup {
                id: layoutSettingsPopup
                x: -width + parent.width
                y: parent.height + Kirigami.Units.smallSpacing
                width: 320
                padding: Kirigami.Units.largeSpacing

                // Sync UI state when values change externally (undo/redo, load layout)
                Connections {
                    target: editorController
                    function onZonePaddingChanged() {
                        zonePaddingOverrideCheck.checked = editorController.hasZonePaddingOverride
                        if (editorController.hasZonePaddingOverride) {
                            zonePaddingSpin.value = editorController.zonePadding
                        } else {
                            zonePaddingSpin.value = editorController.globalZonePadding
                        }
                    }
                    function onOuterGapChanged() {
                        outerGapOverrideCheck.checked = editorController.hasOuterGapOverride
                        if (editorController.hasOuterGapOverride) {
                            outerGapSpin.value = editorController.outerGap
                        } else {
                            outerGapSpin.value = editorController.globalOuterGap
                        }
                    }
                    function onGlobalZonePaddingChanged() {
                        if (!editorController.hasZonePaddingOverride) {
                            zonePaddingSpin.value = editorController.globalZonePadding
                        }
                    }
                    function onGlobalOuterGapChanged() {
                        if (!editorController.hasOuterGapOverride) {
                            outerGapSpin.value = editorController.globalOuterGap
                        }
                    }
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: Kirigami.Units.mediumSpacing

                    Label {
                        text: i18nc("@title", "Layout Gap Overrides")
                        font.bold: true
                        Layout.fillWidth: true
                    }

                    Label {
                        text: i18nc("@info", "Override global settings for this layout only.")
                        wrapMode: Text.WordWrap
                        opacity: 0.7
                        Layout.fillWidth: true
                        Layout.bottomMargin: Kirigami.Units.smallSpacing
                    }

                    // Zone Padding override
                    GridLayout {
                        columns: 3
                        columnSpacing: Kirigami.Units.smallSpacing
                        rowSpacing: Kirigami.Units.smallSpacing
                        Layout.fillWidth: true

                        CheckBox {
                            id: zonePaddingOverrideCheck
                            text: i18nc("@option:check", "Zone Padding")
                            checked: editorController ? editorController.hasZonePaddingOverride : false
                            Layout.fillWidth: true
                            onToggled: {
                                if (editorController) {
                                    if (checked) {
                                        editorController.zonePadding = editorController.globalZonePadding
                                    } else {
                                        editorController.clearZonePaddingOverride()
                                    }
                                }
                            }
                        }

                        SpinBox {
                            id: zonePaddingSpin
                            from: 0
                            to: 100
                            value: editorController ? editorController.globalZonePadding : 8
                            enabled: zonePaddingOverrideCheck.checked
                            Layout.preferredWidth: 90
                            onValueModified: {
                                if (editorController && zonePaddingOverrideCheck.checked) {
                                    editorController.zonePadding = value
                                }
                            }
                            Component.onCompleted: {
                                if (editorController && editorController.hasZonePaddingOverride) {
                                    value = editorController.zonePadding
                                }
                            }
                        }

                        Label {
                            text: zonePaddingOverrideCheck.checked
                                ? i18nc("@label", "px")
                                : i18nc("@label showing global default", "px (global)")
                            opacity: zonePaddingOverrideCheck.checked ? 1.0 : 0.6
                        }

                        // Outer Gap override
                        CheckBox {
                            id: outerGapOverrideCheck
                            text: i18nc("@option:check", "Edge Gap")
                            checked: editorController ? editorController.hasOuterGapOverride : false
                            Layout.fillWidth: true
                            onToggled: {
                                if (editorController) {
                                    if (checked) {
                                        editorController.outerGap = editorController.globalOuterGap
                                    } else {
                                        editorController.clearOuterGapOverride()
                                    }
                                }
                            }
                        }

                        SpinBox {
                            id: outerGapSpin
                            from: 0
                            to: 100
                            value: editorController ? editorController.globalOuterGap : 8
                            enabled: outerGapOverrideCheck.checked
                            Layout.preferredWidth: 90
                            onValueModified: {
                                if (editorController && outerGapOverrideCheck.checked) {
                                    editorController.outerGap = value
                                }
                            }
                            Component.onCompleted: {
                                if (editorController && editorController.hasOuterGapOverride) {
                                    value = editorController.outerGap
                                }
                            }
                        }

                        Label {
                            text: outerGapOverrideCheck.checked
                                ? i18nc("@label", "px")
                                : i18nc("@label showing global default", "px (global)")
                            opacity: outerGapOverrideCheck.checked ? 1.0 : 0.6
                        }
                    }

                    // Info about where to change global settings
                    Label {
                        text: i18nc("@info", "Change global defaults in System Settings.")
                        opacity: 0.5
                        font.italic: true
                        Layout.fillWidth: true
                        Layout.topMargin: Kirigami.Units.smallSpacing
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
        // SHADER SETTINGS BUTTON
        // ═══════════════════════════════════════════════════════════════
        ToolButton {
            id: shaderButton

            icon.name: "preferences-desktop-effects"
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
                icon.name: "help-contents"
                onClicked: helpDialog.open()
                ToolTip.text: i18nc("@tooltip", "Keyboard shortcuts and usage guide (F1)")
                ToolTip.visible: hovered
                Accessible.name: i18nc("@action", "Help")
                Accessible.description: i18nc("@info", "Open keyboard shortcuts and usage guide")
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

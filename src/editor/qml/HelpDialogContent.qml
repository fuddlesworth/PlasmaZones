// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Help dialog content for the layout editor
 *
 * Displays keyboard shortcuts and usage instructions with proper KDE HIG formatting.
 */
ScrollView {
    id: helpContent

    // Required properties from parent
    required property var editorController
    required property var editorWindow

    // Helper function to format shortcut for display
    function formatShortcut(shortcut) {
        if (!shortcut || shortcut === "")
            return "";

        if (shortcut.indexOf("+") === -1 && shortcut.length > 0)
            return shortcut.charAt(0).toUpperCase() + shortcut.slice(1).toLowerCase();

        return shortcut;
    }

    clip: true

    Item {
        width: helpContent.availableWidth
        implicitHeight: columnLayout.implicitHeight + (Kirigami.Units.largeSpacing * 2)

        ColumnLayout {
            id: columnLayout

            anchors.fill: parent
            anchors.margins: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.largeSpacing

            // Keyboard Shortcuts Section
            Label {
                Layout.fillWidth: true
                Layout.topMargin: Kirigami.Units.smallSpacing
                text: i18nc("@title:group", "Keyboard Shortcuts")
                font.bold: true
                font.pixelSize: Kirigami.Theme.defaultFont.pixelSize * 1.15
            }

            Kirigami.Separator {
                Layout.fillWidth: true
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: Kirigami.Units.gridUnit * 3
                rowSpacing: Kirigami.Units.smallSpacing

                // Close
                Label {
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 14
                    text: i18nc("@label", "Close editor:")
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    text: editorController ? formatShortcut(editorController.editorCloseShortcut) : "Escape"
                    font.family: Kirigami.Theme.fixedWidthFont.family
                    color: Kirigami.Theme.highlightColor
                }

                // Save
                Label {
                    text: i18nc("@label", "Save layout:")
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    text: editorController ? formatShortcut(editorController.editorSaveShortcut) : "Ctrl+S"
                    font.family: Kirigami.Theme.fixedWidthFont.family
                    color: Kirigami.Theme.highlightColor
                }

                // Delete
                Label {
                    text: i18nc("@label", "Delete selected zone:")
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    text: editorController ? formatShortcut(editorController.editorDeleteShortcut) : "Delete"
                    font.family: Kirigami.Theme.fixedWidthFont.family
                    color: Kirigami.Theme.highlightColor
                }

                // Undo
                Label {
                    text: i18nc("@label", "Undo last operation:")
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    text: i18n("Ctrl+Z")
                    font.family: Kirigami.Theme.fixedWidthFont.family
                    color: Kirigami.Theme.highlightColor
                }

                // Redo
                Label {
                    text: i18nc("@label", "Redo last undone operation:")
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    text: i18n("Ctrl+Shift+Z")
                    font.family: Kirigami.Theme.fixedWidthFont.family
                    color: Kirigami.Theme.highlightColor
                }

                // Duplicate
                Label {
                    text: i18nc("@label", "Duplicate zone:")
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    text: editorController ? formatShortcut(editorController.editorDuplicateShortcut) : "Ctrl+D"
                    font.family: Kirigami.Theme.fixedWidthFont.family
                    color: Kirigami.Theme.highlightColor
                }

                // Copy
                Label {
                    text: i18nc("@label", "Copy zone:")
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    text: i18n("Ctrl+C")
                    font.family: Kirigami.Theme.fixedWidthFont.family
                    color: Kirigami.Theme.highlightColor
                }

                // Cut
                Label {
                    text: i18nc("@label", "Cut zone:")
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    text: i18n("Ctrl+X")
                    font.family: Kirigami.Theme.fixedWidthFont.family
                    color: Kirigami.Theme.highlightColor
                }

                // Paste
                Label {
                    text: i18nc("@label", "Paste zone:")
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    text: i18n("Ctrl+V")
                    font.family: Kirigami.Theme.fixedWidthFont.family
                    color: Kirigami.Theme.highlightColor
                }

                // Paste with Offset
                Label {
                    text: i18nc("@label", "Paste with offset:")
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    text: i18n("Ctrl+Shift+V")
                    font.family: Kirigami.Theme.fixedWidthFont.family
                    color: Kirigami.Theme.highlightColor
                }

                // Split Horizontal
                Label {
                    text: i18nc("@label", "Split horizontally:")
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    text: editorController ? formatShortcut(editorController.editorSplitHorizontalShortcut) : "Ctrl+Shift+H"
                    font.family: Kirigami.Theme.fixedWidthFont.family
                    color: Kirigami.Theme.highlightColor
                }

                // Split Vertical
                Label {
                    text: i18nc("@label", "Split vertically:")
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    text: editorController ? formatShortcut(editorController.editorSplitVerticalShortcut) : "Ctrl+Alt+V"
                    font.family: Kirigami.Theme.fixedWidthFont.family
                    color: Kirigami.Theme.highlightColor
                }

                // Fill
                Label {
                    text: i18nc("@label", "Fill available space:")
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    text: editorController ? formatShortcut(editorController.editorFillShortcut) : "Ctrl+Shift+F"
                    font.family: Kirigami.Theme.fixedWidthFont.family
                    color: Kirigami.Theme.highlightColor
                }

                // Fullscreen mode
                Label {
                    text: i18nc("@label", "Fullscreen mode:")
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    text: i18n("F11")
                    font.family: Kirigami.Theme.fixedWidthFont.family
                    color: Kirigami.Theme.highlightColor
                }

            }

            // Creating Zones Section
            Label {
                Layout.fillWidth: true
                Layout.topMargin: Kirigami.Units.gridUnit
                text: i18nc("@title:group", "Creating Zones")
                font.bold: true
                font.pixelSize: Kirigami.Theme.defaultFont.pixelSize * 1.15
            }

            Kirigami.Separator {
                Layout.fillWidth: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Label {
                    Layout.fillWidth: true
                    text: i18nc("@info", "Click \"Add Zone\" in the bottom toolbar, then drag on the canvas to create a new zone")
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    text: i18nc("@info", "Double-click the canvas to quickly add a zone at that location")
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    text: i18nc("@info", "Use the Templates dropdown to apply predefined layouts")
                    wrapMode: Text.WordWrap
                }

            }

            // Editing Zones Section
            Label {
                Layout.fillWidth: true
                Layout.topMargin: Kirigami.Units.gridUnit
                text: i18nc("@title:group", "Editing Zones")
                font.bold: true
                font.pixelSize: Kirigami.Theme.defaultFont.pixelSize * 1.15
            }

            Kirigami.Separator {
                Layout.fillWidth: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Label {
                    Layout.fillWidth: true
                    text: i18nc("@info", "Click a zone to select it")
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    text: i18nc("@info", "Drag a zone to move it")
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    text: i18nc("@info", "Drag corners or edges to resize a zone")
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    text: i18nc("@info", "Hover over a selected zone to see action buttons (delete, duplicate, etc.)")
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    text: i18nc("@info", "Right-click a zone for the context menu with additional options")
                    wrapMode: Text.WordWrap
                }

            }

            // Keyboard Navigation Section
            Label {
                Layout.fillWidth: true
                Layout.topMargin: Kirigami.Units.gridUnit
                text: i18nc("@title:group", "Keyboard Navigation")
                font.bold: true
                font.pixelSize: Kirigami.Theme.defaultFont.pixelSize * 1.15
            }

            Kirigami.Separator {
                Layout.fillWidth: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Label {
                    Layout.fillWidth: true
                    text: i18nc("@info", "Arrow keys: Move selected zone by 1% per press")
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    text: i18nc("@info", "Shift+Arrow keys: Resize selected zone by 1% per press")
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    text: i18nc("@info", "Ctrl+Tab / Ctrl+Shift+Tab: Navigate between zones")
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    Layout.topMargin: Kirigami.Units.smallSpacing
                    text: i18nc("@info", "Note: Tab and Shift+Tab are reserved for standard keyboard navigation")
                    wrapMode: Text.WordWrap
                    font.italic: true
                    opacity: 0.75
                }

            }

            // Accessibility Section
            Label {
                Layout.fillWidth: true
                Layout.topMargin: Kirigami.Units.gridUnit
                text: i18nc("@title:group", "Accessibility")
                font.bold: true
                font.pixelSize: Kirigami.Theme.defaultFont.pixelSize * 1.15
            }

            Kirigami.Separator {
                Layout.fillWidth: true
            }

            Label {
                Layout.fillWidth: true
                text: i18nc("@info", "Zone information is announced to screen readers when a zone is selected. All controls are keyboard accessible.")
                wrapMode: Text.WordWrap
            }

            // Spacer at bottom
            Item {
                Layout.minimumHeight: Kirigami.Units.gridUnit
            }

        }

    }

}

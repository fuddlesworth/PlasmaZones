// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Quick reference guide for the layout editor
 */
ScrollView {
    // ═══════════════════════════════════════════════════════════════════
    // COMPONENTS
    // ═══════════════════════════════════════════════════════════════════

    id: helpContent

    required property var editorController
    required property var editorWindow
    // Template mode swaps the zone-only groups for the strip's own bindings;
    // the always-live groups (save, undo, fullscreen, help) stay.
    property bool templateMode: false

    clip: true
    contentWidth: availableWidth

    Item {
        width: helpContent.availableWidth
        implicitHeight: mainColumn.implicitHeight + Kirigami.Units.largeSpacing * 2

        ColumnLayout {
            id: mainColumn

            anchors.fill: parent
            anchors.margins: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.largeSpacing

            // ═══════════════════════════════════════════════════════════════
            // KEYBOARD SHORTCUTS
            // ═══════════════════════════════════════════════════════════════
            // File component (SectionHeader.qml); title-only, no icon
            SectionHeader {
                title: i18nc("@title:group", "Keyboard Shortcuts")
                Layout.columnSpan: 2
            }

            GridLayout {
                columns: 2
                columnSpacing: Kirigami.Units.largeSpacing
                rowSpacing: Kirigami.Units.smallSpacing
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.gridUnit

                // File & Window
                SubsectionHeader {
                    title: i18nc("@title:group", "File & Window")
                    Layout.columnSpan: 2
                }

                ShortcutLabel {
                    action: helpContent.templateMode ? i18nc("@action", "Save template") : i18nc("@action", "Save layout")
                    shortcut: "Ctrl+S"
                }

                ShortcutLabel {
                    action: i18n("Close editor")
                    shortcut: "Escape"
                }

                ShortcutLabel {
                    action: i18n("Toggle fullscreen")
                    shortcut: "F11"
                }

                ShortcutLabel {
                    action: i18n("Show help")
                    shortcut: "F1"
                }

                // Edit
                SubsectionHeader {
                    title: i18nc("@title:group", "Edit")
                    Layout.columnSpan: 2
                    Layout.topMargin: Kirigami.Units.mediumSpacing
                }

                ShortcutLabel {
                    action: i18n("Undo")
                    shortcut: "Ctrl+Z"
                }

                ShortcutLabel {
                    action: i18n("Redo")
                    shortcut: "Ctrl+Shift+Z"
                }

                ShortcutLabel {
                    visible: !helpContent.templateMode
                    action: i18n("Delete zone(s)")
                    shortcut: "Delete"
                }

                ShortcutLabel {
                    visible: !helpContent.templateMode
                    action: i18n("Select all")
                    shortcut: "Ctrl+A"
                }

                ShortcutLabel {
                    visible: !helpContent.templateMode
                    action: i18n("Duplicate")
                    shortcut: helpContent.editorWindow.formatShortcut(editorController ? editorController.editorDuplicateShortcut : "Ctrl+D")
                }

                ShortcutLabel {
                    visible: !helpContent.templateMode
                    action: i18n("Copy")
                    shortcut: "Ctrl+C"
                }

                ShortcutLabel {
                    visible: !helpContent.templateMode
                    action: i18n("Cut")
                    shortcut: "Ctrl+X"
                }

                ShortcutLabel {
                    visible: !helpContent.templateMode
                    action: i18n("Paste")
                    shortcut: "Ctrl+V"
                }

                ShortcutLabel {
                    visible: !helpContent.templateMode
                    action: i18n("Paste offset")
                    shortcut: "Ctrl+Shift+V"
                }

                // Zone Operations
                SubsectionHeader {
                    visible: !helpContent.templateMode
                    title: i18nc("@title:group", "Zone Operations")
                    Layout.columnSpan: 2
                    Layout.topMargin: Kirigami.Units.mediumSpacing
                }

                ShortcutLabel {
                    visible: !helpContent.templateMode
                    action: i18n("Split horizontal")
                    shortcut: helpContent.editorWindow.formatShortcut(editorController ? editorController.editorSplitHorizontalShortcut : "Ctrl+Shift+H")
                }

                ShortcutLabel {
                    visible: !helpContent.templateMode
                    action: i18n("Split vertical")
                    shortcut: helpContent.editorWindow.formatShortcut(editorController ? editorController.editorSplitVerticalShortcut : "Ctrl+Alt+V")
                }

                ShortcutLabel {
                    visible: !helpContent.templateMode
                    action: i18n("Fill space")
                    shortcut: helpContent.editorWindow.formatShortcut(editorController ? editorController.editorFillShortcut : "Ctrl+Shift+F")
                }

                // Template Columns - the strip canvas bindings
                SubsectionHeader {
                    visible: helpContent.templateMode
                    title: i18nc("@title:group", "Template Columns")
                    Layout.columnSpan: 2
                    Layout.topMargin: Kirigami.Units.mediumSpacing
                }

                ShortcutLabel {
                    visible: helpContent.templateMode
                    action: i18nc("@action", "Select column")
                    shortcut: i18n("Arrow keys")
                }

                ShortcutLabel {
                    visible: helpContent.templateMode
                    action: i18nc("@action", "Resize column 1%")
                    shortcut: i18n("Shift+Arrows")
                }

                ShortcutLabel {
                    visible: helpContent.templateMode
                    action: i18nc("@action", "Reorder column")
                    shortcut: i18nc("@shortcut", "Ctrl+Arrows")
                }

                ShortcutLabel {
                    visible: helpContent.templateMode
                    action: i18nc("@action", "Remove column")
                    shortcut: "Delete"
                }

                // Navigation
                SubsectionHeader {
                    visible: !helpContent.templateMode
                    title: i18nc("@title:group", "Navigation")
                    Layout.columnSpan: 2
                    Layout.topMargin: Kirigami.Units.mediumSpacing
                }

                ShortcutLabel {
                    visible: !helpContent.templateMode
                    action: i18n("Move zone 1%")
                    shortcut: i18n("Arrow keys")
                }

                ShortcutLabel {
                    visible: !helpContent.templateMode
                    action: i18n("Resize zone 1%")
                    shortcut: i18n("Shift+Arrows")
                }

                ShortcutLabel {
                    visible: !helpContent.templateMode
                    action: i18n("Next zone")
                    shortcut: "Ctrl+Tab"
                }

                ShortcutLabel {
                    visible: !helpContent.templateMode
                    action: i18n("Previous zone")
                    shortcut: "Ctrl+Shift+Tab"
                }
            }

            // ═══════════════════════════════════════════════════════════════
            // MOUSE ACTIONS
            // ═══════════════════════════════════════════════════════════════
            SectionHeader {
                title: i18nc("@title:group", "Mouse Actions")
                Layout.columnSpan: 2
            }

            GridLayout {
                columns: 2
                columnSpacing: Kirigami.Units.largeSpacing
                rowSpacing: Kirigami.Units.smallSpacing
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.gridUnit

                ShortcutLabel {
                    visible: !helpContent.templateMode
                    action: i18n("Create zone")
                    shortcut: i18n("Double-click")
                }

                ShortcutLabel {
                    visible: !helpContent.templateMode
                    action: i18n("Select zone")
                    shortcut: i18n("Click")
                }

                ShortcutLabel {
                    visible: !helpContent.templateMode
                    action: i18n("Multi-select")
                    shortcut: i18n("Ctrl+Click")
                }

                ShortcutLabel {
                    visible: !helpContent.templateMode
                    action: i18n("Move zone")
                    shortcut: i18n("Drag")
                }

                ShortcutLabel {
                    visible: !helpContent.templateMode
                    action: i18n("Resize zone")
                    shortcut: i18n("Drag edge")
                }

                ShortcutLabel {
                    visible: !helpContent.templateMode
                    action: i18n("Context menu")
                    shortcut: i18n("Right-click")
                }

                ShortcutLabel {
                    visible: helpContent.templateMode
                    action: i18nc("@action", "Select column")
                    shortcut: i18n("Click")
                }

                ShortcutLabel {
                    visible: helpContent.templateMode
                    action: i18nc("@action", "Resize column")
                    shortcut: i18nc("@info mouse gesture", "Drag its right divider")
                }

                ShortcutLabel {
                    visible: helpContent.templateMode
                    action: i18nc("@action", "Deselect")
                    shortcut: i18nc("@info mouse gesture", "Click empty space")
                }
            }

            // ═══════════════════════════════════════════════════════════════
            // TIPS
            // ═══════════════════════════════════════════════════════════════
            SectionHeader {
                title: i18nc("@title:group", "Tips")
                Layout.columnSpan: 2
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.gridUnit
                spacing: Kirigami.Units.smallSpacing

                TipLabel {
                    visible: !helpContent.templateMode
                    tipText: i18n("Hover a selected zone to reveal action buttons")
                }

                TipLabel {
                    visible: !helpContent.templateMode
                    tipText: i18n("Use Templates dropdown for common layouts")
                }

                TipLabel {
                    visible: !helpContent.templateMode
                    tipText: i18n("Enable grid snapping for precise alignment")
                }

                TipLabel {
                    visible: !helpContent.templateMode
                    tipText: i18n("Per-layout gaps in layout settings (top bar)")
                }

                TipLabel {
                    visible: !helpContent.templateMode
                    tipText: i18n("Zones can overlap for multi-zone snapping")
                }

                TipLabel {
                    visible: helpContent.templateMode
                    tipText: i18nc("@info tip", "Hover or select a column to reveal its action buttons")
                }

                TipLabel {
                    visible: helpContent.templateMode
                    tipText: i18nc("@info tip", "Add columns from the bottom bar")
                }

                TipLabel {
                    visible: helpContent.templateMode
                    tipText: i18nc("@info tip", "Presets are the sizes the width and height cycling shortcuts step through")
                }
            }

            // ═══════════════════════════════════════════════════════════════
            // ACCESSIBILITY
            // ═══════════════════════════════════════════════════════════════
            SectionHeader {
                title: i18nc("@title:group", "Accessibility")
                Layout.columnSpan: 2
            }

            Label {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.gridUnit
                text: helpContent.templateMode ? i18nc("@info", "Column info announced to screen readers. Tab navigates UI, arrow keys navigate columns.") : i18n("Zone info announced to screen readers. Tab navigates UI, Ctrl+Tab navigates zones.")
                wrapMode: Text.WordWrap
                opacity: 0.8
            }

            Item {
                Layout.fillHeight: true
            }
        }
    }

    component SubsectionHeader: Label {
        required property string title

        text: title
        font.weight: Font.Medium
        opacity: 0.6
    }

    component ShortcutLabel: RowLayout {
        required property string action
        required property string shortcut

        spacing: Kirigami.Units.smallSpacing

        Label {
            text: action
            Layout.preferredWidth: Kirigami.Units.gridUnit * 9
        }

        Label {
            text: shortcut
            font.family: Kirigami.Theme.fixedWidthFont.family
        }
    }

    component TipLabel: Label {
        required property string tipText

        text: i18nc("@item:inlistbox Bullet list item, %1 is the tip", "• %1", tipText)
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
}

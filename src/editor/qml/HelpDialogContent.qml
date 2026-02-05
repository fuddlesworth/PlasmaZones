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
    id: helpContent

    required property var editorController
    required property var editorWindow

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
            SectionHeader { title: i18nc("@title:group", "Keyboard Shortcuts") }

            GridLayout {
                columns: 2
                columnSpacing: Kirigami.Units.largeSpacing
                rowSpacing: Kirigami.Units.smallSpacing
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.gridUnit

                // File & Window
                SubsectionHeader { title: i18nc("@title:group", "File & Window"); Layout.columnSpan: 2 }
                ShortcutLabel { action: i18n("Save layout"); shortcut: "Ctrl+S" }
                ShortcutLabel { action: i18n("Close editor"); shortcut: "Escape" }
                ShortcutLabel { action: i18n("Toggle fullscreen"); shortcut: "F11" }
                ShortcutLabel { action: i18n("Show help"); shortcut: "F1" }

                // Edit
                SubsectionHeader { title: i18nc("@title:group", "Edit"); Layout.columnSpan: 2; Layout.topMargin: Kirigami.Units.mediumSpacing }
                ShortcutLabel { action: i18n("Undo"); shortcut: "Ctrl+Z" }
                ShortcutLabel { action: i18n("Redo"); shortcut: "Ctrl+Shift+Z" }
                ShortcutLabel { action: i18n("Delete zone(s)"); shortcut: "Delete" }
                ShortcutLabel { action: i18n("Select all"); shortcut: "Ctrl+A" }
                ShortcutLabel { action: i18n("Duplicate"); shortcut: editorController?.editorDuplicateShortcut ?? "Ctrl+D" }
                ShortcutLabel { action: i18n("Copy"); shortcut: "Ctrl+C" }
                ShortcutLabel { action: i18n("Cut"); shortcut: "Ctrl+X" }
                ShortcutLabel { action: i18n("Paste"); shortcut: "Ctrl+V" }
                ShortcutLabel { action: i18n("Paste offset"); shortcut: "Ctrl+Shift+V" }

                // Zone Operations
                SubsectionHeader { title: i18nc("@title:group", "Zone Operations"); Layout.columnSpan: 2; Layout.topMargin: Kirigami.Units.mediumSpacing }
                ShortcutLabel { action: i18n("Split horizontal"); shortcut: editorController?.editorSplitHorizontalShortcut ?? "Ctrl+Shift+H" }
                ShortcutLabel { action: i18n("Split vertical"); shortcut: editorController?.editorSplitVerticalShortcut ?? "Ctrl+Alt+V" }
                ShortcutLabel { action: i18n("Fill space"); shortcut: editorController?.editorFillShortcut ?? "Ctrl+Shift+F" }

                // Navigation
                SubsectionHeader { title: i18nc("@title:group", "Navigation"); Layout.columnSpan: 2; Layout.topMargin: Kirigami.Units.mediumSpacing }
                ShortcutLabel { action: i18n("Move zone 1%"); shortcut: i18n("Arrow keys") }
                ShortcutLabel { action: i18n("Resize zone 1%"); shortcut: i18n("Shift+Arrows") }
                ShortcutLabel { action: i18n("Next zone"); shortcut: "Ctrl+Tab" }
                ShortcutLabel { action: i18n("Previous zone"); shortcut: "Ctrl+Shift+Tab" }
            }

            // ═══════════════════════════════════════════════════════════════
            // MOUSE ACTIONS
            // ═══════════════════════════════════════════════════════════════
            SectionHeader { title: i18nc("@title:group", "Mouse Actions") }

            GridLayout {
                columns: 2
                columnSpacing: Kirigami.Units.largeSpacing
                rowSpacing: Kirigami.Units.smallSpacing
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.gridUnit

                ShortcutLabel { action: i18n("Create zone"); shortcut: i18n("Double-click") }
                ShortcutLabel { action: i18n("Select zone"); shortcut: i18n("Click") }
                ShortcutLabel { action: i18n("Multi-select"); shortcut: i18n("Ctrl+Click") }
                ShortcutLabel { action: i18n("Move zone"); shortcut: i18n("Drag") }
                ShortcutLabel { action: i18n("Resize zone"); shortcut: i18n("Drag edge") }
                ShortcutLabel { action: i18n("Context menu"); shortcut: i18n("Right-click") }
            }

            // ═══════════════════════════════════════════════════════════════
            // TIPS
            // ═══════════════════════════════════════════════════════════════
            SectionHeader { title: i18nc("@title:group", "Tips") }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.gridUnit
                spacing: Kirigami.Units.smallSpacing

                TipLabel { tipText: i18n("Hover a selected zone to reveal action buttons") }
                TipLabel { tipText: i18n("Use Templates dropdown for common layouts") }
                TipLabel { tipText: i18n("Enable grid snapping for precise alignment") }
                TipLabel { tipText: i18n("Per-layout gaps in gear button (top bar)") }
                TipLabel { tipText: i18n("Zones can overlap for multi-zone snapping") }
            }

            // ═══════════════════════════════════════════════════════════════
            // ACCESSIBILITY
            // ═══════════════════════════════════════════════════════════════
            SectionHeader { title: i18nc("@title:group", "Accessibility") }

            Label {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.gridUnit
                text: i18n("Zone info announced to screen readers. Tab navigates UI, Ctrl+Tab navigates zones.")
                wrapMode: Text.WordWrap
                opacity: 0.8
            }

            Item { Layout.fillHeight: true }
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // COMPONENTS
    // ═══════════════════════════════════════════════════════════════════

    component SectionHeader: RowLayout {
        required property string title
        Layout.fillWidth: true
        Layout.columnSpan: 2
        spacing: Kirigami.Units.smallSpacing

        Rectangle {
            width: Math.round(Kirigami.Units.smallSpacing * 0.75)
            height: sectionLabel.height
            color: Kirigami.Theme.highlightColor
            radius: Math.round(Kirigami.Units.smallSpacing / 4)
        }
        Label {
            id: sectionLabel
            text: parent.title
            font.weight: Font.DemiBold
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
            color: Kirigami.Theme.linkColor
        }
    }

    component TipLabel: Label {
        required property string tipText
        text: "• " + tipText
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
}

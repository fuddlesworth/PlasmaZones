// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

/**
 * @brief Keyboard shortcuts for the layout editor
 *
 * Provides configurable shortcuts for common operations.
 * Extracted from EditorWindow.qml to reduce file size.
 */
Item {
    id: shortcuts

    // Required properties
    required property var editorController
    required property var editorWindow
    required property var confirmCloseDialog
    required property var helpDialog
    required property bool fullscreenMode
    // Helper property to safely access undoController
    property var undoController: editorController ? editorController.undoController : null
    property bool canUndo: undoController ? undoController.canUndo : false
    property bool canRedo: undoController ? undoController.canRedo : false

    signal fullscreenToggled()

    // Helper function to format shortcut for display
    function formatShortcut(shortcut) {
        if (!shortcut || shortcut === "")
            return "";

        if (shortcut.indexOf("+") === -1 && shortcut.length > 0)
            return shortcut.charAt(0).toUpperCase() + shortcut.slice(1).toLowerCase();

        return shortcut;
    }

    // Fullscreen toggle shortcut (F11 - standard convention)
    Shortcut {
        id: fullscreenShortcut

        sequence: "F11"
        onActivated: shortcuts.fullscreenToggled()
    }

    // Close shortcut (Escape - standard for modal windows/overlays)
    // In fullscreen mode, Escape exits fullscreen first instead of closing
    Shortcut {
        id: closeShortcut

        sequence: "Escape"
        onActivated: {
            // If in fullscreen mode, exit it first
            if (shortcuts.fullscreenMode) {
                shortcuts.fullscreenToggled();
                return ;
            }
            // Normal close behavior
            if (editorController && editorController.hasUnsavedChanges)
                confirmCloseDialog.open();
            else
                editorWindow.close();
        }
    }

    // Save shortcut (StandardKey.Save - respects KDE/Qt system shortcuts)
    Shortcut {
        id: saveShortcut

        sequences: [StandardKey.Save]
        enabled: editorController !== null
        onActivated: {
            if (editorController)
                editorController.saveLayout();

        }
    }

    // Delete selected zone(s) shortcut (StandardKey.Delete - respects system shortcuts)
    // Supports multi-selection - deletes all selected zones as single undo operation
    Shortcut {
        id: deleteShortcut

        sequences: [StandardKey.Delete]
        enabled: editorController && editorController.selectionCount > 0
        onActivated: {
            if (editorController)
                editorController.deleteSelectedZones();

        }
    }

    // Select all zones shortcut (StandardKey.SelectAll - respects system shortcuts)
    Shortcut {
        id: selectAllShortcut

        sequences: [StandardKey.SelectAll]
        enabled: editorController !== null
        onActivated: {
            if (editorController)
                editorController.selectAll();

        }
    }

    // Duplicate zone(s) shortcut (configurable) - duplicates all selected zones
    Shortcut {
        id: duplicateShortcut

        sequence: editorController ? editorController.editorDuplicateShortcut : "Ctrl+D"
        enabled: editorController && editorController.selectionCount > 0
        onActivated: {
            if (editorController)
                editorController.duplicateSelectedZones();

        }
    }

    // Copy zone(s) shortcut (StandardKey.Copy - respects system shortcuts)
    Shortcut {
        id: copyShortcut

        sequences: [StandardKey.Copy]
        context: Qt.ApplicationShortcut
        enabled: editorController && editorController.selectionCount > 0
        onActivated: {
            if (editorController)
                editorController.copyZones(editorController.selectedZoneIds);

        }
    }

    // Cut zone(s) shortcut (StandardKey.Cut - respects system shortcuts)
    Shortcut {
        id: cutShortcut

        sequences: [StandardKey.Cut]
        context: Qt.ApplicationShortcut
        enabled: editorController && editorController.selectionCount > 0
        onActivated: {
            if (editorController)
                editorController.cutZones(editorController.selectedZoneIds);

        }
    }

    // Paste zone shortcut (StandardKey.Paste - respects system shortcuts)
    Shortcut {
        id: pasteShortcut

        sequences: [StandardKey.Paste]
        context: Qt.ApplicationShortcut
        enabled: editorController && editorController.canPaste
        onActivated: {
            if (editorController)
                editorController.pasteZones(false);

        }
    }

    // Paste with offset shortcut (Shift+Paste)
    Shortcut {
        id: pasteOffsetShortcut

        sequences: ["Ctrl+Shift+V", "Shift+Insert"]
        context: Qt.ApplicationShortcut
        enabled: editorController && editorController.canPaste
        onActivated: {
            if (editorController)
                editorController.pasteZones(true);

        }
    }

    // Split zone horizontally shortcut (configurable)
    Shortcut {
        id: splitHorizontalShortcut

        sequence: editorController ? editorController.editorSplitHorizontalShortcut : "Ctrl+Shift+H"
        enabled: editorWindow.selectedZoneId !== "" && editorController !== null
        onActivated: {
            if (editorController && editorWindow.selectedZoneId)
                editorController.splitZone(editorWindow.selectedZoneId, true);

        }
    }

    // Split zone vertically shortcut (configurable)
    // Note: Default changed from Ctrl+Shift+V to avoid conflict with Paste with Offset
    Shortcut {
        id: splitVerticalShortcut

        sequence: editorController ? editorController.editorSplitVerticalShortcut : "Ctrl+Alt+V"
        enabled: editorWindow.selectedZoneId !== "" && editorController !== null
        onActivated: {
            if (editorController && editorWindow.selectedZoneId)
                editorController.splitZone(editorWindow.selectedZoneId, false);

        }
    }

    // Fill available space shortcut (configurable)
    Shortcut {
        id: fillShortcut

        sequence: editorController ? editorController.editorFillShortcut : "Ctrl+Shift+F"
        enabled: editorWindow.selectedZoneId !== "" && editorController !== null
        onActivated: {
            // Fallback if zone not found in repeater

            if (editorController && editorWindow.selectedZoneId) {
                // Find the selected zone in the repeater and trigger animated fill
                var zoneItem = editorWindow.findZoneItemById(editorWindow.selectedZoneId);
                if (zoneItem)
                    zoneItem.animatedExpandToFill();
                else
                    editorController.expandToFillSpace(editorWindow.selectedZoneId);
            }
        }
    }

    // Reactive updates when undoController properties change
    Connections {
        function onCanUndoChanged() {
            shortcuts.canUndo = shortcuts.undoController ? shortcuts.undoController.canUndo : false;
        }

        function onCanRedoChanged() {
            shortcuts.canRedo = shortcuts.undoController ? shortcuts.undoController.canRedo : false;
        }

        target: shortcuts.undoController
        enabled: shortcuts.undoController !== null
    }

    // Undo shortcut (StandardKey.Undo - respects system shortcuts)
    Shortcut {
        id: undoShortcut

        sequences: [StandardKey.Undo]
        enabled: shortcuts.canUndo
        onActivated: {
            if (shortcuts.undoController)
                shortcuts.undoController.undo();

        }
    }

    // Redo shortcut (StandardKey.Redo - respects system shortcuts)
    Shortcut {
        id: redoShortcut

        sequences: [StandardKey.Redo]
        enabled: shortcuts.canRedo
        onActivated: {
            if (shortcuts.undoController)
                shortcuts.undoController.redo();

        }
    }

    // Help shortcut (StandardKey.HelpContents - respects system shortcuts)
    Shortcut {
        id: helpShortcut

        sequences: [StandardKey.HelpContents]
        onActivated: helpDialog.open()
    }

    // Update shortcut sequences when they change (app-specific shortcuts only)
    // Standard shortcuts (Save, Delete, Close, etc.) use StandardKey and don't need updating
    // Use Loader to conditionally create Connections only when editorController is available
    // This prevents "function in an invalid context" errors during component destruction
    Loader {
        id: shortcutConnectionsLoader

        active: editorController !== null

        sourceComponent: Connections {
            function onEditorDuplicateShortcutChanged() {
                if (editorController)
                    duplicateShortcut.sequence = editorController.editorDuplicateShortcut;

            }

            function onEditorSplitHorizontalShortcutChanged() {
                if (editorController)
                    splitHorizontalShortcut.sequence = editorController.editorSplitHorizontalShortcut;

            }

            function onEditorSplitVerticalShortcutChanged() {
                if (editorController)
                    splitVerticalShortcut.sequence = editorController.editorSplitVerticalShortcut;

            }

            function onEditorFillShortcutChanged() {
                if (editorController)
                    fillShortcut.sequence = editorController.editorFillShortcut;

            }

            target: editorController
        }

    }

}

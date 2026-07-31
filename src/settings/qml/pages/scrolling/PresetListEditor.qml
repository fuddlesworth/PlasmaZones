// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Editor for one scrolling preset list (column widths or window
 * heights): a reorderable list of percent entries with add and remove,
 * replacing the raw comma-separated text field.
 *
 * The stored value stays the canonical comma-joined fraction string. Every
 * edit joins the working array and writes it through `commit`, then the
 * `presets` binding delivers back whatever the schema's canonicalizer kept
 * (dropped duplicates, the 16-entry cap, the nothing-survives default), so
 * the list always shows the effective presets. Order is meaningful — the
 * cycle shortcuts step through it — which is why the rows reorder.
 *
 * Duplicate values cannot occur (the canonicalizer de-dupes), so the value
 * string itself is a stable row id for ReorderableColumn.
 */
ColumnLayout {
    id: editor

    /// Canonical comma-joined fraction string (bind an appSettings property).
    required property string presets
    /// Called with the new comma-joined string on every edit.
    required property var commit
    /// Accessible label stem, e.g. "column width preset".
    required property string entryName

    readonly property var _values: presets.length > 0 ? presets.split(",") : []

    spacing: Kirigami.Units.smallSpacing

    function _commitList(list) {
        commit(list.join(","));
    }

    ReorderableColumn {
        Layout.fillWidth: true
        visible: editor._values.length > 0
        items: editor._values
        headerRowHeight: Kirigami.Units.gridUnit * 2
        idOf: function (item) {
            return item;
        }
        accessibleNameOf: function (item) {
            return i18n("%1% %2", Math.round(parseFloat(item) * 100), editor.entryName);
        }
        onMoveRequested: function (fromIndex, toIndex) {
            var next = editor._values.slice();
            if (fromIndex < 0 || fromIndex >= next.length || toIndex < 0 || toIndex >= next.length)
                return;
            var moved = next.splice(fromIndex, 1)[0];
            next.splice(toIndex, 0, moved);
            editor._commitList(next);
        }

        rowDelegate: RowLayout {
            id: presetRow

            // Read off the hosting Loader once, like ChainEditor's delegate.
            readonly property string entryValue: parent.rowModelData
            readonly property int entryIndex: parent.rowIndex
            readonly property int entryPercent: Math.round(parseFloat(entryValue) * 100)

            spacing: Kirigami.Units.smallSpacing

            Label {
                Layout.fillWidth: true
                text: i18n("%1%", presetRow.entryPercent)
            }

            ToolButton {
                icon.name: "list-remove"
                Accessible.name: i18n("Remove %1% %2", presetRow.entryPercent, editor.entryName)
                onClicked: {
                    var next = editor._values.slice();
                    next.splice(presetRow.entryIndex, 1);
                    editor._commitList(next);
                }
            }
        }
    }

    RowLayout {
        spacing: Kirigami.Units.smallSpacing

        SettingsSpinBox {
            id: addSpin

            accessibleName: i18n("New %1 percent", editor.entryName)
            from: 1
            to: 100
            value: 50
            stepSize: 5
        }

        Button {
            text: i18n("Add")
            icon.name: "list-add"
            Accessible.name: i18n("Add %1", editor.entryName)
            onClicked: {
                var next = editor._values.slice();
                // Three decimals keeps 1/3-style entries distinct without
                // fighting the canonicalizer's number formatting.
                next.push((addSpin.value / 100).toFixed(3));
                editor._commitList(next);
            }
        }
    }
}

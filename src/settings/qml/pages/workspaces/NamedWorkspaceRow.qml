// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief One named-workspace declaration row (ExpandableRowDelegate).
 *
 * Header: icon · name · chevron (the shared expand indicator) · pinned-
 * monitor label · remove button. Expansion: the editable fields — name,
 * pinned monitor, position, and the two per-name shortcuts.
 *
 * Edits emit whole-field commit signals; the page mutates its staged array
 * IN PLACE and writes the composite, so this row (and its expansion state)
 * survives every field edit. Because the entry object mutates in place, the
 * header mirrors the two fields it shows through local properties updated at
 * commit time instead of bindings that would never re-evaluate.
 */
ExpandableRowDelegate {
    id: row

    /// The declaration map: name / output / position / focusShortcut / moveShortcut.
    required property var entry
    required property int entryIndex
    /// settingsController.screens rows for the monitor combo.
    required property var screenOptions
    /// Called with (entryIndex) → array of the OTHER rows' names, read fresh
    /// at commit time (a plain array prop would go stale across in-place
    /// renames of siblings).
    required property var siblingNamesOf

    // Header mirrors (see the class comment).
    property string headerName: entry.name
    property string headerOutput: entry.output

    signal fieldEdited(int index, string field, var value)
    signal removeRequested(int index)

    function _monitorLabel(outputId) {
        if (outputId === "")
            return i18n("Any monitor");
        for (var i = 0; i < screenOptions.length; ++i)
            if (screenOptions[i].value === outputId)
                return screenOptions[i].label;
        return outputId;
    }

    expandable: true

    Kirigami.Icon {
        source: "virtual-desktops"
        Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
        Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
    }

    Label {
        Layout.fillWidth: true
        elide: Text.ElideRight
        text: row.headerName !== "" ? row.headerName : i18n("(unnamed)")
    }

    ExpandChevron {
        expanded: row.expanded
    }

    Label {
        opacity: 0.7
        font: Kirigami.Theme.smallFont
        text: row._monitorLabel(row.headerOutput)
    }

    ToolButton {
        icon.name: "edit-delete"
        Accessible.name: i18n("Remove named workspace")
        ToolTip.text: i18n("Remove named workspace")
        ToolTip.visible: hovered
        onClicked: row.removeRequested(row.entryIndex)
    }

    expansionContent: GridLayout {
        columns: 2
        columnSpacing: Kirigami.Units.largeSpacing
        rowSpacing: Kirigami.Units.smallSpacing

        Label {
            text: i18n("Name")
        }

        TextField {
            id: nameField

            Layout.fillWidth: true
            text: row.entry.name
            Accessible.name: i18n("Workspace name")
            onEditingFinished: {
                var trimmed = text.trim();
                if (trimmed === row.entry.name)
                    return; // unchanged; tabbing through must not dirty the page

                var siblings = row.siblingNamesOf(row.entryIndex);
                for (var i = 0; i < siblings.length; ++i) {
                    if (siblings[i] === trimmed) {
                        text = row.entry.name; // names are unique; refuse the duplicate
                        return;
                    }
                }
                row.headerName = trimmed;
                row.fieldEdited(row.entryIndex, "name", trimmed);
            }
        }

        Label {
            text: i18n("Monitor")
        }

        WideComboBox {
            textRole: "label"
            valueRole: "value"
            Accessible.name: i18n("Pinned monitor")
            model: row.screenOptions
            storedValue: row.entry.output
            onActivated: {
                row.headerOutput = currentValue;
                row.fieldEdited(row.entryIndex, "output", currentValue);
            }
        }

        Label {
            text: i18n("Position")
        }

        SettingsSpinBox {
            id: positionSpin

            from: -1
            to: 19
            value: row.entry.position
            unitText: ""
            accessibleName: i18n("Preferred position in the monitor's list")
            tooltipText: i18n("Where in the monitor's workspace list this workspace prefers to sit. Automatic places it before the trailing empty workspace.")
            textFromValue: function (value, locale) {
                return value < 0 ? i18n("Automatic") : String(value + 1);
            }
            onValueModified: value => {
                row.fieldEdited(row.entryIndex, "position", value);
            }
        }

        Label {
            text: i18n("Focus shortcut")
        }

        ShortcutCaptureField {
            Layout.fillWidth: true
            accessibleName: i18n("Focus named workspace shortcut")
            keySequence: row.entry.focusShortcut
            onKeySequenceModified: seq => {
                row.fieldEdited(row.entryIndex, "focusShortcut", seq);
            }
        }

        Label {
            text: i18n("Move window shortcut")
        }

        ShortcutCaptureField {
            Layout.fillWidth: true
            accessibleName: i18n("Move window to named workspace shortcut")
            keySequence: row.entry.moveShortcut
            onKeySequenceModified: seq => {
                row.fieldEdited(row.entryIndex, "moveShortcut", seq);
            }
        }
    }
}

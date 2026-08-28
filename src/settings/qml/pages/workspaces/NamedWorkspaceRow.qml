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

    /// Why the last rename attempt was refused, or empty. Shown under the name
    /// field and cleared as soon as the user types again — a refusal that only
    /// snapped the text back would look like the edit was simply lost.
    property string nameError: ""

    signal fieldEdited(int index, string field, var value)
    signal removeRequested(int index)

    /// Point the name field back at the stored name.
    ///
    /// Used for every exit from the rename handler, refusal or not, so the
    /// field always shows what the store actually holds — including the
    /// trimmed form after a rename that only changed surrounding whitespace.
    /// Reassigned as a Qt.binding rather than a plain string so the
    /// declarative `text: row.entry.name` binding survives; a bare assignment
    /// would sever it and leave a later `entry` replacement unreflected.
    function _restoreNameField() {
        nameField.text = Qt.binding(function () {
            return row.entry.name;
        });
    }

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
        Layout.alignment: Qt.AlignVCenter
        Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
        Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
    }

    // Title column, RuleRow's shape: bold name over a dimmed one-line
    // summary of where the workspace lives.
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 0

        Label {
            Layout.fillWidth: true
            elide: Text.ElideRight
            font.bold: true
            text: row.headerName !== "" ? row.headerName : i18n("(unnamed)")
        }

        Label {
            Layout.fillWidth: true
            elide: Text.ElideRight
            opacity: 0.7
            text: row._monitorLabel(row.headerOutput)
        }
    }

    // Pinned-monitor badge — only when actually pinned (the subtitle already
    // says "Any monitor" for the unpinned case). MetadataChip is the shared
    // badge the settings lists use, in its highlighted (state) flavour, which
    // is the same recipe RuleRow's category badge follows.
    MetadataChip {
        visible: row.headerOutput !== ""
        Layout.alignment: Qt.AlignVCenter
        highlighted: true
        text: i18nc("Badge on a named workspace pinned to a monitor", "Pinned")
    }

    ExpandChevron {
        expanded: row.expanded
    }

    ToolButton {
        icon.name: "edit-delete"
        Layout.alignment: Qt.AlignVCenter
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

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing / 2

            TextField {
                id: nameField

                Layout.fillWidth: true
                text: row.entry.name
                Accessible.name: i18n("Workspace name")
                Accessible.description: row.nameError
                onTextEdited: row.nameError = ""
                onEditingFinished: {
                    // Names are compared trimmed and case-sensitively, which is
                    // the daemon's identity rule: the config schema trims a
                    // name before storing it (canonicalNamedEntries) and the
                    // reconciler matches declarations with plain QString
                    // equality, so "Work" and "work" are two workspaces.
                    var trimmed = text.trim();
                    var stored = ("" + row.entry.name).trim();
                    if (trimmed === stored) {
                        // Unchanged: tabbing through must not dirty the page.
                        // Still write the trimmed form back, so a rename that
                        // only added whitespace does not leave the field
                        // showing text the store will never hold.
                        row.nameError = "";
                        row._restoreNameField();
                        return;
                    }
                    // An empty name is not a name. The schema drops an entry
                    // whose name is empty (canonicalNamedEntries skips it), so
                    // committing one would delete the declaration on the next
                    // round-trip and take its shortcuts with it. The Add form
                    // refuses the same input.
                    if (trimmed === "") {
                        row.nameError = i18n("A workspace needs a name.");
                        row._restoreNameField();
                        return;
                    }
                    var siblings = row.siblingNamesOf(row.entryIndex);
                    for (var i = 0; i < siblings.length; ++i) {
                        if (siblings[i] === trimmed) {
                            row.nameError = i18n("Another workspace already uses that name.");
                            row._restoreNameField();
                            return;
                        }
                    }
                    row.nameError = "";
                    row.headerName = trimmed;
                    row.fieldEdited(row.entryIndex, "name", trimmed);
                    row._restoreNameField();
                }
            }

            Label {
                Layout.fillWidth: true
                visible: row.nameError !== ""
                text: row.nameError
                wrapMode: Text.WordWrap
                font: Kirigami.Theme.smallFont
                color: Kirigami.Theme.negativeTextColor
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
            // A monitor that is currently unplugged is not in `screenOptions`,
            // and WideComboBox clamps an unresolved storedValue to index 0 —
            // which is "Any monitor", so the pin would read as if it had been
            // dropped. Show the stored output id instead, the same fallback
            // the rules editors and the quick-shortcut combo use.
            displayText: (row.entry.output !== "" && indexOfValue(row.entry.output) < 0) ? row.entry.output : currentText
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
            // The reconciler's desktop cap, minus one for the zero-based
            // slice index. Read off the controller rather than spelled here:
            // WorkspaceReconciler::DefaultDesktopCap is the one place the
            // limit is decided.
            to: settingsController.workspacesDesktopCap - 1
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

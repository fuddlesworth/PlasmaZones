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
 * survives every field edit. Because the entry object mutates in place, EVERY
 * displayed field is mirrored on a local property updated at commit time
 * instead of bound straight to the map, which would never re-evaluate.
 */
ExpandableRowDelegate {
    id: row

    /// The declaration map: name / output / position / focusShortcut / moveShortcut.
    required property var entry
    required property int entryIndex
    /// settingsController.screens rows for the monitor combo.
    required property var screenOptions
    /// Called with (trimmedName, entryIndex) → why that name is refused, or an
    /// empty string. Owned by the page so the Add form and every row apply one
    /// rule, read fresh at commit time (a snapshot would go stale across
    /// in-place renames of siblings).
    required property var nameRefusalFor

    /// Every read of `entry` goes through here. The page null-guards its own
    /// resolvers for the same reason (see its `idOf` comment): during a model
    /// reset the delegate's modelData detaches before its destruction handler
    /// runs, and the bindings below re-evaluate in that window.
    function _field(name, fallback) {
        return (row.entry && row.entry[name] !== undefined) ? row.entry[name] : fallback;
    }

    // Mirrors of every field the row displays.
    //
    // The page commits a field edit IN PLACE on the same map object, which
    // changes no property and re-evaluates no binding, so a control bound
    // straight to `entry.*` would freeze on whatever the row was built with.
    // That is masked for a control that only ever shows what it itself wrote,
    // and it stops being masked the moment the value moves from anywhere else,
    // so all five carry a mirror rather than the two that happen to be visible
    // while collapsed.
    property string headerName: row._field("name", "")
    property string headerOutput: row._field("output", "")
    property int entryPosition: row._field("position", -1)
    property string entryFocusShortcut: row._field("focusShortcut", "")
    property string entryMoveShortcut: row._field("moveShortcut", "")

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
    /// declarative `text: row.headerName` binding survives; a bare assignment
    /// would sever it and leave a later rename unreflected.
    function _restoreNameField() {
        nameField.text = Qt.binding(function () {
            return row.headerName;
        });
    }

    function _monitorLabel(outputId) {
        if (outputId === "")
            return i18n("Any monitor");
        for (var i = 0; i < screenOptions.length; ++i)
            if (screenOptions[i].value === outputId)
                return screenOptions[i].label;
        return i18nc("%1 is a monitor id the system does not currently report", "%1 (not connected)", outputId);
    }

    /// Whether the pinned monitor is one the system currently reports. A pin
    /// naming something absent is not a pin the daemon can act on right now:
    /// it either drops the id or defers the claim. The row cannot tell those
    /// two apart, so it does not guess — it drops the badge to its neutral
    /// flavour and says "not connected" in the subtitle, rather than showing
    /// the confident state pill for a pin nothing has realized.
    readonly property bool outputResolved: {
        if (row.headerOutput === "")
            return false;
        for (var i = 0; i < row.screenOptions.length; ++i)
            if (row.screenOptions[i].value === row.headerOutput)
                return true;
        return false;
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
        highlighted: row.outputResolved
        text: i18nc("Badge on a named workspace pinned to a monitor", "Pinned")
        Accessible.description: row.outputResolved ? "" : i18n("The pinned monitor is not connected.")
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
                text: row.headerName
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
                    var stored = ("" + row.headerName).trim();
                    if (trimmed === stored) {
                        // Unchanged: tabbing through must not dirty the page.
                        // Still write the trimmed form back, so a rename that
                        // only added whitespace does not leave the field
                        // showing text the store will never hold.
                        row.nameError = "";
                        row._restoreNameField();
                        return;
                    }
                    // The page owns the rule so the Add form and every row
                    // refuse the same input. It is the schema's own rule:
                    // canonicalNamedEntries drops an entry whose name is
                    // empty, over-long or control-bearing, and committing one
                    // would delete the declaration on the next round-trip and
                    // take its shortcuts with it.
                    var refusal = row.nameRefusalFor(trimmed, row.entryIndex);
                    if (refusal !== "") {
                        row.nameError = refusal;
                        row._restoreNameField();
                        return;
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
            storedValue: row.headerOutput
            // A monitor that is currently unplugged is not in `screenOptions`,
            // and WideComboBox clamps an unresolved storedValue to index 0 —
            // which is "Any monitor", so the pin would read as if it had been
            // dropped. Show the stored output id instead, the same fallback
            // the rules editors and the quick-shortcut combo use.
            // Read through the header mirror, not `entry.output`: the page
            // commits a field edit IN PLACE on the same map object, which
            // changes no property and re-evaluates no binding. `headerOutput`
            // is the mirror that exists to carry that edit (see the class
            // comment), so the fallback follows the pin instead of freezing on
            // whatever the row was built with.
            displayText: (row.headerOutput !== "" && indexOfValue(row.headerOutput) < 0) ? row.headerOutput : currentText
            onActivated: {
                // The mirror first: `storedValue` follows it, so this is also
                // what keeps the combo showing the pick.
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
            // The reconciler's DEFAULT desktop cap, minus one for the
            // zero-based slice index. It is an authoring ceiling and nothing
            // more: what the daemon actually honours is the target monitor's
            // own workspace count at the moment it places the workspace,
            // which is typically far lower and which the settings app is
            // never told (see SettingsController::workspacesDesktopCap). The
            // control is therefore worded as a PREFERENCE — the tooltip says
            // the number is clamped to the monitor's list — rather than
            // pretending the offered range is all reachable.
            to: settingsController.workspacesDesktopCap - 1
            value: row.entryPosition
            unitText: ""
            accessibleName: i18n("Preferred position in the monitor's list")
            tooltipText: i18n("Where in the monitor's workspace list this workspace prefers to sit. A number past the end of that list lands at the end. Automatic places it before the trailing empty workspace.")
            textFromValue: function (value, locale) {
                return value < 0 ? i18n("Automatic") : String(value + 1);
            }
            onValueModified: value => {
                row.entryPosition = value;
                row.fieldEdited(row.entryIndex, "position", value);
            }
        }

        Label {
            text: i18n("Focus shortcut")
        }

        ShortcutCaptureField {
            Layout.fillWidth: true
            accessibleName: i18n("Focus named workspace shortcut")
            keySequence: row.entryFocusShortcut
            onKeySequenceModified: seq => {
                row.entryFocusShortcut = seq;
                row.fieldEdited(row.entryIndex, "focusShortcut", seq);
            }
        }

        Label {
            text: i18n("Move window shortcut")
        }

        ShortcutCaptureField {
            Layout.fillWidth: true
            accessibleName: i18n("Move window to named workspace shortcut")
            keySequence: row.entryMoveShortcut
            onKeySequenceModified: seq => {
                row.entryMoveShortcut = seq;
                row.fieldEdited(row.entryIndex, "moveShortcut", seq);
            }
        }
    }
}

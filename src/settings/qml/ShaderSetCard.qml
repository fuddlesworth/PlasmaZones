// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief One saved set in the ShaderSetsPage list.
 *
 * Built on the shared ExpandableRowDelegate shell (same hover / press
 * highlight as the Rules list and the decoration pack rows), with expansion
 * disabled: everything fits on the row.
 *
 * Layout, left to right: the name over its description, then the trailing
 * cluster — the last-updated stamp (fixed position, so a long description
 * cannot push it around), the metadata badges (coverage chips and the
 * coverage-count badge), the "Active" pill when the set matches live state, and
 * the actions (Apply, hidden while active, plus the overflow menu).
 *
 * Apply is hidden while the set is active: the pill already says there is
 * nothing to apply, and a permanently disabled button reads as broken.
 *
 * The host supplies the same `bridge` (a ShaderSetStore) the page binds,
 * plus the domain-worded label callbacks.
 */
ExpandableRowDelegate {
    id: row

    required property var modelData
    required property var bridge
    /// token (e.g. "window") → translated chip label.
    required property var coverageLabel
    /// count → translated "%n Surfaces" / "%n Overrides" badge label.
    required property var coverageCountLabel
    /// name → translated apply-confirmation subtitle.
    required property var applySubtitleFor

    readonly property string setName: modelData.name ?? ""
    readonly property bool isActive: modelData.active === true

    // Everything fits on the row itself, so there is nothing to expand.
    expandable: false
    // The shell is an ItemDelegate, so the row is focusable even with the click
    // inert. Name it, or a screen reader announces an unlabelled row — and give it
    // a list-item role, or the row is announced as a button that does nothing when
    // activated. The actions live in the trailing controls, not on the row.
    Accessible.role: Accessible.ListItem
    // Fold the Active state and the coverage count into the name: the pill and
    // the badges are plain Rectangles with no accessible role, so without this a
    // screen reader cannot tell the applied set from any other row, nor how much
    // a set covers.
    Accessible.name: {
        const count = row.coverageCountLabel(row.modelData.coverageCount ?? 0);
        return row.isActive ? i18nc("@item accessible name of the currently applied set, with its coverage summary", "%1, active, %2", row.setName, count) : i18nc("@item accessible name of a saved set, with its coverage summary", "%1, %2", row.setName, count);
    }

    // ── Header ──────────────────────────────────────────────────────────

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 0

        Label {
            Layout.fillWidth: true
            text: row.setName
            font.bold: true
            elide: Text.ElideRight
        }

        Label {
            Layout.fillWidth: true
            text: row.modelData.description ?? ""
            color: Kirigami.Theme.disabledTextColor
            elide: Text.ElideRight
            visible: text.length > 0
        }
    }

    // Last-updated stamp — leads the right-aligned cluster instead of
    // trailing the description, so it sits at a fixed spot on every row
    // regardless of how long the description is. The store always carries
    // `modified` (the set file's mtime), but a set file whose mtime the
    // filesystem cannot report would yield an invalid date, so render only a
    // valid one rather than printing "Invalid Date" at the user.
    Label {
        id: modifiedStamp

        readonly property date modified: row.modelData.modified ?? new Date(NaN)
        /// The store always carries `modified`, but a file whose mtime the
        /// filesystem cannot report yields an invalid date. Read the predicate,
        /// not `visible`, which returns EFFECTIVE (parent-suppressed) visibility.
        readonly property bool hasStamp: !isNaN(modifiedStamp.modified.getTime())

        visible: modifiedStamp.hasStamp
        Layout.alignment: Qt.AlignVCenter
        text: modifiedStamp.hasStamp ? i18n("Updated %1", modifiedStamp.modified.toLocaleString(Qt.locale(), Locale.ShortFormat)) : ""
        color: Kirigami.Theme.disabledTextColor
        font: Kirigami.Theme.smallFont
    }

    // ── Badge cluster: metadata (coverage chips → count), then state
    //    (Active), then actions (Apply → overflow) — grouped so the eye
    //    reads each kind as a unit.

    // Coverage chips — which parts of the taxonomy the set carries. The Row
    // groups them at smallSpacing inside the header's largeSpacing cluster, and
    // hides itself when there is nothing to show: a visible-but-empty Row would
    // still eat a spacing slot, leaving a phantom gap in the badge cluster.
    Row {
        visible: (row.modelData.coverage?.length ?? 0) > 0
        Layout.alignment: Qt.AlignVCenter
        spacing: Kirigami.Units.smallSpacing

        Repeater {
            model: row.modelData.coverage ?? []

            delegate: MetadataChip {
                id: chip

                required property string modelData

                text: row.coverageLabel(chip.modelData)
            }
        }
    }

    // Hidden at zero, like the coverage Row above: a "0 Surfaces" badge would
    // only appear for a hand-placed set file the store cannot apply anyway.
    MetadataChip {
        visible: (row.modelData.coverageCount ?? 0) > 0
        Layout.alignment: Qt.AlignVCenter
        text: row.coverageCountLabel(row.modelData.coverageCount ?? 0)
    }

    // Active pill — highlight-tinted so it reads as state, distinct from the
    // neutral metadata badges before it (same split as the Rules list's
    // category vs count badges). Sits next to the actions: it occupies the
    // slot Apply takes on inactive rows, saying why there is no Apply here.
    MetadataChip {
        visible: row.isActive
        Layout.alignment: Qt.AlignVCenter
        highlighted: true
        iconName: "dialog-ok"
        text: i18nc("@label badge on the saved set matching the current state", "Active")
    }

    Button {
        visible: !row.isActive
        text: i18n("Apply")
        icon.name: "dialog-ok-apply"
        Layout.alignment: Qt.AlignVCenter
        Accessible.name: i18n("Apply set %1", row.setName)
        onClicked: row._openDialog(applyConfirmLoader, null)
    }

    ToolButton {
        id: overflowButton

        icon.name: "overflow-menu"
        display: AbstractButton.IconOnly
        Layout.alignment: Qt.AlignVCenter
        ToolTip.text: i18n("More actions")
        ToolTip.visible: hovered
        Accessible.name: i18n("More actions for set %1", row.setName)
        // Anchored, not popup()-at-cursor: a keyboard activation has no
        // meaningful cursor position and would drop the menu wherever the
        // pointer happened to rest.
        onClicked: {
            overflowMenuLoader.active = true;
            overflowMenuLoader.item.popup(overflowButton, 0, overflowButton.height);
        }
    }

    // ── Menu + dialogs ──────────────────────────────────────────────────

    // The menu and the four dialogs are built on FIRST USE, not up front. The
    // Repeater rebuilds every row whenever setsChanged fires (which the store also
    // raises for live-state edits), and a row that has never been interacted with
    // has no reason to carry five popups through that.
    Loader {
        id: overflowMenuLoader

        active: false
        sourceComponent: Menu {
            MenuItem {
                text: i18n("Edit…")
                icon.name: "document-edit"
                onTriggered: row._openDialog(editDialogLoader, function (dlg) {
                    dlg.seed(row.setName, row.modelData.description ?? "");
                })
            }

            MenuItem {
                text: i18n("Export…")
                icon.name: "document-export"
                onTriggered: row._openDialog(exportDialogLoader, null)
            }

            MenuSeparator {}

            MenuItem {
                text: i18n("Delete")
                icon.name: "edit-delete"
                onTriggered: row._openDialog(deleteConfirmLoader, null)
            }
        }
    }

    /// Materialise @p loader's item if it is not there yet, run @p setup on it, and
    /// open it. Loader.item is null until the component is instantiated, so every
    /// caller would otherwise repeat the same three lines.
    function _openDialog(loader, setup) {
        loader.active = true;
        if (setup)
            setup(loader.item);

        loader.item.open();
    }

    // Apply replaces the profile on every path the set covers. Confirm
    // before the batch write.
    //
    // The explicit close() is load-bearing: Kirigami.Dialog forwards the
    // button box's applied() straight through, and ApplyRole (unlike
    // Accept / Reject) does not dismiss the dialog. Without it the prompt
    // stays on screen after the set is applied. Same reason deleteConfirm
    // below closes itself, and the same fix CurveEditorDialog uses.
    Loader {
        id: applyConfirmLoader

        active: false
        sourceComponent: Kirigami.PromptDialog {
            title: i18n("Apply set?")
            subtitle: row.applySubtitleFor(row.setName)
            standardButtons: Kirigami.Dialog.Apply | Kirigami.Dialog.Cancel
            onApplied: {
                if (row.bridge)
                    row.bridge.applySet(row.setName);

                close();
            }
        }
    }

    // Delete is permanent — the JSON file is removed and only a backup
    // would bring it back.
    Loader {
        id: deleteConfirmLoader

        active: false
        sourceComponent: Kirigami.PromptDialog {
            title: i18n("Delete set?")
            subtitle: i18n("“%1” will be permanently removed.", row.setName)
            standardButtons: Kirigami.Dialog.Discard | Kirigami.Dialog.Cancel
            onDiscarded: {
                if (row.bridge)
                    row.bridge.removeSet(row.setName);

                close();
            }
        }
    }

    // Metadata editor — name and description travel together through one
    // updateSet call so a rename can't drop the description (or vice versa).
    Loader {
        id: editDialogLoader

        active: false
        sourceComponent: Kirigami.PromptDialog {
            id: editDialog

            /// Seed the fields for this open. IMPERATIVE on purpose: the dialog
            /// item persists across opens (the Loader stays active), and typing
            /// breaks a TextField's `text:` binding for good, so a
            /// binding-through-property seed would silently stop reaching the
            /// fields from the second open on. An imperative write lands
            /// regardless of the binding's state.
            function seed(name, description) {
                editNameField.text = name;
                editDescField.text = description;
            }
            /// True when the typed name is one updateSet will actually accept.
            /// Gates BOTH the Ok button and the Enter key: Ok is AcceptRole and
            /// Kirigami.Dialog.accept() does not consult the button box, so a name
            /// the store then refuses would close the dialog anyway and swallow the
            /// user's description edit along with it.
            ///
            /// canUseSetName is the store's own refusal set (empty, unslugifiable,
            /// or colliding with another set). QML cannot reproduce the slug rule,
            /// so asking the store is the only way this cannot drift from it.
            readonly property bool nameUsable: row.bridge ? row.bridge.canUseSetName(editNameField.text, row.setName) : false

            title: i18n("Edit set")
            standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
            // Installed on open, not at construction: the gate is only meaningful
            // while the dialog is live, and onOpened is already where focus is set.
            onOpened: {
                standardButton(Kirigami.Dialog.Ok).enabled = Qt.binding(function () {
                    return editDialog.nameUsable;
                });
                editNameField.forceActiveFocus();
            }
            onAccepted: {
                if (row.bridge)
                    row.bridge.updateSet(row.setName, editNameField.text.trim(), editDescField.text.trim());
            }

            ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                TextField {
                    id: editNameField

                    Layout.fillWidth: true
                    placeholderText: i18n("Set name…")
                    Accessible.name: i18n("Set name")
                    onAccepted: if (editDialog.nameUsable)
                        editDialog.accept()
                }

                TextField {
                    id: editDescField

                    Layout.fillWidth: true
                    placeholderText: i18n("Description (optional)…")
                    Accessible.name: i18n("Set description")
                    onAccepted: if (editDialog.nameUsable)
                        editDialog.accept()
                }
            }
        }
    }

    Loader {
        id: exportDialogLoader

        active: false
        sourceComponent: FileDialog {
            title: i18n("Export Set")
            nameFilters: [i18n("PlasmaZones Set (*.json)"), i18n("All files (*)")]
            defaultSuffix: "json"
            fileMode: FileDialog.SaveFile
            onAccepted: {
                if (row.bridge)
                    row.bridge.exportSet(row.setName, settingsController.urlToLocalFile(selectedFile));
            }
        }
    }
}

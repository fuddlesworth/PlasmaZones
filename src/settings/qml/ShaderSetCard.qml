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
 * cannot push it around), the metadata badges (coverage chips, a "Baseline"
 * chip, the coverage-count badge), the "Active" pill when the set matches
 * live state, and the actions (Apply, hidden while active, plus the overflow
 * menu).
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
    // The shell is an ItemDelegate, so the row is focusable even with the
    // click inert. Name it, or a screen reader announces an unlabelled row.
    Accessible.name: row.setName

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
            opacity: 0.7
            elide: Text.ElideRight
            visible: text.length > 0
        }
    }

    // Last-updated stamp — leads the right-aligned cluster instead of
    // trailing the description, so it sits at a fixed spot on every row
    // regardless of how long the description is.
    Label {
        visible: row.modelData.modified !== undefined
        Layout.alignment: Qt.AlignVCenter
        text: row.modelData.modified !== undefined ? i18n("Updated %1", row.modelData.modified.toLocaleString(Qt.locale(), Locale.ShortFormat)) : ""
        color: Kirigami.Theme.disabledTextColor
        font.pointSize: Kirigami.Theme.smallFont.pointSize
    }

    // ── Badge cluster: metadata (coverage chips → count), then state
    //    (Active), then actions (Apply → overflow) — grouped so the eye
    //    reads each kind as a unit.

    // Coverage chips — which parts of the taxonomy the set carries, neutral
    // like the Rules list's count badges. Held in a plain Row rather than
    // dropped straight into the header RowLayout: a bare Repeater is itself a
    // zero-width layout child, so the layout would allocate spacing around it
    // and leave a phantom gap in the cluster even when coverage is empty.
    Row {
        Layout.alignment: Qt.AlignVCenter
        spacing: Kirigami.Units.smallSpacing

        Repeater {
            model: row.modelData.coverage ?? []

            delegate: Rectangle {
                id: chip

                required property string modelData

                implicitWidth: chipLabel.implicitWidth + Kirigami.Units.largeSpacing
                implicitHeight: chipLabel.implicitHeight + Kirigami.Units.smallSpacing
                radius: Kirigami.Units.smallSpacing
                color: Kirigami.Theme.alternateBackgroundColor

                Label {
                    id: chipLabel

                    anchors.centerIn: parent
                    text: row.coverageLabel(chip.modelData)
                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                    opacity: 0.7
                }
            }
        }

        // The baseline is the set's global default, not one surface, so it
        // gets its own chip rather than a taxonomy label.
        Rectangle {
            visible: row.modelData.hasBaseline === true
            implicitWidth: baselineLabel.implicitWidth + Kirigami.Units.largeSpacing
            implicitHeight: baselineLabel.implicitHeight + Kirigami.Units.smallSpacing
            radius: Kirigami.Units.smallSpacing
            color: Kirigami.Theme.alternateBackgroundColor

            Label {
                id: baselineLabel

                anchors.centerIn: parent
                text: i18nc("@label chip for a set's global default profile", "Baseline")
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                opacity: 0.7
            }
        }
    }

    // Coverage-count badge, neutral like the Rules list's count badges.
    Rectangle {
        Layout.alignment: Qt.AlignVCenter
        implicitWidth: countLabel.implicitWidth + Kirigami.Units.largeSpacing
        implicitHeight: countLabel.implicitHeight + Kirigami.Units.smallSpacing
        radius: Kirigami.Units.smallSpacing
        color: Kirigami.Theme.alternateBackgroundColor

        Label {
            id: countLabel

            anchors.centerIn: parent
            text: row.coverageCountLabel(row.modelData.coverageCount ?? 0)
            font.pointSize: Kirigami.Theme.smallFont.pointSize
            opacity: 0.7
        }
    }

    // Active pill — highlight-tinted so it reads as state, distinct from the
    // neutral metadata badges before it (same split as the Rules list's
    // category vs count badges). Sits next to the actions: it occupies the
    // slot Apply takes on inactive rows, saying why there is no Apply here.
    Rectangle {
        visible: row.isActive
        Layout.alignment: Qt.AlignVCenter
        implicitWidth: activeRow.implicitWidth + Kirigami.Units.largeSpacing
        implicitHeight: activeRow.implicitHeight + Kirigami.Units.smallSpacing
        radius: Kirigami.Units.smallSpacing
        color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.18)

        RowLayout {
            id: activeRow

            anchors.centerIn: parent
            spacing: Kirigami.Units.smallSpacing / 2

            Kirigami.Icon {
                source: "dialog-ok"
                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                Layout.preferredHeight: Kirigami.Units.iconSizes.small
            }

            Label {
                text: i18nc("@label badge on the saved set matching the current state", "Active")
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                opacity: 0.85
            }
        }
    }

    Button {
        visible: !row.isActive
        text: i18n("Apply")
        icon.name: "dialog-ok-apply"
        Layout.alignment: Qt.AlignVCenter
        Accessible.name: i18n("Apply set %1", row.setName)
        onClicked: applyConfirm.open()
    }

    ToolButton {
        icon.name: "overflow-menu"
        display: AbstractButton.IconOnly
        Layout.alignment: Qt.AlignVCenter
        ToolTip.text: i18n("More actions")
        ToolTip.visible: hovered
        Accessible.name: i18n("More actions for set %1", row.setName)
        onClicked: overflowMenu.popup()
    }

    // ── Menu + dialogs ──────────────────────────────────────────────────

    Menu {
        id: overflowMenu

        MenuItem {
            text: i18n("Edit…")
            icon.name: "document-edit"
            onTriggered: {
                editNameField.text = row.setName;
                editDescField.text = row.modelData.description ?? "";
                editDialog.open();
            }
        }

        MenuItem {
            text: i18n("Export…")
            icon.name: "document-export"
            onTriggered: exportDialog.open()
        }

        MenuSeparator {}

        MenuItem {
            text: i18n("Delete")
            icon.name: "edit-delete"
            onTriggered: deleteConfirm.open()
        }
    }

    // Apply replaces the profile on every path the set covers. Confirm
    // before the batch write.
    Kirigami.PromptDialog {
        id: applyConfirm

        title: i18n("Apply set?")
        subtitle: row.applySubtitleFor(row.setName)
        standardButtons: Kirigami.Dialog.Apply | Kirigami.Dialog.Cancel
        onApplied: row.bridge.applySet(row.setName)
    }

    // Delete is permanent — the JSON file is removed and only a backup
    // would bring it back.
    Kirigami.PromptDialog {
        id: deleteConfirm

        title: i18n("Delete set?")
        subtitle: i18n("\"%1\" will be permanently removed.", row.setName)
        standardButtons: Kirigami.Dialog.Discard | Kirigami.Dialog.Cancel
        onDiscarded: {
            row.bridge.removeSet(row.setName);
            deleteConfirm.close();
        }
    }

    // Metadata editor — name and description travel together through one
    // updateSet call so a rename can't drop the description (or vice versa).
    Kirigami.PromptDialog {
        id: editDialog

        title: i18n("Edit set")
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        onOpened: editNameField.forceActiveFocus()
        onAccepted: {
            const newName = editNameField.text.trim();
            if (newName.length > 0)
                row.bridge.updateSet(row.setName, newName, editDescField.text.trim());
        }

        ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            TextField {
                id: editNameField

                Layout.fillWidth: true
                placeholderText: i18n("Set name…")
                Accessible.name: i18n("Set name")
                onAccepted: editDialog.accept()
            }

            TextField {
                id: editDescField

                Layout.fillWidth: true
                placeholderText: i18n("Description (optional)…")
                Accessible.name: i18n("Set description")
                onAccepted: editDialog.accept()
            }
        }
    }

    FileDialog {
        id: exportDialog

        title: i18n("Export Set")
        nameFilters: [i18n("PlasmaZones Set (*.json)"), i18n("All files (*)")]
        defaultSuffix: "json"
        fileMode: FileDialog.SaveFile
        onAccepted: row.bridge.exportSet(row.setName, settingsController.urlToLocalFile(selectedFile))
    }
}

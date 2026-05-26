// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation
import org.phosphor.settings.ui

/**
 * Persistent dirty indicator + slide-in unsaved-changes action bar.
 *
 * Two visual elements stacked vertically:
 *
 *   1. A 1-px accent line that's always present. Tints highlightColor
 *      when controller.dirty, neutral border otherwise — a calm
 *      always-visible "you have unsaved work" signal.
 *
 *   2. A slide-in notification bar that's hidden when clean and
 *      animates open when controller.dirty flips true. Contains an
 *      icon + "Unsaved changes" label and two action buttons:
 *      Discard (flat — opens a confirm prompt before throwing away
 *      edits) and Save (highlighted — calls controller.applyAll()).
 *      Matches the legacy PlasmaZones footer 1:1.
 */
ColumnLayout {
    id: root

    required property ApplicationController controller

    /** Emitted after the user confirms a discard. Consumers can hook
     *  this to flash a toast, log telemetry, etc. — the discard
     *  itself is already routed through `controller.discardAll()`. */
    signal discarded()
    /** Emitted after the user clicks Save. Same wiring rationale as
     *  `discarded()`: `controller.applyAll()` has already run by the
     *  time consumers see this. */
    signal saved()

    spacing: 0

    // ── Persistent accent line ──────────────────────────────────────
    Rectangle {
        Layout.fillWidth: true
        height: Math.round(Kirigami.Units.devicePixelRatio)
        color: root.controller.dirty ? Kirigami.Theme.highlightColor : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)

        Behavior on color {
            PhosphorMotionAnimation {
                profile: "widget.tint"
            }

        }

    }

    // ── Slide-in unsaved-changes bar ────────────────────────────────
    // The slide is driven by an internal real property (`expansion`
    // 0..1) with a Behavior, and Layout.preferredHeight is bound to
    // `expansion * barContent.implicitHeight`. Animating a plain
    // property and binding the attached property to it works
    // consistently — `Behavior on Layout.preferredHeight` is
    // unreliable in Qt 6 because some Layout pipelines bypass it.
    Item {
        id: dirtyBar

        property real expansion: root.controller.dirty ? 1 : 0

        Layout.fillWidth: true
        Layout.preferredHeight: expansion * barContent.implicitHeight
        clip: true

        Rectangle {
            id: barContent

            width: parent.width
            implicitHeight: barRow.implicitHeight + Kirigami.Units.smallSpacing * 3
            anchors.bottom: parent.bottom
            color: Qt.rgba(Kirigami.Theme.neutralTextColor.r, Kirigami.Theme.neutralTextColor.g, Kirigami.Theme.neutralTextColor.b, 0.12)

            // Top accent line for the bar itself, separate from the
            // persistent line above. Always neutralTextColor so the
            // bar reads as a distinct surface.
            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: Math.round(Kirigami.Units.devicePixelRatio)
                color: Kirigami.Theme.neutralTextColor
                opacity: 0.4
            }

            RowLayout {
                id: barRow

                anchors.fill: parent
                anchors.leftMargin: Kirigami.Units.largeSpacing
                anchors.rightMargin: Kirigami.Units.largeSpacing
                anchors.topMargin: Kirigami.Units.smallSpacing * 1.5
                anchors.bottomMargin: Kirigami.Units.smallSpacing * 1.5
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Icon {
                    source: "dialog-information"
                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                    Layout.preferredHeight: Kirigami.Units.iconSizes.small
                    color: Kirigami.Theme.neutralTextColor
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    text: qsTr("Unsaved changes")
                    color: Kirigami.Theme.neutralTextColor
                }

                QQC2.Button {
                    text: qsTr("Discard")
                    icon.name: "edit-undo"
                    flat: true
                    Accessible.name: qsTr("Discard changes")
                    // Confirm-before-throw matches the legacy chrome:
                    // unsaved edits are easy to lose, so the
                    // destructive action gates behind a prompt rather
                    // than firing immediately.
                    onClicked: confirmDiscardDialog.open()
                }

                QQC2.Button {
                    text: qsTr("Save")
                    icon.name: "document-save"
                    highlighted: true
                    Accessible.name: qsTr("Save settings")
                    onClicked: {
                        root.controller.applyAll();
                        root.saved();
                    }
                }

            }

        }

        // Use the project's accordion motion profiles. Direction is
        // taken from `controller.dirty` (the same flag driving
        // `expansion` above) — reading `expansion` here would re-
        // evaluate during the Behavior and pick the wrong leg as the
        // value approaches its target.
        Behavior on expansion {
            PhosphorMotionAnimation {
                profile: root.controller.dirty ? "widget.accordionExpand" : "widget.accordionCollapse"
            }

        }

    }

    // ── Discard-confirm prompt ──────────────────────────────────────
    // Lives inside the footer (not in SettingsAppWindow) so consumers
    // get the prompt automatically just by mounting the footer.
    // Custom-action layout matches the legacy resetConfirmDialog
    // verbatim: "Discard" + "Cancel", no standard buttons.
    Kirigami.PromptDialog {
        id: confirmDiscardDialog

        title: qsTr("Discard Changes")
        subtitle: qsTr("Are you sure you want to discard all unsaved changes?")
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: qsTr("Discard")
                icon.name: "edit-undo"
                onTriggered: {
                    confirmDiscardDialog.close();
                    root.controller.discardAll();
                    root.discarded();
                }
            },
            Kirigami.Action {
                text: qsTr("Cancel")
                icon.name: "dialog-cancel"
                onTriggered: confirmDiscardDialog.close()
            }
        ]
    }

}

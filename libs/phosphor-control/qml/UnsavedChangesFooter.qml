// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation
import org.phosphor.control

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
 *      Matches the legacy Phosphor footer 1:1.
 */
ColumnLayout {
    id: root

    required property ApplicationController controller
    /// True while an async apply OR discard is in flight; both
    /// action buttons disable on this. Hoisted from per-button
    /// `!applying && !discarding` to dedupe and provide a single
    /// hook a consumer could read.
    readonly property bool actionsBusy: root.controller.applying || root.controller.discarding

    // Pin the colour set to Window so the persistent accent line +
    // notification surfaces sample the chrome's background palette
    // rather than inheriting whatever role-set the parent page used
    // (View, Selection, Complementary, etc.). `inherit: false` stops
    // a darker parent surface from dragging the footer into a
    // mismatched tint. Matches the legacy Phosphor chrome's
    // footer notification surface.
    Kirigami.Theme.colorSet: Kirigami.Theme.Window
    Kirigami.Theme.inherit: false

    /** Emitted when the user confirms Discard in the inline prompt —
     *  fires at the dispatch site (when `discardAllAsync` is called),
     *  NOT when the async batch completes. Consumers that need the
     *  completion notification (per-domain ok/errors) should connect
     *  to `controller.discardAllComplete(ok, errors)` directly. This
     *  signal exists for UI-only side effects (toast, telemetry) that
     *  fire on user intent rather than commit success. */
    signal discardRequested
    /** Emitted when the user clicks Save — fires at the dispatch
     *  site (when `applyAllAsync` is called), NOT when the async
     *  batch completes. See `discardRequested()` for the rationale;
     *  for completion notification connect to
     *  `controller.applyAllComplete(ok, errors)`. */
    signal saveRequested

    spacing: 0

    // ── Persistent accent line ──────────────────────────────────────
    Rectangle {
        id: accentLine

        // Default Rectangle color is white; gate the tint Behavior on
        // Component.completed so the first paint lands without an
        // animated white→target flash. Same first-paint gate idiom as
        // SidebarRow.qml / SidebarBackButton.qml.
        property bool _behaviorReady: false

        Component.onCompleted: _behaviorReady = true
        Layout.fillWidth: true
        height: 1
        color: root.controller.dirty ? Kirigami.Theme.highlightColor : Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)

        Behavior on color {
            enabled: accentLine._behaviorReady

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
            color: Kirigami.Theme.neutralBackgroundColor

            // Top accent line for the bar itself, separate from the
            // persistent line above. An opaque mix of the bar surface
            // and neutralTextColor so the bar reads as a distinct
            // caution surface on every scheme.
            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.neutralBackgroundColor, Kirigami.Theme.neutralTextColor, 0.4)
            }

            RowLayout {
                id: barRow

                anchors.fill: parent
                anchors.leftMargin: Kirigami.Units.largeSpacing
                anchors.rightMargin: Kirigami.Units.largeSpacing
                // Round non-integer Kirigami multipliers to avoid
                // sub-pixel margin values on HiDPI (smallSpacing*1.5
                // = 6 on 1x, 12 on 2x — integer, fine; on 1.5x it's
                // 9 which is still integer — but on themes that scale
                // smallSpacing to odd values like 5, the *1.5 produces
                // 7.5 and Qt rounds inconsistently). Math.round keeps
                // the rail's rhythm pixel-aligned on every DPR.
                anchors.topMargin: Math.round(Kirigami.Units.smallSpacing * 1.5)
                anchors.bottomMargin: Math.round(Kirigami.Units.smallSpacing * 1.5)
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Icon {
                    // Symbolic variant + isMask: true. The plain
                    // "dialog-information" icon ships in multi-color
                    // form in most icon themes, and Kirigami.Icon's
                    // color binding only applies to monochrome / mask
                    // sources — otherwise the tint here is silently
                    // ignored and the warning glyph keeps its theme's
                    // built-in colour, mismatching the "Unsaved
                    // changes" label beside it.
                    source: "dialog-information-symbolic"
                    isMask: true
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
                    // Disabled while a save / discard is in flight so
                    // the user can't fire a second batch on top of an
                    // open D-Bus call. Label flips to "Discarding…"
                    // for symmetry with Save.
                    text: root.controller.discarding ? qsTr("Discarding…") : qsTr("Discard")
                    icon.name: "edit-undo"
                    flat: true
                    enabled: !root.actionsBusy
                    Accessible.name: qsTr("Discard changes")
                    // Confirm-before-throw matches the legacy chrome:
                    // unsaved edits are easy to lose, so the
                    // destructive action gates behind a prompt rather
                    // than firing immediately.
                    onClicked: confirmDiscardDialog.open()
                }

                QQC2.Button {
                    text: root.controller.applying ? qsTr("Saving…") : qsTr("Save")
                    icon.name: "document-save"
                    highlighted: true
                    enabled: !root.actionsBusy
                    Accessible.name: qsTr("Save settings")
                    onClicked: {
                        // applyAllAsync dispatches each domain's
                        // apply() and emits applyAllComplete(ok,
                        // errors) when every domain's applyResult
                        // has landed. The chrome's `applying` flag
                        // drives the button label + a (future) toast
                        // on completion.
                        root.controller.applyAllAsync();
                        root.saveRequested();
                    }
                }
            }
        }

        // `profile` is a live binding on controller.dirty: PhosphorMotionAnimation::setProfile
        // re-applies easing + duration on every dirty flip. Qt's running
        // QPropertyAnimation caches its easing/duration at start-time and
        // doesn't retarget mid-flight, so a dirty flip DURING a slide
        // doesn't rubber-band the running animation — the new profile
        // only takes effect on the next Behavior kickoff. That is the
        // property we want here: the leg's direction is captured cleanly
        // at the moment it starts, and a stale-cache slot-ordering trap
        // (the prior `_slideProfile` cache + Connections handler shape
        // tripped on Qt's binding-vs-Connections wiring order) is gone.
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
                    root.controller.discardAllAsync();
                    root.discardRequested();
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

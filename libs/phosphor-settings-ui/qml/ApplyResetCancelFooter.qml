// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
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
 *      icon + "Unsaved changes" label and three action buttons:
 *      Reset Current Page (visible only when a page is selected),
 *      Cancel (= controller.discardAll), Apply (= controller.applyAll).
 *
 * Apply and Cancel act on the whole application (all staging domains).
 * Reset is per-page: it calls controller.resetCurrentPage which
 * forwards to the active PageController's resetToDefaults(). Pages
 * with no factory defaults just no-op.
 */
ColumnLayout {
    id: root

    required property ApplicationController controller

    spacing: 0

    // ── Persistent accent line ──────────────────────────────────────
    Rectangle {
        Layout.fillWidth: true
        height: Math.round(Kirigami.Units.devicePixelRatio)
        color: root.controller.dirty ? Kirigami.Theme.highlightColor : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)

        Behavior on color {
            ColorAnimation {
                duration: Kirigami.Units.shortDuration
            }

        }

    }

    // ── Slide-in unsaved-changes bar ────────────────────────────────
    Item {
        id: dirtyBar

        Layout.fillWidth: true
        Layout.preferredHeight: root.controller.dirty ? barContent.implicitHeight : 0
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
                    text: qsTr("Reset Current Page")
                    visible: root.controller.currentPageId !== ""
                    icon.name: "edit-undo-symbolic"
                    flat: true
                    onClicked: root.controller.resetCurrentPage()
                }

                QQC2.Button {
                    text: qsTr("Cancel")
                    icon.name: "edit-undo"
                    flat: true
                    onClicked: root.controller.discardAll()
                }

                QQC2.Button {
                    text: qsTr("Apply")
                    icon.name: "document-save"
                    highlighted: true
                    onClicked: root.controller.applyAll()
                }

            }

        }

        Behavior on Layout.preferredHeight {
            NumberAnimation {
                duration: Kirigami.Units.longDuration
                easing.type: root.controller.dirty ? Easing.OutCubic : Easing.InCubic
            }

        }

    }

}

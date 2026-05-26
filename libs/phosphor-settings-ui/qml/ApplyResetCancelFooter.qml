// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.settings.ui

/**
 * Bottom button bar: Reset (current page) | Apply | Cancel.
 *
 * Apply and Cancel act on the whole application (all staging domains).
 * Reset resets only the current page to its factory defaults; the user
 * still needs to hit Apply afterwards to persist.
 */
ColumnLayout {
    id: root

    required property ApplicationController controller

    spacing: 0

    // Persistent accent line that tints highlightColor when dirty —
    // a calm always-visible "you have unsaved work" signal.
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

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        Item {
            Layout.fillWidth: true
            implicitHeight: applyButton.implicitHeight + Kirigami.Units.smallSpacing * 2
        }

        QQC2.Button {
            text: qsTr("Reset Current Page")
            enabled: root.controller.currentPageId !== ""
            icon.name: "edit-undo-symbolic"
            onClicked: root.controller.resetCurrentPage()
            Layout.margins: Kirigami.Units.smallSpacing
        }

        QQC2.Button {
            id: cancelButton

            text: qsTr("Cancel")
            enabled: root.controller.dirty
            icon.name: "dialog-cancel"
            onClicked: root.controller.discardAll()
            Layout.margins: Kirigami.Units.smallSpacing
        }

        QQC2.Button {
            id: applyButton

            text: qsTr("Apply")
            enabled: root.controller.dirty
            icon.name: "dialog-ok-apply"
            highlighted: true
            onClicked: root.controller.applyAll()
            Layout.margins: Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
        }

    }

}

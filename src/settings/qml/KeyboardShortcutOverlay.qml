// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation

/**
 * Full-screen modal overlay listing keyboard shortcuts.
 *
 * Toggled by the parent window's `_showShortcuts` flag (consumers
 * bind `shown: window._showShortcuts` and the overlay calls back
 * `onDismiss` to flip it false). The whole surface fades in/out via
 * `widget.fadeIn` / `widget.fadeOut` at 200 ms.
 *
 * Anchored relative to whatever `parent` is assigned — typical wiring
 * is `parent: window.contentItem` so the overlay covers the entire
 * window including the sidebar + footer.
 */
Rectangle {
    id: root

    /// Drives visibility + animation direction. Consumers bind this to
    /// their window-scope toggle.
    property bool shown: false
    // Theme-tinted text-color shades used multiple times below — extract
    // once to a readonly property so a future theme tweak only touches
    // one place (E32 follow-up).
    readonly property color overlayBg: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.6)
    readonly property color subtleBorder: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)
    readonly property color keyChipBg: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08)

    /// Fired when the user dismisses the overlay (Esc key, background
    /// click). Consumers flip their toggle false.
    signal dismiss()

    anchors.fill: parent
    color: root.overlayBg
    visible: opacity > 0
    opacity: root.shown ? 1 : 0
    z: 200
    Keys.onEscapePressed: root.dismiss()
    focus: root.shown

    MouseArea {
        anchors.fill: parent
        onClicked: root.dismiss()
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.6, Kirigami.Units.gridUnit * 30)
        height: shortcutContent.implicitHeight + Kirigami.Units.largeSpacing * 3
        radius: Kirigami.Units.smallSpacing * 2
        color: Kirigami.Theme.backgroundColor
        border.width: Math.round(Kirigami.Units.devicePixelRatio)
        border.color: root.subtleBorder

        ColumnLayout {
            id: shortcutContent

            anchors.fill: parent
            anchors.margins: Kirigami.Units.largeSpacing * 1.5
            spacing: Kirigami.Units.largeSpacing

            Label {
                text: i18n("Keyboard Shortcuts")
                font.weight: Font.DemiBold
                font.pixelSize: Kirigami.Units.gridUnit * 1.2
                Layout.alignment: Qt.AlignHCenter
            }

            Kirigami.Separator {
                Layout.fillWidth: true
            }

            Repeater {
                model: [{
                    "key": "Meta+Shift+P",
                    "action": i18n("Open PlasmaZones Settings")
                }, {
                    "key": "Meta+Shift+E",
                    "action": i18n("Open Zone Editor")
                }, {
                    "key": "Ctrl+PgUp",
                    "action": i18n("Previous page")
                }, {
                    "key": "Ctrl+PgDown",
                    "action": i18n("Next page")
                }, {
                    "key": "?",
                    "action": i18n("Toggle this overlay")
                }]

                delegate: RowLayout {
                    Layout.fillWidth: true

                    Label {
                        text: modelData.action
                        Layout.fillWidth: true
                        opacity: 0.7
                    }

                    Rectangle {
                        implicitWidth: keyLabel.implicitWidth + Kirigami.Units.largeSpacing
                        implicitHeight: keyLabel.implicitHeight + Kirigami.Units.smallSpacing
                        radius: Kirigami.Units.smallSpacing / 2
                        color: root.keyChipBg
                        border.width: Math.round(Kirigami.Units.devicePixelRatio)
                        border.color: root.subtleBorder

                        Label {
                            id: keyLabel

                            anchors.centerIn: parent
                            text: modelData.key
                            font: Kirigami.Theme.smallFont
                        }

                    }

                }

            }

            Kirigami.Separator {
                Layout.fillWidth: true
            }

            Label {
                text: i18n("Press ? or Escape to close")
                opacity: 0.4
                Layout.alignment: Qt.AlignHCenter
                font: Kirigami.Theme.smallFont
            }

        }

    }

    Behavior on opacity {
        PhosphorMotionAnimation {
            profile: root.shown ? "widget.fadeIn" : "widget.fadeOut"
            durationOverride: 200
        }

    }

}

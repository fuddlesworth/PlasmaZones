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
 * `widget.fadeIn` / `widget.fadeOut` over Kirigami.Units.longDuration.
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
    /// Settings facade exposing `openSettingsShortcut` / `openEditorShortcut`.
    /// Declared as a required property so the dependency is explicit
    /// — previously read via the implicit context property, which
    /// would silently bind to whatever `appSettings` resolved to at
    /// each consumer's scope.
    required property var appSettings
    // Theme-tinted text-color shades used multiple times below — extract
    // once to a readonly property so a future theme tweak only touches
    // one place (E32 follow-up).
    readonly property color overlayBg: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.6)
    readonly property color subtleBorder: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)
    readonly property color keyChipBg: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08)

    /// Hoisted shortcuts model — the previous shape declared the array
    /// inline as the Repeater's `model:` binding, so every binding
    /// re-evaluation of any nested expression rebuilt the entire
    /// model array. Moving it to a `readonly property` keeps the
    /// array stable across binding re-evals and only rebuilds it
    /// when the dependent inputs (the openSettings / openEditor
    /// shortcut strings, language change re-running i18n()) actually
    /// change.
    readonly property var shortcutsModel: [
        {
            "key": (root.appSettings.openSettingsShortcut && root.appSettings.openSettingsShortcut.length > 0) ? root.appSettings.openSettingsShortcut : "Meta+Shift+P",
            "action": i18n("Open PlasmaZones Settings")
        },
        {
            "key": (root.appSettings.openEditorShortcut && root.appSettings.openEditorShortcut.length > 0) ? root.appSettings.openEditorShortcut : "Meta+Shift+E",
            "action": i18n("Open Zone Editor")
        },
        {
            "key": "Ctrl+PgUp",
            "action": i18n("Previous page")
        },
        {
            "key": "Ctrl+PgDown",
            "action": i18n("Next page")
        },
        {
            "key": "Alt+Left",
            "action": i18n("Back")
        },
        {
            "key": "Alt+Right",
            "action": i18n("Forward")
        },
        {
            "key": "?",
            "action": i18n("Toggle this overlay")
        }
    ]

    /// Fired when the user dismisses the overlay (Esc key, background
    /// click). Consumers flip their toggle false.
    signal dismiss

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
        border.width: 1
        border.color: root.subtleBorder

        // Swallow clicks inside the dialog body so curious clicks on key
        // chips or labels don't bubble through to the background MouseArea
        // and dismiss the overlay. Only true background clicks + Esc
        // should dismiss.
        MouseArea {
            anchors.fill: parent
            onClicked: {}
        }

        ColumnLayout {
            id: shortcutContent

            anchors.fill: parent
            anchors.margins: Kirigami.Units.largeSpacing * 1.5
            spacing: Kirigami.Units.largeSpacing

            Kirigami.Heading {
                text: i18n("Keyboard Shortcuts")
                level: 2
                Layout.alignment: Qt.AlignHCenter
            }

            Kirigami.Separator {
                Layout.fillWidth: true
            }

            Repeater {
                // The two global shortcuts (Open Settings, Open Editor)
                // are user-rebindable via SettingsController — read
                // the live values so the overlay stays truthful after
                // the user remaps them. Ctrl+PgUp/Down and "?" are
                // local to this window (not user-configurable), so
                // their literals stay inline. Falls back to the
                // legacy default only if the setting is empty.
                //
                // Backing array hoisted to `root.shortcutsModel` so a
                // language change rebuilds the model exactly once
                // (vs. once per binding re-eval previously).
                model: root.shortcutsModel

                delegate: RowLayout {
                    // Required `modelData` declaration — Qt 6.5+ deprecates the
                    // contextual `modelData` injected automatically by Repeater;
                    // QML_REQUIRES_REGISTRATION builds emit a warning unless
                    // the delegate consumes it through a required property.
                    required property var modelData

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
                        border.width: 1
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
        // Kirigami.Units.longDuration ≈ 250ms on the default profile;
        // scales with the user's animation-speed preference (and
        // honours "reduce motion" by going to 0). Was a hardcoded
        // 200ms that ignored both.
        PhosphorMotionAnimation {
            profile: root.shown ? "widget.fadeIn" : "widget.fadeOut"
            durationOverride: Kirigami.Units.longDuration
        }
    }
}

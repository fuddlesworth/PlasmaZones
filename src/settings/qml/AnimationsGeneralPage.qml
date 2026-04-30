// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Animations → General — global defaults that apply to every event.
 *
 * This is the "global" branch of the inheritance tree: any sub-page event
 * without its own override falls back through its category up to here.
 * Edits route through `appSettings.animation*` (the existing config-driven
 * `Global` profile in `kSettingsDrivenProfilePaths`), not through the
 * per-event ProfileLoader pipeline — so this page intentionally bypasses
 * `AnimationsPageController.setOverride` and writes through the same
 * `Settings::animationProfile` Q_PROPERTYs the legacy General > Animations
 * card has always used.
 */
AnimationSubPage {
    id: page

    // Slider sizing constants needed by EasingSettings (mirrors GeneralPage's
    // contract — the embedded component reads these via its `constants` prop).
    readonly property int sliderPreferredWidth: Kirigami.Units.gridUnit * 16
    readonly property int sliderValueLabelWidth: Kirigami.Units.gridUnit * 3

    Kirigami.InlineMessage {
        Layout.fillWidth: true
        type: Kirigami.MessageType.Information
        visible: true
        text: i18n("These defaults apply to every animation event unless a sub-page (Window, Zone, OSD, etc.) defines its own override.")
    }

    SettingsCard {
        id: animationsCard

        Layout.fillWidth: true
        headerText: i18n("Global animation defaults")
        showToggle: true
        toggleChecked: appSettings.animationsEnabled
        collapsible: true
        onToggleClicked: function(checked) {
            appSettings.animationsEnabled = checked;
        }

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.largeSpacing

            // Drag-handle bezier preview — same component the legacy
            // General page hosts. Edits flip the global easing curve.
            EasingPreview {
                id: easingPreview

                Layout.fillWidth: true
                Layout.maximumWidth: Kirigami.Units.gridUnit * 28
                Layout.alignment: Qt.AlignHCenter
                curve: appSettings.animationEasingCurve
                animationDuration: appSettings.animationDuration
                previewEnabled: animationsCard.toggleChecked
                opacity: animationsCard.toggleChecked ? 1 : 0.4
                onCurveEdited: function(newCurve) {
                    appSettings.animationEasingCurve = newCurve;
                }
            }

            SettingsSeparator {
            }

            // Existing global-profile controls (preset / style / direction /
            // amplitude / period / bounces / duration / sequence mode /
            // stagger / min distance). Reused verbatim — refactoring into
            // a per-event-card shape is a future cleanup phase.
            EasingSettings {
                Layout.fillWidth: true
                // `appSettings` is resolved internally via
                // settingsController.settings — see EasingSettings.qml's
                // header comment for the qmlcachegen timing-window
                // explanation.
                constants: page
                animationsEnabled: animationsCard.toggleChecked
                easingPreview: easingPreview
            }

        }

    }

}

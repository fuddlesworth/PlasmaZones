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
 *
 * The enable toggle and the curve / timing-mode / duration editor live in
 * the shared GlobalTimingDefaultsCard (also hosted by the simple-mode
 * Animations page), which owns the spring-string encode/parse round-trip
 * and the per-axis caching that preserves an easing curve across a Spring
 * preview. This page adds the sequencing, stagger and minimum-distance rows
 * to that card as default children, plus the animation window filter below
 * it.
 */
SettingsFlickable {
    id: page

    readonly property QtObject appSettings: settingsController.settings

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            // Kirigami.InlineMessage defaults to hidden; opt in explicitly.
            visible: true
            text: i18n("These defaults apply to every animation event unless a sub-page (Windows, OSDs, Desktops, etc.) defines its own override.")
        }

        GlobalTimingDefaultsCard {
            id: defaultsCard

            Layout.fillWidth: true
            cardSettings: page.appSettings
            collapsible: true

            SettingsSeparator {}

            SettingsRow {
                title: i18n("Multiple windows")
                searchAnchor: "multipleWindows"
                description: i18n("How to animate when moving several windows at once")

                WideComboBox {
                    id: sequenceModeCombo

                    Accessible.name: i18n("Multiple windows")
                    enabled: defaultsCard.toggleChecked
                    model: [i18n("All at once"), i18n("One by one")]
                    onActivated: index => {
                        page.appSettings.animationSequenceMode = index;
                    }

                    // Guarded Binding so a user activation can't sever the
                    // binding and a config change keeps refreshing the
                    // control. RestoreNone + the popup gate keeps an open
                    // dropdown from being clobbered mid-selection.
                    Binding on currentIndex {
                        value: page.appSettings.animationSequenceMode
                        when: !sequenceModeCombo.popup.visible
                        restoreMode: Binding.RestoreNone
                    }
                }
            }

            SettingsRow {
                visible: page.appSettings.animationSequenceMode === 1
                title: i18n("Stagger delay")
                searchAnchor: "staggerDelay"
                description: i18n("Pause between each window's animation start")

                SettingsSlider {
                    accessibleName: i18n("Stagger delay")
                    enabled: defaultsCard.toggleChecked
                    from: settingsController.generalPage.animationStaggerIntervalMin
                    to: settingsController.generalPage.animationStaggerIntervalMax
                    stepSize: 10
                    value: page.appSettings.animationStaggerInterval
                    valueSuffix: " ms"
                    labelWidth: Kirigami.Units.gridUnit * 4
                    onMoved: value => {
                        page.appSettings.animationStaggerInterval = Math.round(value);
                    }
                }
            }

            SettingsSeparator {}

            SettingsRow {
                title: i18n("Minimum distance")
                searchAnchor: "minimumDistance"
                description: page.appSettings.animationMinDistance === 0 ? i18n("Always animates with no distance threshold") : i18n("Skip animation when geometry changes less than this")

                SettingsSpinBox {
                    id: minDistanceSpin

                    accessibleName: i18n("Minimum distance")
                    enabled: defaultsCard.toggleChecked
                    from: settingsController.generalPage.animationMinDistanceMin
                    to: settingsController.generalPage.animationMinDistanceMax
                    stepSize: 5
                    // Feed value through a guarded Binding so a config change
                    // keeps refreshing the control: a plain `value:` binding is
                    // destroyed by SettingsSpinBox's own edit echo after the
                    // first edit. RestoreNone + the focus gate keeps a live edit
                    // from being clobbered.
                    Binding on value {
                        value: page.appSettings.animationMinDistance
                        when: !minDistanceSpin.editing
                        restoreMode: Binding.RestoreNone
                    }
                    // Match the "zero = disabled, otherwise px" treatment used
                    // by the Minimum window width / height spinboxes below so
                    // the user sees a consistent "Off" / "%1 px" rendering
                    // across every threshold-with-disable spinbox on the page.
                    unitText: ""
                    onValueModified: value => {
                        page.appSettings.animationMinDistance = value;
                    }
                    textFromValue: function (value) {
                        return value === 0 ? i18n("Off") : i18nc("pixel-unit suffix in spin box", "%1 px", value);
                    }
                }
            }
        }

        // ── Window Filtering ─────────────────────────────────────────
        // Animation-global gating: which windows are eligible for any
        // animation at all. Relocated here from the old Animations App
        // Rules page — these are global config toggles, not per-window
        // rules, so they belong on the General page. Per-window
        // overrides now live on the unified Rules page. The card carries its
        // own explainer banner, so both hosts get it from one declaration.
        AnimationWindowFilterCard {
            cardSettings: page.appSettings
        }
    }
}

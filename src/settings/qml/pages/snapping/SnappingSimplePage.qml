// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief The simple-mode snapping surface (SimpleOnly counterpart of the
 * Overlay → Behavior page).
 *
 * Leads with the everyday snapping decisions in one card: when the zone
 * overlay appears while dragging (always, or on a held trigger) and Snap
 * Assist (the fill-remaining-zones window picker). The shared
 * SnappingZoneSpanCard, SnappingWindowHandlingCard and SnappingFocusCard
 * follow, so multi-zone spanning, window handling and focus policy are here
 * too — the same components the advanced pages host. Left to advanced mode:
 * the overlay's tap-to-toggle latch, the Snap Assist hold trigger, display
 * filtering, overlay appearance, the zone selector, priority, shortcuts and
 * shaders.
 */
SettingsFlickable {
    id: root

    readonly property var settingsBridge: settingsController.snappingBehaviorPage
    readonly property int sliderPreferredWidth: Kirigami.Units.gridUnit * 16

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Snapping")
            searchAnchor: "snappingSimple"
            // Matches every shared card below it, and the advanced
            // counterpart's lead card — a non-collapsible card at the top of a
            // page of collapsible ones reads as a broken affordance.
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Show zones on every drag")
                    searchAnchor: "simpleActivateOnEveryDrag"
                    description: i18n("Show the zone overlay on every window drag without requiring a modifier key or mouse button")

                    SettingsSwitch {
                        id: alwaysActivateSwitch

                        checked: root.settingsBridge.alwaysActivateOnDrag
                        accessibleName: i18n("Show zones on every window drag")
                        onToggled: function (newValue) {
                            root.settingsBridge.alwaysActivateOnDrag = newValue;
                        }
                    }
                }

                SettingsSeparator {}

                // Shared row — the same component the advanced Behavior page
                // hosts, so the dual-purpose inversion (with "Show zones on
                // every drag" on, the held trigger HIDES the overlay) is
                // stated once for both surfaces. The wording is this page's
                // own: simple mode talks about showing and hiding zones and
                // leaves "activate" to the advanced page.
                SnappingDragTriggerRow {
                    searchAnchor: "simpleHoldToActivate"
                    settingsBridge: root.settingsBridge
                    alwaysActive: alwaysActivateSwitch.checked
                    controlPreferredWidth: root.sliderPreferredWidth
                    activeTitle: i18n("Hold to hide zones")
                    inactiveTitle: i18n("Hold to show zones")
                    activeDescription: i18n("Hold a modifier or mouse button while dragging to hide the zone overlay")
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Snap Assist")
                    searchAnchor: "simpleSnapAssist"
                    description: i18n("Offer to fill the remaining empty zones after you snap a window")

                    SettingsSwitch {
                        id: snapAssistFeatureSwitch

                        checked: appSettings.snapAssistFeatureEnabled
                        accessibleName: i18n("Snap Assist")
                        onToggled: function (newValue) {
                            appSettings.snapAssistFeatureEnabled = newValue;
                        }
                    }
                }

                SettingsSeparator {
                    enabled: snapAssistFeatureSwitch.checked
                }

                SettingsRow {
                    enabled: snapAssistFeatureSwitch.checked
                    title: i18n("Always show after snapping")
                    searchAnchor: "simpleAlwaysShowAfterSnapping"
                    description: i18n("Show the window picker after every snap, without waiting for you to hold anything")

                    SettingsSwitch {
                        checked: appSettings.snapAssistEnabled
                        accessibleName: i18n("Always show after snapping")
                        onToggled: function (newValue) {
                            appSettings.snapAssistEnabled = newValue;
                        }
                    }
                }
            }
        }

        // The three cards below are the same components the advanced
        // Snapping pages host, so those rows cannot drift. In the card ABOVE,
        // the hold-trigger row is likewise shared, because its meaning
        // inverts with the switch and the runtime mirrors that inversion. The
        // remaining rows there are this page's own, binding the same settings
        // as the advanced Triggers and Snap Assist cards but with plainer
        // titles and without the advanced pages' extra qualifiers.
        SnappingZoneSpanCard {
            Layout.fillWidth: true
            settingsBridge: root.settingsBridge
            sliderPreferredWidth: root.sliderPreferredWidth
        }

        SnappingWindowHandlingCard {
            Layout.fillWidth: true
        }

        SnappingFocusCard {
            Layout.fillWidth: true
        }
    }
}

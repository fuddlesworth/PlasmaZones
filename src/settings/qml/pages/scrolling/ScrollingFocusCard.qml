// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// QtQuick is load-bearing here: the Accessible attached type on the centering
// combo box below comes from it, not from Layouts or Kirigami.
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief The scrolling Focus and view card, the peer of TilingFocusCard and
 * SnappingFocusCard. Alongside the two focus rows those siblings carry, it
 * holds the viewport rows the strip needs: how the view follows the focused
 * column, how a column at the screen edge is shown (crop versus resize), and
 * the two wheel chords ("scroll keys") that drive the strip. All belong with
 * focus rather than on a page of their own, so the card hosts them and the
 * former Scrolling → View leaf is gone. Which way the strip runs is the Strip
 * direction card above this one, which moved out to take a per-monitor scope
 * chip.
 *
 * Most rows bind the appSettings context property, so the card carries no
 * per-page state. App-wide only, matching the tiling/snapping window pages:
 * per-context centering is a rules job (the SetCenterFocusedColumn context
 * action), not a per-monitor chip. The two scroll-key rows are the
 * exception: trigger lists need the bitmask/enum conversion the
 * scrollingBehaviorPage sub-controller performs, exactly as the drag
 * re-insert card's trigger row does.
 */
SettingsCard {
    id: root

    readonly property var settingsBridge: settingsController.scrollingBehaviorPage
    readonly property var _scrollConsts: settingsController.scrollingConstants()

    headerText: i18n("Focus and view")
    searchAnchor: "scrollingFocus"
    collapsible: true

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        SettingsRow {
            title: i18n("Center the focused column")
            searchAnchor: "centerFocusedColumn"
            description: i18nc("the words Never, Always, and On overflow must match the option labels shown in the picker beside this text", "With Never, the strip stays still until the focused column would leave the screen. With Always, the focused column parks in the middle. With On overflow, it centers only once the strip runs past the edge of the screen.")

            WideComboBox {
                Accessible.name: i18n("Center the focused column")
                textRole: "text"
                valueRole: "value"
                model: settingsController.valueOptions("Scrolling", "CenterFocusedColumn")
                storedValue: appSettings.scrollingCenterFocusedColumn
                onActivated: appSettings.scrollingCenterFocusedColumn = currentValue
            }
        }

        SettingsSeparator {}

        SettingsRow {
            title: i18n("Center a lone column")
            searchAnchor: "alwaysCenterSingleColumn"
            description: i18nc("the quoted phrase Center the focused column and the word Never must match the sibling row's title and option label", "When the strip holds a single column, center it even when Center the focused column is set to Never.")

            SettingsSwitch {
                checked: appSettings.scrollingAlwaysCenterSingleColumn
                accessibleName: i18n("Center a lone column")
                onToggled: function (newValue) {
                    appSettings.scrollingAlwaysCenterSingleColumn = newValue;
                }
            }
        }

        SettingsSeparator {}

        SettingsRow {
            title: i18n("Crop columns at the screen edge")
            searchAnchor: "cropStraddlers"
            description: i18n("When this is on, a column at the screen edge keeps its full size and is cut off at the edge. When it is off, the column shrinks to fit, or slides away once too little of it is left. Cropping stops fullscreen video and games from being sent straight to the screen, so they use more power while any screen uses scrolling.")

            SettingsSwitch {
                checked: appSettings.scrollingCropStraddlers
                accessibleName: i18n("Crop columns at the screen edge")
                onToggled: function (newValue) {
                    appSettings.scrollingCropStraddlers = newValue;
                }
            }
        }

        SettingsSeparator {}

        SettingsRow {
            title: i18n("Focus new windows")
            searchAnchor: "scrollingFocusNewWindows"
            description: i18n("Focus a window when it opens.")

            SettingsSwitch {
                checked: appSettings.scrollingFocusNewWindows
                accessibleName: i18n("Focus newly opened windows")
                onToggled: function (newValue) {
                    appSettings.scrollingFocusNewWindows = newValue;
                }
            }
        }

        SettingsSeparator {}

        SettingsRow {
            title: i18n("Focus follows mouse")
            searchAnchor: "scrollingFocusFollowsMouse"
            description: i18n("Moving the mouse pointer over a window gives it focus.")

            SettingsSwitch {
                id: focusFollowsMouseSwitch

                checked: appSettings.scrollingFocusFollowsMouse
                accessibleName: i18n("Focus follows mouse pointer")
                onToggled: function (newValue) {
                    appSettings.scrollingFocusFollowsMouse = newValue;
                }
            }
        }

        // Dependent row: it hugs the switch that gates it, with no separator
        // between them, and stays visible while disabled so a deep link can
        // still reveal its anchor. Same caveat as the scroll-key rows below.
        SettingsRow {
            title: i18n("Limit how far the strip scrolls")
            searchAnchor: "scrollingFocusFollowsMouseMaxScroll"
            description: i18n("Moving the pointer onto a column that is partly off screen scrolls the strip to bring it in. When that scroll would be longer than this share of the work area along the strip, the pointer is ignored and focus stays put. At 100% nothing is ignored.")
            enabled: focusFollowsMouseSwitch.checked
            visible: true

            SettingsSlider {
                accessibleName: i18n("Limit how far the strip scrolls")
                from: root._scrollConsts.ffmMaxScrollMin
                to: root._scrollConsts.ffmMaxScrollMax
                stepSize: 1
                tickMarkStepSize: 5
                value: appSettings.scrollingFocusFollowsMouseMaxScroll
                onMoved: function (newValue) {
                    appSettings.scrollingFocusFollowsMouseMaxScroll = Math.round(newValue);
                }
            }
        }

        SettingsSeparator {}

        SettingsRow {
            title: i18n("Scroll the strip with the mouse wheel")
            searchAnchor: "wheelFocusEnabled"
            description: i18n("Turn the wheel with a scroll key held to move along the strip. When this is off, both scroll keys are left to the compositor.")

            SettingsSwitch {
                id: wheelEnabledSwitch

                checked: appSettings.scrollingWheelFocusEnabled
                accessibleName: i18n("Scroll the strip with the mouse wheel")
                onToggled: function (newValue) {
                    appSettings.scrollingWheelFocusEnabled = newValue;
                }
            }
        }

        // Both lists holding the same chord is legal, and the effect resolves
        // it the same way every time (focus wins), but the view binding is
        // then dead and nothing else on the page would say so.
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Warning
            text: i18n("Both scroll keys use the same chord, so the view scroll key never runs. Give them different chords to use both.")
            visible: root.settingsBridge.wheelTriggersCollide && wheelEnabledSwitch.checked
        }

        // The two scroll keys hug the switch that gates them, and stay
        // visible while disabled so a deep link can still reveal their
        // anchors.
        //
        // CAVEAT for all three of the rows that follow (both scroll keys and
        // the invert row), and for the scroll-cap row above that points here:
        // the sanctioned `visible: true` idiom drops BOTH of SettingsRow's
        // gates, so marking any of them advancedOnly later would silently
        // keep it visible in simple mode. Re-plumb the visible binding if
        // that curation ever happens.
        SettingsRow {
            title: i18n("Scroll key for column focus")
            searchAnchor: "wheelFocusTriggers"
            description: i18n("Hold this and turn the wheel to move focus from column to column.")
            enabled: wheelEnabledSwitch.checked
            visible: true

            ModifierAndMouseCheckBoxes {
                width: TriggerLabels.editorPreferredWidth
                // Modifiers only, matching the "scroll key" these rows are
                // named for. The exact matcher compares buttons as a SUBSET
                // even though it compares modifiers exactly, so a
                // modifier-only chord would shadow a button-bearing one and
                // the longer binding could never be reached. This is the UI
                // half of that rule; canonicalWheelTriggerList enforces it in
                // storage, so a hand-edited config cannot get a button in
                // either.
                acceptMode: acceptModeMetaOnly
                accessibleContext: i18nc("@info:accessibility a sentence fragment substituted into 'Remove trigger for %1' and 'Reset %1 to defaults'", "the column focus scroll key")
                triggers: root.settingsBridge.scrollingWheelFocusTriggers
                defaultTriggers: root.settingsBridge.defaultScrollingWheelFocusTriggers
                tooltipEnabled: false
                onTriggersModified: triggers => {
                    root.settingsBridge.scrollingWheelFocusTriggers = triggers;
                }
            }
        }

        SettingsRow {
            title: i18n("Scroll key for the view")
            searchAnchor: "wheelViewTriggers"
            description: i18n("Hold this and turn the wheel to move the view along the strip without changing which column has focus.")
            enabled: wheelEnabledSwitch.checked
            visible: true

            ModifierAndMouseCheckBoxes {
                width: TriggerLabels.editorPreferredWidth
                // Modifiers only, for the same reason as the focus row above.
                acceptMode: acceptModeMetaOnly
                accessibleContext: i18nc("@info:accessibility a sentence fragment substituted into 'Remove trigger for %1' and 'Reset %1 to defaults'", "the view scroll key")
                triggers: root.settingsBridge.scrollingWheelViewTriggers
                defaultTriggers: root.settingsBridge.defaultScrollingWheelViewTriggers
                tooltipEnabled: false
                onTriggersModified: triggers => {
                    root.settingsBridge.scrollingWheelViewTriggers = triggers;
                }
            }
        }

        // Dependent row: it hugs the row that gates it (no separator between
        // them, the card's convention) and stays visible while disabled rather
        // than taking SettingsRow's default collapse, because it carries a
        // search anchor a deep link must reveal. See the caveat above the two
        // scroll-key rows for what the `visible: true` override costs.
        SettingsRow {
            title: i18n("Invert wheel direction")
            searchAnchor: "wheelFocusInverted"
            description: i18n("Scrolling down moves toward the start of the strip instead of the end, for both scroll keys.")
            enabled: wheelEnabledSwitch.checked
            visible: true

            SettingsSwitch {
                checked: appSettings.scrollingWheelFocusInverted
                accessibleName: i18n("Invert wheel direction")
                onToggled: function (newValue) {
                    appSettings.scrollingWheelFocusInverted = newValue;
                }
            }
        }
    }
}

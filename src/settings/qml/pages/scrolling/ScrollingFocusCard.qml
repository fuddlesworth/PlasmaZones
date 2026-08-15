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
 * the Meta+wheel column-focus gesture. All belong with focus rather than on
 * a page of their own, so the card hosts them and the former Scrolling →
 * View leaf is gone.
 *
 * All rows bind the appSettings context property, so the card carries no
 * per-page state. App-wide only, matching the tiling/snapping window pages:
 * per-context centering is a rules job (the SetCenterFocusedColumn context
 * action), not a per-monitor chip.
 */
SettingsCard {
    headerText: i18n("Focus and view")
    searchAnchor: "scrollingFocus"
    collapsible: true

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        SettingsRow {
            title: i18n("Strip direction")
            searchAnchor: "stripAxis"
            description: i18nc("the words Match the screen shape, Side to side, and Top to bottom must match the option labels shown in the picker beside this text", "Which way the strip runs. Match the screen shape runs it top to bottom on a monitor that is taller than it is wide, and side to side otherwise. Columns still divide across the strip whichever way it runs.")

            WideComboBox {
                Accessible.name: i18n("Strip direction")
                textRole: "text"
                valueRole: "value"
                model: settingsController.valueOptions("Scrolling", "StripAxis")
                storedValue: appSettings.scrollingStripAxis
                onActivated: appSettings.scrollingStripAxis = currentValue
            }
        }

        SettingsSeparator {}

        SettingsRow {
            title: i18n("Center the focused column")
            searchAnchor: "centerFocusedColumn"
            description: i18nc("the words Never, Always, and On overflow must match the option labels shown in the picker beside this text", "With Never, the strip stays still until the focused column would leave the screen. With Always, the focused column parks in the middle. With On overflow, it centers only once the strip is wider than the screen.")

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
            description: i18n("When this is on, a column at the screen edge keeps its full size and is cut off at the edge. When it is off, the column shrinks to fit, or slides away once too little of it is left. Cropping costs some efficiency in fullscreen video and games while any screen uses scrolling.")

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
                checked: appSettings.scrollingFocusFollowsMouse
                accessibleName: i18n("Focus follows mouse pointer")
                onToggled: function (newValue) {
                    appSettings.scrollingFocusFollowsMouse = newValue;
                }
            }
        }

        SettingsSeparator {}

        SettingsRow {
            title: i18n("Scroll columns with the mouse wheel")
            searchAnchor: "wheelFocusEnabled"
            description: i18n("Hold Meta and scroll to move column focus along the strip. When this is off, the compositor keeps the Meta+wheel shortcut.")

            SettingsSwitch {
                checked: appSettings.scrollingWheelFocusEnabled
                accessibleName: i18n("Scroll columns with the mouse wheel")
                onToggled: function (newValue) {
                    appSettings.scrollingWheelFocusEnabled = newValue;
                }
            }
        }

        // Dependent row: it hugs the row that gates it (no separator between
        // them, the card's convention) and stays visible while disabled rather
        // than taking SettingsRow's default collapse, because it carries a
        // search anchor a deep link must reveal. CAVEAT the sanctioned
        // `visible: true` idiom hides: this override drops BOTH of
        // SettingsRow's gates, so marking this row advancedOnly later would
        // silently keep it visible in simple mode — re-plumb the visible
        // binding if that curation ever happens.
        SettingsRow {
            title: i18n("Invert wheel direction")
            searchAnchor: "wheelFocusInverted"
            description: i18n("Scrolling down focuses the previous column instead of the next one.")
            enabled: appSettings.scrollingWheelFocusEnabled
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

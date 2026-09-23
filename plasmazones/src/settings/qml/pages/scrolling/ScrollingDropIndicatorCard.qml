// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief The drop-target highlight painted while a drag re-insert is armed.
 *
 * Sits beside the Triggers card because it only ever appears during the drag
 * those triggers arm.
 *
 * Deliberately mirrors the snapping zone overlay's appearance vocabulary
 * (Snapping → Overlay → Appearance): a fill colour, a border colour, a fill
 * opacity, a border width and a corner radius, with the same bounds. The two
 * highlights answer the same question — "which space is about to receive this
 * window?" — so a user who has tuned one should find the other already
 * speaking their language.
 *
 * What it does NOT borrow from that page: labels, zone numbers and the
 * active/inactive colour pair. There is no text in a drop indicator, and
 * exactly one drop target at a time, so there is no inactive state to colour.
 *
 * The layout itself is not configurable, unlike the tab indicator's. The rect
 * comes from the engine's own layout math and cannot be positioned
 * independently of where the drop actually lands.
 */
SettingsCard {
    id: root

    /// The page-level ColorDialog, shared by both colour rows. Deliberately
    /// `var` rather than a typed Dialog, matching ThemeFallbackColorRow's own
    /// picker property: the row duck-types anything exposing accepted /
    /// rejected / selectedColor / open(), and typing it here would drag a
    /// QtQuick.Dialogs import into a card that never constructs one.
    /// Page-level for the same reason the Tabs page shares one: a page rebuild
    /// while a row-scoped dialog is open would tear the popup down under the
    /// user.
    required property var picker

    /// Bounds, read through the controller so C++ stays the single home for
    /// these numbers (the same accessor the Columns and Tabs pages use).
    readonly property var _scrollConsts: settingsController.scrollingConstants()
    /// The opacity slider is integral, so the stored 0..1 fraction rides it as
    /// whole percent. Matches the snapping Appearance page's opacity rows.
    readonly property int opacitySliderMax: 100

    headerText: i18n("Drop indicator")
    searchAnchor: "scrollingDropIndicator"
    collapsible: true
    // The master switch lives in the card HEADER (like the Borders and
    // Opacity-and-tint cards), not as a body row: search deep-links to the
    // colour rows below land on the invisible-target fallback while the
    // indicator is off, and that fallback scrolls to and pulses the card
    // header on the assumption its toggle is what un-hides the target.
    showToggle: true
    toggleChecked: appSettings.scrollingDropIndicatorEnabled
    onToggleClicked: checked => appSettings.scrollingDropIndicatorEnabled = checked

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        ThemeFallbackColorRow {
            id: dropFillColorRow

            title: i18n("Fill color")
            // Overridden because the title already says "color"; the default
            // would announce "Fill color color".
            swatchAccessibleName: i18nc("@action:button", "Drop indicator fill color")
            searchAnchor: "scrollingDropIndicatorColor"
            description: i18n("Color filling the space the window will land in. Follows the color scheme unless you pick one.")

            storedColor: appSettings.scrollingDropIndicatorColorRaw
            // The RESOLVED colour, not Kirigami.Theme's own highlight: Settings
            // owns the fallback now (resolvedSystemColor), and previewing a
            // second resolution here could show a swatch the indicator never
            // paints. Its NOTIFY rides the palette-change fan-out, so the
            // preview still follows a theme switch live.
            themeColor: appSettings.scrollingDropIndicatorColor
            picker: root.picker
            onColorChosen: function (hex) {
                // The FILL alpha is owned by the opacity slider below (the
                // paint path replaces the colour's own alpha with it), so
                // store the pick opaque like the snapping fill rows do —
                // otherwise the swatch previews a transparency the
                // indicator never draws. The border row keeps its alpha:
                // it has no slider, so the colour's alpha is its single
                // control.
                appSettings.scrollingDropIndicatorColorRaw = hex === dropFillColorRow.sentinel ? hex : "#FF" + hex.slice(3);
            }
        }

        SettingsSeparator {}

        SettingsRow {
            title: i18n("Fill opacity")
            searchAnchor: "scrollingDropIndicatorOpacity"
            description: i18n("How solid the fill is. This replaces any transparency carried by the fill color.")

            SettingsSlider {
                accessibleName: i18n("Fill opacity")
                from: root._scrollConsts.dropOpacityMin * root.opacitySliderMax
                to: root._scrollConsts.dropOpacityMax * root.opacitySliderMax
                value: appSettings.scrollingDropIndicatorOpacity * root.opacitySliderMax
                onMoved: value => {
                    return appSettings.scrollingDropIndicatorOpacity = value / root.opacitySliderMax;
                }
            }
        }

        SettingsSeparator {}

        ThemeFallbackColorRow {
            title: i18n("Border color")
            // See the fill row above.
            swatchAccessibleName: i18nc("@action:button", "Drop indicator border color")
            searchAnchor: "scrollingDropIndicatorBorderColor"
            description: i18n("Color of the indicator's edge. Follows the color scheme unless you pick one.")

            storedColor: appSettings.scrollingDropIndicatorBorderColorRaw
            // See the fill row: the resolved colour, which is opaque so an
            // unset border previews as solid as it draws.
            themeColor: appSettings.scrollingDropIndicatorBorderColor
            picker: root.picker
            onColorChosen: function (hex) {
                appSettings.scrollingDropIndicatorBorderColorRaw = hex;
            }
        }

        SettingsSeparator {}

        SettingsRow {
            title: i18n("Border width")
            searchAnchor: "scrollingDropIndicatorBorderWidth"
            description: i18n("Thickness of the indicator's edge in pixels. Zero draws the fill with no edge.")

            SettingsSpinBox {
                id: dropBorderWidthSpin

                accessibleName: i18n("Border width")
                from: root._scrollConsts.dropBorderWidthMin
                to: root._scrollConsts.dropBorderWidthMax
                onValueModified: value => {
                    return appSettings.scrollingDropIndicatorBorderWidth = value;
                }
                // Guarded Binding rather than a plain `value:` binding, which
                // SettingsSpinBox's own edit echo destroys after the first
                // edit. RestoreNone + the focus gate keeps a live edit from
                // being clobbered. Same shape as the snapping Appearance
                // page's border spins.
                Binding on value {
                    value: appSettings.scrollingDropIndicatorBorderWidth
                    when: !dropBorderWidthSpin.editing
                    restoreMode: Binding.RestoreNone
                }
            }
        }

        SettingsSeparator {}

        SettingsRow {
            title: i18n("Corner radius")
            searchAnchor: "scrollingDropIndicatorBorderRadius"
            description: i18n("Corner rounding of the indicator in pixels.")

            SettingsSpinBox {
                id: dropBorderRadiusSpin

                accessibleName: i18n("Corner radius")
                from: root._scrollConsts.dropBorderRadiusMin
                to: root._scrollConsts.dropBorderRadiusMax
                onValueModified: value => {
                    return appSettings.scrollingDropIndicatorBorderRadius = value;
                }
                // See the border width spin: guarded Binding so a config change
                // keeps refreshing after the first edit destroys a plain one.
                Binding on value {
                    value: appSettings.scrollingDropIndicatorBorderRadius
                    when: !dropBorderRadiusSpin.editing
                    restoreMode: Binding.RestoreNone
                }
            }
        }
    }
}

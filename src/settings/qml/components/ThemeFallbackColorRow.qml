// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kirigami as Kirigami

/**
 * @brief One theme-fallback colour row: a swatch, the hex, and a reset.
 *
 * The standard control for every colour setting whose default is "follow a
 * system colour", wrapped in SettingsRow chrome for the settings pages. A
 * stored SENTINEL value (empty string by default) means "follow", which no
 * plain colour control can express — a picker always has some colour
 * selected. So the row pairs the swatch with an explicit Reset that clears
 * back to the sentinel, and previews the theme colour it would fall back to
 * while it is unset. Without that pairing there would be no way back to the
 * default once a colour had been picked.
 *
 * A host whose config key uses a different sentinel (the rules editor's
 * "accent") sets `sentinel` and `fallbackLabel` to match; the row itself
 * never interprets the value beyond comparing against the sentinel. Hosts
 * with different chrome use ThemeFallbackColorControl, the inner control
 * this row wraps, directly.
 *
 * The picker itself is PAGE-LEVEL and passed in rather than owned here: a page
 * rebuild while the dialog is open would destroy a row-scoped dialog and tear
 * the popup down under the user. (A card collapse would not — SettingsCard
 * only drives its clip height and never destroys the subtree.) The accepted
 * handler is transient and self-disconnects, so several rows can share one
 * dialog without crossing wires.
 */
SettingsRow {
    id: root

    /// The stored value: a hex string, or `sentinel` for "follow the theme".
    property string storedColor: ""
    /// The stored value that means "follow the system colour". Empty string
    /// for the config theme-fallback keys; a host key with a reserved token
    /// (e.g. "accent") overrides it.
    property string sentinel: ""
    /// What the label shows while the sentinel is stored, naming the system
    /// colour being followed.
    property string fallbackLabel: i18n("Color scheme")
    /// What the Reset button announces and its tooltip says.
    property string resetAccessibleName: i18n("Reset to the color scheme")
    property string resetToolTip: i18n("Follow the color scheme")
    /// What the indicator actually draws while storedColor is the sentinel.
    /// Shown as the swatch so the row previews the real result rather than a
    /// blank.
    property color themeColor: Kirigami.Theme.highlightColor
    /// The page-level ColorDialog (see the class note). Deliberately `var`
    /// rather than a typed `Dialog`: the row duck-types anything exposing
    /// accepted / rejected / selectedColor / open(), so a host can substitute
    /// its own picker without this file importing QtQuick.Dialogs.
    property var picker: null

    /// What a screen reader announces for the swatch. Defaults to the title
    /// with "color" appended, which reads correctly for a title naming the
    /// thing being coloured ("Active tab" -> "Active tab color"). A host whose
    /// title already says "color" MUST override this, or the announcement
    /// stutters ("Fill color color").
    property string swatchAccessibleName: i18nc("@action:button", "%1 color", root.title)

    /// Emitted with the chosen `#AARRGGBB`, or the sentinel on reset.
    signal colorChosen(string hex)

    ThemeFallbackColorControl {
        storedColor: root.storedColor
        sentinel: root.sentinel
        fallbackLabel: root.fallbackLabel
        resetAccessibleName: root.resetAccessibleName
        resetToolTip: root.resetToolTip
        themeColor: root.themeColor
        picker: root.picker
        swatchAccessibleName: root.swatchAccessibleName
        onColorChosen: function (hex) {
            root.colorChosen(hex);
        }
    }
}

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief One tab-indicator colour row: a swatch, the hex, and a reset.
 *
 * The three tab colours all store EMPTY to mean "follow the colour scheme",
 * which no plain colour control can express — a picker always has some colour
 * selected. So the row pairs the swatch with an explicit Reset that clears
 * back to empty, and previews the theme colour it would fall back to while it
 * is unset. Without that pairing there would be no way back to the default
 * once a colour had been picked.
 *
 * The picker itself is PAGE-LEVEL and passed in rather than owned here: a
 * card collapse or a page rebuild while the dialog is open would destroy a
 * row-scoped dialog and tear the popup down under the user. The accepted
 * handler is transient and self-disconnects, so several rows can share one
 * dialog without crossing wires.
 */
SettingsRow {
    id: root

    /// The stored value: a hex string, or EMPTY for "follow the theme".
    property string storedColor: ""
    /// What the indicator actually draws while storedColor is empty. Shown as
    /// the swatch so the row previews the real result rather than a blank.
    property color themeColor: Kirigami.Theme.highlightColor
    /// The page-level ColorDialog (see the class note).
    property var picker: null

    /// Emitted with the chosen `#AARRGGBB`, or an EMPTY string on reset.
    signal colorChosen(string hex)

    readonly property bool _followsTheme: root.storedColor.length === 0

    // Qt's color.toString() drops the alpha channel when fully opaque and
    // keeps it otherwise, so the hex label's width would jump between 6 and 8
    // digits. Always emit the full 8-digit form, matching ColorSwatchRow.
    //
    // Takes a COLOR, not a string. Handing it `storedColor` reads `.a`/`.r`
    // off a QML string, which are undefined, and `Math.round(undefined * 255)`
    // is NaN — the label rendered a literal "#NANNANNANNAN". Anything already
    // in string form wants _displayHex below instead.
    function _toHexArgb(c) {
        function pad(v) {
            return Math.round(v * 255).toString(16).padStart(2, '0');
        }
        return ("#" + pad(c.a) + pad(c.r) + pad(c.g) + pad(c.b)).toUpperCase();
    }

    /// The stored value as the label shows it. Already a hex string — the
    /// picker writes it through _toHexArgb — so this only normalises case. A
    /// hand-edited short form (`#RGB`) is shown VERBATIM rather than silently
    /// rewritten into a longer one the user never typed.
    function _displayHex(s) {
        return s.toUpperCase();
    }

    RowLayout {
        spacing: Kirigami.Units.smallSpacing

        ColorButton {
            id: swatch

            color: root._followsTheme ? root.themeColor : root.storedColor
            Accessible.name: root.title
            onClicked: {
                if (!root.picker)
                    return;
                var picker = root.picker;

                function acceptedHandler() {
                    picker.accepted.disconnect(acceptedHandler);
                    picker.rejected.disconnect(rejectedHandler);
                    root.colorChosen(root._toHexArgb(picker.selectedColor));
                }

                function rejectedHandler() {
                    picker.accepted.disconnect(acceptedHandler);
                    picker.rejected.disconnect(rejectedHandler);
                }

                picker.accepted.connect(acceptedHandler);
                picker.rejected.connect(rejectedHandler);
                // Seed imperatively: ColorDialog writes selectedColor itself
                // as the user drags, and that JS-side write would sever a
                // declarative binding after the first edit.
                picker.selectedColor = swatch.color;
                picker.open();
            }
        }

        QQC2.Label {
            text: root._followsTheme ? i18n("Color scheme") : root._displayHex(root.storedColor)
            color: root._followsTheme ? Kirigami.Theme.disabledTextColor : Kirigami.Theme.textColor
            font.family: root._followsTheme ? Kirigami.Theme.defaultFont.family : "monospace"
            Layout.preferredWidth: Kirigami.Units.gridUnit * 6
            elide: Text.ElideRight
        }

        QQC2.Button {
            icon.name: "edit-reset"
            // Nothing to reset while the colour already follows the theme.
            enabled: !root._followsTheme
            text: i18n("Reset")
            display: QQC2.AbstractButton.IconOnly
            Accessible.name: i18n("Reset to the color scheme")
            onClicked: root.colorChosen("")

            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.text: i18n("Follow the color scheme")
        }
    }
}

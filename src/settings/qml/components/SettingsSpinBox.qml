// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief A SpinBox + unit label in a FormLayout-compatible RowLayout.
 *
 * Wraps a SpinBox with an adjacent Label showing the unit text
 * (default "px"). Supports optional tooltip and custom textFromValue.
 */
RowLayout {
    id: root

    property int from: 0
    property int to: 100
    property int stepSize: 1
    property int value: 0
    property string unitText: i18nc("pixels unit suffix in a spin box", "px")
    property string tooltipText
    property var textFromValue: null
    //* @brief Opt-in typing (QQC2 editable). Off by default because it is
    //* only safe with a textFromValue whose output the DEFAULT valueFromText
    //* can parse back — the plain localized number. Hosts that embed a unit
    //* suffix in textFromValue must leave this off (or pair a matching
    //* valueFromText), so it is per-instance rather than blanket-on.
    //* Wide-range spins (hundreds of button steps end to end) want it on.
    property alias editable: spinBox.editable
    //* @brief Screen-reader name for the INNER SpinBox (the focusable control).
    //* Setting Accessible.name on this RowLayout wrapper never reaches the
    //* SpinBox, so callers use this instead (mirrors SettingsSlider).
    property string accessibleName: ""

    /// True while the inner SpinBox has keyboard focus. A host that feeds
    /// `value` through an external Binding gates it on `!editing` so a live edit
    /// is not overwritten, while still letting a later reload refresh the value
    /// (the inner onValueModified echo to `root.value` otherwise destroys a
    /// computed `value:` binding on the host side after the first edit).
    readonly property alias editing: spinBox.activeFocus

    signal valueModified(int value)

    spacing: Kirigami.Units.smallSpacing

    SpinBox {
        id: spinBox

        // Fold the unit into the spoken name. The unit is a sibling Label, so
        // a screen reader parked on the spin box would otherwise announce a
        // bare number with no idea whether it is pixels, milliseconds or a
        // count.
        Accessible.name: root.accessibleName.length > 0 && root.unitText.length > 0 ? root.accessibleName + " " + root.unitText : root.accessibleName
        from: root.from
        to: root.to
        stepSize: root.stepSize
        value: root.value
        onValueModified: {
            root.value = value;
            root.valueModified(value);
        }
        textFromValue: root.textFromValue ? root.textFromValue : function (value, locale) {
            return Number(value).toLocaleString(locale, 'f', 0);
        }
        ToolTip.visible: root.tooltipText.length > 0 && hovered
        ToolTip.text: root.tooltipText
    }

    Label {
        text: root.unitText
    }
}

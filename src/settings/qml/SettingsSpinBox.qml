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
    property string unitText: "px"
    property string tooltipText
    property var textFromValue: null

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

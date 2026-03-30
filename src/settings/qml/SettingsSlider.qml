// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief A Slider + value label in a FormLayout-compatible RowLayout.
 *
 * Wraps a Slider with an adjacent Label showing the current value
 * and an optional suffix (default "%").
 *
 * For custom formatting, set formatValue to a function(value) -> string.
 * When formatValue is set, valueSuffix is ignored.
 */
RowLayout {
    id: root

    property real from: 0
    property real to: 100
    property real stepSize: 1
    property real value: 0
    property string valueSuffix: "%"
    property int sliderWidth: Kirigami.Units.gridUnit * 16
    property int labelWidth: Kirigami.Units.gridUnit * 3
    property var formatValue: null
    //* @brief Provides direct access to the internal Slider for Binding targets.
    readonly property alias slider: slider

    signal moved(real value)

    spacing: Kirigami.Units.smallSpacing

    Binding {
        target: slider
        property: "value"
        value: root.value
        when: !slider.pressed
    }

    Slider {
        id: slider

        Layout.preferredWidth: root.sliderWidth
        from: root.from
        to: root.to
        stepSize: root.stepSize
        onMoved: {
            root.value = value;
            root.moved(value);
        }
    }

    Label {
        text: root.formatValue ? root.formatValue(slider.value) : Math.round(slider.value) + root.valueSuffix
        Layout.preferredWidth: root.labelWidth
    }

}

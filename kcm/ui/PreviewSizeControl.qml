// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Enhanced preview size control with presets and live preview
 *
 * Provides an intuitive way to configure zone selector preview dimensions
 * with visual feedback, common presets, and smart aspect ratio handling.
 */
Item {
    id: root

    // Current dimensions
    property int previewWidth: 180
    property int previewHeight: 101
    property bool lockAspectRatio: true
    property real screenAspectRatio: 16 / 9
    property bool enabled: true
    // Constraints
    property int minWidth: 80
    property int maxWidth: 400
    property int minHeight: 60
    property int maxHeight: 300
    // Presets: name -> { width, description }
    readonly property var presets: [{
        "name": i18n("Small"),
        "width": 120,
        "desc": i18n("Compact layout previews")
    }, {
        "name": i18n("Medium"),
        "width": 180,
        "desc": i18n("Balanced size (default)")
    }, {
        "name": i18n("Large"),
        "width": 260,
        "desc": i18n("Detailed layout previews")
    }]
    // Determine current preset (-1 if custom)
    property int currentPresetIndex: {
        for (var i = 0; i < presets.length; i++) {
            if (Math.abs(previewWidth - presets[i].width) <= 15)
                return i;

        }
        return -1; // Custom
    }

    signal widthChanged(int width)
    signal heightChanged(int height)
    signal aspectLockChanged(bool locked)

    implicitWidth: 320
    implicitHeight: contentColumn.implicitHeight

    ColumnLayout {
        id: contentColumn

        anchors.fill: parent
        spacing: Kirigami.Units.smallSpacing
        opacity: root.enabled ? 1 : 0.6

        // Preset buttons row
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Label {
                text: i18n("Presets:")
                Layout.preferredWidth: 60
            }

            Repeater {
                model: root.presets

                Button {
                    required property var modelData
                    required property int index

                    text: modelData.name
                    flat: root.currentPresetIndex !== index
                    highlighted: root.currentPresetIndex === index
                    enabled: root.enabled
                    implicitWidth: Math.max(70, implicitContentWidth + Kirigami.Units.largeSpacing)
                    onClicked: {
                        root.previewWidth = modelData.width;
                        root.widthChanged(modelData.width);
                        if (root.lockAspectRatio) {
                            var newHeight = Math.round(modelData.width / root.screenAspectRatio);
                            newHeight = Math.max(root.minHeight, Math.min(root.maxHeight, newHeight));
                            root.previewHeight = newHeight;
                            root.heightChanged(newHeight);
                        }
                    }
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.text: modelData.desc + " (" + modelData.width + "px)"
                }

            }

            // Custom indicator
            Label {
                visible: root.currentPresetIndex === -1
                text: i18n("(Custom)")
                font.italic: true
                opacity: 0.7
            }

        }

        // Width slider with value
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Label {
                text: i18n("Width:")
                Layout.preferredWidth: 60
            }

            Slider {
                id: widthSlider

                Layout.fillWidth: true
                from: root.minWidth
                to: root.maxWidth
                value: root.previewWidth
                stepSize: 5
                snapMode: Slider.SnapOnRelease
                enabled: root.enabled
                onMoved: {
                    root.previewWidth = value;
                    root.widthChanged(value);
                    if (root.lockAspectRatio) {
                        var newHeight = Math.round(value / root.screenAspectRatio);
                        newHeight = Math.max(root.minHeight, Math.min(root.maxHeight, newHeight));
                        root.previewHeight = newHeight;
                        root.heightChanged(newHeight);
                    }
                }

                // Keep slider in sync with external changes
                Binding on value {
                    value: root.previewWidth
                    restoreMode: Binding.RestoreNone
                }

            }

            SpinBox {
                id: widthSpinBox

                from: root.minWidth
                to: root.maxWidth
                value: root.previewWidth
                editable: true
                enabled: root.enabled
                Layout.preferredWidth: 100
                textFromValue: function(value, locale) {
                    return value + " px";
                }
                valueFromText: function(text, locale) {
                    var num = parseInt(text.replace(/[^0-9]/g, ""));
                    return isNaN(num) ? root.minWidth : Math.max(root.minWidth, Math.min(root.maxWidth, num));
                }
                onValueModified: {
                    root.previewWidth = value;
                    root.widthChanged(value);
                    if (root.lockAspectRatio) {
                        var newHeight = Math.round(value / root.screenAspectRatio);
                        newHeight = Math.max(root.minHeight, Math.min(root.maxHeight, newHeight));
                        root.previewHeight = newHeight;
                        root.heightChanged(newHeight);
                    }
                }

                // Keep spinbox in sync
                Binding on value {
                    value: root.previewWidth
                    restoreMode: Binding.RestoreNone
                }

            }

        }

        // Height slider with value (visually dimmed when aspect locked)
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing
            opacity: root.lockAspectRatio ? 0.5 : 1

            Label {
                text: i18n("Height:")
                Layout.preferredWidth: 60
            }

            Slider {
                id: heightSlider

                Layout.fillWidth: true
                from: root.minHeight
                to: root.maxHeight
                value: root.previewHeight
                stepSize: 5
                snapMode: Slider.SnapOnRelease
                enabled: root.enabled && !root.lockAspectRatio
                onMoved: {
                    root.previewHeight = value;
                    root.heightChanged(value);
                }

                Binding on value {
                    value: root.previewHeight
                    restoreMode: Binding.RestoreNone
                }

            }

            SpinBox {
                id: heightSpinBox

                from: root.minHeight
                to: root.maxHeight
                value: root.previewHeight
                editable: true
                enabled: root.enabled && !root.lockAspectRatio
                Layout.preferredWidth: 100
                textFromValue: function(value, locale) {
                    return value + " px";
                }
                valueFromText: function(text, locale) {
                    var num = parseInt(text.replace(/[^0-9]/g, ""));
                    return isNaN(num) ? root.minHeight : Math.max(root.minHeight, Math.min(root.maxHeight, num));
                }
                onValueModified: {
                    root.previewHeight = value;
                    root.heightChanged(value);
                }

                Binding on value {
                    value: root.previewHeight
                    restoreMode: Binding.RestoreNone
                }

            }

        }

        // Aspect ratio lock and live preview row
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing

            CheckBox {
                id: aspectLockCheck

                text: i18n("Lock aspect ratio")
                checked: root.lockAspectRatio
                enabled: root.enabled
                onToggled: {
                    root.lockAspectRatio = checked;
                    root.aspectLockChanged(checked);
                    if (checked) {
                        // Recalculate height to match current width
                        var newHeight = Math.round(root.previewWidth / root.screenAspectRatio);
                        newHeight = Math.max(root.minHeight, Math.min(root.maxHeight, newHeight));
                        root.previewHeight = newHeight;
                        root.heightChanged(newHeight);
                    }
                }
            }

            // Lock icon indicator
            Kirigami.Icon {
                source: root.lockAspectRatio ? "object-locked" : "object-unlocked"
                width: Kirigami.Units.iconSizes.small
                height: width
                color: root.lockAspectRatio ? Kirigami.Theme.highlightColor : Kirigami.Theme.textColor
                opacity: 0.7
            }

            Item {
                Layout.fillWidth: true
            }

            // Live mini preview showing actual proportions
            Rectangle {
                id: livePreview

                // Scale to fit in available space while maintaining proportions
                property real maxPreviewWidth: 80
                property real maxPreviewHeight: 50
                property real scale: Math.min(maxPreviewWidth / root.previewWidth, maxPreviewHeight / root.previewHeight)

                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                width: root.previewWidth * scale
                height: root.previewHeight * scale
                color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.15)
                radius: 4
                border.color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.6)
                border.width: 1

                // Simulated zone preview inside
                Row {
                    anchors.centerIn: parent
                    anchors.margins: 2
                    spacing: 2

                    Repeater {
                        model: 3

                        Rectangle {
                            width: (livePreview.width - 8) / 3
                            height: livePreview.height - 6
                            radius: 2
                            color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)
                            border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.3)
                            border.width: 1
                        }

                    }

                }

                // Dimension label
                Label {
                    anchors.top: parent.bottom
                    anchors.topMargin: 2
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: root.previewWidth + " x " + root.previewHeight
                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                    opacity: 0.6
                }

                MouseArea {
                    id: previewMouse

                    anchors.fill: parent
                    hoverEnabled: true
                    ToolTip.visible: containsMouse
                    ToolTip.delay: 300
                    ToolTip.text: i18n("Preview: %1 x %2 pixels\nAspect ratio: %3", root.previewWidth, root.previewHeight, (root.previewWidth / root.previewHeight).toFixed(2))
                }

            }

        }

    }

}

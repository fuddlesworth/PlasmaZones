// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    readonly property int sliderPreferredWidth: 200
    readonly property int sliderValueLabelWidth: 40

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // --- Launch Editor ---
        Kirigami.Card {
            Layout.fillWidth: true

            header: Kirigami.Heading {
                text: i18n("Layout Editor")
                level: 2
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: i18n("Open the layout editor to create and modify zone layouts. The settings below control editor behavior.")
                }

                Button {
                    Layout.alignment: Qt.AlignLeft
                    text: i18n("Launch Layout Editor")
                    icon.name: "document-edit"
                    highlighted: true
                    onClicked: settingsController.launchEditor()
                }

            }

        }

        // =================================================================
        // KEYBOARD SHORTCUTS CARD
        // =================================================================
        Kirigami.Card {
            Layout.fillWidth: true

            header: Kirigami.Heading {
                level: 3
                text: i18n("Keyboard Shortcuts")
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: i18n("These shortcuts are active inside the layout editor.")
                    opacity: 0.7
                }

                Kirigami.Separator {
                    Layout.fillWidth: true
                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                        text: i18n("Duplicate zone:")
                    }

                    TextField {
                        Layout.fillWidth: true
                        text: settingsController.editorDuplicateShortcut
                        onEditingFinished: settingsController.editorDuplicateShortcut = text
                        placeholderText: "Ctrl+D"
                    }

                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                        text: i18n("Split horizontally:")
                    }

                    TextField {
                        Layout.fillWidth: true
                        text: settingsController.editorSplitHorizontalShortcut
                        onEditingFinished: settingsController.editorSplitHorizontalShortcut = text
                        placeholderText: "Ctrl+Shift+H"
                    }

                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                        text: i18n("Split vertically:")
                    }

                    TextField {
                        Layout.fillWidth: true
                        text: settingsController.editorSplitVerticalShortcut
                        onEditingFinished: settingsController.editorSplitVerticalShortcut = text
                        placeholderText: "Ctrl+Alt+V"
                    }

                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                        text: i18n("Fill space:")
                    }

                    TextField {
                        Layout.fillWidth: true
                        text: settingsController.editorFillShortcut
                        onEditingFinished: settingsController.editorFillShortcut = text
                        placeholderText: "Ctrl+Shift+F"
                    }

                }

                Button {
                    text: i18n("Reset to defaults")
                    icon.name: "edit-reset"
                    onClicked: settingsController.resetEditorDefaults()
                    ToolTip.visible: hovered
                    ToolTip.text: i18n("Reset all editor shortcuts to their default values")
                }

            }

        }

        // =================================================================
        // SNAPPING CARD
        // =================================================================
        Kirigami.Card {
            Layout.fillWidth: true

            header: Kirigami.Heading {
                level: 3
                text: i18n("Snapping")
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                CheckBox {
                    text: i18n("Enable grid snapping")
                    checked: settingsController.editorGridSnappingEnabled
                    onToggled: settingsController.editorGridSnappingEnabled = checked
                    ToolTip.visible: hovered
                    ToolTip.text: i18n("Snap zones to a grid while dragging or resizing")
                }

                CheckBox {
                    text: i18n("Enable edge snapping")
                    checked: settingsController.editorEdgeSnappingEnabled
                    onToggled: settingsController.editorEdgeSnappingEnabled = checked
                    ToolTip.visible: hovered
                    ToolTip.text: i18n("Snap zones to edges of other zones while dragging or resizing")
                }

                Kirigami.Separator {
                    Layout.fillWidth: true
                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Grid interval X:")
                    }

                    Slider {
                        id: snapIntervalXSlider

                        Layout.preferredWidth: root.sliderPreferredWidth
                        from: 0.01
                        to: 0.5
                        stepSize: 0.01
                        value: settingsController.editorSnapIntervalX
                        onMoved: settingsController.editorSnapIntervalX = value
                    }

                    Label {
                        text: Math.round(snapIntervalXSlider.value * 100) + "%"
                        Layout.preferredWidth: root.sliderValueLabelWidth
                    }

                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Grid interval Y:")
                    }

                    Slider {
                        id: snapIntervalYSlider

                        Layout.preferredWidth: root.sliderPreferredWidth
                        from: 0.01
                        to: 0.5
                        stepSize: 0.01
                        value: settingsController.editorSnapIntervalY
                        onMoved: settingsController.editorSnapIntervalY = value
                    }

                    Label {
                        text: Math.round(snapIntervalYSlider.value * 100) + "%"
                        Layout.preferredWidth: root.sliderValueLabelWidth
                    }

                }

            }

        }

    }

    // Keep inputs in sync with settingsController properties
    Connections {
        function onEditorSnapIntervalXChanged() {
            if (!snapIntervalXSlider.pressed)
                snapIntervalXSlider.value = settingsController.editorSnapIntervalX;

        }

        function onEditorSnapIntervalYChanged() {
            if (!snapIntervalYSlider.pressed)
                snapIntervalYSlider.value = settingsController.editorSnapIntervalY;

        }

        target: settingsController
    }

}

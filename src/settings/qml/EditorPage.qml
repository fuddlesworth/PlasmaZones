// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    contentHeight: content.implicitHeight

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

        // --- Keyboard Shortcuts ---
        Kirigami.Card {
            Layout.fillWidth: true

            header: Kirigami.Heading {
                text: i18n("Keyboard Shortcuts")
                level: 2
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

            }

        }

        // --- Grid Snapping ---
        Kirigami.Card {
            Layout.fillWidth: true

            header: Kirigami.Heading {
                text: i18n("Snapping")
                level: 2
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: i18n("Snapping controls how zones align while dragging or resizing in the editor.")
                    opacity: 0.7
                }

                Kirigami.Separator {
                    Layout.fillWidth: true
                }

                CheckBox {
                    text: i18n("Enable grid snapping")
                    checked: settingsController.editorGridSnappingEnabled
                    onToggled: settingsController.editorGridSnappingEnabled = checked
                }

                CheckBox {
                    text: i18n("Enable edge snapping")
                    checked: settingsController.editorEdgeSnappingEnabled
                    onToggled: settingsController.editorEdgeSnappingEnabled = checked
                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                        text: i18n("Grid interval X:")
                    }

                    SpinBox {
                        id: snapXSpinBox

                        from: 1
                        to: 100
                        value: Math.round(settingsController.editorSnapIntervalX * 100)
                        onValueModified: settingsController.editorSnapIntervalX = value / 100
                        textFromValue: function(value, locale) {
                            return value + "%";
                        }
                        valueFromText: function(text, locale) {
                            return parseInt(text);
                        }
                    }

                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                        text: i18n("Grid interval Y:")
                    }

                    SpinBox {
                        id: snapYSpinBox

                        from: 1
                        to: 100
                        value: Math.round(settingsController.editorSnapIntervalY * 100)
                        onValueModified: settingsController.editorSnapIntervalY = value / 100
                        textFromValue: function(value, locale) {
                            return value + "%";
                        }
                        valueFromText: function(text, locale) {
                            return parseInt(text);
                        }
                    }

                }

            }

        }

        // --- Reset ---
        Kirigami.Card {
            Layout.fillWidth: true

            contentItem: RowLayout {
                spacing: Kirigami.Units.smallSpacing

                Item {
                    Layout.fillWidth: true
                }

                Button {
                    text: i18n("Reset Editor Settings to Defaults")
                    icon.name: "edit-undo"
                    onClicked: settingsController.resetEditorDefaults()
                }

            }

        }

    }

}

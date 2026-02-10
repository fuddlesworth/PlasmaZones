// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.Dialog {
    id: dialog
    title: i18n("Choose Label Font")
    standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
    preferredWidth: Math.min(Kirigami.Units.gridUnit * 32, parent.width * 0.85)
    preferredHeight: Math.min(Kirigami.Units.gridUnit * 30, parent.height * 0.85)

    // KCM reference for QFontDatabase helpers
    required property var kcm

    // Output properties (committed on accept)
    property string selectedFamily: ""
    property int selectedWeight: Font.Bold
    property bool selectedItalic: false
    property bool selectedUnderline: false
    property bool selectedStrikeout: false

    // Internal working copies
    property string workingFamily: ""
    property string workingStyle: ""
    property int workingWeight: Font.Bold
    property bool workingItalic: false
    property bool workingUnderline: false
    property bool workingStrikeout: false

    property var allFontFamilies: []
    property string searchText: ""
    property var availableStyles: []
    property bool wasDefault: false
    property string systemFontFamily: ""

    function open() {
        workingWeight = selectedWeight
        workingItalic = selectedItalic
        workingUnderline = selectedUnderline
        workingStrikeout = selectedStrikeout

        if (allFontFamilies.length === 0) {
            allFontFamilies = Qt.fontFamilies()
        }

        // Resolve system default font for display when no family is set
        systemFontFamily = Qt.application.font.family
        wasDefault = (selectedFamily === "")
        workingFamily = wasDefault ? systemFontFamily : selectedFamily

        searchText = ""
        updateStyles()
        visible = true

        Qt.callLater(scrollToSelection)
    }

    function updateStyles() {
        if (workingFamily === "") {
            availableStyles = []
            workingStyle = ""
            return
        }
        availableStyles = kcm.fontStylesForFamily(workingFamily)
        // Find the style matching current weight/italic
        workingStyle = findMatchingStyle()
    }

    function findMatchingStyle() {
        // Try to find a style that matches current weight + italic
        for (var i = 0; i < availableStyles.length; i++) {
            var sw = kcm.fontStyleWeight(workingFamily, availableStyles[i])
            var si = kcm.fontStyleItalic(workingFamily, availableStyles[i])
            if (sw === workingWeight && si === workingItalic) {
                return availableStyles[i]
            }
        }
        // Fall back to closest weight match
        var bestIdx = 0
        var bestDist = 9999
        for (var j = 0; j < availableStyles.length; j++) {
            var w = kcm.fontStyleWeight(workingFamily, availableStyles[j])
            var it = kcm.fontStyleItalic(workingFamily, availableStyles[j])
            var dist = Math.abs(w - workingWeight) + (it !== workingItalic ? 500 : 0)
            if (dist < bestDist) {
                bestDist = dist
                bestIdx = j
            }
        }
        if (availableStyles.length > 0) {
            workingWeight = kcm.fontStyleWeight(workingFamily, availableStyles[bestIdx])
            workingItalic = kcm.fontStyleItalic(workingFamily, availableStyles[bestIdx])
            return availableStyles[bestIdx]
        }
        return ""
    }

    function scrollToSelection() {
        if (workingFamily !== "") {
            var families = filteredFamilies()
            for (var i = 0; i < families.length; i++) {
                if (families[i] === workingFamily) {
                    familyList.positionViewAtIndex(i, ListView.Center)
                    break
                }
            }
        }
        if (workingStyle !== "") {
            for (var j = 0; j < availableStyles.length; j++) {
                if (availableStyles[j] === workingStyle) {
                    styleList.positionViewAtIndex(j, ListView.Center)
                    break
                }
            }
        }
    }

    function filteredFamilies() {
        if (searchText === "") return allFontFamilies
        var lower = searchText.toLowerCase()
        return allFontFamilies.filter(function(f) {
            return f.toLowerCase().indexOf(lower) !== -1
        })
    }

    onAccepted: {
        // If user didn't change from system default, keep empty (= follow system)
        selectedFamily = (wasDefault && workingFamily === systemFontFamily) ? "" : workingFamily
        selectedWeight = workingWeight
        selectedItalic = workingItalic
        selectedUnderline = workingUnderline
        selectedStrikeout = workingStrikeout
    }

    ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        // Search
        TextField {
            id: searchField
            Layout.fillWidth: true
            placeholderText: i18n("Search fonts...")
            text: dialog.searchText
            onTextChanged: dialog.searchText = text
        }

        // Two-column: Family | Style
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Kirigami.Units.smallSpacing

            // Family column
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 3
                spacing: 0

                Label {
                    text: i18n("Family")
                    font.bold: true
                }

                ListView {
                    id: familyList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: Kirigami.Units.gridUnit * 12
                    clip: true
                    model: dialog.filteredFamilies()

                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                    }

                    delegate: ItemDelegate {
                        width: familyList.width - (familyList.ScrollBar.vertical.visible ? familyList.ScrollBar.vertical.width : 0)
                        text: modelData
                        font.family: modelData
                        highlighted: modelData === dialog.workingFamily
                        onClicked: {
                            dialog.workingFamily = modelData
                            dialog.wasDefault = false
                            dialog.updateStyles()
                        }
                    }
                }
            }

            Kirigami.Separator {
                Layout.fillHeight: true
            }

            // Style column
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 2
                spacing: 0

                Label {
                    text: i18n("Style")
                    font.bold: true
                }

                ListView {
                    id: styleList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: Kirigami.Units.gridUnit * 12
                    clip: true
                    model: dialog.availableStyles

                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                    }

                    delegate: ItemDelegate {
                        width: styleList.width - (styleList.ScrollBar.vertical.visible ? styleList.ScrollBar.vertical.width : 0)
                        text: modelData
                        highlighted: modelData === dialog.workingStyle
                        onClicked: {
                            dialog.workingStyle = modelData
                            dialog.workingWeight = kcm.fontStyleWeight(dialog.workingFamily, modelData)
                            dialog.workingItalic = kcm.fontStyleItalic(dialog.workingFamily, modelData)
                        }
                    }
                }
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        // Effects
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing

            Label {
                text: i18n("Effects:")
                Layout.alignment: Qt.AlignVCenter
            }

            CheckBox {
                text: i18n("Underline")
                checked: dialog.workingUnderline
                onToggled: dialog.workingUnderline = checked
            }

            CheckBox {
                text: i18n("Strikeout")
                checked: dialog.workingStrikeout
                onToggled: dialog.workingStrikeout = checked
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        // Preview
        Label {
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 3
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: i18n("AaBbCc 123")
            font.family: dialog.workingFamily
            font.weight: dialog.workingWeight
            font.italic: dialog.workingItalic
            font.underline: dialog.workingUnderline
            font.strikeout: dialog.workingStrikeout
            font.pixelSize: Kirigami.Units.gridUnit * 2
        }
    }
}

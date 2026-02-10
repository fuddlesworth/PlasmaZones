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
    property string _family: ""
    property string _style: ""
    property int _weight: Font.Bold
    property bool _italic: false
    property bool _underline: false
    property bool _strikeout: false

    property var _allFamilies: []
    property string _searchText: ""
    property var _styles: []
    property bool _wasDefault: false
    property string _systemFont: ""

    function open() {
        _weight = selectedWeight
        _italic = selectedItalic
        _underline = selectedUnderline
        _strikeout = selectedStrikeout

        if (_allFamilies.length === 0) {
            _allFamilies = Qt.fontFamilies()
        }

        // Resolve system default font for display when no family is set
        _systemFont = Qt.application.font.family
        _wasDefault = (selectedFamily === "")
        _family = _wasDefault ? _systemFont : selectedFamily

        _searchText = ""
        _updateStyles()
        visible = true

        Qt.callLater(_scrollToSelection)
    }

    function _updateStyles() {
        if (_family === "") {
            _styles = []
            _style = ""
            return
        }
        _styles = kcm.fontStylesForFamily(_family)
        // Find the style matching current weight/italic
        _style = _findMatchingStyle()
    }

    function _findMatchingStyle() {
        // Try to find a style that matches current weight + italic
        for (var i = 0; i < _styles.length; i++) {
            var sw = kcm.fontStyleWeight(_family, _styles[i])
            var si = kcm.fontStyleItalic(_family, _styles[i])
            if (sw === _weight && si === _italic) {
                return _styles[i]
            }
        }
        // Fall back to closest weight match
        var bestIdx = 0
        var bestDist = 9999
        for (var j = 0; j < _styles.length; j++) {
            var w = kcm.fontStyleWeight(_family, _styles[j])
            var it = kcm.fontStyleItalic(_family, _styles[j])
            var dist = Math.abs(w - _weight) + (it !== _italic ? 500 : 0)
            if (dist < bestDist) {
                bestDist = dist
                bestIdx = j
            }
        }
        if (_styles.length > 0) {
            _weight = kcm.fontStyleWeight(_family, _styles[bestIdx])
            _italic = kcm.fontStyleItalic(_family, _styles[bestIdx])
            return _styles[bestIdx]
        }
        return ""
    }

    function _scrollToSelection() {
        if (_family !== "") {
            var families = _filteredFamilies()
            for (var i = 0; i < families.length; i++) {
                if (families[i] === _family) {
                    familyList.positionViewAtIndex(i, ListView.Center)
                    break
                }
            }
        }
        if (_style !== "") {
            for (var j = 0; j < _styles.length; j++) {
                if (_styles[j] === _style) {
                    styleList.positionViewAtIndex(j, ListView.Center)
                    break
                }
            }
        }
    }

    function _filteredFamilies() {
        if (_searchText === "") return _allFamilies
        var lower = _searchText.toLowerCase()
        return _allFamilies.filter(function(f) {
            return f.toLowerCase().indexOf(lower) !== -1
        })
    }

    onAccepted: {
        // If user didn't change from system default, keep empty (= follow system)
        selectedFamily = (_wasDefault && _family === _systemFont) ? "" : _family
        selectedWeight = _weight
        selectedItalic = _italic
        selectedUnderline = _underline
        selectedStrikeout = _strikeout
    }

    ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        // Search
        TextField {
            id: searchField
            Layout.fillWidth: true
            placeholderText: i18n("Search fonts...")
            text: dialog._searchText
            onTextChanged: dialog._searchText = text
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
                    model: dialog._filteredFamilies()

                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                    }

                    delegate: ItemDelegate {
                        width: familyList.width - (familyList.ScrollBar.vertical.visible ? familyList.ScrollBar.vertical.width : 0)
                        text: modelData
                        font.family: modelData
                        highlighted: modelData === dialog._family
                        onClicked: {
                            dialog._family = modelData
                            dialog._wasDefault = false
                            dialog._updateStyles()
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
                    model: dialog._styles

                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                    }

                    delegate: ItemDelegate {
                        width: styleList.width - (styleList.ScrollBar.vertical.visible ? styleList.ScrollBar.vertical.width : 0)
                        text: modelData
                        highlighted: modelData === dialog._style
                        onClicked: {
                            dialog._style = modelData
                            dialog._weight = kcm.fontStyleWeight(dialog._family, modelData)
                            dialog._italic = kcm.fontStyleItalic(dialog._family, modelData)
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
                checked: dialog._underline
                onToggled: dialog._underline = checked
            }

            CheckBox {
                text: i18n("Strikeout")
                checked: dialog._strikeout
                onToggled: dialog._strikeout = checked
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
            font.family: dialog._family
            font.weight: dialog._weight
            font.italic: dialog._italic
            font.underline: dialog._underline
            font.strikeout: dialog._strikeout
            font.pixelSize: Kirigami.Units.gridUnit * 2
        }
    }
}

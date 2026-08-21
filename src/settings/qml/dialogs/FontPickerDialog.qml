// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.Dialog {
    id: dialog

    // Settings reference for QFontDatabase helpers
    required property var appSettings
    // Output properties (committed on accept)
    property string selectedFamily: ""
    property int selectedWeight: Font.Bold
    property bool selectedItalic: false
    property bool selectedUnderline: false
    property bool selectedStrikeout: false
    // Internal working copies. `working*` is the RESOLVED face — what the
    // preview draws, what the style list highlights, and what onAccepted
    // commits. `requested*` is what the user actually asked for and is never
    // written by the resolver, so browsing a family that has no matching face
    // cannot downgrade the request for every family visited afterwards.
    property string workingFamily: ""
    property string workingStyle: ""
    property int workingWeight: Font.Bold
    property bool workingItalic: false
    property bool workingUnderline: false
    property bool workingStrikeout: false
    property int requestedWeight: Font.Bold
    property bool requestedItalic: false
    property var allFontFamilies: []
    property string searchText: ""
    property var availableStyles: []
    property bool wasDefault: false
    property string systemFontFamily: ""
    // Preview text size (px), adjustable via slider
    property int previewSize: Kirigami.Units.gridUnit * 2

    function open() {
        workingWeight = selectedWeight;
        workingItalic = selectedItalic;
        workingUnderline = selectedUnderline;
        workingStrikeout = selectedStrikeout;
        // Re-seeded on every open, not just initialised once: without this a
        // request abandoned with Cancel would still be steering the matching
        // the next time the dialog is opened.
        requestedWeight = selectedWeight;
        requestedItalic = selectedItalic;
        if (allFontFamilies.length === 0)
            allFontFamilies = Qt.fontFamilies();

        // Resolve system default font for display when no family is set
        systemFontFamily = Qt.application.font.family;
        wasDefault = (selectedFamily === "");
        workingFamily = wasDefault ? systemFontFamily : selectedFamily;
        searchText = "";
        updateStyles();
        visible = true;
        Qt.callLater(scrollToSelection);
        // Somewhere to type and somewhere to tab from. Without this the dialog
        // opened with no focused control, so a keyboard user had no entry
        // point into it at all.
        Qt.callLater(function () {
            searchField.forceActiveFocus();
        });
    }

    function updateStyles() {
        if (workingFamily === "") {
            availableStyles = [];
            workingStyle = "";
            return;
        }
        availableStyles = dedupStyles(workingFamily, appSettings.fontStylesForFamily(workingFamily));
        // Find the style matching current weight/italic
        workingStyle = findMatchingStyle();
    }

    /// Whether a style name reads as a slanted face. Used only to break ties
    /// between two names that resolve to the SAME (weight, italic) pair, so a
    /// family whose oblique faces report italic()==false keeps the upright
    /// name.
    function styleNameLooksSlanted(name) {
        var lower = name.toLowerCase();
        return lower.indexOf("italic") !== -1 || lower.indexOf("oblique") !== -1;
    }

    /// One entry per distinct (weight, italic) pair. The dialog stores a weight
    /// and an italic flag and never a style name, so two names resolving to the
    /// same pair are the same pick with two spellings, and the second one looks
    /// like a choice that does nothing. Where they collide, keep the name whose
    /// spelling agrees with the italic flag: DejaVu Sans lists "Bold Oblique"
    /// before "Bold" and both come back (700, false), so a plain first-seen
    /// dedup would keep the wrong one and offer no way to ask for Bold.
    function dedupStyles(family, styles) {
        var byKey = {};
        var order = [];
        for (var i = 0; i < styles.length; i++) {
            var name = styles[i];
            var italic = appSettings.fontStyleItalic(family, name);
            var key = appSettings.fontStyleWeight(family, name) + ":" + italic;
            if (byKey[key] === undefined) {
                byKey[key] = name;
                order.push(key);
            } else if (styleNameLooksSlanted(name) === italic && styleNameLooksSlanted(byKey[key]) !== italic) {
                byKey[key] = name;
            }
        }
        var result = [];
        for (var j = 0; j < order.length; j++)
            result.push(byKey[order[j]]);
        return result;
    }

    function findMatchingStyle() {
        // Both loops match against the REQUEST. Matching against the resolved
        // value instead made every family without an exact face narrow the
        // request permanently, so a walk through a few families ended on a
        // weight the user never picked.
        for (var i = 0; i < availableStyles.length; i++) {
            var sw = appSettings.fontStyleWeight(workingFamily, availableStyles[i]);
            var si = appSettings.fontStyleItalic(workingFamily, availableStyles[i]);
            if (sw === requestedWeight && si === requestedItalic) {
                workingWeight = sw;
                workingItalic = si;
                return availableStyles[i];
            }
        }
        // Fall back to closest weight match
        var bestIdx = 0;
        var bestDist = 9999;
        for (var j = 0; j < availableStyles.length; j++) {
            var w = appSettings.fontStyleWeight(workingFamily, availableStyles[j]);
            var it = appSettings.fontStyleItalic(workingFamily, availableStyles[j]);
            var dist = Math.abs(w - requestedWeight) + (it !== requestedItalic ? 500 : 0);
            if (dist < bestDist) {
                bestDist = dist;
                bestIdx = j;
            }
        }
        if (availableStyles.length > 0) {
            workingWeight = appSettings.fontStyleWeight(workingFamily, availableStyles[bestIdx]);
            workingItalic = appSettings.fontStyleItalic(workingFamily, availableStyles[bestIdx]);
            return availableStyles[bestIdx];
        }
        return "";
    }

    function scrollToSelection() {
        if (workingFamily !== "") {
            var families = visibleFamilies;
            for (var i = 0; i < families.length; i++) {
                if (families[i] === workingFamily) {
                    familyList.positionViewAtIndex(i, ListView.Center);
                    break;
                }
            }
        }
        if (workingStyle !== "") {
            for (var j = 0; j < availableStyles.length; j++) {
                if (availableStyles[j] === workingStyle) {
                    styleList.positionViewAtIndex(j, ListView.Center);
                    break;
                }
            }
        }
    }

    /// Commit a family choice. The single path a click and a keyboard Return
    /// both take, so the two cannot drift.
    function selectFamily(family) {
        workingFamily = family;
        // Choosing the system family back re-selects follow-the-system rather
        // than pinning that family by name. Unconditionally clearing this made
        // the choice one-way: nothing inside the dialog could get back to it
        // once any family had been chosen.
        wasDefault = (family === systemFontFamily);
        updateStyles();
    }

    /// Commit a style choice. An explicit style pick is the only thing that
    /// restates the REQUEST, so a later family that does carry this face gets
    /// it back rather than staying on whatever the last fallback resolved to.
    function selectStyle(style) {
        workingStyle = style;
        workingWeight = appSettings.fontStyleWeight(workingFamily, style);
        workingItalic = appSettings.fontStyleItalic(workingFamily, style);
        requestedWeight = workingWeight;
        requestedItalic = workingItalic;
    }

    /// The family list the view actually shows, recomputed only when the
    /// search text or the family set changes.
    ///
    /// A binding straight onto `filteredFamilies()` handed the ListView a NEW
    /// array on every keystroke, which tears down and rebuilds every visible
    /// delegate and resets `currentIndex`. That reset is why keyboard
    /// selection could not be wired to `currentIndex` safely: the index moved
    /// under the user while they typed. Caching it makes the model identity
    /// stable between keystrokes that do not change the result.
    readonly property var visibleFamilies: {
        if (searchText === "")
            return allFontFamilies;

        var lower = searchText.toLowerCase();
        return allFontFamilies.filter(function (f) {
            return f.toLowerCase().indexOf(lower) !== -1;
        });
    }

    title: i18n("Choose Label Font")
    standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
    padding: Kirigami.Units.largeSpacing
    preferredWidth: Math.min(Kirigami.Units.gridUnit * 32, parent.width * 0.85)
    preferredHeight: Math.min(Kirigami.Units.gridUnit * 30, parent.height * 0.85)
    onAccepted: {
        // If user didn't change from system default, keep empty (= follow system)
        selectedFamily = (wasDefault && workingFamily === systemFontFamily) ? "" : workingFamily;
        selectedWeight = workingWeight;
        // The weight commits the RESOLVED face, because the fallback picked a
        // real nearer weight and persisting an unachievable one would only
        // mislead. Italic commits the REQUEST, because Qt synthesizes an
        // oblique for a family that has no italic face, so what the user asked
        // for is what they get. See the Italic checkbox for the full note.
        selectedItalic = requestedItalic;
        selectedUnderline = workingUnderline;
        selectedStrikeout = workingStrikeout;
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
                spacing: Kirigami.Units.smallSpacing

                Label {
                    text: i18n("Family")
                    font.weight: Font.DemiBold
                }

                ListView {
                    id: familyList

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: Kirigami.Units.gridUnit * 12
                    clip: true
                    model: dialog.visibleFamilies
                    keyNavigationEnabled: true
                    // Follows the selection rather than driving it. Writing
                    // the family FROM currentIndex would fire on every model
                    // rebuild — including the ones a keystroke in the search
                    // field causes — and overwrite the user's choice while
                    // they were still typing. So the arrow keys move the
                    // cursor, and only an explicit Return or click commits it.
                    currentIndex: dialog.visibleFamilies.indexOf(dialog.workingFamily)
                    Keys.onReturnPressed: familyList.commitCurrent()
                    Keys.onEnterPressed: familyList.commitCurrent()

                    function commitCurrent() {
                        if (currentIndex < 0 || currentIndex >= dialog.visibleFamilies.length)
                            return;
                        dialog.selectFamily(dialog.visibleFamilies[currentIndex]);
                    }

                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                    }

                    delegate: ItemDelegate {
                        width: familyList.width - (familyList.ScrollBar.vertical.visible ? familyList.ScrollBar.vertical.width : 0)
                        text: modelData
                        font.family: modelData
                        highlighted: modelData === dialog.workingFamily
                        onClicked: dialog.selectFamily(modelData)
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
                spacing: Kirigami.Units.smallSpacing

                Label {
                    text: i18n("Style")
                    font.weight: Font.DemiBold
                }

                ListView {
                    id: styleList

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: Kirigami.Units.gridUnit * 12
                    clip: true
                    model: dialog.availableStyles
                    keyNavigationEnabled: true
                    // Same shape as the family list: the cursor follows the
                    // selection, and only Return or a click commits it.
                    currentIndex: dialog.availableStyles.indexOf(dialog.workingStyle)
                    Keys.onReturnPressed: styleList.commitCurrent()
                    Keys.onEnterPressed: styleList.commitCurrent()

                    function commitCurrent() {
                        if (currentIndex < 0 || currentIndex >= dialog.availableStyles.length)
                            return;
                        dialog.selectStyle(dialog.availableStyles[currentIndex]);
                    }

                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                    }

                    delegate: ItemDelegate {
                        width: styleList.width - (styleList.ScrollBar.vertical.visible ? styleList.ScrollBar.vertical.width : 0)
                        text: modelData
                        highlighted: modelData === dialog.workingStyle
                        onClicked: dialog.selectStyle(modelData)
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
                text: i18n("Italic")
                // Bound to the REQUEST, not the resolved face. On a family
                // whose oblique faces report italic() == false — DejaVu Sans
                // among them — no style row can express italic, so without
                // this control italic was simply unreachable there, and
                // browsing onto such a family silently cleared it.
                //
                // Safe to honour as asked, because Qt synthesizes an oblique
                // when a family has no italic face. That is why italic commits
                // the request while the WEIGHT commits the resolved face: a
                // weight fallback picks a real nearer face, whereas italic is
                // binary and always achievable.
                checked: dialog.requestedItalic
                onToggled: {
                    dialog.requestedItalic = checked;
                    dialog.updateStyles();
                }
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

        // Preview size slider
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing

            Label {
                text: i18n("Size:")
                Layout.alignment: Qt.AlignVCenter
            }

            Slider {
                id: sizeSlider

                Layout.fillWidth: true
                from: Kirigami.Units.gridUnit
                to: Kirigami.Units.gridUnit * 5
                value: dialog.previewSize
                onMoved: dialog.previewSize = value
            }
        }

        // Preview
        Label {
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 3
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignVCenter
            leftPadding: Kirigami.Units.smallSpacing
            text: i18n("AaBbCc 123")
            font.family: dialog.workingFamily
            font.weight: dialog.workingWeight
            // The request rather than the resolved face, matching what
            // onAccepted commits, so the preview cannot show upright text for
            // a font that will be stored italic.
            font.italic: dialog.requestedItalic
            font.underline: dialog.workingUnderline
            font.strikeout: dialog.workingStrikeout
            font.pixelSize: dialog.previewSize
        }
    }
}

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * The cheatsheet's filter field, with a leading search glyph and a trailing
 * "shown of total" counter.
 *
 * This is the ONE piece of the sheet that needs the shell surface to hold
 * keyboard focus. The passive shell is kbd-None for every other slot, and
 * OverlayService flips it to Exclusive for the cheatsheet's lifetime alone
 * (see the anyKeyboardGrabbing derivation in shellhost_bridge.cpp). Two
 * consequences worth knowing when reading this file:
 *
 *  - Escape never arrives here. It is bound as a daemon-side global grab
 *    (daemon/cheatsheet.cpp), and KWin routes global shortcuts before any
 *    surface sees the key, so Escape always closes the sheet outright rather
 *    than first clearing the query. The footer tells the user as much.
 *  - Every other PlasmaZones shortcut stays live while the field has focus,
 *    for the same reason. Typing plain text is unaffected; a chord is not.
 */
Rectangle {
    id: root

    /// Live query text. The host binds this rather than reading the field
    /// directly so the filter has a single source.
    property alias text: field.text
    /// Rows answering the filter, and the total the current mode offers.
    ///
    /// The filter dims rather than removes, so this counter is the only place
    /// the sheet says how well a query landed. It matters most at the extreme:
    /// a query nothing answers leaves a full but uniformly dimmed card, which
    /// on its own could read as a rendering fault rather than as no matches.
    property int shownCount: 0
    property int totalCount: 0
    readonly property bool hasQuery: field.text.trim().length > 0
    readonly property bool noMatches: hasQuery && shownCount === 0
    property string fontFamily: ""
    property real fontSizeScale: 1

    readonly property int rowFontSize: Math.round(Kirigami.Theme.defaultFont.pixelSize * fontSizeScale)
    readonly property string effectiveFamily: fontFamily.length > 0 ? fontFamily : Kirigami.Theme.defaultFont.family

    /// Focus the field. Called by the host once the content is mounted. One
    /// shot is enough: forceActiveFocus is a scene-local claim, so the field
    /// is already the scene's focus item by the time the compositor's
    /// focus-in arrives, and the first keystroke lands here.
    function takeFocus() {
        field.forceActiveFocus();
    }

    implicitHeight: layout.implicitHeight + Kirigami.Units.smallSpacing * 2
    radius: Kirigami.Units.smallSpacing
    color: Qt.alpha(Kirigami.Theme.textColor, 0.06)
    border.width: 1
    border.color: root.noMatches ? Kirigami.Theme.negativeTextColor : (field.activeFocus ? Kirigami.Theme.highlightColor : Qt.alpha(Kirigami.Theme.textColor, 0.15))

    RowLayout {
        id: layout

        anchors.fill: parent
        anchors.leftMargin: Math.round(Kirigami.Units.smallSpacing * 1.5)
        anchors.rightMargin: Math.round(Kirigami.Units.smallSpacing * 1.5)
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Icon {
            // "edit-find" is the name the rest of the tree uses for this, and
            // it is a Breeze action icon rather than a themed alias, so it
            // resolves without a fallback.
            source: "edit-find"
            // Sized off the row type rather than a Units icon constant so the
            // glyph tracks the user's overlay font scale like everything else
            // on the card does.
            implicitWidth: root.rowFontSize
            implicitHeight: root.rowFontSize
            opacity: 0.55
            Accessible.ignored: true
        }

        TextField {
            id: field

            Layout.fillWidth: true
            placeholderText: i18n("Filter shortcuts")
            // The card is the whole UI; a second frame inside it reads as a
            // nested control. The parent Rectangle draws the field's chrome.
            background: null
            padding: 0
            color: Kirigami.Theme.textColor
            font.family: root.effectiveFamily
            font.pixelSize: root.rowFontSize
            // A shortcut sheet is a reference, not a form: nothing here
            // benefits from the shell guessing at completions.
            inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
            Accessible.name: i18n("Filter shortcuts")
            Accessible.role: Accessible.EditableText
            // The counter beside the field is the only surface that reports
            // how a query landed, and it is kept out of the a11y tree so it
            // does not fire on every keystroke. Carrying its text here instead
            // means a screen reader reaches the result on demand rather than
            // being read a running tally.
            Accessible.description: counter.text
        }

        Label {
            id: counter

            // Counter, not a result summary: it is a running state readout
            // that changes on every keystroke, so it stays terse. The
            // no-matches case gets words rather than a bare zero, since it is
            // the one state the dimmed card cannot express by itself.
            text: root.noMatches ? i18n("No matches") : i18nc("matching shortcuts out of the total", "%1 of %2", root.shownCount, root.totalCount)
            color: root.noMatches ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.disabledTextColor
            font.family: root.effectiveFamily
            font.pixelSize: Math.round(root.rowFontSize * 0.85)
            // Announced by the empty state and by each row; a live counter
            // read out on every keystroke would drown both.
            Accessible.ignored: true
        }
    }
}

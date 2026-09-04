// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

/**
 * One category block on the cheatsheet: a heading, the category's bound rows,
 * and a disclosure line standing in for its unbound ones.
 *
 * Groups are never split across columns (see the packer in
 * CheatsheetContent), so a block is always whole and its heading always
 * introduces the rows directly beneath it.
 */
Column {
    id: root

    /// Translated category name.
    required property string name
    /// Catalog rows in this category that have at least one binding.
    required property var assignedRows
    /// Catalog rows in this category with no binding. Collapsed behind a
    /// count line unless `expanded`.
    required property var unassignedRows
    /// Whether the unassigned rows are currently disclosed.
    property bool expanded: false
    /// Catalog id of the row whose tooltip the sheet has latched open.
    property string latchedRowId: ""
    property var queryTerms: []
    /// True while the sheet has a filter typed into it. Splits "everything is
    /// relevant" from "this row happens to match", which look identical from a
    /// row's own point of view but should not render the same.
    property bool queryActive: false
    /// How many of this category's collapsed unassigned rows answer the query.
    /// The disclosure line reports them, so a match hidden behind it is still
    /// announced without the sheet resizing itself to reveal it.
    property int unassignedMatches: 0
    /// The sheet's row-matching predicate, borrowed rather than reimplemented
    /// so the heading, the rows and the counter can never disagree about what
    /// counts as a match.
    property var matcher: null
    property bool scrollerMoving: false
    property bool layoutsAreTemplates: false
    property string fontFamily: ""
    property real fontSizeScale: 1

    signal expandToggled
    signal latchRequested(string rowId)
    signal latchCleared

    readonly property string effectiveFamily: fontFamily.length > 0 ? fontFamily : Kirigami.Theme.defaultFont.family
    readonly property int rowFontSize: Math.round(Kirigami.Theme.defaultFont.pixelSize * fontSizeScale)

    /// True when this row answers the query, or when there is no query. Rows
    /// that do not are dimmed rather than dropped.
    function rowMatched(row) {
        if (!root.queryActive || !root.matcher)
            return true;
        return root.matcher(row);
    }

    /// Whether anything in this category answers the query. Drives the
    /// heading, so a category with nothing to offer recedes as a block
    /// instead of standing at full accent over a wall of dimmed rows.
    readonly property bool hasMatch: {
        if (!root.queryActive)
            return true;
        if (root.unassignedMatches > 0)
            return true;
        for (let i = 0; i < root.assignedRows.length; ++i) {
            if (root.rowMatched(root.assignedRows[i]))
                return true;
        }
        return false;
    }

    spacing: Kirigami.Units.smallSpacing

    // Heading — a rule running to the column edge, so a two-row category
    // reads as a block rather than dissolving into its neighbours. The
    // previous heading was a 0.7-opacity Label with nothing but vertical gap
    // separating one group from the next, which left short groups invisible.
    Item {
        width: root.width
        height: headingLabel.implicitHeight

        Label {
            id: headingLabel

            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: root.name
            // Capitalisation as a FONT property, not text.toUpperCase(): the
            // string stays intact for accessibility and for locales whose
            // case mapping is not a round trip.
            font.capitalization: Font.AllUppercase
            font.family: root.effectiveFamily
            font.pixelSize: Math.round(Kirigami.Theme.defaultFont.pixelSize * 0.85 * root.fontSizeScale)
            font.bold: true
            // Uppercase set solid needs the tracking back; without it the
            // caps crowd at small sizes.
            font.letterSpacing: 0.8
            color: Kirigami.Theme.highlightColor
            opacity: root.hasMatch ? 1 : 0.3
            Accessible.role: Accessible.Heading
            Accessible.name: root.name
        }

        Rectangle {
            anchors.left: headingLabel.right
            anchors.leftMargin: Kirigami.Units.smallSpacing
            anchors.right: parent.right
            anchors.verticalCenter: headingLabel.verticalCenter
            height: 1
            color: Qt.alpha(Kirigami.Theme.textColor, 0.15)
            opacity: root.hasMatch ? 1 : 0.3
            // Purely a rule; the heading label carries the announcement.
            Accessible.ignored: true
            // A very long translated category can consume the whole column
            // width, at which point the rule has nowhere to go.
            visible: width > 0
        }
    }

    Repeater {
        model: root.assignedRows

        delegate: CheatsheetRow {
            // `modelData` is CheatsheetRow's own required property; the
            // Repeater fills it. Re-declaring it here would shadow the row's
            // and self-assign.
            width: root.width
            queryTerms: root.queryTerms
            matched: root.rowMatched(modelData)
            latched: root.latchedRowId.length > 0 && root.latchedRowId === modelData.id
            scrollerMoving: root.scrollerMoving
            layoutsAreTemplates: root.layoutsAreTemplates
            fontFamily: root.fontFamily
            fontSizeScale: root.fontSizeScale
            onLatchRequested: root.latchRequested(modelData.id)
            onLatchCleared: root.latchCleared()
        }
    }

    // Unassigned disclosure. Six italic "Unassigned" rows at the same weight
    // as bound ones padded the Scrolling column while telling the reader
    // nothing, but hiding them outright would lose the answer to "is there a
    // key for this?" — so they collapse to one line and open on click.
    Label {
        id: disclosure

        visible: root.unassignedRows.length > 0
        width: root.width
        wrapMode: Text.Wrap
        // With a query up, the line reports its own hidden matches instead of
        // its total, so the one thing worth clicking says so.
        text: root.expanded ? i18n("Hide unassigned actions") : (root.queryActive && root.unassignedMatches > 0 ? i18np("%n unassigned action matches", "%n unassigned actions match", root.unassignedMatches) : i18np("%n unassigned action", "%n unassigned actions", root.unassignedRows.length))
        color: root.queryActive && root.unassignedMatches > 0 ? Kirigami.Theme.highlightColor : (disclosureArea.containsMouse ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor)
        // Dimmed along with the rest of a category that has nothing to offer
        // the query, but never below the rows it stands in for.
        opacity: !root.queryActive || root.unassignedMatches > 0 ? 1 : 0.35
        font.family: root.effectiveFamily
        font.pixelSize: Math.round(root.rowFontSize * 0.9)
        Accessible.role: Accessible.Button
        Accessible.name: disclosure.text
        Accessible.onPressAction: root.expandToggled()

        MouseArea {
            id: disclosureArea

            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.expandToggled()
        }
    }

    Repeater {
        model: root.expanded ? root.unassignedRows : []

        delegate: CheatsheetRow {
            // `modelData` is CheatsheetRow's own required property; the
            // Repeater fills it. Re-declaring it here would shadow the row's
            // and self-assign.
            width: root.width
            queryTerms: root.queryTerms
            matched: root.rowMatched(modelData)
            latched: root.latchedRowId.length > 0 && root.latchedRowId === modelData.id
            scrollerMoving: root.scrollerMoving
            layoutsAreTemplates: root.layoutsAreTemplates
            fontFamily: root.fontFamily
            fontSizeScale: root.fontSizeScale
            onLatchRequested: root.latchRequested(modelData.id)
            onLatchCleared: root.latchCleared()
        }
    }
}

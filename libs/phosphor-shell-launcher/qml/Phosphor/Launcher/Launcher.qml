// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Launcher.Launcher, the spotlight-style launcher surface.
//
// A search field, a strip of provider pills, and the ranked results,
// per docs/phosphor-shell-design/mockups/launcher-spotlight.svg. Like the
// other Phase 3/4 surfaces it renders into whatever it is parented to and
// owns no window: the shell opens it as a screen-centred popout, the demo
// puts it in a plain window.
//
// All the data comes from `results`, a LauncherModel the host builds from
// its provider registry. This file knows nothing about providers beyond
// the roles the model exposes.
//
// Keyboard, all from the search field so focus never has to leave it:
//   Up / Down      move the selection
//   Return         primary action on the selection
//   Alt+Return     alternate action, when the row offers one
//   Tab / Shift+Tab cycle the provider filter
//   Escape         dismissed()
// Clicking a row activates it; clicking a pill sets the filter.

// Bound is correct HERE, unlike in shell.qml: the pill and row delegates
// are instantiated by Repeater / ListView in this file's own context, so
// their handlers may reach this file's ids (list, field, root) and the
// pragma makes that explicit to the tooling. shell.qml must not use it
// because its components are built from C++ against a foreign context.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Phosphor.Theme
import Phosphor.Widgets

FocusScope {
    id: root

    // The LauncherModel. Required in practice; typed var so a test or the
    // demo can hand in any object with the same properties and methods.
    required property var results

    // Emitted after a successful activation, so the host closes the
    // surface. A refused activation (unknown row, action not offered,
    // provider failed) emits nothing and the surface stays open.
    signal activated
    // Escape, or whatever else the surface treats as "put me away".
    signal dismissed

    // Anchors, positioners and layouts mirror under a right-to-left locale,
    // but only when this is set; QML does not infer it from the application
    // layout direction. Inherited so every row and the pill strip follow.
    LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true

    // PopoutHost's contract: a content root carries its implicit sizes.
    //
    // Clamped HERE as well as by the host. 640 is the design width, but on
    // an output narrower than that the card would be cut off on both edges
    // at once, which is the one failure a centred surface cannot recover
    // from. `Screen.width` is the output this item is shown on, so the card
    // shrinks to fit a small display and stays 640 everywhere else.
    implicitWidth: Math.min(640, Screen.width - 2 * Tokens.spacing_xl)
    implicitHeight: column.implicitHeight + 2 * Tokens.spacing_l

    // The list's own row metrics, in one place. The height cap below and the
    // section header both derived from these independently, so changing a
    // row's height silently desynced the cap from what it was capping.
    readonly property int _rowHeight: 56
    readonly property int _sectionHeight: 28
    // How many rows the card shows before the list scrolls. Eight is about a
    // screenful without the card dominating a small output.
    readonly property int _visibleRows: 8

    // What the user has typed. Read-only: the field is the only writer,
    // and the model's query follows it. Exposed so a host (or a test) can
    // see the two agree without reaching into the field.
    readonly property alias queryText: field.text

    // Tell the model whether anyone is looking. Providers stay subscribed
    // all session so that opening the launcher is instant, and without this
    // every clipboard copy and every window opening reset the model behind a
    // surface nobody could see.
    Binding {
        target: root.results
        property: "active"
        value: root.visible
        restoreMode: Binding.RestoreNone
    }

    // Reset for a fresh open: empty query, all providers, selection on
    // top, keyboard in the field. The host calls this on open rather than
    // rebuilding the surface, so provider state (the apps scan) survives.
    function reset(): void {
        // The field is the only writer of results.query, so clearing the
        // model without clearing the field leaves the two disagreeing until
        // the next keystroke, which then re-sends the stale prefix.
        field.text = "";
        root.results.query = "";
        root.results.providerFilter = "";
        list.currentIndex = 0;
        field.forceActiveFocus();
    }

    function activateCurrent(alternate: bool): void {
        if (list.currentIndex < 0 || list.currentIndex >= list.count)
            return;
        const row = list.currentIndex;
        // Read BEFORE activating. A repeatable alternate action is
        // destructive by nature: the clipboard's remove drops the row and
        // rebuilds the model synchronously, so asking afterwards would be
        // asking about whatever moved into that index.
        const repeatable = alternate && root.results.alternateIsRepeatable(row);
        if (!root.results.activate(row, alternate))
            return;
        // A repeatable action leaves the surface open: removing one
        // clipboard entry usually means removing the next, and closing would
        // mean reopening and retyping to get there.
        if (repeatable)
            return;
        root.activated();
    }

    Rectangle {
        anchors.fill: parent
        radius: Tokens.radius_xl
        color: Theme.surface_container

        layer.enabled: true
        layer.effect: ElevationShadow {
            level: 3
        }
    }

    ColumnLayout {
        id: column

        anchors.fill: parent
        anchors.margins: Tokens.spacing_l
        spacing: Tokens.spacing_m

        PhosphorTextField {
            id: field

            Layout.fillWidth: true
            placeholderText: qsTr("Search apps, windows and commands")
            focus: true

            onTextChanged: {
                root.results.query = text;
                list.currentIndex = 0;
            }

            // Every key the field does not consume itself. Handled here,
            // not on the ListView, so focus stays in the field and typing
            // keeps working while the selection moves.
            Keys.onUpPressed: event => {
                list.currentIndex = Math.max(0, list.currentIndex - 1);
                event.accepted = true;
            }
            Keys.onDownPressed: event => {
                list.currentIndex = Math.min(list.count - 1, list.currentIndex + 1);
                event.accepted = true;
            }
            Keys.onReturnPressed: event => {
                root.activateCurrent((event.modifiers & Qt.AltModifier) !== 0);
                event.accepted = true;
            }
            Keys.onEnterPressed: event => {
                root.activateCurrent((event.modifiers & Qt.AltModifier) !== 0);
                event.accepted = true;
            }
            Keys.onTabPressed: event => {
                root.results.cycleProviderFilter(1);
                list.currentIndex = 0;
                event.accepted = true;
            }
            Keys.onBacktabPressed: event => {
                root.results.cycleProviderFilter(-1);
                list.currentIndex = 0;
                event.accepted = true;
            }
            Keys.onEscapePressed: event => {
                root.dismissed();
                event.accepted = true;
            }
        }

        // Provider pills. "All" plus one per provider that has rows for
        // the current query; a provider with nothing to show is hidden
        // rather than greyed, so the strip only ever offers live choices.
        Flow {
            id: pills

            Layout.fillWidth: true
            spacing: Tokens.spacing_s
            visible: list.count > 0 || root.results.providerFilter !== ""

            PhosphorPill {
                text: qsTr("All")
                selected: root.results.providerFilter === ""
                onClicked: {
                    root.results.providerFilter = "";
                    list.currentIndex = 0;
                    field.forceActiveFocus();
                }
            }

            Repeater {
                model: root.results.providers

                PhosphorPill {
                    id: pill

                    required property var modelData

                    // The selected pill stays visible even at zero, so a
                    // filter that survives a query change keeps the reason
                    // for the empty list on screen.
                    visible: pill.modelData.count > 0 || pill.selected
                    // The count goes through the locale, so a locale using
                    // non-Latin digits shows its own. JS stringification
                    // always produced Latin ones.
                    text: qsTr("%1 %2", "provider filter pill: provider name, then its result count").arg(pill.modelData.name).arg(Number(pill.modelData.count).toLocaleString(Qt.locale()))
                    selected: root.results.providerFilter === pill.modelData.id
                    onClicked: {
                        root.results.providerFilter = pill.modelData.id;
                        list.currentIndex = 0;
                        field.forceActiveFocus();
                    }
                }
            }
        }

        ListView {
            id: list

            Layout.fillWidth: true
            // Capped at a screenful, derived from the same metrics the rows
            // and section headers use. Four section headers is the worst
            // case, one per provider.
            Layout.preferredHeight: Math.min(contentHeight, root._visibleRows * root._rowHeight + 4 * root._sectionHeight)
            clip: true
            model: root.results
            currentIndex: 0
            keyNavigationEnabled: false
            highlightMoveDuration: Motion.duration_short_2
            highlightResizeDuration: 0
            boundsBehavior: Flickable.StopAtBounds

            // Rows are grouped under their provider; the model orders
            // providers by their best row, so the first header is the
            // most relevant provider.
            section.property: "providerName"
            section.criteria: ViewSection.FullString
            section.delegate: Item {
                id: sectionHeader

                required property string section

                width: ListView.view.width
                height: root._sectionHeight

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Tokens.spacing_s
                    anchors.verticalCenter: parent.verticalCenter
                    // Named, not reached through the implicit parent chain.
                    // This file sets ComponentBehavior: Bound precisely so
                    // scoping is explicit, and a parent-chain lookup is the
                    // one thing that silently is not.
                    text: sectionHeader.section
                    textFormat: Text.PlainText
                    color: Theme.on_surface_variant
                    font.family: Tokens.font_family
                    font.pixelSize: Tokens.font_size_label_s
                    font.weight: Tokens.font_weight_medium
                    font.capitalization: Font.AllUppercase
                }
            }

            delegate: LauncherResultRow {
                id: row

                width: ListView.view.width
                current: ListView.isCurrentItem
                onClicked: {
                    list.currentIndex = row.index;
                    root.activateCurrent(false);
                }
            }

            // The model resets on every query; keep the selection valid
            // and on top so Enter always means "the best hit".
            onCountChanged: {
                if (currentIndex < 0 || currentIndex >= count)
                    currentIndex = 0;
            }
        }

        // Empty state. Without this the card is a bare text field: no pills,
        // no rows, and the hint line hidden, which is exactly the moment the
        // user most needs to be told what to do and how to leave.
        Text {
            Layout.fillWidth: true
            visible: list.count === 0
            text: root.results.query.length > 0 ? qsTr("No results for %1").arg(root.results.query) : qsTr("Type to search")
            textFormat: Text.PlainText
            color: Theme.on_surface_variant
            font.family: Tokens.font_family
            font.pixelSize: Tokens.font_size_body_s
            elide: Text.ElideRight
        }

        // The mockup's hint line. Always present, so the key legend does not
        // disappear in the empty state.
        Text {
            Layout.fillWidth: true
            // The current row's own action label, read through a typed cast
            // so the property is resolvable by the tooling rather than a
            // dynamic lookup on a bare Item.
            readonly property LauncherResultRow currentRow: list.currentItem as LauncherResultRow
            text: qsTr("↑↓ navigate · ↵ %1 · Alt+↵ alternate · Tab cycles providers · Esc closes").arg(currentRow ? currentRow.primaryActionLabel : qsTr("Open"))
            textFormat: Text.PlainText
            color: Theme.on_surface_variant
            font.family: Tokens.font_family
            font.pixelSize: Tokens.font_size_label_s
            elide: Text.ElideRight
        }
    }
}

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

    // PopoutHost's contract: a content root carries its implicit sizes.
    implicitWidth: 640
    implicitHeight: column.implicitHeight + 2 * Tokens.spacing_l

    // Reset for a fresh open: empty query, all providers, selection on
    // top, keyboard in the field. The host calls this on open rather than
    // rebuilding the surface, so provider state (the apps scan) survives.
    function reset(): void {
        root.results.query = "";
        root.results.providerFilter = "";
        list.currentIndex = 0;
        field.forceActiveFocus();
    }

    function activateCurrent(alternate: bool): void {
        if (list.currentIndex < 0 || list.currentIndex >= list.count)
            return;
        if (root.results.activate(list.currentIndex, alternate))
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
            placeholderText: qsTr("Search apps and windows, run commands, do sums")
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

                    visible: pill.modelData.count > 0
                    text: qsTr("%1 %2").arg(pill.modelData.name).arg(pill.modelData.count)
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
            Layout.preferredHeight: Math.min(contentHeight, 8 * 56 + 4 * 28)
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
                required property string section

                width: ListView.view.width
                height: 28

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Tokens.spacing_s
                    anchors.verticalCenter: parent.verticalCenter
                    text: parent.section
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

        // The mockup's hint line. Only when there is something to act on.
        Text {
            Layout.fillWidth: true
            visible: list.count > 0
            // The current row's own action label, read through a typed cast
            // so the property is resolvable by the tooling rather than a
            // dynamic lookup on a bare Item.
            readonly property LauncherResultRow currentRow: list.currentItem as LauncherResultRow
            text: qsTr("↑↓ navigate · ↵ %1 · ⌥↵ alternate · Tab cycles providers · Esc closes").arg(currentRow ? currentRow.primaryActionLabel : qsTr("open"))
            color: Theme.on_surface_variant
            font.family: Tokens.font_family
            font.pixelSize: Tokens.font_size_label_s
            elide: Text.ElideRight
        }
    }
}

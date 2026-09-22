// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Appearance → Overlays → Layouts — zone-overlay shader assignments.
 *
 * Edits the OverlayShaderTree through the overlaysPage bridge: one
 * always-present global-default card (path "") followed by one override
 * card per layout, each of which inherits the global default until its
 * toggle engages an override. Pack browsing and installation live on the
 * Shader Library page; this page is only about which layout draws what.
 *
 * The layout card list is rebuilt from the controller on
 * `shaderProfileChanged`, which also fires when layouts are added,
 * removed, or renamed.
 */
SettingsFlickable {
    id: page

    readonly property var bridge: settingsController.overlaysPage

    // A ListModel rather than a plain array, because assigning an array resets
    // the Repeater wholesale: every delegate is destroyed and recreated,
    // dropping the per-card latch and collapse state. Skipping the assignment
    // when nothing moved is not enough on its own, because the list genuinely
    // does move under the user's hands. Toggling a "Deleted layout" card off
    // writes a clearOverride, which drops that row and rebuilds every OTHER
    // card as a side effect of the row the user just acted on.
    //
    // The sync below touches only the rows that actually differ, so an
    // unrelated card keeps its delegate and its state.
    ListModel {
        id: layoutModel
    }

    function _refreshLayouts() {
        var next = page.bridge ? page.bridge.assignableLayouts() : [];
        // Walk both lists in step. A row whose id still matches is updated in
        // place (a rename), never replaced. An id that has gone takes its row
        // with it; a new id is inserted where it belongs.
        var i = 0;
        while (i < next.length) {
            if (i >= layoutModel.count) {
                layoutModel.append(next[i]);
                i++;
                continue;
            }
            if (layoutModel.get(i).id === next[i].id) {
                var row = layoutModel.get(i);
                if (row.name !== next[i].name || row.missing !== next[i].missing)
                    layoutModel.set(i, next[i]);
                i++;
                continue;
            }
            // Does the current row still exist further down the new list? If
            // not it is gone; if so, something was inserted before it.
            var stillThere = false;
            for (var j = i + 1; j < next.length; j++) {
                if (next[j].id === layoutModel.get(i).id) {
                    stillThere = true;
                    break;
                }
            }
            if (stillThere)
                layoutModel.insert(i, next[i]);
            else
                layoutModel.remove(i);
        }
        while (layoutModel.count > next.length)
            layoutModel.remove(layoutModel.count - 1);
    }

    Component.onCompleted: page._refreshLayouts()

    Connections {
        target: page.bridge
        function onShaderProfileChanged() {
            page._refreshLayouts();
        }
    }

    contentHeight: col.implicitHeight
    clip: true

    ColumnLayout {
        id: col

        width: page.width
        spacing: Kirigami.Units.smallSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.bottomMargin: Kirigami.Units.smallSpacing
            type: Kirigami.MessageType.Information
            text: i18nc("@info banner on the overlay shader assignments page", "The global default applies to every layout. Each layout card can override it. To install more packs, go to the Library's Shaders page.")
        }

        OverlayShaderAssignmentCard {
            Layout.fillWidth: true
            assignmentPath: ""
            // Deliberately uncontexted and lower-case to match the identical
            // string the coverage chip and the browser already use, so the
            // three surfaces share one catalogue entry and its translations.
            cardLabel: i18n("Global default")
            isBaseline: true
        }

        Repeater {
            model: layoutModel

            OverlayShaderAssignmentCard {
                // The row object rather than three required roles: one of the
                // roles is called `id`, which is a reserved attribute name in
                // QML and cannot be declared as a property.
                required property var model

                Layout.fillWidth: true
                assignmentPath: model.id
                // A deleted layout has no name left to show, so two stale
                // overrides would otherwise render as the same label with no
                // way to tell which card clears which. The id's leading group
                // is enough to tell them apart and is what the layout files
                // are named by.
                // The absent-layout wording comes from the controller so this
                // page, the set coverage chip and the browser's usage list all
                // render the same state the same way.
                cardLabel: model.missing ? page.bridge.absentLayoutLabel(model.id) : (model.name.length > 0 ? model.name : i18n("Unnamed Layout"))
            }
        }
    }
}

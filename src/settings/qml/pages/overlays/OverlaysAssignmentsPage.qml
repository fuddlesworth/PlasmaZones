// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Snapping → Shaders — zone-overlay shader assignments.
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

    property var _layouts: []

    /// The leading group of a layout UUID, braces stripped — enough to tell
    /// two stale overrides apart on screen without printing all 36 characters.
    function _shortId(id) {
        var bare = String(id).replace(/[{}]/g, "");
        var dash = bare.indexOf("-");
        return dash > 0 ? bare.substring(0, dash) : bare.substring(0, 8);
    }

    // Reassigning a plain array resets the Repeater wholesale (every card
    // delegate is destroyed and recreated, dropping per-card latch and
    // collapse state), so only publish a new array when the list content
    // actually changed. shaderProfileChanged fires for every tree write,
    // including the card's own parameter edits, where the layout list is
    // identical.
    function _refreshLayouts() {
        var next = page.bridge ? page.bridge.assignableLayouts() : [];
        var cur = page._layouts;
        if (next.length === cur.length) {
            var same = true;
            for (var i = 0; i < next.length; i++) {
                if (next[i].id !== cur[i].id || next[i].name !== cur[i].name || next[i].missing !== cur[i].missing) {
                    same = false;
                    break;
                }
            }
            if (same)
                return;
        }
        page._layouts = next;
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
            text: i18n("The global default applies to every layout. Each layout card can override it. Install more packs from the Shader Library page.")
        }

        OverlayShaderAssignmentCard {
            Layout.fillWidth: true
            assignmentPath: ""
            cardLabel: i18n("Global Default")
            isBaseline: true
        }

        Repeater {
            model: page._layouts

            OverlayShaderAssignmentCard {
                required property var modelData

                Layout.fillWidth: true
                assignmentPath: modelData.id
                // A deleted layout has no name left to show, so two stale
                // overrides would otherwise render as the same label with no
                // way to tell which card clears which. The id's leading group
                // is enough to tell them apart and is what the layout files
                // are named by.
                cardLabel: modelData.missing ? i18n("Deleted layout %1", page._shortId(modelData.id)) : (modelData.name.length > 0 ? modelData.name : i18n("Unnamed layout"))
            }
        }
    }
}

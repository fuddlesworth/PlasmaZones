// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Snapping → Shaders — zone-overlay shader assignments.
 *
 * Edits the OverlayShaderTree through the snappingShadersPage bridge: one
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

    readonly property var bridge: settingsController.snappingShadersPage

    property var _layouts: []

    function _refreshLayouts() {
        page._layouts = page.bridge ? page.bridge.assignableLayouts() : [];
    }

    Component.onCompleted: page._refreshLayouts()

    Connections {
        target: page.bridge
        function onShaderProfileChanged(path) {
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
            visible: true
            text: i18n("The global default applies to every layout. Each layout card can override it. Install more packs from the Shader Library page.")
        }

        SnappingShaderAssignmentCard {
            Layout.fillWidth: true
            assignmentPath: ""
            cardLabel: i18n("Global Default")
            isBaseline: true
        }

        Repeater {
            model: page._layouts

            SnappingShaderAssignmentCard {
                required property var modelData

                Layout.fillWidth: true
                assignmentPath: modelData.id
                cardLabel: modelData.name.length > 0 ? modelData.name : modelData.id
            }
        }
    }
}

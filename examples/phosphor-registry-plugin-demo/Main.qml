// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// phosphor-registry-plugin-demo entry QML. Mirrors the in-process
// demo's bar but adds a Reload button that asks the controller to
// trigger an immediate rescan of the plugin root. Also displays the
// active plugin root so the hot-reload exercise has visible context.

import Phosphor.RegistryPluginDemo
import Phosphor.Theme
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window

    width: 820
    height: 420
    visible: true
    title: qsTr("Phosphor Registry Plugin Demo")
    color: Theme.background

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Tokens.spacing_xl
        spacing: Tokens.spacing_l

        Text {
            text: qsTr("Phosphor.Registry — plugin demo + hot-reload")
            color: Theme.on_surface
            font.pixelSize: Tokens.font_size_display_s
            font.family: Tokens.font_family
            font.weight: Tokens.font_weight_medium
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Two built-in widgets (clock, colorsquare) plus the cpu-meter widget loaded as a separate .so from the plugin root below.")
            color: Theme.on_surface_variant
            font.pixelSize: Tokens.font_size_body_m
            font.family: Tokens.font_family
            wrapMode: Text.WordWrap
        }

        Rectangle {
            id: bar

            Layout.fillWidth: true
            Layout.preferredHeight: Tokens.spacing_xxxl
            color: Theme.surface
            radius: Tokens.radius_m
            border.color: Theme.outline_variant
            border.width: 1

            Row {
                id: barRow

                anchors.fill: parent
                anchors.margins: Tokens.spacing_s
                spacing: Tokens.spacing_m
            }
        }

        RowLayout {
            spacing: Tokens.spacing_m

            Button {
                text: qsTr("Reload plugins")
                onClicked: demoController.reloadPlugins()
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("Plugin root: %1").arg(demoController.pluginRoot)
                color: Theme.on_surface_variant
                font.pixelSize: Tokens.font_size_body_s
                font.family: Tokens.font_family
                elide: Text.ElideLeft
            }
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Registered factories: %1").arg(demoController.factoryIds.join(", "))
            color: Theme.on_surface_variant
            font.pixelSize: Tokens.font_size_body_s
            font.family: Tokens.font_family
        }

        Item {
            Layout.fillHeight: true
        }
    }

    function rebuildBar() {
        for (let i = barRow.children.length - 1; i >= 0; --i) {
            barRow.children[i].destroy();
        }
        const ids = demoController.factoryIds;
        for (let i = 0; i < ids.length; ++i) {
            demoController.createWidgetFor(ids[i], barRow);
        }
    }

    Component.onCompleted: rebuildBar()
    Connections {
        target: demoController
        function onFactoryIdsChanged() {
            rebuildBar();
        }
    }
}

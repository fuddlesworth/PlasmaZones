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

                // Repeater-driven widget instantiation. Each slot is
                // a JS-owned Item delegate; the C++ factory parents
                // a real widget under the slot in Component.onCompleted.
                // When demoController.factoryIds changes (register /
                // unregister / plugin hot-reload), the Repeater
                // destroys the old delegates; QObject's parent-cascade
                // takes the C++-owned widgets with them. This avoids
                // the "destroy() on an indestructible object" error
                // that a manual `child.destroy()` loop produces — JS
                // can't destroy C++-owned QQuickItems directly, but
                // it can destroy its own delegate Items.
                Repeater {
                    model: demoController ? demoController.factoryIds : []

                    delegate: Item {
                        id: slot

                        required property string modelData
                        property Item widget: null

                        implicitWidth: widget ? widget.implicitWidth : 0
                        implicitHeight: widget ? widget.implicitHeight : 0

                        Component.onCompleted: {
                            if (demoController) {
                                slot.widget = demoController.createWidgetFor(slot.modelData, slot);
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            spacing: Tokens.spacing_m

            // Inline themed button. The default QtQuick.Controls Basic
            // Button renders as a flat grey rectangle that doesn't
            // match the rest of the demo's Phosphor.Theme chrome.
            // A real shell would use a shared PhosphorButton.qml;
            // for the demo, inlining keeps the file count down.
            Rectangle {
                id: reloadButton

                implicitWidth: reloadLabel.implicitWidth + Tokens.spacing_xl
                implicitHeight: Tokens.spacing_xxl
                color: reloadMouse.containsPress ? Qt.darker(Theme.primary, 1.15) : reloadMouse.containsMouse ? Qt.lighter(Theme.primary, 1.1) : Theme.primary
                radius: Tokens.radius_s
                border.color: Theme.outline_variant
                border.width: 1

                Text {
                    id: reloadLabel

                    anchors.centerIn: parent
                    text: qsTr("Reload plugins")
                    color: Theme.on_primary
                    font.pixelSize: Tokens.font_size_body_m
                    font.family: Tokens.font_family
                    font.weight: Tokens.font_weight_medium
                }

                MouseArea {
                    id: reloadMouse

                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: demoController.reloadPlugins()
                }
            }

            Text {
                Layout.fillWidth: true
                // demoController is a context property and may not be
                // bound on every QML evaluation path (e.g., if a future
                // refactor moves the binding into an inner component
                // that evaluates before the root context is wired).
                // Guard with `?:` so the binding can never trip a
                // null-deref TypeError.
                text: qsTr("Plugin root: %1").arg(demoController ? demoController.pluginRoot : "")
                color: Theme.on_surface_variant
                font.pixelSize: Tokens.font_size_body_s
                font.family: Tokens.font_family
                elide: Text.ElideLeft
            }
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Registered factories: %1").arg(demoController ? demoController.factoryIds.join(", ") : "")
            color: Theme.on_surface_variant
            font.pixelSize: Tokens.font_size_body_s
            font.family: Tokens.font_family
        }

        Item {
            Layout.fillHeight: true
        }
    }
}

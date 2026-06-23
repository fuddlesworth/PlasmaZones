// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// phosphor-registry-demo entry QML. Window with a single horizontal
// bar at the top. The bar is built by enumerating the registry's
// factoryIds and asking the demo controller to instantiate each
// widget.

import Phosphor.RegistryDemo
import Phosphor.Theme
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window

    width: 720
    height: 360
    visible: true
    title: qsTr("Phosphor Registry Demo")
    color: Theme.background

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Tokens.spacing_xl
        spacing: Tokens.spacing_l

        Text {
            text: qsTr("Phosphor.Registry — in-process demo")
            color: Theme.on_surface
            font.pixelSize: Tokens.font_size_display_s
            font.family: Tokens.font_family
            font.weight: Tokens.font_weight_medium
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Each widget below was instantiated from a factory the demo's main() registered with Registry<IBarWidgetFactory>.")
            color: Theme.on_surface_variant
            font.pixelSize: Tokens.font_size_body_m
            font.family: Tokens.font_family
            wrapMode: Text.WordWrap
        }

        // The "bar". A Row that lays out one widget per registered
        // factory id, driven by a Repeater on demoController.factoryIds.
        // Lets QML manage widget lifetime via delegate destruction:
        // when the model changes, the Repeater destroys old delegates,
        // QObject's parent-cascade takes the C++-owned widgets with
        // them. Avoids the "destroy() on an indestructible object"
        // error a manual `child.destroy()` loop would produce.
        Rectangle {
            id: bar

            Layout.fillWidth: true
            Layout.preferredHeight: Tokens.spacing_xxxl
            color: Theme.surface
            radius: Tokens.radius_m
            border.color: Theme.outline_variant
            border.width: 1

            Row {
                anchors.fill: parent
                anchors.margins: Tokens.spacing_s
                spacing: Tokens.spacing_m

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

        // Status footer. Lists current registered ids so the
        // demo's state is obvious without a debug console. Guarded
        // with `?:` so the binding can never trip a null-deref
        // TypeError even on early evaluation paths.
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

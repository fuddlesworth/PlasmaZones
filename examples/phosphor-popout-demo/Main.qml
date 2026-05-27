// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// phosphor-popout-demo entry QML. Window with five buttons and a
// status bar. The host item below the buttons is where popouts get
// parented; click outside a cooperative popout to dismiss, modals
// block other cooperatives, detached popouts ignore the arbitration
// entirely and stay open across modal cycles.

import Phosphor.Popout
import Phosphor.Theme
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window

    width: 960
    height: 640
    visible: true
    title: qsTr("Phosphor Popout Demo")
    color: Theme.background
    Component.onCompleted: {
        demoController.wire(popoutHost, popoutHostTemplate, _qmlEngine);
    }

    // PopoutHost.qml from Phosphor.Popout, instantiated per-popout by
    // the C++ transport. The component itself is just a template.
    Component {
        id: popoutHostTemplate

        PopoutHost {
        }

    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Tokens.spacing_xl
        spacing: Tokens.spacing_l

        Text {
            text: qsTr("Phosphor.Popout arbitration demo")
            color: Theme.on_surface
            font.pixelSize: Tokens.font_size_display_s
            font.family: Tokens.font_family
            font.weight: Tokens.font_weight_medium
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Cooperative buttons share the default scope, so opening one closes the other. " + "Modal closes every cooperative and rejects new cooperative opens until dismissed. " + "Detached ignores arbitration entirely and stays put.")
            color: Theme.on_surface_variant
            font.pixelSize: Tokens.font_size_body_m
            font.family: Tokens.font_family
            wrapMode: Text.WordWrap
        }

        RowLayout {
            spacing: Tokens.spacing_m

            Button {
                text: qsTr("Cooperative A (Calendar)")
                onClicked: demoController.toggleCooperativeA()
            }

            Button {
                text: qsTr("Cooperative B (Note)")
                onClicked: demoController.toggleCooperativeB()
            }

            Button {
                text: qsTr("Modal (Alert)")
                onClicked: demoController.toggleModal()
            }

            Button {
                text: qsTr("Detached (Pinned)")
                onClicked: demoController.toggleDetached()
            }

            Button {
                text: qsTr("Close all")
                onClicked: demoController.closeAll()
            }

        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Tokens.spacing_xxl
            color: demoController.modalActive ? Theme.error_container : Theme.surface_container
            radius: Tokens.radius_s

            Text {
                anchors.fill: parent
                anchors.margins: Tokens.spacing_s
                verticalAlignment: Text.AlignVCenter
                color: demoController.modalActive ? Theme.on_error : Theme.on_surface_variant
                font.pixelSize: Tokens.font_size_body_s
                font.family: Tokens.font_family
                text: {
                    const ids = demoController.openPopoutIds;
                    if (ids.length === 0)
                        return qsTr("No popouts open.");

                    return qsTr("Open: ") + ids.join(", ") + (demoController.modalActive ? qsTr("  ·  modal active") : "");
                }
            }

        }

        // The popout host area. Everything below the controls is where
        // popouts get parented. Filling with a subtle background so the
        // empty state is visually distinct from the chrome above.
        Rectangle {
            id: popoutHost

            objectName: "popoutHost"
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.surface
            radius: Tokens.radius_m
            border.color: Theme.outline_variant
            border.width: 1

            Text {
                anchors.centerIn: parent
                visible: demoController.openPopoutIds.length === 0
                text: qsTr("Click a button above to open a popout.")
                color: Theme.on_surface_variant
                font.pixelSize: Tokens.font_size_body_l
                font.family: Tokens.font_family
            }

        }

    }

}

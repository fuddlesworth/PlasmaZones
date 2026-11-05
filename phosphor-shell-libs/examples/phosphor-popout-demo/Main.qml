// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// phosphor-popout-demo entry QML. Window with five buttons and a
// status bar. The host item below the buttons is where popouts get
// parented. Click outside a cooperative popout to dismiss it.
// Modals block other cooperatives. Detached popouts ignore the
// arbitration entirely and stay open across modal cycles.

import Phosphor.Popout
import Phosphor.PopoutDemo
import Phosphor.Theme
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window

    // Single source of truth for which popoutIds are open. Each
    // cooperative/detached button's accent binding reads one field
    // off this object, keeping the per-button bindings short and the
    // openPopoutIds-shape detail centralised. The Modal button binds
    // demoController.modalActive directly, so it isn't represented
    // here.
    readonly property var openSet: {
        const ids = demoController.openPopoutIds;
        return {
            "calendar": ids.indexOf("calendar") !== -1,
            "quick-note": ids.indexOf("quick-note") !== -1,
            "pinned-note": ids.indexOf("pinned-note") !== -1
        };
    }

    width: 960
    height: 640
    visible: true
    title: qsTr("Phosphor Popout Demo")
    color: Theme.background
    Component.onCompleted: {
        demoController.wire(popoutHost, popoutHostTemplate);
    }

    // PopoutHost.qml from Phosphor.Popout, instantiated per-popout by
    // the C++ transport. The component itself is just a template.
    Component {
        id: popoutHostTemplate

        PopoutHost {}
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
            // One qsTr() per logical sentence. Concatenating "..." + "..."
            // inside a single qsTr makes only the first literal
            // translatable and leaks the joiner spaces into the
            // translation memory. Keeping them separate lets translators
            // reorder and rephrase each sentence independently.
            text: qsTr("Cooperative buttons share the default scope, so opening one closes the other.") + " " + qsTr("Modal closes every cooperative and rejects new cooperative opens until dismissed.") + " " + qsTr("Detached ignores arbitration entirely and stays put.")
            color: Theme.on_surface_variant
            font.pixelSize: Tokens.font_size_body_m
            font.family: Tokens.font_family
            wrapMode: Text.WordWrap
        }

        RowLayout {
            spacing: Tokens.spacing_m

            PhosphorButton {
                text: qsTr("Cooperative A (Calendar)")
                accented: window.openSet["calendar"]
                accentColor: Theme.primary
                labelColor: Theme.on_primary
                onClicked: demoController.toggleCooperativeA()
            }

            PhosphorButton {
                text: qsTr("Cooperative B (Note)")
                accented: window.openSet["quick-note"]
                accentColor: Theme.primary
                labelColor: Theme.on_primary
                onClicked: demoController.toggleCooperativeB()
            }

            PhosphorButton {
                text: qsTr("Modal (Alert)")
                accented: demoController.modalActive
                accentColor: Theme.error
                labelColor: Theme.on_error
                onClicked: demoController.toggleModal()
            }

            PhosphorButton {
                text: qsTr("Detached (Pinned)")
                accented: window.openSet["pinned-note"]
                accentColor: Theme.tertiary
                labelColor: Theme.on_tertiary
                onClicked: demoController.toggleDetached()
            }

            PhosphorButton {
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

                    const list = ids.join(", ");
                    if (demoController.modalActive)
                        return qsTr("Open: %1. Modal active.").arg(list);

                    return qsTr("Open: %1").arg(list);
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

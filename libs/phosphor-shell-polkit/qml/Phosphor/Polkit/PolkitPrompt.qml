// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Polkit.PolkitPrompt, the authentication card under its band.
//
// A 2 px rose band across the requesting window's top edge (or the
// card's own width at the screen edge when the requester has no window)
// with the 360 px card hanging 8 px under it (A3 §9), the same
// band-and-card geometry as a toast without importing the toast module.
// The band breathes 0.55 to 1.0 at 1.2 s while the request is open. The
// card: abyss glass with a 1 px rose stroke, the AUTHENTICATION eyebrow,
// the request's message, the requester and action id in tabular figures,
// the password field (radius 6, 1 px stroke, white while focused, echo
// dots) and Cancel / Authenticate as text actions.
//
// `agent` is duck-typed: respond(text) and cancel(), the surface of
// PhosphorServicePolkit::PolkitAgent (or the shell's controller in front
// of it). `request` is the AuthRequest: message, actionId, prompt, echo.
// Enter submits, Escape cancels. `errorText` (the agent's PAM error) is
// shown in rose under the field and clears the input; the field's edge
// pulses. The response goes straight to `agent.respond` and is cleared
// from the field at once; this file never keeps, logs or echoes it.
//
//   PolkitPrompt {
//       agent: PolkitRegistry; request: PolkitRegistry.activeRequest
//       requester: PolkitRegistry.requesterName; errorText: PolkitRegistry.lastError
//       bandWidth: window.width; anchored: true
//   }

import QtQuick
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

FocusScope {
    id: prompt

    property var agent: null
    property var request: null
    property string requester: ""
    property string errorText: ""
    // The band spans the requester's window when anchored, else the card.
    property bool anchored: false
    property real bandWidth: 360
    // The password as typed, for a host or a test to drive submit();
    // cleared by submit() and on every error.
    property alias password: field.text

    signal submitted
    signal cancelled

    // The surface pack on the card (A1 §2.4, `shell.phosphor.popout`),
    // set by the composition root.
    property Component decoration: null

    readonly property real cardWidth: 360
    readonly property string message: prompt.request && prompt.request.message !== undefined ? String(prompt.request.message) : ""
    readonly property string actionId: prompt.request && prompt.request.actionId !== undefined ? String(prompt.request.actionId) : ""
    readonly property string fieldPrompt: {
        const p = prompt.request && prompt.request.prompt !== undefined ? String(prompt.request.prompt).trim() : "";
        return p !== "" ? p.replace(/:$/, "") : qsTr("Password");
    }
    readonly property bool echo: !!(prompt.request && prompt.request.echo === true)

    implicitWidth: Math.max(prompt.cardWidth, prompt.anchored ? prompt.bandWidth : prompt.cardWidth)
    implicitHeight: 2 + Tokens.spacing_s + card.height

    Accessible.role: Accessible.Dialog
    Accessible.name: qsTr("Authentication required")

    function submit(): void {
        if (!prompt.agent || typeof prompt.agent.respond !== "function")
            return;
        const text = field.text;
        field.text = "";
        prompt.agent.respond(text);
        prompt.submitted();
    }

    function cancel(): void {
        field.text = "";
        if (prompt.agent && typeof prompt.agent.cancel === "function")
            prompt.agent.cancel();
        prompt.cancelled();
    }

    onErrorTextChanged: {
        if (errorText !== "") {
            field.text = "";
            fieldPulse.restart();
            field.forceActiveFocus();
        }
    }

    Keys.onEscapePressed: cancel()

    // The band draws out from its centre on open and retracts on close
    // (the host animates `bandReveal`); it breathes while open.
    property real bandReveal: 1

    Rectangle {
        id: band

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        width: (prompt.anchored ? prompt.bandWidth : prompt.cardWidth) * Math.max(0, Math.min(1, prompt.bandReveal))
        height: 2
        color: Spectrum.hot

        SequentialAnimation on opacity {
            running: band.visible && !Motion.reducedMotion
            loops: Animation.Infinite
            NumberAnimation {
                to: 0.55
                duration: 600
                easing: Motion.release
            }
            NumberAnimation {
                to: 1
                duration: 600
                easing: Motion.reveal
            }
        }
    }

    DecorationSlot {
        id: decorationSlot

        anchors.fill: parent
        component: prompt.decoration
        contentItem: card
        surfacePath: "shell.phosphor.popout"
    }

    Item {
        id: card

        // The pack's capture item.
        property bool shaderAnchor: true

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 2 + Tokens.spacing_s
        width: prompt.cardWidth
        height: Math.max(140, column.implicitHeight + 2 * Tokens.spacing_l)

        Rectangle {
            anchors.fill: parent
            radius: Tokens.radius_container
            color: Theme.background
            opacity: 0.92
        }
        SpectrumStroke {
            anchors.fill: parent
            radius: Tokens.radius_container
            t: 1
            active: true
            visible: !decorationSlot.active
        }

        ColumnLayout {
            id: column

            anchors.fill: parent
            anchors.margins: Tokens.spacing_l
            spacing: Tokens.spacing_s

            Text {
                text: qsTr("Authentication").toUpperCase()
                color: Theme.on_surface_variant
                font.family: Tokens.font_family_ui
                font.pixelSize: Tokens.font_size_label_m
                font.letterSpacing: Tokens.font_size_label_m * 0.08
                Layout.fillWidth: true
            }

            Text {
                text: prompt.message
                color: Theme.on_surface
                font.family: Tokens.font_family_ui
                font.pixelSize: Tokens.font_size_body_l
                wrapMode: Text.WordWrap
                maximumLineCount: 4
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            TabularText {
                text: prompt.requester !== "" ? prompt.requester + " · " + prompt.actionId : prompt.actionId
                color: Theme.on_surface_variant
                font.pixelSize: Tokens.font_size_label_m
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }

            // The field: radius 6, 1 px stroke, white while focused (focus
            // is white, never a hue), rose while an error is showing.
            Item {
                id: fieldFrame

                Layout.fillWidth: true
                Layout.preferredHeight: 40
                Layout.topMargin: Tokens.spacing_xs

                Rectangle {
                    id: fieldEdge

                    anchors.fill: parent
                    radius: Tokens.radius_edge
                    color: "transparent"
                    border.width: 1
                    border.color: prompt.errorText !== "" ? Spectrum.hot : (field.activeFocus ? Spectrum.focus : Theme.outline)
                    opacity: field.activeFocus || prompt.errorText !== "" ? Tokens.stroke_active : Tokens.stroke_resting

                    Behavior on border.color {
                        ColorAnimation {
                            duration: Motion.duration_enter_content
                            easing: Motion.reveal
                        }
                    }
                }

                // Wrong password: a rose pulse on the field edge (A3 §9c).
                SequentialAnimation {
                    id: fieldPulse

                    NumberAnimation {
                        target: fieldEdge
                        property: "opacity"
                        to: 1.4
                        duration: Motion.duration_tick
                        easing: Motion.tick
                    }
                    NumberAnimation {
                        target: fieldEdge
                        property: "opacity"
                        to: Tokens.stroke_active
                        duration: Motion.duration_release
                        easing: Motion.release
                    }
                }

                TextInput {
                    id: field

                    anchors.fill: parent
                    anchors.leftMargin: Tokens.spacing_m
                    anchors.rightMargin: Tokens.spacing_m
                    verticalAlignment: TextInput.AlignVCenter
                    clip: true
                    focus: true
                    echoMode: prompt.echo ? TextInput.Normal : TextInput.Password
                    passwordCharacter: "•"
                    inputMethodHints: prompt.echo ? Qt.ImhNone : (Qt.ImhHiddenText | Qt.ImhSensitiveData | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText)
                    color: Theme.on_surface
                    selectionColor: Spectrum.hot
                    selectedTextColor: Theme.on_surface
                    font.family: Tokens.font_family_ui
                    font.pixelSize: Tokens.font_size_body_l
                    Accessible.role: Accessible.EditableText
                    Accessible.name: prompt.fieldPrompt
                    onAccepted: prompt.submit()
                }

                Text {
                    anchors.left: field.left
                    anchors.right: field.right
                    anchors.verticalCenter: field.verticalCenter
                    text: prompt.fieldPrompt
                    color: Theme.on_surface_variant
                    font.family: Tokens.font_family_ui
                    font.pixelSize: Tokens.font_size_body_l
                    elide: Text.ElideRight
                    visible: field.text.length === 0
                }
            }

            Text {
                visible: prompt.errorText !== ""
                text: prompt.errorText
                color: Spectrum.hot
                font.family: Tokens.font_family_ui
                font.pixelSize: Tokens.font_size_label_m
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Tokens.spacing_xs
                spacing: Tokens.spacing_xl

                Item {
                    Layout.fillWidth: true
                }

                PolkitAction {
                    text: qsTr("Cancel")
                    onActivated: prompt.cancel()
                }

                PolkitAction {
                    text: qsTr("Authenticate")
                    primary: true
                    onActivated: prompt.submit()
                }
            }
        }
    }
}

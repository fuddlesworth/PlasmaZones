// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.ControlCenter.DetailPanel, the drill-in view for one tile.
//
// A tile that controls something with choices (which network, which audio
// device, which power profile) expands into this panel: a titled surface
// with a back affordance that slides over the tile grid. The panel owns
// the chrome and the dismissal contract only. The content is supplied by
// the tile, so the panel never learns what a network list is.
//
// The grid stays mounted behind the panel rather than being torn down, so
// returning from a detail view is instant and the tiles keep their live
// service bindings while the user is drilled in.

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: root

    // The tile this panel is showing, or "" when closed. Presentation
    // only; the panel does not resolve it to anything.
    property string tileId: ""
    // Title shown in the panel header. The host sets it from the tile.
    property string title: ""
    // Whether the panel is showing. Drives the slide/fade transition.
    property bool open: false

    // The user asked to go back to the grid (back button, Escape, or a
    // click on the scrim).
    signal dismissed

    // Content goes here, so a caller's children land inside the panel body
    // rather than on top of the header.
    default property alias content: body.data

    // Content supplied as a Component instead, which is how the host feeds
    // it: a tile declares its own detail view and ControlCenter hands the
    // Component over when that tile is drilled into. Instantiated only while
    // the panel is open, so a detail view nobody has opened costs nothing.
    property Component contentComponent: null

    Loader {
        parent: body
        anchors.fill: parent
        active: root.open && root.contentComponent !== null
        sourceComponent: root.contentComponent
    }

    visible: opacity > 0
    opacity: root.open ? 1 : 0
    // Slides up from slightly below as it fades in. Small offset: this is
    // a view change inside one surface, not a new surface arriving.
    y: root.open ? 0 : Tokens.spacing_l

    Behavior on opacity {
        NumberAnimation {
            duration: Motion.duration_short_4
            easing: Motion.standard
        }
    }

    Behavior on y {
        NumberAnimation {
            duration: Motion.duration_short_4
            easing: Motion.emphasized
        }
    }

    // Escape returns to the grid, the same as the back button. Only while
    // open, so a closed panel does not eat the key from whatever else
    // would handle it.
    Keys.onEscapePressed: event => {
        if (root.open) {
            root.dismissed();
            event.accepted = true;
        }
    }

    // Swallow presses that land on the panel background so they do not
    // reach the tile grid mounted behind it.
    TapHandler {
        enabled: root.open
    }

    Rectangle {
        anchors.fill: parent
        radius: Tokens.radius_l
        color: Theme.surface_container

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Tokens.spacing_m
            spacing: Tokens.spacing_m

            RowLayout {
                Layout.fillWidth: true
                spacing: Tokens.spacing_s

                Item {
                    id: backButton

                    implicitWidth: 28
                    implicitHeight: 28

                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Back")
                    Accessible.onPressAction: root.dismissed()

                    activeFocusOnTab: root.open
                    Keys.onSpacePressed: event => backButton._activateFromKey(event)
                    Keys.onReturnPressed: event => backButton._activateFromKey(event)
                    Keys.onEnterPressed: event => backButton._activateFromKey(event)

                    function _activateFromKey(event: var): void {
                        if (event.isAutoRepeat)
                            return;
                        backRipple.start(width / 2, height / 2);
                        root.dismissed();
                    }

                    HoverHandler {
                        cursorShape: Qt.PointingHandCursor
                    }

                    Kirigami.Icon {
                        anchors.centerIn: parent
                        width: 16
                        height: 16
                        source: "go-previous-symbolic"
                        color: Theme.on_surface
                    }

                    PhosphorRipple {
                        id: backRipple

                        anchors.fill: parent
                        radius: width / 2
                        rippleColor: Theme.on_surface
                        focused: backButton.activeFocus
                        onTapped: root.dismissed()
                    }
                }

                Text {
                    text: root.title
                    color: Theme.on_surface
                    font.family: Tokens.font_family
                    font.pixelSize: Tokens.font_size_title_s
                    font.weight: Tokens.font_weight_medium
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }

            Item {
                id: body

                Layout.fillWidth: true
                Layout.fillHeight: true
            }
        }
    }
}

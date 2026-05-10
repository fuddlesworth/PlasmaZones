// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// Single continuous top panel: one layer-shell surface spans the full
// screen edge, three child zones anchor to left / center / right.

import Phosphor.Shell 1.0
import QtQuick
import QtQuick.Window

// Trade-off vs the three-panel layout: one exclusive zone, one shader,
// one wl_surface — cheaper for the compositor. Anchors don't reserve
// space, so a very long center zone CAN overlap left/right; pick this
// pattern when "absolutely centered clock" matters more than overlap
// avoidance.
PanelWindow {
    // contentArea

    id: root

    // Anchors exposed for popup positioning.
    property alias menuAnchor: menuButton
    property alias calendarAnchor: clockLabel
    required property var shellState
    required property string clockText
    required property string cpuPercent
    required property string memPercent
    required property string batteryPercent
    required property bool batteryVisible
    // Screen height in PHYSICAL pixels — gradient.frag needs this to
    // map the panel's UV (which spans the surface height) to the
    // wallpaper's UV (which spans the screen's height). iResolution
    // in the shader is already physical-pixel scaled by DPR, so we
    // multiply here too.
    readonly property real screenHeightPx: Screen.height * Screen.devicePixelRatio
    // Shadow height in physical pixels — same DPR adjustment as
    // screenHeightPx so the shader compares apples to apples against
    // iResolution.y.
    readonly property real shadowSizePx: root.shadowSize * Screen.devicePixelRatio

    edge: PanelWindow.Top
    thickness: 38
    // Extra surface strip beneath the visible panel that the shader
    // renders a drop-shadow into. exclusiveZone reservation stays at
    // `thickness` so other windows tile right under the panel and the
    // shadow visually darkens their top edge.
    shadowSize: 14
    alignment: PanelWindow.Fill
    exclusiveZoneEnabled: true

    ShaderBackground {
        // Canonical slot-keyed names — `setShaderParams` only honours
        // `customParams<N>_<x|y|z|w>` and `customColor<N>` keys.
        // Friendly names like "speed" would be silently dropped.
        // 0.55 leaves enough wallpaper visible to read the
        // mauve→sky gradient as a tint rather than an opaque wash.
        // Subtle crystalline grain. Higher reads as static noise;
        // lower and the panel looks plastic against the wallpaper.
        // No corner radius for a continuous screen-spanning panel.
        // Voronoi cells per panel width.
        // Blur radius in wallpaper pixels — 8 gives a soft but
        // recognisable backdrop on a 1080p+ wallpaper.
        // Bind the desktop wallpaper as a sampler at SRB slot 11 so the
        // shader can read it via `uniform sampler2D uWallpaper`. The
        // service decodes the image off the GUI thread; while the load
        // is in flight, `image` is null and the shader's textureSize-
        // based check falls back to the gradient-only path.
        // shadowSize must match the physical-pixel height of the
        // extra surface strip below the visible panel (set via
        // PanelWindow.shadowSize above). A mismatch would draw the
        // shadow band into the panel zone or leave the strip
        // un-drawn (transparent).
        // 0.45 looks like a real drop-shadow against the
        // Catppuccin wallpaper palette without obscuring the
        // window content underneath.

        anchors.fill: parent
        playing: root.visible
        shaderSource: Qt.resolvedUrl("shaders/gradient.frag")
        // useWallpaper MUST stay true regardless of whether a real
        // wallpaper is loaded yet: gradient.frag declares binding 11
        // unconditionally, and toggling useWallpaper drops that slot
        // from the SRB. A pipeline whose shader uses a binding the SRB
        // doesn't bind silently fails to draw — which is what makes
        // the whole panel invisible during the cold-start window
        // before the async wallpaper load completes. Keeping it true
        // means the renderer's 1×1 transparent fallback fills the
        // slot, and the shader's `textureSize > 1` check selects the
        // correct visual path data-driven from the texture size.
        useWallpaper: true
        wallpaperTexture: PhosphorShell.wallpaper.image
        //   customParams1: speed / baseAngle / tintOpacity / frostAmount
        //   customParams2: cornerRadius / frostScale
        //   customParams3: screenHeight / blurRadius
        //   customParams4: shadowSize / shadowOpacity
        shaderParams: {
            "customParams1_x": 1.2,
            "customParams1_y": 0,
            "customParams1_z": 0.55,
            "customParams1_w": 0.08,
            "customParams2_x": 0,
            "customParams2_y": 24,
            "customParams3_x": root.screenHeightPx,
            "customParams3_y": 8,
            "customParams4_x": root.shadowSizePx,
            "customParams4_y": 0.45
        }
        // Catppuccin mocha mauve → macchiato sky.
        customColor1: "#cba6f7"
        customColor2: "#89dceb"
    }

    // Content occupies the VISIBLE panel region only — never the
    // shadow strip below. Zones below anchor against this item's
    // verticalCenter so the menu button / clock / settings line up
    // with the panel midline rather than with the surface midline
    // (which is shifted down by half the shadow size).
    Item {
        id: contentArea

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.thickness

        // ─── Left zone: menu button + workspace dots ─────────────────────
        Row {
            id: leftZone

            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 12
            spacing: 12

            Rectangle {
                id: menuButton

                width: 30
                height: 30
                anchors.verticalCenter: parent.verticalCenter
                radius: 8
                color: menuArea.containsMouse ? "#45475a" : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: root.shellState.menuOpen ? "✕" : "☰"
                    color: root.shellState.menuOpen ? "#f38ba8" : "#1e1e2e"
                    font.pixelSize: 14
                    font.weight: Font.Bold
                }

                MouseArea {
                    id: menuArea

                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    Accessible.role: Accessible.Button
                    Accessible.name: root.shellState.menuOpen ? "Close menu" : "Open menu"
                    onClicked: root.shellState.menuOpen = !root.shellState.menuOpen
                }

            }

            Row {
                spacing: 6
                anchors.verticalCenter: parent.verticalCenter

                // NOTE: this example uses indices for "workspaces" which is
                // pedagogical only — production shells should bind to
                // compositor-provided workspace IDs (per the project's
                // "Zone IDs, never indices" rule).
                Repeater {
                    model: 5

                    Rectangle {
                        required property int index

                        width: index === root.shellState.activeWorkspace ? 20 : 8
                        height: 8
                        radius: 4
                        color: index === root.shellState.activeWorkspace ? "#1e1e2e" : "#6c7086"

                        MouseArea {
                            anchors.fill: parent
                            Accessible.role: Accessible.Button
                            Accessible.name: "Switch to workspace " + (parent.index + 1)
                            onClicked: root.shellState.activeWorkspace = parent.index
                        }

                        Behavior on width {
                            NumberAnimation {
                                duration: 150
                            }

                        }

                    }

                }

            }

        }

        // ─── Center zone: clock + calendar trigger ───────────────────────
        Text {
            id: clockLabel

            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            // U+2026 ELLIPSIS while the Process is producing its first output.
            text: root.clockText || "…"
            color: "#1e1e2e"
            font.pixelSize: 13
            font.weight: Font.Bold
            leftPadding: 20
            rightPadding: 20

            // MouseArea hosted on Text — clickable region tracks the Text's
            // implicit-size + padding box. Hit testing is geometry-driven,
            // so a Text item is a valid (if unconventional) MouseArea host;
            // anchoring the trigger to clockLabel matches `calendarAnchor`
            // so the popup originates visually from the clock.
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                Accessible.role: Accessible.Button
                Accessible.name: root.shellState.calendarOpen ? "Close calendar" : "Open calendar"
                onClicked: root.shellState.calendarOpen = !root.shellState.calendarOpen
            }

        }

        // ─── Right zone: CPU / MEM / BAT readouts + settings ─────────────
        Row {
            id: rightZone

            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.rightMargin: 12
            spacing: 14

            Row {
                spacing: 4
                anchors.verticalCenter: parent.verticalCenter

                Text {
                    text: "CPU"
                    color: "#1e1e2e"
                    font.pixelSize: 11
                    font.weight: Font.Medium
                }

                Text {
                    text: (root.cpuPercent || "0") + "%"
                    color: "#1e1e2e"
                    font.pixelSize: 11
                }

            }

            Row {
                spacing: 4
                anchors.verticalCenter: parent.verticalCenter

                Text {
                    text: "MEM"
                    color: "#1e1e2e"
                    font.pixelSize: 11
                    font.weight: Font.Medium
                }

                Text {
                    text: (root.memPercent || "0") + "%"
                    color: "#1e1e2e"
                    font.pixelSize: 11
                }

            }

            Row {
                spacing: 4
                anchors.verticalCenter: parent.verticalCenter
                // Both gates: the file must exist AND the read must have
                // produced a value. FileView.exists can flicker true during
                // cold-start before the read completes; without the length
                // check the row would briefly render a bare "%" sign.
                visible: root.batteryVisible && root.batteryPercent.length > 0

                Text {
                    text: "BAT"
                    color: "#1e1e2e"
                    font.pixelSize: 11
                    font.weight: Font.Medium
                }

                Text {
                    text: root.batteryPercent + "%"
                    color: "#1e1e2e"
                    font.pixelSize: 11
                }

            }

            Rectangle {
                width: 26
                height: 26
                anchors.verticalCenter: parent.verticalCenter
                radius: 6
                color: settingsArea.containsMouse ? "#45475a" : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "⚙"
                    color: "#1e1e2e"
                    font.pixelSize: 13
                }

                MouseArea {
                    id: settingsArea

                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    Accessible.role: Accessible.Button
                    Accessible.name: root.shellState.settingsOpen ? "Close settings" : "Open settings"
                    onClicked: root.shellState.settingsOpen = !root.shellState.settingsOpen
                }

            }

        }

    }

}

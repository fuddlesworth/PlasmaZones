// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// Single continuous top panel: one layer-shell surface spans the full
// screen edge, three child zones anchor to left / center / right.

import Phosphor.Services 1.0
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
    property alias mediaAnchor: mprisWidget.popupAnchor
    property alias mediaPlayer: mprisWidget.currentPlayer
    readonly property real panelSurfaceHeight: root.thickness + root.shadowSize
    required property var shellState
    required property string clockText
    required property string cpuPercent
    required property string memPercent
    required property string batteryPercent
    required property bool batteryVisible
    // Surface-to-screen height RATIO (DPR-independent). The shader
    // multiplies the panel's surface UV by this to get the wallpaper-
    // texture UV for the strip of wallpaper the panel sits on top of.
    // A ratio sidesteps having to reconcile `Screen.devicePixelRatio`
    // with the panel's actual rendering DPR — they cancel out here.
    readonly property real panelToScreenH: (root.thickness + root.shadowSize) / Math.max(Screen.height, 1)
    // Shadow strip as fraction of total surface height (also DPR-
    // independent). The shader uses 1 - shadowFraction as the
    // panel/shadow split position in surface-local UV.
    readonly property real shadowFraction: root.shadowSize / Math.max(root.thickness + root.shadowSize, 1)
    // Concave corner carve radius as fraction of total surface height
    // — Quickshell/Noctalia inner-curves at the panel-to-desktop
    // boundary. 0 = sharp 90° corners.
    readonly property real cornerCarveFraction: root.cornerCarveRadius / Math.max(root.thickness + root.shadowSize, 1)

    edge: PanelWindow.Top
    thickness: 38
    // Extra surface strip beneath the visible panel that the shader
    // renders a drop-shadow into. exclusiveZone reservation stays at
    // `thickness` so other windows tile right under the panel and the
    // shadow visually darkens their top edge.
    shadowSize: 14
    // Concave inner curves at the panel's bottom-left / bottom-right
    // corners — wallpaper "flows around" the panel instead of meeting
    // it at a hard 90°. Roughly 1/4..1/3 of thickness looks natural;
    // 12 px on a 38 px panel sits in the middle of that range.
    cornerCarveRadius: 12
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
        // Ratio form is DPR-independent — see the property comment
        // above. Passing physical-pixel screenHeight + shadowSize
        // separately would risk a 2× mismatch if Screen.dpr ever
        // disagreed with the panel's actual rendering DPR (which
        // produced the "shadow strip is half the panel" symptom
        // observed in the earlier rev).
        // 0.5 gives a clearly readable drop shadow without
        // obscuring window content immediately below the panel.

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
        //   customParams3: panelToScreenH / blurRadius
        //   customParams4: shadowFraction / shadowOpacity
        //   customParams5: cornerCarveFraction
        shaderParams: {
            "customParams1_x": 1.2,
            "customParams1_y": 0,
            "customParams1_z": 0.55,
            "customParams1_w": 0.08,
            "customParams2_x": 0,
            "customParams2_y": 24,
            "customParams3_x": root.panelToScreenH,
            "customParams3_y": 8,
            "customParams4_x": root.shadowFraction,
            "customParams4_y": 0.5,
            "customParams5_x": root.cornerCarveFraction,
            "customParams5_y": 0
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
                            hoverEnabled: true
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

        // ─── System-tray (StatusNotifierItem) plumbing ────────────────────
        // One host per panel — owns the DBus watcher + items collection.
        // Lives outside the Row so the Repeater can reference it via
        // anchors-of-anchors without lifetime races on Row re-layout.
        StatusNotifierHost {
            id: trayHost
        }

        StatusNotifierItemModel {
            id: trayModel

            host: trayHost
        }

        // ─── Right zone: tray + CPU / MEM / BAT readouts + settings ──────
        Row {
            id: rightZone

            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.rightMargin: 12
            spacing: 14

            // Tray icons. Each delegate is a 22-px clickable image with
            // hover background + cascade-popup menu on right-click. The
            // model auto-refreshes when items register/unregister/change
            // status — no manual binding needed. The Row's width collapses
            // to 0 when empty, but the OUTER rightZone Row's `spacing: 14`
            // still allocates 14px between this Row and the CPU readout
            // even when the tray is empty. Gate `visible` on the (now
            // properly declared) count Q_PROPERTY so the spacing collapses
            // along with the children.
            Row {
                spacing: 6
                anchors.verticalCenter: parent.verticalCenter
                visible: trayModel.count > 0

                Repeater {
                    model: trayModel

                    delegate: Rectangle {
                        id: trayDelegate

                        required property int index
                        required property string itemId
                        required property string title
                        required property string iconUrl
                        required property string toolTipTitle
                        required property string toolTipBody
                        required property string menuPath
                        required property bool itemIsMenu
                        required property string dbusService

                        width: 26
                        height: 26
                        radius: 6
                        color: trayMouse.containsMouse ? "#45475a" : "transparent"

                        // Fallback glyph — first letter of title in a
                        // muted circle. Renders when iconUrl is empty
                        // (which happens for items that ship an
                        // IconName the resolver couldn't match against
                        // any installed theme, common for apps that
                        // depend on their own bundled icon dir without
                        // setting IconThemePath properly). Keeps the
                        // tray slot visible + clickable instead of
                        // collapsing to 0 px.
                        Rectangle {
                            anchors.centerIn: parent
                            visible: trayDelegate.iconUrl.length === 0
                            width: 18
                            height: 18
                            radius: 9
                            color: "#45475a"

                            Text {
                                anchors.centerIn: parent
                                text: trayDelegate.title.length > 0 ? trayDelegate.title.charAt(0).toUpperCase() : "?"
                                color: "#cdd6f4"
                                font.pixelSize: 10
                                font.weight: Font.Bold
                            }
                        }

                        Image {
                            id: trayIcon

                            anchors.centerIn: parent
                            visible: trayDelegate.iconUrl.length > 0
                            width: 18
                            height: 18
                            // Image.source is a QUrl property — feed
                            // the model's URL-form role, which encodes
                            // a cacheKey so the URL changes whenever
                            // the underlying QImage data updates. The
                            // engine routes the URL back through
                            // image://phosphor-services/ provider.
                            // sourceSize keeps the on-screen size stable
                            // regardless of the icon's intrinsic res.
                            source: trayDelegate.iconUrl
                            sourceSize.width: 36
                            sourceSize.height: 36
                            smooth: true
                            // Some apps emit `Passive` items they never
                            // upgrade — keep them visible but slightly
                            // dimmed so they don't dominate the panel.
                            opacity: 1
                        }

                        MouseArea {
                            // SNI button-mapping, same as KDE Plasma:

                            id: trayMouse

                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                            Accessible.role: Accessible.Button
                            Accessible.name: trayDelegate.toolTipTitle.length > 0 ? trayDelegate.toolTipTitle : trayDelegate.title
                            onClicked: function (mouse) {
                                // Translate the delegate-local click
                                // coords to screen coords so the item's
                                // process can position any popup it
                                // wants to render relative to "where
                                // the user clicked the tray icon".
                                const global = trayDelegate.mapToGlobal(mouse.x, mouse.y);
                                //   LEFT   → Activate() — the app's primary action,
                                //            e.g. "open main window". Exception:
                                //            items that set ItemIsMenu = true have
                                //            no main window (the icon IS the menu),
                                //            so left-click opens the dbusmenu
                                //            instead. KeePassXC and the per-shell
                                //            "system tray menu" widgets are the
                                //            common cases.
                                //   MIDDLE → SecondaryActivate() — typically
                                //            play/pause for media items, "show
                                //            quick action" for others.
                                //   RIGHT  → open dbusmenu when the item
                                //            advertises one, else fall back to
                                //            ContextMenu() so the app can render
                                //            its own.
                                if (mouse.button === Qt.LeftButton) {
                                    if (trayDelegate.itemIsMenu && trayDelegate.menuPath.length > 0)
                                        trayMenu.openFor(trayDelegate);
                                    else
                                        trayModel.activate(trayDelegate.index, global.x, global.y);
                                } else if (mouse.button === Qt.MiddleButton) {
                                    trayModel.secondaryActivate(trayDelegate.index, global.x, global.y);
                                } else if (mouse.button === Qt.RightButton) {
                                    if (trayDelegate.menuPath.length > 0)
                                        trayMenu.openFor(trayDelegate);
                                    else
                                        trayModel.contextMenu(trayDelegate.index, global.x, global.y);
                                }
                            }
                            // Forward scroll events as SNI Scroll() —
                            // volume widgets (PulseAudio applet, PipeWire
                            // tray), brightness controls, etc. use this
                            // for fine-grained adjustment without
                            // opening their menu. Y-axis maps to
                            // "vertical" orientation per spec; X is
                            // rarer but spec-defined for completeness.
                            onWheel: function (wheel) {
                                if (wheel.angleDelta.y !== 0)
                                    trayModel.scroll(trayDelegate.index, wheel.angleDelta.y, "vertical");

                                if (wheel.angleDelta.x !== 0)
                                    trayModel.scroll(trayDelegate.index, wheel.angleDelta.x, "horizontal");
                            }
                        }
                    }
                }
            }

            MprisWidget {
                id: mprisWidget
                anchors.verticalCenter: parent.verticalCenter
                onPopupRequested: root.shellState.mediaOpen = !root.shellState.mediaOpen
            }

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

    // dbusmenu cascade for tray right-clicks. Re-anchored per click
    // via trayMenu.openFor(delegate). Lives at the panel-root level
    // so the popup surface is a sibling of the panel, not a child of
    // any Row (which would re-parent it on layout changes).
    TrayMenuPopup {
        id: trayMenu

        shellState: root.shellState
    }
}

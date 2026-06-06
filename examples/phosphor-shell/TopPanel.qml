// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// Single continuous top panel: one layer-shell surface spans the full
// screen edge, three child zones anchor to left / center / right.

import Phosphor.Service.Sni 1.0
import Phosphor.Shell 1.0
import Phosphor.Theme
import QtQuick

// Trade-off vs the three-panel layout: one exclusive zone, one shader,
// one wl_surface (cheaper for the compositor). Anchors don't reserve
// space, so a very long center zone CAN overlap left/right; pick this
// pattern when "absolutely centered clock" matters more than overlap
// avoidance.
PanelWindow {
    id: root

    // Anchors exposed for popup positioning.
    property alias menuAnchor: menuButton
    property alias calendarAnchor: clockLabel
    property alias mediaAnchor: mprisWidget.popupAnchor
    property alias mediaPlayer: mprisWidget.currentPlayer
    required property var shellState
    required property string clockText
    required property string cpuPercent
    required property string memPercent
    required property string batteryPercent
    required property bool batteryVisible
    // Named metrics. See MprisWidget.qml for the centralisation rationale.
    readonly property int leftMargin: 12
    readonly property int rightMargin: 12
    readonly property int leftZoneSpacing: 12
    readonly property int rightZoneSpacing: 14
    readonly property int workspaceDotSpacing: 6
    readonly property int workspaceDotActiveWidth: 20
    readonly property int workspaceDotSize: 8
    readonly property int workspaceDotRadius: 4
    readonly property int menuButtonSize: 30
    readonly property int settingsSize: 26
    readonly property int trayDelegateSize: 26
    readonly property int trayIconSize: 18
    readonly property int trayIconSourceSize: 36
    readonly property int traySpacing: 6
    readonly property int trayRadius: 6
    readonly property int menuRadius: 8
    readonly property int statRowSpacing: 4
    readonly property int clockPaddingX: 20
    readonly property int statFontSize: 11
    readonly property int menuGlyphSize: 14
    readonly property int clockFontSize: 13
    readonly property int settingsGlyphSize: 13
    readonly property int fallbackGlyphSize: 10
    readonly property real passiveTrayOpacity: 0.55
    readonly property int workspaceWidthAnimMs: 150
    // Shadow strip as fraction of total surface height (also DPR-
    // independent). The shader uses 1 - shadowFraction as the
    // panel/shadow split position in surface-local UV. The Math.max
    // guard handles a transient thickness=0 layout state without
    // dividing by zero; clamping the result to a usable range stops
    // a degenerate thickness from producing "the entire surface is
    // shadow" (which would happen if thickness=0 with shadowSize>0).
    readonly property real shadowFraction: Math.min(0.5, root.shadowSize / Math.max(root.thickness + root.shadowSize, 1))
    // Concave corner carve radius as fraction of total surface height.
    // Quickshell/Noctalia inner-curves at the panel-to-desktop boundary.
    // 0 = sharp 90 degree corners.
    readonly property real cornerCarveFraction: Math.min(0.5, root.cornerCarveRadius / Math.max(root.thickness + root.shadowSize, 1))

    edge: PanelWindow.Top
    thickness: 38
    // Extra surface strip beneath the visible panel that the shader
    // renders a drop-shadow into. exclusiveZone reservation stays at
    // `thickness` so other windows tile right under the panel and the
    // shadow visually darkens their top edge.
    shadowSize: 14
    // Concave inner curves at the panel's bottom-left / bottom-right
    // corners: wallpaper "flows around" the panel instead of meeting
    // it at a hard 90°. Roughly 1/4..1/3 of thickness looks natural;
    // 12 px on a 38 px panel sits in the middle of that range.
    cornerCarveRadius: 12
    alignment: PanelWindow.Fill
    exclusiveZoneEnabled: true

    // Translucent animated gradient: no wallpaper sampling; the
    // compositor blends it over whatever sits behind the panel surface.
    ShaderBackground {
        anchors.fill: parent
        playing: root.visible
        shaderSource: Qt.resolvedUrl("shaders/gradient.frag")
        useWallpaper: false
        //   customParams1: speed / baseAngle / tintOpacity / frostAmount
        //   customParams2: cornerRadius / frostScale
        //   customParams4: shadowFraction / shadowOpacity
        //   customParams5: cornerCarveFraction
        shaderParams: {
            "customParams1_x": 1.2,
            "customParams1_y": 0,
            "customParams1_z": 0.9,
            "customParams1_w": 0.08,
            "customParams2_x": 0,
            "customParams2_y": 24,
            "customParams4_x": root.shadowFraction,
            "customParams4_y": 0.5,
            "customParams5_x": root.cornerCarveFraction
        }
        // Catppuccin mocha mauve → macchiato sky.
        customColor1: "#cba6f7"
        customColor2: "#89dceb"
    }

    // Content occupies the VISIBLE panel region only: never the
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
            anchors.leftMargin: root.leftMargin
            spacing: root.leftZoneSpacing

            Rectangle {
                id: menuButton

                width: root.menuButtonSize
                height: root.menuButtonSize
                anchors.verticalCenter: parent.verticalCenter
                radius: root.menuRadius
                color: menuArea.containsMouse ? Theme.surface_container_high : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: root.shellState.menuOpen ? "✕" : "☰"
                    color: root.shellState.menuOpen ? Theme.error : Theme.on_surface
                    font.pixelSize: root.menuGlyphSize
                    font.weight: Font.Bold
                }

                MouseArea {
                    id: menuArea

                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    Accessible.role: Accessible.Button
                    Accessible.name: root.shellState.menuOpen ? qsTr("Close menu") : qsTr("Open menu")
                    onClicked: root.shellState.togglePopup("menu")
                }
            }

            Row {
                spacing: root.workspaceDotSpacing
                anchors.verticalCenter: parent.verticalCenter

                // NOTE: this example uses indices for "workspaces" which
                // is pedagogical only. Production shells should bind to
                // compositor-provided workspace IDs (per the project's
                // "Zone IDs, never indices" rule).
                Repeater {
                    model: 5

                    Rectangle {
                        required property int index

                        width: index === root.shellState.activeWorkspace ? root.workspaceDotActiveWidth : root.workspaceDotSize
                        height: root.workspaceDotSize
                        radius: root.workspaceDotRadius
                        color: index === root.shellState.activeWorkspace ? Theme.on_surface : Theme.outline_variant

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            Accessible.role: Accessible.Button
                            Accessible.name: qsTr("Switch to workspace %1").arg(parent.index + 1)
                            onClicked: root.shellState.activeWorkspace = parent.index
                        }

                        Behavior on width {
                            NumberAnimation {
                                duration: root.workspaceWidthAnimMs
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
            color: Theme.on_surface
            font.pixelSize: root.clockFontSize
            font.weight: Font.Bold
            leftPadding: root.clockPaddingX
            rightPadding: root.clockPaddingX

            // MouseArea hosted on Text: the clickable region tracks the
            // Text's implicit-size + padding box. Hit testing is geometry-
            // driven, so a Text item is a valid (if unconventional)
            // MouseArea host; anchoring the trigger to clockLabel matches
            // `calendarAnchor` so the popup originates visually from the
            // clock.
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                Accessible.role: Accessible.Button
                Accessible.name: root.shellState.calendarOpen ? qsTr("Close calendar") : qsTr("Open calendar")
                onClicked: root.shellState.togglePopup("calendar")
            }
        }

        // ─── System-tray (StatusNotifierItem) plumbing ────────────────────
        // One host per panel: owns the DBus watcher + items collection.
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
            anchors.rightMargin: root.rightMargin
            spacing: root.rightZoneSpacing

            // Tray icons. Each delegate is a clickable image with hover
            // background + cascade-popup menu on right-click. The model
            // auto-refreshes when items register/unregister/change status
            // (no manual binding needed). Qt6 Row/Column collapse both an
            // invisible child AND its adjacent spacing slot, so gating
            // `visible` on the count Q_PROPERTY removes both the tray Row
            // and the right-zone gap when the tray is empty.
            Row {
                spacing: root.traySpacing
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
                        required property var status
                        required property string toolTipTitle
                        required property string toolTipBody
                        required property string menuPath
                        required property bool itemIsMenu
                        required property string dbusService

                        width: root.trayDelegateSize
                        height: root.trayDelegateSize
                        radius: root.trayRadius
                        color: trayMouse.containsMouse ? Theme.surface_container_high : "transparent"

                        // Fallback glyph: first letter of title in a
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
                            width: root.trayIconSize
                            height: root.trayIconSize
                            radius: width / 2
                            color: Theme.surface_container_high

                            Text {
                                anchors.centerIn: parent
                                text: trayDelegate.title.length > 0 ? trayDelegate.title.charAt(0).toUpperCase() : "?"
                                color: Theme.on_surface
                                font.pixelSize: root.fallbackGlyphSize
                                font.weight: Font.Bold
                            }
                        }

                        Image {
                            anchors.centerIn: parent
                            visible: trayDelegate.iconUrl.length > 0
                            width: root.trayIconSize
                            height: root.trayIconSize
                            // Image.source is a QUrl property; feed it
                            // the model's URL-form role, which encodes
                            // a cacheKey so the URL changes whenever
                            // the underlying QImage data updates. The
                            // engine routes the URL back through the
                            // icon-theme image provider mounted by
                            // PhosphorServiceIconTheme::installImageProvider.
                            // sourceSize keeps the on-screen size stable
                            // regardless of the icon's intrinsic resolution.
                            source: trayDelegate.iconUrl
                            sourceSize.width: root.trayIconSourceSize
                            sourceSize.height: root.trayIconSourceSize
                            smooth: true
                            // Dim Passive tray items so a chatty app
                            // that never upgrades to Active doesn't
                            // dominate the panel; Active stays at 1.
                            // status is the model's StatusRole value,
                            // which is the StatusNotifierItem::Status
                            // enum integer.
                            opacity: trayDelegate.status === StatusNotifierItem.Passive ? root.passiveTrayOpacity : 1
                        }

                        MouseArea {
                            id: trayMouse

                            // SNI button mapping, mirrors KDE Plasma.

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
                                //   LEFT   → Activate(): the app's primary action,
                                //            e.g. "open main window". Exception:
                                //            items that set ItemIsMenu = true have
                                //            no main window (the icon IS the menu),
                                //            so left-click opens the dbusmenu
                                //            instead. KeePassXC and the per-shell
                                //            "system tray menu" widgets are the
                                //            common cases.
                                //   MIDDLE → SecondaryActivate(): typically
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
                            // Forward scroll events as SNI Scroll():
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
                onPopupRequested: root.shellState.togglePopup("media")
            }

            Row {
                spacing: root.statRowSpacing
                anchors.verticalCenter: parent.verticalCenter

                Text {
                    text: qsTr("CPU")
                    color: Theme.on_surface
                    font.pixelSize: root.statFontSize
                    font.weight: Font.Medium
                }

                Text {
                    text: (root.cpuPercent || "0") + "%"
                    color: Theme.on_surface
                    font.pixelSize: root.statFontSize
                }
            }

            Row {
                spacing: root.statRowSpacing
                anchors.verticalCenter: parent.verticalCenter

                Text {
                    text: qsTr("MEM")
                    color: Theme.on_surface
                    font.pixelSize: root.statFontSize
                    font.weight: Font.Medium
                }

                Text {
                    text: (root.memPercent || "0") + "%"
                    color: Theme.on_surface
                    font.pixelSize: root.statFontSize
                }
            }

            Row {
                spacing: root.statRowSpacing
                anchors.verticalCenter: parent.verticalCenter
                // Hide the row entirely when UPower has not published a
                // displayDevice yet. shell.qml's binding renders "" when
                // displayDevice is null and "0" + "%" once it appears
                // (UPowerDevice initialises percentage = 0.0), so the
                // length check on batteryPercent is redundant under the
                // current binding contract; the batteryVisible gate is
                // the only one that matters.
                visible: root.batteryVisible

                Text {
                    text: qsTr("BAT")
                    color: Theme.on_surface
                    font.pixelSize: root.statFontSize
                    font.weight: Font.Medium
                }

                Text {
                    text: root.batteryPercent + "%"
                    color: Theme.on_surface
                    font.pixelSize: root.statFontSize
                }
            }

            Rectangle {
                width: root.settingsSize
                height: root.settingsSize
                anchors.verticalCenter: parent.verticalCenter
                radius: root.trayRadius
                color: settingsArea.containsMouse ? Theme.surface_container_high : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "⚙"
                    color: Theme.on_surface
                    font.pixelSize: root.settingsGlyphSize
                }

                MouseArea {
                    id: settingsArea

                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    Accessible.role: Accessible.Button
                    Accessible.name: root.shellState.settingsOpen ? qsTr("Close settings") : qsTr("Open settings")
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
    }
}

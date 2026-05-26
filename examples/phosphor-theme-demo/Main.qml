// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// phosphor-theme-demo — swatch sheet.
// Renders every M3 token in the active palette as a labelled card.
// The window background, text, and outline all bind through the Theme
// singleton — so when PaletteStore reloads from disk, this whole
// view retints live without any manual refresh path.

import Phosphor.Theme
import Phosphor.ThemeDemo
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root

    // Listen for load failures from PaletteStore. The status bar's
    // visibility tracks `lastError`, so it auto-hides on the next
    // successful reload.
    property string lastError: ""
    // Track which preset the user last applied via the button row. The
    // store doesn't remember this (its `sourcePath` is for on-disk loads
    // only), so the demo holds it locally for highlight state. Cleared
    // when an external source overrides via loadFromFile.
    property string activePreset: "dark"

    width: 960
    height: 720
    visible: true
    title: qsTr("Phosphor Theme — Swatch Sheet")
    color: Theme.background

    Connections {
        function onLoadError(path, reason) {
            root.lastError = path + " — " + reason;
        }

        function onPaletteChanged() {
            root.lastError = "";
        }

        target: Theme.paletteStore
    }

    ColumnLayout {
        // ─── Swatch grid ─────────────────────────────────────────────────
        // Drives a Repeater off `Theme.palette` so adding tokens to the
        // C++ default palette automatically expands the demo — no QML
        // changes needed when phosphor-theme gains a new token.

        anchors.fill: parent
        anchors.margins: Tokens.spacing_xl
        spacing: Tokens.spacing_l

        // ─── Header ──────────────────────────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Tokens.spacing_xs

            Text {
                text: qsTr("Phosphor Theme")
                color: Theme.on_surface
                font.pixelSize: Tokens.font_size_display_m
                font.weight: Tokens.font_weight_bold
                font.family: Tokens.font_family
            }

            Text {
                text: Theme.paletteStore.sourcePath.length > 0 ? qsTr("source: %1").arg(Theme.paletteStore.sourcePath) : qsTr("source: built-in (preset: %1)").arg(root.activePreset)
                color: Theme.on_surface_variant
                font.pixelSize: Tokens.font_size_body_m
                font.family: Tokens.font_family
            }

        }

        // ─── Preset switcher ─────────────────────────────────────────────
        // Each button pushes a hand-curated palette through
        // PaletteStore.applyTokens. That's the same code path matugen
        // uses for wallpaper-driven retints, so the proof is real, not
        // a mock: clicking a button retints every bound surface in this
        // window (and any other window sharing the same engine) in one
        // frame.
        RowLayout {
            Layout.fillWidth: true
            spacing: Tokens.spacing_s

            Text {
                text: qsTr("Preset:")
                color: Theme.on_surface_variant
                font.pixelSize: Tokens.font_size_label_l
                font.family: Tokens.font_family
                Layout.alignment: Qt.AlignVCenter
            }

            Repeater {
                model: PresetPalettes.names()

                Rectangle {
                    required property string modelData
                    readonly property bool isActive: root.activePreset === modelData
                    readonly property bool isHovered: hover.containsMouse

                    Layout.preferredWidth: label.implicitWidth + Tokens.spacing_l * 2
                    Layout.preferredHeight: 32
                    radius: Tokens.radius_full
                    color: isActive ? Theme.primary : isHovered ? Theme.surface_container_high : Theme.surface_container
                    border.color: isActive ? Theme.primary : Theme.outline_variant
                    border.width: 1

                    Text {
                        id: label

                        anchors.centerIn: parent
                        text: modelData
                        color: parent.isActive ? Theme.on_primary : Theme.on_surface
                        font.pixelSize: Tokens.font_size_label_l
                        font.family: Tokens.font_family
                        font.weight: Tokens.font_weight_medium
                    }

                    MouseArea {
                        id: hover

                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("Apply %1 preset").arg(modelData)
                        onClicked: {
                            const tokens = PresetPalettes.byName(modelData);
                            Theme.paletteStore.applyTokens(tokens);
                            root.activePreset = modelData;
                        }
                    }

                }

            }

            Item {
                Layout.fillWidth: true
            }

        }

        // ─── Brand gradient strip ────────────────────────────────────────
        // cyan → blue → purple → rose — the signature accent sweep. Sits
        // above the grid so the canonical four stops are visible at a
        // glance.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            radius: Tokens.radius_l
            border.color: Theme.outline_variant
            border.width: 1

            gradient: Gradient {
                orientation: Gradient.Horizontal

                GradientStop {
                    position: 0
                    color: Theme.brand_stop_0
                }

                GradientStop {
                    position: 0.33
                    color: Theme.brand_stop_1
                }

                GradientStop {
                    position: 0.66
                    color: Theme.brand_stop_2
                }

                GradientStop {
                    position: 1
                    color: Theme.brand_stop_3
                }

            }

        }

        // GridLayout with a computed `columns` count reflows on window
        // resize. Swatches share the row evenly via Layout.fillWidth;
        // the 220 px minimum below is the swatch's intrinsic width, so
        // every card has at least enough room for the longest token
        // label without eliding.
        ScrollView {
            id: scrollView

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            GridLayout {
                width: scrollView.availableWidth
                columnSpacing: Tokens.spacing_m
                rowSpacing: Tokens.spacing_m
                columns: Math.max(1, Math.floor((width + columnSpacing) / (220 + columnSpacing)))

                Repeater {
                    // QVariantMap iteration order is alphabetical by key,
                    // which is good enough for a flat reference card.
                    model: Object.keys(Theme.palette).sort()

                    Swatch {
                        required property string modelData

                        tokenName: modelData
                        tokenColor: Theme.palette[modelData]
                        Layout.fillWidth: true
                    }

                }

            }

        }

        // ─── Status bar ──────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            visible: root.lastError.length > 0
            color: Theme.error_container
            radius: Tokens.radius_s

            Text {
                anchors.fill: parent
                anchors.margins: Tokens.spacing_s
                text: root.lastError
                color: Theme.on_error
                font.pixelSize: Tokens.font_size_body_s
                font.family: Tokens.font_family
                elide: Text.ElideMiddle
                verticalAlignment: Text.AlignVCenter
            }

        }

    }

}

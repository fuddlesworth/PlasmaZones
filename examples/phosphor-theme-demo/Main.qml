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
                text: Theme.paletteStore.sourcePath.length > 0 ? qsTr("source: %1").arg(Theme.paletteStore.sourcePath) : qsTr("source: built-in defaults")
                color: Theme.on_surface_variant
                font.pixelSize: Tokens.font_size_body_m
                font.family: Tokens.font_family
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

        // ─── Swatch grid ─────────────────────────────────────────────────
        // Drives a Repeater off `Theme.palette` so adding tokens to the
        // C++ default palette automatically expands the demo — no QML
        // changes needed when phosphor-theme gains a new token.
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            Flow {
                width: parent.width
                spacing: Tokens.spacing_m

                Repeater {
                    // QVariantMap iteration order is alphabetical by key,
                    // which is good enough for a flat reference card.
                    model: Object.keys(Theme.palette).sort()

                    Swatch {
                        required property string modelData

                        tokenName: modelData
                        tokenColor: Theme.palette[modelData]
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

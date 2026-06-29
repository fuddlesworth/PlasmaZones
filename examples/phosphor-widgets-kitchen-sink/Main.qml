// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// phosphor-widgets-kitchen-sink, the Phase 3.1 acceptance demo.
//
// One scrollable window listing every Phosphor.Widgets atom in its
// enabled and disabled states. Hover, press, and focus states are live:
// interact with the enabled specimens to see the state-layer tint,
// ripple, and focus outline. The header cycles the accent token and
// resets the palette, proving every atom retints live through the Theme
// singleton with no per-widget refresh.

import Phosphor.Theme
import Phosphor.Widgets
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root

    // Round-robin index into the accent list below, advanced by the
    // "Cycle accent" button. Held here because PaletteStore doesn't track
    // demo-applied overrides.
    property int accentIndex: 0

    readonly property var accents: ["#3B82F6", "#A855F7", "#22D3EE", "#10B981", "#FBBF24", "#F43F5E"]

    width: 880
    height: 900
    visible: true
    title: qsTr("Phosphor Widgets, Kitchen Sink")
    color: Theme.background

    // Apply an accent override the same way matugen / the theme presets
    // do: PaletteStore.applyTokens merges over the active palette, so
    // every `Theme.primary` binding re-evaluates live.
    function cycleAccent() {
        accentIndex = (accentIndex + 1) % accents.length;
        Theme.paletteStore.applyTokens({
            "primary": accents[accentIndex]
        });
    }

    header: ToolBar {
        background: Rectangle {
            color: Theme.surface_container
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Tokens.spacing_l
            anchors.rightMargin: Tokens.spacing_l
            spacing: Tokens.spacing_m

            Label {
                text: qsTr("Phosphor.Widgets")
                color: Theme.on_surface
                font.pixelSize: Tokens.font_size_title_l
                font.weight: Tokens.font_weight_demibold
                Layout.fillWidth: true
            }

            PhosphorButton {
                text: qsTr("Cycle accent")
                variant: PhosphorButton.Tonal
                onClicked: root.cycleAccent()
            }

            PhosphorButton {
                text: qsTr("Reset")
                variant: PhosphorButton.Text
                onClicked: {
                    root.accentIndex = 0;
                    Theme.paletteStore.resetToDefaults();
                }
            }
        }
    }

    ScrollView {
        id: scroller

        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            // availableWidth (not root.width) so content never slips under
            // the vertical scrollbar when one appears.
            width: scroller.availableWidth
            spacing: Tokens.spacing_xl

            // ── Buttons ──────────────────────────────────────────────
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Tokens.spacing_xl
                Layout.rightMargin: Tokens.spacing_xl
                Layout.topMargin: Tokens.spacing_xl
                spacing: Tokens.spacing_m

                Label {
                    text: qsTr("Buttons")
                    color: Theme.on_surface_variant
                    font.pixelSize: Tokens.font_size_body_m
                    font.weight: Tokens.font_weight_demibold
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: Tokens.spacing_m

                    PhosphorButton {
                        text: qsTr("Filled")
                        variant: PhosphorButton.Filled
                    }

                    PhosphorButton {
                        text: qsTr("Tonal")
                        variant: PhosphorButton.Tonal
                    }

                    PhosphorButton {
                        text: qsTr("Outlined")
                        variant: PhosphorButton.Outlined
                    }

                    PhosphorButton {
                        text: qsTr("Text")
                        variant: PhosphorButton.Text
                    }

                    PhosphorButton {
                        text: qsTr("Disabled")
                        variant: PhosphorButton.Filled
                        enabled: false
                    }
                }
            }

            // ── Pills ────────────────────────────────────────────────
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Tokens.spacing_xl
                Layout.rightMargin: Tokens.spacing_xl
                spacing: Tokens.spacing_m

                Label {
                    text: qsTr("Pills")
                    color: Theme.on_surface_variant
                    font.pixelSize: Tokens.font_size_body_m
                    font.weight: Tokens.font_weight_demibold
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: Tokens.spacing_m

                    PhosphorPill {
                        // Local toggle state for the demo; a real host
                        // binds selected to a service property.
                        property bool checked: true

                        text: qsTr("Wi-Fi")
                        selected: checked
                        onToggled: checked = !checked
                    }

                    PhosphorPill {
                        property bool checked: false

                        text: qsTr("Bluetooth")
                        selected: checked
                        onToggled: checked = !checked
                    }

                    PhosphorPill {
                        text: qsTr("Disabled")
                        selected: true
                        enabled: false
                    }
                }
            }

            // ── Slider ───────────────────────────────────────────────
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Tokens.spacing_xl
                Layout.rightMargin: Tokens.spacing_xl
                spacing: Tokens.spacing_m

                Label {
                    text: qsTr("Slider")
                    color: Theme.on_surface_variant
                    font.pixelSize: Tokens.font_size_body_m
                    font.weight: Tokens.font_weight_demibold
                }

                PhosphorSlider {
                    Layout.preferredWidth: 320
                    from: 0
                    to: 100
                    value: 40
                }

                PhosphorSlider {
                    Layout.preferredWidth: 320
                    from: 0
                    to: 100
                    value: 70
                    enabled: false
                }
            }

            // ── Text fields ──────────────────────────────────────────
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Tokens.spacing_xl
                Layout.rightMargin: Tokens.spacing_xl
                spacing: Tokens.spacing_m

                Label {
                    text: qsTr("Text fields")
                    color: Theme.on_surface_variant
                    font.pixelSize: Tokens.font_size_body_m
                    font.weight: Tokens.font_weight_demibold
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: Tokens.spacing_m

                    PhosphorTextField {
                        placeholderText: qsTr("Search")
                    }

                    PhosphorTextField {
                        text: qsTr("Typed value")
                    }

                    PhosphorTextField {
                        placeholderText: qsTr("Disabled")
                        enabled: false
                    }
                }
            }

            // ── Cards ────────────────────────────────────────────────
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Tokens.spacing_xl
                Layout.rightMargin: Tokens.spacing_xl
                Layout.bottomMargin: Tokens.spacing_xl
                spacing: Tokens.spacing_m

                Label {
                    text: qsTr("Cards (elevation 0 → 5: shadow deepens, surface tint rises)")
                    color: Theme.on_surface_variant
                    font.pixelSize: Tokens.font_size_body_m
                    font.weight: Tokens.font_weight_demibold
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: Tokens.spacing_l

                    Repeater {
                        model: 6

                        PhosphorCard {
                            required property int index

                            elevation: index

                            ColumnLayout {
                                spacing: Tokens.spacing_s

                                Label {
                                    text: qsTr("Card")
                                    color: Theme.on_surface
                                    font.pixelSize: Tokens.font_size_title_s
                                    font.weight: Tokens.font_weight_medium
                                }

                                Label {
                                    text: qsTr("Elevation %1").arg(index)
                                    color: Theme.on_surface_variant
                                    font.pixelSize: Tokens.font_size_body_s
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

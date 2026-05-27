// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// phosphor-theme-demo, swatch sheet.
// Renders every M3 token in the active palette as a labelled card.
// The window background, text, and outline all bind through the Theme
// singleton, so when PaletteStore reloads from disk, this whole
// view retints live without any manual refresh path.

import Phosphor.Theme
import Phosphor.ThemeDemo
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
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
    // Set when matugen produces a palette from a user-picked wallpaper.
    // Stays set across re-runs so the header shows what the active
    // matugen palette was derived from.
    property string wallpaperPath: ""
    // Snapshot of the palette's sorted key list. Recomputing this on
    // every paletteChanged would tear down and rebuild every Swatch
    // delegate (the Repeater rebinds when model changes), destroying
    // hover state and burning frames. The key set only changes when
    // tokens are added/removed (rare, matugen never removes tokens
    // thanks to PaletteStore's merge semantics), so a JSON-stringify
    // comparison cheaply gates the assignment.
    property var swatchKeys: []

    function refreshSwatchKeys() {
        const next = Object.keys(Theme.palette).sort();
        if (JSON.stringify(next) !== JSON.stringify(swatchKeys))
            swatchKeys = next;

    }

    Component.onCompleted: refreshSwatchKeys()
    width: 960
    height: 720
    visible: true
    title: qsTr("Phosphor Theme, Swatch Sheet")
    color: Theme.background

    Connections {
        function onLoadError(path, reason) {
            root.lastError = path + ": " + reason;
        }

        function onPaletteChanged() {
            root.lastError = "";
            // Re-sync the swatch key list only if the set of keys
            // actually changed (refreshSwatchKeys gates the assignment).
            root.refreshSwatchKeys();
        }

        target: Theme.paletteStore
    }

    // Matugen subprocess. Spawned on-demand from the "Wallpaper..." button.
    // Failures (matugen missing, image unreadable, unexpected JSON shape)
    // route through the same status bar PaletteStore uses, so any single
    // visible error surface tells the user what went wrong.
    MatugenRunner {
        id: matugen

        mode: "dark"
        onPaletteReady: function(tokens, wp) {
            // Matugen emits M3 tokens but not Phosphor's brand-gradient
            // extensions (brand_stop_0..3) or the ANSI status colors
            // (success / warning / info). PaletteStore's merge semantics
            // would leave those at the *previous* palette's values, which
            // is technically correct (preserves user overrides) but
            // visibly wrong for a "use this wallpaper" UX: the gradient
            // strip would stay on the old colors. Synthesize the brand
            // stops from M3 accents in display order, cyan ish → blue
            // → purple → rose, matching the canonical Phosphor stop
            // assignment in the default palette.
            const augmented = Object.assign({
            }, tokens);
            if (tokens.tertiary)
                augmented.brand_stop_0 = tokens.tertiary;

            if (tokens.primary)
                augmented.brand_stop_1 = tokens.primary;

            if (tokens.secondary)
                augmented.brand_stop_2 = tokens.secondary;

            if (tokens.error)
                augmented.brand_stop_3 = tokens.error;

            Theme.paletteStore.applyTokens(augmented);
            root.activePreset = "wallpaper";
            root.wallpaperPath = wp;
            root.lastError = "";
        }
        onFailed: function(wp, reason) {
            root.lastError = qsTr("matugen: %1, %2").arg(wp).arg(reason);
        }
    }

    FileDialog {
        id: wallpaperDialog

        title: qsTr("Pick a wallpaper for matugen")
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.webp *.bmp)"), qsTr("All files (*)")]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            // selectedFile is a QUrl. MatugenRunner.run has a QUrl
            // overload that calls QUrl::toLocalFile internally so every
            // legal file URL form resolves correctly (file:/path,
            // file:///path, percent-encoded, etc.) and non-file URLs
            // get rejected with a structured error instead of being
            // string-munged with a brittle regex.
            matugen.run(selectedFile);
        }
    }

    ColumnLayout {
        // ─── Swatch grid ─────────────────────────────────────────────────
        // Drives a Repeater off `Theme.palette` so adding tokens to the
        // C++ default palette automatically expands the demo, no QML
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
                text: {
                    if (root.activePreset === "wallpaper" && root.wallpaperPath.length > 0) {
                        const base = root.wallpaperPath.split("/").pop();
                        return qsTr("source: matugen ← %1").arg(base);
                    }
                    if (Theme.paletteStore.sourcePath.length > 0)
                        return qsTr("source: %1").arg(Theme.paletteStore.sourcePath);

                    return qsTr("source: built-in (preset: %1)").arg(root.activePreset);
                }
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
                    Layout.preferredHeight: Tokens.spacing_xxl
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

            // ─── Wallpaper trigger ───────────────────────────────────────
            // Runs matugen on a user-picked image and applies the resulting
            // palette to PaletteStore. While the subprocess is in flight
            // the pill swaps to a "running…" label and is non-interactive.
            Rectangle {
                readonly property bool isActive: root.activePreset === "wallpaper"
                readonly property bool isHovered: wallpaperHover.containsMouse && !matugen.running
                readonly property string pillLabel: matugen.running ? qsTr("running…") : qsTr("wallpaper…")

                Layout.preferredWidth: wallpaperLabel.implicitWidth + Tokens.spacing_l * 2
                Layout.preferredHeight: Tokens.spacing_xxl
                radius: Tokens.radius_full
                opacity: matugen.running ? 0.6 : 1
                color: isActive ? Theme.tertiary : isHovered ? Theme.surface_container_high : Theme.surface_container
                border.color: isActive ? Theme.tertiary : Theme.outline_variant
                border.width: 1

                Text {
                    id: wallpaperLabel

                    anchors.centerIn: parent
                    text: parent.pillLabel
                    color: parent.isActive ? Theme.on_tertiary : Theme.on_surface
                    font.pixelSize: Tokens.font_size_label_l
                    font.family: Tokens.font_family
                    font.weight: Tokens.font_weight_medium
                }

                MouseArea {
                    id: wallpaperHover

                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: matugen.running ? Qt.ArrowCursor : Qt.PointingHandCursor
                    enabled: !matugen.running
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Pick a wallpaper and run matugen")
                    onClicked: wallpaperDialog.open()
                }

            }

            Item {
                Layout.fillWidth: true
            }

        }

        // ─── Brand gradient strip ────────────────────────────────────────
        // cyan → blue → purple → rose, the signature accent sweep. Sits
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
                    // Bind to the cached key list (see root.swatchKeys)
                    // so a wallpaper-driven retint changes swatch colors
                    // in place rather than rebuilding the Repeater.
                    model: root.swatchKeys

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
            Layout.preferredHeight: Tokens.spacing_xxl
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

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Theme.Tokens, non-color design tokens.
// Spacing, radius, elevation, and typography. These do NOT change per
// wallpaper or matugen run. The canonical Phosphor palette page leaves
// them undefined, so we own the values. M3 defaults with adjustments
// for the "large rounding, generous spacing" aesthetic agreed in the
// mockup conventions.

pragma Singleton

import QtQuick

QtObject {
    // ─── Spacing scale ───────────────────────────────────────────────────
    // M3 uses a 4 dp baseline. We follow that. Every gap, padding, or
    // margin in shell QML must be one of these values rather than a
    // one-off pixel count.
    readonly property int spacing_xxs: 2
    readonly property int spacing_xs: 4
    readonly property int spacing_s: 8
    readonly property int spacing_m: 12
    readonly property int spacing_l: 16
    readonly property int spacing_xl: 24
    readonly property int spacing_xxl: 32
    readonly property int spacing_xxxl: 48
    // ─── Radius scale ────────────────────────────────────────────────────
    // M3 defines extra_small → extra_large + full pill. The shell biases
    // toward 16-24 px on cards/popouts to match the mockup baseline.
    readonly property int radius_none: 0
    readonly property int radius_xs: 4
    readonly property int radius_s: 8
    readonly property int radius_m: 12
    readonly property int radius_l: 16
    readonly property int radius_xl: 24
    readonly property int radius_xxl: 32
    readonly property int radius_full: 9999
    // ─── Elevation (Y offset / blur / opacity per tier) ──────────────────
    // Rendered by ElevationShadow.qml into MultiEffect drop-shadow
    // parameters (it is the single consumer of these tiers). M3 levels 0
    // through 5. Most shell surfaces sit at level 1 for the bar, level 2
    // for popouts, level 3 for modals.
    readonly property var elevation_0: ({
            "y": 0,
            "blur": 0,
            "opacity": 0
        })
    readonly property var elevation_1: ({
            "y": 1,
            "blur": 3,
            "opacity": 0.15
        })
    readonly property var elevation_2: ({
            "y": 2,
            "blur": 6,
            "opacity": 0.18
        })
    readonly property var elevation_3: ({
            "y": 6,
            "blur": 12,
            "opacity": 0.22
        })
    readonly property var elevation_4: ({
            "y": 8,
            "blur": 24,
            "opacity": 0.25
        })
    readonly property var elevation_5: ({
            "y": 12,
            "blur": 32,
            "opacity": 0.3
        })
    // ─── Typography ──────────────────────────────────────────────────────
    // Font-family is the system default. The shell respects the user's
    // system font choice rather than a hardcoded face. Size and weight
    // are tokenised so widgets bind these instead of opening every Text
    // delegate to tune sizes.
    readonly property string font_family: Qt.application.font.family
    readonly property int font_size_display_l: 32
    readonly property int font_size_display_m: 24
    readonly property int font_size_display_s: 20
    readonly property int font_size_title_l: 18
    readonly property int font_size_title_m: 16
    readonly property int font_size_title_s: 14
    readonly property int font_size_body_l: 14
    readonly property int font_size_body_m: 13
    readonly property int font_size_body_s: 12
    readonly property int font_size_label_l: 13
    readonly property int font_size_label_m: 12
    readonly property int font_size_label_s: 11
    readonly property int font_weight_regular: Font.Normal
    readonly property int font_weight_medium: Font.Medium
    readonly property int font_weight_demibold: Font.DemiBold
    readonly property int font_weight_bold: Font.Bold
}

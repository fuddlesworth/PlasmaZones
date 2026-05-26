// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Theme.Theme — color-token singleton.
// QML consumers write `Theme.primary` / `Theme.on_surface` / etc. instead
// of poking at the underlying PaletteStore by string key. Names mirror
// the canonical Phosphor palette tokens exactly (snake_case verbatim, per
// project_phosphor_default_palette memory).
// Binding-tracking note: every named accessor below indexes into the
// `palette` QVariantMap rather than calling PaletteStore.token(). The
// QML engine tracks property reads, not method calls — `palette` is a
// Q_PROPERTY with NOTIFY paletteChanged, so bindings on `Theme.primary`
// re-evaluate every time the store reloads. Calling token() would NOT
// retint live; the swatches would update (they read palette directly)
// but the surrounding chrome would not. Keep the index form.

import QtQuick
pragma Singleton

QtObject {
    // Direct handle on the underlying store so consumers that need the
    // full token map (theme editors, palette inspectors, the swatch
    // demo) can iterate. Day-to-day QML should prefer the named
    // accessors below.
    readonly property var paletteStore: PaletteStore
    // The active token map (token name → QColor). Every accessor below
    // routes through this — that's how change-tracking works (see the
    // file-level comment above).
    readonly property var palette: PaletteStore.palette
    // ─── Surfaces ────────────────────────────────────────────────────────
    readonly property color background: palette["background"]
    readonly property color surface: palette["surface"]
    readonly property color surface_container: palette["surface_container"]
    readonly property color surface_container_high: palette["surface_container_high"]
    readonly property color surface_variant: palette["surface_variant"]
    readonly property color on_surface: palette["on_surface"]
    readonly property color on_surface_variant: palette["on_surface_variant"]
    // ─── Accents ─────────────────────────────────────────────────────────
    readonly property color primary: palette["primary"]
    readonly property color on_primary: palette["on_primary"]
    readonly property color primary_container: palette["primary_container"]
    readonly property color on_primary_container: palette["on_primary_container"]
    readonly property color secondary: palette["secondary"]
    readonly property color on_secondary: palette["on_secondary"]
    readonly property color secondary_container: palette["secondary_container"]
    readonly property color tertiary: palette["tertiary"]
    readonly property color on_tertiary: palette["on_tertiary"]
    readonly property color tertiary_container: palette["tertiary_container"]
    // ─── Error ───────────────────────────────────────────────────────────
    readonly property color error: palette["error"]
    readonly property color on_error: palette["on_error"]
    readonly property color error_container: palette["error_container"]
    // ─── Outline ─────────────────────────────────────────────────────────
    readonly property color outline: palette["outline"]
    readonly property color outline_variant: palette["outline_variant"]
    // ─── Status (ANSI 16 derived) ────────────────────────────────────────
    readonly property color success: palette["success"]
    readonly property color success_bright: palette["success_bright"]
    readonly property color warning: palette["warning"]
    readonly property color warning_bright: palette["warning_bright"]
    readonly property color error_bright: palette["error_bright"]
    readonly property color info: palette["info"]
    readonly property color info_bright: palette["info_bright"]
    // ─── Brand gradient stops ────────────────────────────────────────────
    // cyan → blue → purple → rose. Use as `gradient` stops on accent
    // surfaces, connected-corner highlights, and shader uniforms.
    readonly property color brand_stop_0: palette["brand_stop_0"]
    readonly property color brand_stop_1: palette["brand_stop_1"]
    readonly property color brand_stop_2: palette["brand_stop_2"]
    readonly property color brand_stop_3: palette["brand_stop_3"]
}

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Theme.Theme — color-token singleton.
// QML consumers write `Theme.primary` / `Theme.on_surface` / etc. instead
// of poking at the underlying PaletteStore by string key. Names mirror
// the canonical Phosphor palette tokens exactly (snake_case verbatim, per
// project_phosphor_default_palette memory).
// Bindings track the live palette: when matugen rewrites
// `~/.local/share/phosphor/palettes/current.json` the PaletteStore reloads
// and every `Theme.<token>` binding re-evaluates automatically. Don't
// resolve tokens to strings in JS — keep the binding live.

import QtQuick
pragma Singleton

// PaletteStore is a C++ QML singleton (QML_ELEMENT QML_SINGLETON) declared
// in this same Phosphor.Theme module — types in the same module resolve
// without an explicit import (and a self-import is a hard error).
QtObject {
    // Direct handle on the underlying store so consumers that need the
    // full token map (theme editors, palette inspectors, the swatch demo)
    // can iterate without going through every alias. Day-to-day QML
    // should prefer the named accessors below.
    readonly property var paletteStore: PaletteStore
    readonly property var palette: PaletteStore.palette
    // ─── Surfaces ────────────────────────────────────────────────────────
    readonly property color background: PaletteStore.token("background")
    readonly property color surface: PaletteStore.token("surface")
    readonly property color surface_container: PaletteStore.token("surface_container")
    readonly property color surface_container_high: PaletteStore.token("surface_container_high")
    readonly property color surface_variant: PaletteStore.token("surface_variant")
    readonly property color on_surface: PaletteStore.token("on_surface")
    readonly property color on_surface_variant: PaletteStore.token("on_surface_variant")
    // ─── Accents ─────────────────────────────────────────────────────────
    readonly property color primary: PaletteStore.token("primary")
    readonly property color on_primary: PaletteStore.token("on_primary")
    readonly property color primary_container: PaletteStore.token("primary_container")
    readonly property color on_primary_container: PaletteStore.token("on_primary_container")
    readonly property color secondary: PaletteStore.token("secondary")
    readonly property color on_secondary: PaletteStore.token("on_secondary")
    readonly property color secondary_container: PaletteStore.token("secondary_container")
    readonly property color tertiary: PaletteStore.token("tertiary")
    readonly property color on_tertiary: PaletteStore.token("on_tertiary")
    readonly property color tertiary_container: PaletteStore.token("tertiary_container")
    // ─── Error ───────────────────────────────────────────────────────────
    readonly property color error: PaletteStore.token("error")
    readonly property color on_error: PaletteStore.token("on_error")
    readonly property color error_container: PaletteStore.token("error_container")
    // ─── Outline ─────────────────────────────────────────────────────────
    readonly property color outline: PaletteStore.token("outline")
    readonly property color outline_variant: PaletteStore.token("outline_variant")
    // ─── Status (ANSI 16 derived) ────────────────────────────────────────
    readonly property color success: PaletteStore.token("success")
    readonly property color success_bright: PaletteStore.token("success_bright")
    readonly property color warning: PaletteStore.token("warning")
    readonly property color warning_bright: PaletteStore.token("warning_bright")
    readonly property color error_bright: PaletteStore.token("error_bright")
    readonly property color info: PaletteStore.token("info")
    readonly property color info_bright: PaletteStore.token("info_bright")
    // ─── Brand gradient stops ────────────────────────────────────────────
    // cyan → blue → purple → rose. Use as `gradient` stops on accent
    // surfaces, connected-corner highlights, and shader uniforms.
    readonly property color brand_stop_0: PaletteStore.token("brand_stop_0")
    readonly property color brand_stop_1: PaletteStore.token("brand_stop_1")
    readonly property color brand_stop_2: PaletteStore.token("brand_stop_2")
    readonly property color brand_stop_3: PaletteStore.token("brand_stop_3")
}

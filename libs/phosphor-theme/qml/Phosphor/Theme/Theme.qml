// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Theme.Theme, color-token singleton.
// QML consumers write `Theme.primary` / `Theme.on_surface` / etc. instead
// of poking at the underlying PaletteStore by string key. Names mirror
// the canonical Phosphor palette tokens exactly (snake_case verbatim,
// matching the palette JSON wire format published at
// https://phosphor-works.github.io/palette/).
// Binding-tracking note. Every named accessor below indexes into the
// `palette` QVariantMap rather than calling PaletteStore.token(). The
// QML engine tracks property reads, not method calls. `palette` is a
// Q_PROPERTY with NOTIFY paletteChanged, so bindings on `Theme.primary`
// re-evaluate every time the store reloads. Calling token() would NOT
// retint live. Swatches would update because they read palette directly.
// The surrounding chrome would not. Keep the index form.
// Missing-token defense: `_t(name)` returns a sentinel magenta when a
// token isn't in the active palette. A bare `palette[name]` would yield
// an invalid QColor that renders as transparent black, silently hiding
// real bugs (typo'd token names, partial matugen output). Magenta is
// loud enough to spot during development without crashing the shell.

pragma Singleton

import QtQuick

QtObject {
    id: theme

    // Direct handle on the underlying store so consumers that need the
    // full token map can iterate. Examples are theme editors, palette
    // inspectors, and the swatch demo. Day-to-day QML should prefer
    // the named accessors below. Iterating via .palette retains
    // binding tracking. Calling .token() does NOT. See the file-level
    // comment for the binding-tracking rationale.
    readonly property var paletteStore: PaletteStore
    // The active token map. Token name maps to QColor. Every accessor
    // below routes through this property, which is how change-tracking
    // works. See the file-level comment above for the rationale.
    readonly property var palette: PaletteStore.palette
    // Sentinel for a missing token. Bright magenta is impossible to
    // miss in a normally-themed surface. Exposed as a property so tests
    // and tooling can detect missing-token fallback without scraping the
    // pixel buffer.
    readonly property color missingTokenColor: "#ff00ff"
    // ─── Surfaces ────────────────────────────────────────────────────────
    readonly property color background: _t("background")
    readonly property color surface: _t("surface")
    readonly property color surface_container: _t("surface_container")
    readonly property color surface_container_high: _t("surface_container_high")
    readonly property color surface_variant: _t("surface_variant")
    readonly property color on_surface: _t("on_surface")
    readonly property color on_surface_variant: _t("on_surface_variant")
    // M3 surface tint: the colour blended over elevated surfaces, rising
    // in opacity with elevation. M3 defaults this to the primary accent,
    // and matugen emits it as a distinct token. Prefer an explicit
    // surface_tint from the palette when present (so generated palettes
    // are honoured), otherwise fall back to primary so a palette without
    // the token still tints elevation correctly. Indexes `palette`
    // directly (not _t) to keep the primary fallback instead of the
    // missing-token magenta sentinel; both reads stay binding-tracked.
    readonly property color surface_tint: palette["surface_tint"] !== undefined ? palette["surface_tint"] : primary
    // ─── Accents ─────────────────────────────────────────────────────────
    readonly property color primary: _t("primary")
    readonly property color on_primary: _t("on_primary")
    readonly property color primary_container: _t("primary_container")
    readonly property color on_primary_container: _t("on_primary_container")
    readonly property color secondary: _t("secondary")
    readonly property color on_secondary: _t("on_secondary")
    readonly property color secondary_container: _t("secondary_container")
    readonly property color tertiary: _t("tertiary")
    readonly property color on_tertiary: _t("on_tertiary")
    readonly property color tertiary_container: _t("tertiary_container")
    // ─── Error ───────────────────────────────────────────────────────────
    readonly property color error: _t("error")
    readonly property color on_error: _t("on_error")
    readonly property color error_container: _t("error_container")
    // ─── Outline ─────────────────────────────────────────────────────────
    readonly property color outline: _t("outline")
    readonly property color outline_variant: _t("outline_variant")
    // ─── Status (ANSI 16 derived) ────────────────────────────────────────
    readonly property color success: _t("success")
    readonly property color success_bright: _t("success_bright")
    readonly property color warning: _t("warning")
    readonly property color warning_bright: _t("warning_bright")
    readonly property color error_bright: _t("error_bright")
    readonly property color info: _t("info")
    readonly property color info_bright: _t("info_bright")
    // ─── Brand gradient stops ────────────────────────────────────────────
    // cyan → blue → purple → rose. Use as `gradient` stops on accent
    // surfaces, connected-corner highlights, and shader uniforms.
    readonly property color brand_stop_0: _t("brand_stop_0")
    readonly property color brand_stop_1: _t("brand_stop_1")
    readonly property color brand_stop_2: _t("brand_stop_2")
    readonly property color brand_stop_3: _t("brand_stop_3")

    // Token accessor with a loud-magenta fallback. Inline helper so
    // every accessor below stays a one-liner that reads naturally.
    function _t(name) {
        const v = palette[name];
        return v !== undefined ? v : theme.missingTokenColor;
    }
}

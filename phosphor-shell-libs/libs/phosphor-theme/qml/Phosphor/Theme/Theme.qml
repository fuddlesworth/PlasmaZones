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
// QML engine tracks property reads, not method calls. This binding reads
// PaletteStore.palette and AppearanceStore.values, so Theme.primary follows
// either source's changes. Calling token() would not retint live. Keep the
// index form.
// Missing-token defense: `_t(name)` returns a sentinel magenta when a
// token isn't in the active palette. A bare `palette[name]` would yield
// an invalid QColor that renders as transparent black, silently hiding
// real bugs (typo'd token names, partial matugen output). Magenta is
// loud enough to spot during development without crashing the shell.

pragma Singleton

import QtQuick

QtObject {
    id: theme

    // Direct handle on the underlying legacy store for consumers that
    // edit it. The effective colors, including Appearance selections,
    // are available through palette and the named accessors below.
    readonly property var paletteStore: PaletteStore
    // Appearance's selected library palette overlays the legacy token
    // store, so controls still using Theme follow the same preview and
    // Apply/Revert transaction as the rest of the shell.
    readonly property var palette: {
        const base = PaletteStore.palette;
        const settings = AppearanceStore.values;
        if (settings.palette !== "library")
            return base;
        const selected = settings.libraryPalette;
        const merged = Object.assign({}, base, selected);
        if (selected.background === undefined)
            merged.background = selected.surface;
        return merged;
    }
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
    // ─── Field polarity ──────────────────────────────────────────────────
    // Whether the active palette is a dark field. The palette wire format
    // carries no polarity token, so it is derived from the relative
    // luminance of `background`: matugen's dark schemes sit well under
    // 0.5 and its light schemes well over. Spectrum reads this to pick the
    // dark or light brand ramp.
    readonly property bool isDark: (0.2126 * background.r + 0.7152 * background.g + 0.0722 * background.b) < 0.5

    // Token accessor with a loud-magenta fallback. Inline helper so
    // every accessor below stays a one-liner that reads naturally.
    function _t(name) {
        const v = palette[name];
        return v !== undefined ? v : theme.missingTokenColor;
    }
}

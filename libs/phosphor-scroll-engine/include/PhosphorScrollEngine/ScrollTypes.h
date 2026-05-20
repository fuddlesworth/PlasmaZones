// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QtGlobal>

namespace PhosphorScrollEngine {

// ── Engine-internal constants ──────────────────────────────────────────────
//
// These are the engine's standalone fallbacks — used when no IScrollSettings
// is wired (e.g. unit tests with no FakeScrollSettings) — and the clamp
// bounds defended at the persistence boundary (Column::fromJson) and at the
// per-screen-override read site (ScrollEngine::effective*()). The values are
// tied to ConfigDefaults via static_asserts in src/daemon/daemon/scroll.cpp
// so a drift fails the build.

/// Width a freshly-opened column is created with when no configured default
/// has been pushed yet — niri's middle preset (one half). The single source
/// for this fallback: ScrollEngine::m_defaultColumnWidth, the addColumn*/
/// addWindow* default arguments, and Column's m_width/m_restoreWidth defaults
/// all reference it.
inline constexpr qreal kDefaultColumnWidthFraction = 0.5;

/// Inclusive clamp bounds for a column-width / window-height fraction and for
/// a strip gap (logical px). ScrollEngine's effective*() resolvers clamp every
/// per-screen override to these ranges — defence-in-depth mirroring
/// AutotileEngine's PerScreenConfigResolver, which likewise range-checks every
/// per-screen override on read. The strip-gap range mirrors the Settings
/// schema's scroll-gap clamp (ConfigDefaults::scrollInnerGapMin/Max, i.e.
/// the shared autotile MinGap/MaxGap); kept in sync so the engine guard never
/// disagrees with the boundary the schema and the settings UI enforce.
inline constexpr qreal kMinSizeFraction = 0.1;
inline constexpr qreal kMaxSizeFraction = 1.0;
inline constexpr int kMinStripGap = 0;
inline constexpr int kMaxStripGap = 50;
/// Strip gap the effective*() resolvers fall back to when no IScrollSettings
/// is wired — niri's default strip gap in logical pixels.
inline constexpr int kDefaultStripGap = 8;

/// A column's width, expressed as *intent* — resolved to pixels only at
/// layout time. Mirrors niri's `ColumnWidth`: a column keeps its width when
/// neighbouring columns open or close, so opening a window never reflows the
/// strip.
struct ColumnWidth
{
    enum class Kind {
        Proportion, ///< `value` is a fraction [0..1] of the working-area width
        Fixed, ///< `value` is a logical-pixel width
    };

    Kind kind = Kind::Proportion;
    qreal value = 0.5;

    static constexpr ColumnWidth proportion(qreal fraction)
    {
        return ColumnWidth{Kind::Proportion, fraction};
    }
    static constexpr ColumnWidth fixed(qreal pixels)
    {
        return ColumnWidth{Kind::Fixed, pixels};
    }

    bool operator==(const ColumnWidth& other) const = default;
};

/// A tile's height within its column, expressed as intent. `Auto` tiles
/// share the column's free vertical space in proportion to their `weight`;
/// `Fixed`/`Preset` tiles take a concrete size first.
struct WindowHeight
{
    enum class Kind {
        Auto, ///< share column height with sibling Auto tiles, by `weight`
        Fixed, ///< `fixedPx` logical pixels
        Preset, ///< `presetIndex` into the configured preset-height list
    };

    Kind kind = Kind::Auto;
    qreal weight = 1.0;
    qreal fixedPx = 0.0;
    int presetIndex = 0;

    static constexpr WindowHeight automatic(qreal weight = 1.0)
    {
        WindowHeight h;
        h.kind = Kind::Auto;
        h.weight = weight;
        return h;
    }
    static constexpr WindowHeight fixed(qreal pixels)
    {
        WindowHeight h;
        h.kind = Kind::Fixed;
        h.fixedPx = pixels;
        return h;
    }
    static constexpr WindowHeight preset(int index)
    {
        WindowHeight h;
        h.kind = Kind::Preset;
        h.presetIndex = index;
        return h;
    }

    bool operator==(const WindowHeight& other) const = default;
};

} // namespace PhosphorScrollEngine

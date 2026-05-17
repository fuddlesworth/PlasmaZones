// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QtGlobal>

namespace PhosphorScrollEngine {

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

    static WindowHeight automatic(qreal weight = 1.0)
    {
        WindowHeight h;
        h.kind = Kind::Auto;
        h.weight = weight;
        return h;
    }
    static WindowHeight fixed(qreal pixels)
    {
        WindowHeight h;
        h.kind = Kind::Fixed;
        h.fixedPx = pixels;
        return h;
    }
    static WindowHeight preset(int index)
    {
        WindowHeight h;
        h.kind = Kind::Preset;
        h.presetIndex = index;
        return h;
    }

    bool operator==(const WindowHeight& other) const = default;
};

} // namespace PhosphorScrollEngine

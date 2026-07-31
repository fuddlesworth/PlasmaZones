// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QList>
#include <QRect>
#include <QString>
#include <QVector>

namespace PhosphorScrollEngine {

/// Per-screen override-map keys for ScrollEngine::applyPerScreenConfig.
/// Both sides of the daemon↔engine seam MUST use these accessors — a raw
/// string literal here is a silent rule-override drop when it drifts
/// (mirrors the autotile PerScreenKeys convention).
namespace ScrollPerScreenKeys {
inline QString centerFocusedColumn()
{
    return QStringLiteral("CenterFocusedColumn");
}
/// RULE channel: a bare work-area fraction (SetScrollDefaultColumnWidth).
/// Outranks the settings-channel kind trio below — rule > per-screen
/// setting > global. The settings app never writes this key.
inline QString defaultColumnWidth()
{
    return QStringLiteral("DefaultColumnWidth");
}
inline QString defaultColumnDisplay()
{
    return QStringLiteral("DefaultColumnDisplay");
}
/// SETTINGS channel: the per-screen override map mirrors the config's
/// kind-aware width trio so a monitor can pin Fixed pixels or a preset
/// index, which the rule channel's bare fraction cannot express.
inline QString defaultColumnWidthKind()
{
    return QStringLiteral("DefaultColumnWidthKind");
}
inline QString defaultColumnWidthValue()
{
    return QStringLiteral("DefaultColumnWidthValue");
}
inline QString defaultColumnWidthPresetIndex()
{
    return QStringLiteral("DefaultColumnWidthPresetIndex");
}
/// RULE channel for the default window height: a bare work-area fraction
/// (resolved against the live work area at relayout, committed as Fixed).
inline QString defaultWindowHeight()
{
    return QStringLiteral("DefaultWindowHeight");
}
/// SETTINGS channel height trio (kind / fixed px / preset index).
inline QString defaultWindowHeightKind()
{
    return QStringLiteral("DefaultWindowHeightKind");
}
inline QString defaultWindowHeightValue()
{
    return QStringLiteral("DefaultWindowHeightValue");
}
inline QString defaultWindowHeightPresetIndex()
{
    return QStringLiteral("DefaultWindowHeightPresetIndex");
}
inline QString insertPosition()
{
    return QStringLiteral("InsertPosition");
}
inline QString respectMinimumSize()
{
    return QStringLiteral("RespectMinimumSize");
}
} // namespace ScrollPerScreenKeys

/// The narrowest column width this engine will accept as a proportion of the
/// work area. Every producer of a proportion clamps or validates against it:
/// the config read, the per-screen rule override, the per-window open rule,
/// the preset list, and the persisted-blob boundary.
///
/// KEEP IN SYNC with ConfigDefaults::scrollingDefaultColumnWidthValueMin and
/// the rules-side PhosphorRules::MinColumnWidthRatio. Neither is reachable
/// from here — ConfigDefaults is app-side, and PhosphorRules is a library this
/// one does not link (the dependency runs the other way) — so the bound is
/// hand-mirrored, but at least it is hand-mirrored once.
inline constexpr qreal MinColumnWidthFraction = 0.05;

/// Persistent view-centering policy for the focused column (niri's
/// center-focused-column). Wire/config encoding is the int value; append only.
enum class CenterFocusedColumn : int {
    /// Focusing an off-screen column scrolls the minimum amount to pin it to
    /// the edge it entered from; never centers.
    Never = 0,
    /// The focused column is always centered in the view.
    Always = 1,
    /// Center only when the focused column plus the previously focused one
    /// cannot both fit on screen at once.
    OnOverflow = 2,
};

/// How a column presents its tiles (niri's column display).
enum class ColumnDisplay : int {
    /// Tiles split the column height vertically; all are visible.
    Normal = 0,
    /// Only the active tile is laid out, at full column height; the other
    /// tiles are hidden and represented by a tab-indicator strip.
    Tabbed = 1,
};

/// Column width INTENT — the source of truth the strip stores. Pixel rects are
/// recomputed from this on every relayout against the current work area;
/// pixels are never authoritative.
/// Wire vocabulary of the DEFAULT-column-width KIND setting
/// (IScrollSettings::scrollingDefaultColumnWidthKind). Deliberately
/// distinct from ColumnWidth::Kind — this enum's 2 means "client decides"
/// (a settings-level policy with no per-column representation), while
/// ColumnWidth::Kind's 2 is Preset. Never static_cast between the two.
enum class DefaultWidthKind : int {
    Proportion = 0,
    Fixed = 1,
    ClientDecides = 2,
    /// New columns open at a preset-list index (ColumnWidth::makePreset), so
    /// they reflow with preset-list changes. Appended as 3 — 2 is taken by
    /// ClientDecides and stored configs rely on it.
    Preset = 3,
};

/// Wire vocabulary of the DEFAULT-window-height KIND setting. Unlike the
/// width pair above, this one IS the model enum's vocabulary
/// (WindowHeight::Kind values match 1:1 — Auto/Fixed/Preset, no
/// "client decides" wrinkle on the height axis), so the engine may cast the
/// config value directly after a range guard.
enum class DefaultHeightKind : int {
    Auto = 0,
    Fixed = 1,
    Preset = 2,
};

/// Where a fresh-opened window's new column enters the strip (config
/// default; the openColumnPlacement window rule outranks it). Wire/config
/// encoding is the int value; append only. RightOfActive must stay 0 so an
/// absent key preserves the historical behavior.
enum class ScrollInsertPosition : int {
    RightOfActive = 0,
    LeftOfActive = 1,
    /// Leftmost column of the strip.
    First = 2,
    /// Rightmost column of the strip (niri's append).
    Last = 3,
    /// Stack into the focused column instead of opening a new one.
    IntoActiveColumn = 4,
};

struct ColumnWidth
{
    enum Kind : int {
        /// Fraction of the work-area width. Proportions account for the
        /// inter-column gap: 0.5 + 0.5 tile edge-to-edge with one gap between.
        Proportion = 0,
        /// Absolute pixel width.
        Fixed = 1,
        /// Index into the preset-proportion list (resolved at relayout, so a
        /// preset-list settings change reflows preset-width columns).
        Preset = 2,
    };

    Kind kind = Proportion;
    qreal proportion = 0.5; ///< Kind::Proportion
    int fixedPx = 0; ///< Kind::Fixed
    int presetIdx = 0; ///< Kind::Preset

    static constexpr ColumnWidth makeProportion(qreal p)
    {
        ColumnWidth w;
        w.kind = Proportion;
        w.proportion = p;
        return w;
    }
    static constexpr ColumnWidth makeFixed(int px)
    {
        ColumnWidth w;
        w.kind = Fixed;
        w.fixedPx = px;
        return w;
    }
    static constexpr ColumnWidth makePreset(int idx)
    {
        ColumnWidth w;
        w.kind = Preset;
        w.presetIdx = idx;
        return w;
    }

    bool operator==(const ColumnWidth& other) const
    {
        if (kind != other.kind) {
            return false;
        }
        switch (kind) {
        case Proportion:
            return qFuzzyCompare(proportion, other.proportion);
        case Fixed:
            return fixedPx == other.fixedPx;
        case Preset:
            return presetIdx == other.presetIdx;
        }
        return false;
    }
};

/// Tile height INTENT within a column. Same pixels-are-derived contract as
/// ColumnWidth.
struct WindowHeight
{
    enum Kind : int {
        /// Share the column height left over after Fixed/Preset tiles,
        /// proportionally to weight (the default even split at weight 1).
        Auto = 0,
        /// Absolute pixel height.
        Fixed = 1,
        /// Index into the preset-proportion list.
        Preset = 2,
    };

    Kind kind = Auto;
    qreal weight = 1.0; ///< Kind::Auto
    int fixedPx = 0; ///< Kind::Fixed
    int presetIdx = 0; ///< Kind::Preset

    static WindowHeight makeAuto(qreal w = 1.0)
    {
        WindowHeight h;
        h.kind = Auto;
        h.weight = w;
        return h;
    }
    static WindowHeight makeFixed(int px)
    {
        WindowHeight h;
        h.kind = Fixed;
        h.fixedPx = px;
        return h;
    }
    static WindowHeight makePreset(int idx)
    {
        WindowHeight h;
        h.kind = Preset;
        h.presetIdx = idx;
        return h;
    }

    bool operator==(const WindowHeight& other) const
    {
        if (kind != other.kind) {
            return false;
        }
        switch (kind) {
        case Auto:
            return qFuzzyCompare(weight, other.weight);
        case Fixed:
            return fixedPx == other.fixedPx;
        case Preset:
            return presetIdx == other.presetIdx;
        }
        return false;
    }
};

/// One window in a column.
struct Tile
{
    QString windowId;
    WindowHeight height;
    /// Minimized tiles keep their slot/order but are excluded from layout;
    /// unminimize restores the window into the same slot.
    bool minimized = false;
    /// Client-reported minimum size (0 = unconstrained). Relayout clamps the
    /// resolved rect to it; a window that cannot honour its slot at all is the
    /// engine's cue to float it instead.
    int minWidth = 0;
    int minHeight = 0;
};

/// One column: a vertical stack of tiles.
struct Column
{
    QVector<Tile> tiles;
    int activeTileIdx = 0;
    ColumnWidth width;
    ColumnDisplay display = ColumnDisplay::Normal;

    bool isEmpty() const
    {
        return tiles.isEmpty();
    }
    /// True when every tile is minimized — the column occupies no strip width.
    bool isFullyMinimized() const
    {
        for (const Tile& t : tiles) {
            if (!t.minimized) {
                return false;
            }
        }
        return !tiles.isEmpty();
    }
    int indexOfWindow(const QString& windowId) const
    {
        for (int i = 0; i < tiles.size(); ++i) {
            if (tiles.at(i).windowId == windowId) {
                return i;
            }
        }
        return -1;
    }
};

/// Inputs a relayout resolves pixel rects against. The work area is the
/// screen's available geometry already shrunk by the outer gaps; @c gap is the
/// inner gap between columns and between tiles in a column.
struct ScrollLayoutParams
{
    QRect workArea;
    int gap = 0;
    /// Preset proportion lists (niri defaults: 1/3, 1/2, 2/3). Never empty —
    /// resolvers clamp preset indices into range.
    /// KEEP IN SYNC with ScrollEngine::refreshConfigFromSettings' fallback
    /// list (engine_core.cpp): these are the no-settings defaults, that is
    /// the empty-config fallback, and the ops-suite literal-260 preset
    /// assertion pins THIS copy — a change to only one side would leave
    /// the test green while a configured engine shifts.
    QList<qreal> presetColumnWidths{1.0 / 3.0, 0.5, 2.0 / 3.0};
    QList<qreal> presetWindowHeights{1.0 / 3.0, 0.5, 2.0 / 3.0};
    CenterFocusedColumn centerFocusedColumn = CenterFocusedColumn::Never;
    bool alwaysCenterSingleColumn = false;
    /// Whether the strip's layout math honours client minimum sizes (the
    /// column-width floor, the tile-height floor and its rebalance, and the
    /// interactive-resize floor). Off, the resolved rects obey the user's
    /// intents and the compositor's own min-size enforcement decides what
    /// overhangs. The open-time work-area-oversized float escape ignores
    /// this flag.
    bool respectMinimumSize = true;
    /// The context's default window height intent, seeded onto every
    /// fresh-created tile (restore paths overwrite it via
    /// setWindowHeightIntent). Default-constructed = Auto weight 1, the
    /// historical even split.
    WindowHeight defaultWindowHeight{};
    /// The context's default column width — the un-maximize fallback for a
    /// full-width column with no stored pre-maximize intent.
    ColumnWidth defaultColumnWidth = ColumnWidth::makeProportion(0.5);
};

/// One tile's resolved output for a relayout pass.
struct ResolvedTile
{
    QString windowId;
    /// Absolute pixel rect in screen coordinates. Valid even for hidden
    /// tiles of a tabbed column (they share the active tile's rect) so a
    /// display toggle can animate from a sane origin; not valid for
    /// minimized tiles (those are omitted entirely).
    QRect rect;
    /// True for the non-active tiles of a tabbed column — visually
    /// suppressed, not clickable, represented by the tab strip.
    bool hidden = false;
};

/// One column's resolved output.
struct ResolvedColumn
{
    int columnIndex = -1;
    /// The column's bounding rect (tab-strip anchor for tabbed columns).
    QRect rect;
    bool tabbed = false;
    QVector<ResolvedTile> tiles;
};

/// A full relayout pass over one strip.
struct ResolvedStrip
{
    QVector<ResolvedColumn> columns;
    /// The viewport's left edge in strip coordinates — what the engine uses
    /// to decide which columns are visible vs parked off-screen.
    int viewX = 0;
    /// Total strip width in pixels (all non-minimized columns + gaps).
    int stripWidth = 0;
};

} // namespace PhosphorScrollEngine

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
/// RULE channel (SetScrollInsertPosition); the settings store deliberately
/// does not write it — insert position is app-wide config, per-context
/// only via rules, matching the tiling siblings' exposure.
inline QString insertPosition()
{
    return QStringLiteral("InsertPosition");
}
/// RULE channel for the tab indicator's GEOMETRY half, one key per property
/// so independent rules cascade per-property the way the width trio does.
/// The indicator's paint half never reaches this library (see
/// IScrollSettings), so it overrides through the daemon's own channel and has
/// no key here. Absent key = fall back to the configured value.
inline QString tabIndicatorEnabled()
{
    return QStringLiteral("TabIndicatorEnabled");
}
inline QString tabIndicatorHideWhenSingleTab()
{
    return QStringLiteral("TabIndicatorHideWhenSingleTab");
}
inline QString tabIndicatorPlaceWithinColumn()
{
    return QStringLiteral("TabIndicatorPlaceWithinColumn");
}
inline QString tabIndicatorGap()
{
    return QStringLiteral("TabIndicatorGap");
}
inline QString tabIndicatorWidth()
{
    return QStringLiteral("TabIndicatorWidth");
}
inline QString tabIndicatorLengthProportion()
{
    return QStringLiteral("TabIndicatorLengthProportion");
}
inline QString tabIndicatorPosition()
{
    return QStringLiteral("TabIndicatorPosition");
}
/// TEMPLATE channel: preset lists from the screen's assigned scrolling
/// template, as a QVariantList of doubles. A
/// present, non-empty list replaces the settings-configured preset list
/// WHOLESALE for that screen — no merge, so preset indices and the cycle
/// order stay stable within one template. The daemon writes these from the
/// assignment cascade; neither the rules bridge nor the settings app does.
inline QString presetColumnWidths()
{
    return QStringLiteral("PresetColumnWidths");
}
inline QString presetWindowHeights()
{
    return QStringLiteral("PresetWindowHeights");
}
/// TEMPLATE channel: the template's seed BLUEPRINT as a QVariantList of
/// {width (double fraction), display (int ColumnDisplay)} maps, ordered
/// left to right. Consumed at column CREATION on the fresh-open path: a
/// column materializing while the strip holds fewer columns than the
/// blueprint takes the next entry's width and display (per-window open
/// rules still outrank it). Never resizes existing columns. Only the
/// daemon writes it.
///
/// An entry may carry either key alone: a missing width or display falls
/// through to the effective default rather than reading as zero. That makes
/// the precedence asymmetric on purpose. Within the blueprint, an entry that
/// DOES carry a display outranks a screen-wide SetScrollDefaultColumnDisplay
/// rule; past the blueprint (and for entries that omit the key) the rule
/// decides. A per-column blueprint entry is the more specific statement, so
/// it wins where it speaks and stays silent where it does not. The in-tree
/// daemon always writes both keys on every entry, so the either-key tolerance
/// is a public-API belt for embedder-supplied maps rather than a fix for a
/// shipped bug.
inline QString templateColumns()
{
    return QStringLiteral("TemplateColumns");
}
inline QString templateColumnWidth()
{
    return QStringLiteral("width");
}
inline QString templateColumnDisplay()
{
    return QStringLiteral("display");
}
} // namespace ScrollPerScreenKeys

/// The narrowest column width this engine will accept as a proportion of the
/// work area. Every producer of a proportion clamps or validates against it:
/// the config read, the per-screen rule override, the per-window open rule,
/// the preset list, and the persisted-blob boundary.
///
/// KEEP IN SYNC with ConfigDefaults::scrollingDefaultColumnWidthProportionMin and
/// the rules-side PhosphorRules::MinColumnWidthRatio. Neither is reachable
/// from here — ConfigDefaults is app-side, and PhosphorRules is a library this
/// one does not link (the dependency runs the other way) — so the bound is
/// hand-mirrored, but at least it is hand-mirrored once.
inline constexpr qreal MinColumnWidthFraction = 0.05;

/// The shortest tile height this engine will accept as a proportion of the
/// work area. The height twin of MinColumnWidthFraction, deliberately its own
/// name: the two bounds happen to share a value today, and a caller that
/// clamps a HEIGHT against the width constant would silently follow a later
/// width-only change. KEEP IN SYNC with PhosphorRules::MinColumnWidthRatio
/// (RuleAction.h), which the rules-side height validation currently uses for
/// BOTH fractions; like the width constant above, the bound is hand-mirrored
/// because the dependency runs the other way.
inline constexpr qreal MinWindowHeightFraction = 0.05;

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

/// Which side of the column the tab indicator runs along (niri's
/// TabIndicatorPosition, same declaration order). Left/Right put the
/// indicator on a vertical edge and it runs down the column; Top/Bottom put it
/// on a horizontal edge and it runs across. The settings layer spells the same
/// wire values in ConfigDefaults and settingsschema_scrolling.cpp
/// static_asserts the two agree.
enum class TabIndicatorPosition : int {
    Left = 0,
    Right = 1,
    Top = 2,
    Bottom = 3,
};

/// True when @p position puts the indicator on a vertical edge, so its long
/// axis is the column's HEIGHT and its thickness eats the column's WIDTH.
/// Free function rather than a member so the enum stays a plain wire type.
inline bool isVerticalTabIndicator(TabIndicatorPosition position)
{
    return position == TabIndicatorPosition::Left || position == TabIndicatorPosition::Right;
}

/// The tab indicator's GEOMETRY inputs — the subset of the
/// Scrolling.TabIndicator family that changes resolved rects, so it has to
/// live in the layout params rather than staying overlay-side. The paint-only
/// keys (style, gaps between tabs, corner radius, colours) never reach this
/// library; the daemon reads those straight onto the overlay.
struct TabIndicatorParams
{
    /// Off, no indicator rect is resolved and @c placeWithinColumn reserves
    /// nothing — a tabbed column lays out exactly as it did before the
    /// indicator existed.
    bool enabled = true;
    /// Skip the indicator for a tabbed column holding a single tile. Affects
    /// the resolved rect (and therefore the reservation) as well as the
    /// payload, so a single-tab column under this flag lays out full-bleed.
    bool hideWhenSingleTab = false;
    /// Reserve the indicator's thickness plus its gap out of the column,
    /// shrinking the tile rects, instead of drawing over whatever sits beside
    /// the column. This is the only field here that moves windows.
    bool placeWithinColumn = false;
    /// Gap between the indicator and the window. NEGATIVE IS MEANINGFUL and
    /// matches niri: it slides the indicator onto the window. Under
    /// @c placeWithinColumn a negative gap correspondingly reserves less than
    /// the thickness, and the reservation floors at zero.
    int gap = 5;
    /// Indicator thickness (its short axis) in pixels, EXACT for every style.
    ///
    /// Load-bearing for @c reservedThickness: this library cannot measure
    /// text, so an overlay style that sized itself to its own font would draw
    /// outside the band reserved for it and over the window. Both sides agree
    /// on this one number instead, and a style whose content does not fit
    /// clips.
    int width = 4;
    /// Indicator length along its long axis, as a proportion of the column
    /// extent it runs beside.
    qreal lengthProportion = 0.5;
    TabIndicatorPosition position = TabIndicatorPosition::Left;

    /// Pixels this indicator takes out of the column along the indicator's
    /// THICKNESS axis (the column's width for a left/right indicator, its
    /// height for top/bottom) when
    /// @c placeWithinColumn is set, and 0 otherwise. Floored at 0 so a
    /// negative gap large enough to cancel the thickness cannot GROW the
    /// column. @p tileCount lets the single-tab skip suppress the
    /// reservation, keeping the reservation and the drawn rect in agreement.
    int reservedThickness(int tileCount) const
    {
        if (!placeWithinColumn || !resolvesFor(tileCount)) {
            return 0;
        }
        // qMax(1, width), matching indicatorRectFor's thickness exactly. Using
        // the raw width here would let an embedder-supplied width below 1
        // reserve one number and draw another, so the visual gap would not be
        // the configured one.
        return qMax(0, qMax(1, width) + gap);
    }

    /// Whether a tabbed column with @p tileCount tiles draws an indicator.
    bool resolvesFor(int tileCount) const
    {
        return enabled && tileCount > 0 && !(hideWhenSingleTab && tileCount <= 1);
    }

    /// The rect the column's TILES get, i.e. @p columnRect minus whatever the
    /// indicator reserved. Identical to @p columnRect unless
    /// @c placeWithinColumn is set and an indicator actually resolves.
    QRect contentRectFor(const QRect& columnRect, int tileCount) const
    {
        const int reserved = reservedThickness(tileCount);
        if (reserved <= 0) {
            return columnRect;
        }
        QRect content = columnRect;
        switch (position) {
        case TabIndicatorPosition::Left:
            content.setX(columnRect.x() + reserved);
            break;
        case TabIndicatorPosition::Right:
            content.setWidth(columnRect.width() - reserved);
            break;
        case TabIndicatorPosition::Top:
            content.setY(columnRect.y() + reserved);
            break;
        case TabIndicatorPosition::Bottom:
            content.setHeight(columnRect.height() - reserved);
            break;
        }
        // A column narrower/shorter than its own reservation would invert.
        // Hand back the untouched column instead: an indicator is worth less
        // than a renderable window, and the drawn rect below stays put so the
        // two simply overlap, which is the same thing a negative gap does.
        return content.isValid() ? content : columnRect;
    }

    /// Where the indicator is drawn for a column occupying @p columnRect, in
    /// the same coordinates. Null when no indicator resolves (see
    /// @c resolvesFor). Outside @p columnRect unless @c placeWithinColumn.
    ///
    /// The indicator is CENTERED on its long axis, so shortening it with
    /// @c lengthProportion trims both ends evenly rather than anchoring it to
    /// one corner. That is niri's behaviour and the only choice that keeps a
    /// short indicator visually attached to the column it belongs to.
    QRect indicatorRectFor(const QRect& columnRect, int tileCount) const
    {
        if (!resolvesFor(tileCount)) {
            return QRect();
        }
        const int thickness = qMax(1, width);
        const bool vertical = isVerticalTabIndicator(position);
        const int axisExtent = vertical ? columnRect.height() : columnRect.width();
        // Floor at 1: a proportion small enough to round to nothing would make
        // the indicator vanish while every setting still says it is on.
        const int length = qBound(1, qRound(axisExtent * lengthProportion), axisExtent);
        const int longOffset = (axisExtent - length) / 2;

        // Offset of the indicator's near edge from the column's matching edge,
        // measured OUTWARD; negative means inward, over the window.
        //
        // Outside the column the gap is pure placement, so it moves the
        // indicator one-for-one and a negative gap slides it onto the window.
        //
        // WITHIN the column the gap is spent on the reservation instead, and
        // the indicator sits flush with the column edge. That works until the
        // reservation bottoms out at zero (gap == -thickness, the window now
        // filling the whole column), after which the gap had nowhere left to
        // go and the indicator simply FROZE — every further press of the
        // control did nothing, with no way to tell that from a broken setting.
        // Past that point the leftover is spent the only way still available,
        // by sliding the indicator inward over the window. The control stays
        // continuous across its whole range in both modes, and "a negative gap
        // puts the indicator on top of the window" holds either way.
        const int outward = placeWithinColumn ? -qMax(0, -(thickness + gap)) : (gap + thickness);
        switch (position) {
        case TabIndicatorPosition::Left:
            return QRect(columnRect.x() - outward, columnRect.y() + longOffset, thickness, length);
        case TabIndicatorPosition::Right:
            return QRect(columnRect.x() + columnRect.width() - thickness + outward, columnRect.y() + longOffset,
                         thickness, length);
        case TabIndicatorPosition::Top:
            return QRect(columnRect.x() + longOffset, columnRect.y() - outward, length, thickness);
        case TabIndicatorPosition::Bottom:
            break;
        }
        return QRect(columnRect.x() + longOffset, columnRect.y() + columnRect.height() - thickness + outward, length,
                     thickness);
    }
};

/// Wire vocabulary of the DEFAULT-column-width KIND setting
/// (IScrollSettings::scrollingDefaultColumnWidthKind). Deliberately
/// distinct from ColumnWidth::Kind — this enum's 2 means "client decides"
/// (a settings-level policy with no per-column representation), while
/// ColumnWidth::Kind's 2 is Preset. Never static_cast between the two.
enum class DefaultWidthKind : int {
    Proportion = 0,
    Fixed = 1,
    ClientDecides = 2,
    /// New columns open on a preset VALUE anchor (ColumnWidth::makePreset
    /// takes a fraction), so they reflow with preset-list changes by snapping
    /// to the nearest entry. The config spin stays index-based; the engine
    /// resolves it to the anchor at read time. Appended as 3 — 2 is taken by
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

/// Column width INTENT — the source of truth the strip stores. Pixel rects are
/// recomputed from this on every relayout against the current work area;
/// pixels are never authoritative.
struct ColumnWidth
{
    enum Kind : int {
        /// Fraction of the work-area width. Proportions account for the
        /// inter-column gap: 0.5 + 0.5 tile edge-to-edge with one gap between.
        Proportion = 0,
        /// Absolute pixel width.
        Fixed = 1,
        /// Fraction ANCHOR snapped to the nearest entry of the screen's
        /// effective preset list at relayout. Value-anchored (not an index)
        /// so the intent survives vocabulary changes: a template swap
        /// reflows the column onto the nearest new entry, clearing the
        /// template restores the original width, and a cross-screen move
        /// needs no remap. Cycling steps between vocabulary entries and
        /// writes the NEW entry's value as the anchor.
        Preset = 2,
    };

    Kind kind = Proportion;
    qreal proportion = 0.5; ///< Kind::Proportion
    int fixedPx = 0; ///< Kind::Fixed
    qreal presetFraction = 0.5; ///< Kind::Preset

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
    static constexpr ColumnWidth makePreset(qreal fraction)
    {
        ColumnWidth w;
        w.kind = Preset;
        w.presetFraction = fraction;
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
            // Anchors are always copied FROM vocabulary values, so the cycle
            // no-op gate compares identical doubles in practice; fuzzy keeps
            // JSON round-trips honest.
            return qFuzzyCompare(presetFraction, other.presetFraction);
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
        /// Fraction anchor snapped to the nearest entry of the effective
        /// preset list at relayout — same value-anchored contract as
        /// ColumnWidth::Preset.
        Preset = 2,
    };

    Kind kind = Auto;
    qreal weight = 1.0; ///< Kind::Auto
    int fixedPx = 0; ///< Kind::Fixed
    qreal presetFraction = 0.5; ///< Kind::Preset

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
    static WindowHeight makePreset(qreal fraction)
    {
        WindowHeight h;
        h.kind = Preset;
        h.presetFraction = fraction;
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
            return qFuzzyCompare(presetFraction, other.presetFraction);
        }
        return false;
    }
};

/// Shared nearest-entry resolution for the fraction anchors above — the ONE
/// implementation behind preset resolution, cycling, and height-fraction
/// probes (it replaced three per-site variants).
///
/// An EMPTY list answers -1, not 0: every other answer is a valid index into
/// @p presets, and handing back an out-of-range 0 made the empty case look
/// like a hit that a caller would then use to subscript. Every in-tree caller
/// bails on an empty list before reaching here, so the guard is the belt for
/// the one that does not.
inline int nearestPresetIndex(const QList<qreal>& presets, qreal fraction)
{
    if (presets.isEmpty()) {
        return -1;
    }
    int best = 0;
    for (int i = 1; i < presets.size(); ++i) {
        if (qAbs(presets.at(i) - fraction) < qAbs(presets.at(best) - fraction)) {
            best = i;
        }
    }
    return best;
}

/// The nearest entry's VALUE. An empty list returns @p fallback, matching the
/// old presetAt clamp's 0.5 answer.
inline qreal nearestPresetValue(const QList<qreal>& presets, qreal fraction, qreal fallback = 0.5)
{
    return presets.isEmpty() ? fallback : presets.at(nearestPresetIndex(presets, fraction));
}

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
    /// resolvers snap a Preset fraction anchor to the nearest entry.
    /// KEEP IN SYNC with THREE other copies, not one:
    ///   1. ScrollEngine::m_presetColumnWidths / m_presetWindowHeights, the
    ///      member seeds these mirror (ScrollEngine.h);
    ///   2. ScrollEngine::refreshConfigFromSettings' fallback list
    ///      (engine_core.cpp), the empty-config fallback;
    ///   3. ConfigDefaults' "0.333,0.5,0.667" strings
    ///      (configdefaults_scrolling.h) — the one a CONFIGURED user actually
    ///      gets, and the only one expressed in decimal rather than as
    ///      thirds, so it is already very slightly different by construction.
    /// The ops-suite literal-260 preset assertion pins THIS copy alone, so a
    /// change to any of the other three leaves the test green while the
    /// running engine shifts.
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
    /// Geometry inputs for the tab indicator drawn beside a tabbed column.
    /// Default-constructed = the family's own defaults, so a caller that never
    /// sets it gets an indicator that reserves nothing.
    TabIndicatorParams tabIndicator{};
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
    /// The column's bounding rect. This is the column's FULL extent, before
    /// any within-column indicator reservation — the tiles carry the reduced
    /// rects, and @c tabIndicatorRect carries what was reserved.
    QRect rect;
    bool tabbed = false;
    /// Where the tab indicator is drawn, in the same absolute screen
    /// coordinates as @c rect. NULL (default-constructed) whenever no
    /// indicator resolves: a normal column, the indicator switched off, or a
    /// single-tab column under hideWhenSingleTab. Under placeWithinColumn this
    /// sits inside @c rect; otherwise it sits outside, which is exactly niri's
    /// "the indicator can overlay other windows or go off-screen".
    QRect tabIndicatorRect;
    /// Which edge @c tabIndicatorRect runs along, so a consumer can tell the
    /// indicator's long axis without re-deriving it from the rect's aspect (a
    /// one-tab indicator can be square).
    TabIndicatorPosition tabIndicatorPosition = TabIndicatorPosition::Top;
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

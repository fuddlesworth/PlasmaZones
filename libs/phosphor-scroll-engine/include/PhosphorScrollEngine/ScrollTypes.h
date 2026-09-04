// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorScrollEngine/StripAxis.h>

#include <QList>
#include <QRect>
#include <QString>
#include <QVector>

#include <optional>

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
/// SHARED key: which way this screen's strip runs, as the TRI-STATE
/// intent (0 auto, 1 horizontal, 2 vertical) — not the resolved two-valued
/// PhosphorProtocol::ScrollAxis, whose Horizontal is 0. The two numberings
/// deliberately disagree and must never be cast into each other.
///
/// Written by BOTH channels: the per-screen settings store seeds it and the
/// SetScrollStripAxis context rule overwrites the seed where a rule matched
/// (rule > per-screen setting > global, the same collapse every shared int
/// key gets in the daemon's merge). A rule that matches on desktop or
/// activity can therefore flip the axis on a live populated strip when the
/// context switches; the flip rides the ordinary retile the override-map
/// change schedules.
inline QString stripAxis()
{
    return QStringLiteral("StripAxis");
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
/// RULE channel for the scrolling BEHAVIOUR toggles (SetScrollAlwaysCenter-
/// SingleColumn / …CenterShortColumns / …RespectMinimumSize / …CropStraddlers /
/// …FocusNewWindows / …SmartGaps / …StickyWindowHandling). Like insertPosition these are
/// rules-only: the per-screen settings store does not write them, so an
/// absent key means "use the global config value" and the engine's
/// `effective*` readers supply exactly that fallback.
inline QString alwaysCenterSingleColumn()
{
    return QStringLiteral("AlwaysCenterSingleColumn");
}
inline QString centerShortColumns()
{
    return QStringLiteral("CenterShortColumns");
}
inline QString respectMinimumSize()
{
    return QStringLiteral("RespectMinimumSize");
}
inline QString cropStraddlers()
{
    return QStringLiteral("CropStraddlers");
}
inline QString focusNewWindows()
{
    return QStringLiteral("FocusNewWindows");
}
inline QString smartGaps()
{
    return QStringLiteral("SmartGaps");
}
/// StickyWindowHandling ints (treatAsNormal 0 / restoreOnly 1 / ignoreAll 2).
inline QString stickyWindowHandling()
{
    return QStringLiteral("StickyWindowHandling");
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
/// along the strip. Consumed at column CREATION on the fresh-open path: a
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
/// (ActionParams.h), which the rules-side height validation currently uses for
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
    /// Tiles split the column's CROSS extent between them; all are visible.
    Normal = 0,
    /// Only the active tile is laid out, at the column's content rect (its
    /// CROSS extent less whatever an in-column indicator reserved); the other
    /// tiles are hidden and represented by a tab-indicator strip. That cross
    /// extent is the height intent of the tab that OWNS it
    /// (Column::heightOwnerId), NOT the tab on show: reading the shown tab
    /// would resize the column on every tab switch and break the
    /// compositor's tab cross-fade, which is built on the arriving tab
    /// occupying the rect the outgoing one just vacated. So a tabbed column
    /// need not span the work area (ScrollStrip::tabbedColumnCrossPx).
    Tabbed = 1,
};

/// How far a screen has worked through its context template's seed blueprint.
///
/// The blueprint describes the columns a strip STARTS with, one entry per
/// column, and an entry is spent once a column has taken it. This is that
/// progress made inspectable: @ref total is how many starting columns the
/// blueprint declares (after the engine's own kMaxTemplateEntries cap, so it
/// counts entries that can actually be consumed rather than what an embedder
/// supplied), and @ref used is how many the screen has already taken.
///
/// used == total means the blueprint is exhausted and further columns open at
/// the template's ordinary defaults. Both are zero on a screen with no
/// blueprint, which is also what a screen that is not scrolling reports.
struct ScrollBlueprintProgress
{
    int total = 0;
    int used = 0;

    bool operator==(const ScrollBlueprintProgress&) const = default;
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
/// live in the layout params rather than staying with the paint half. The
/// paint-only keys (style, gaps between tabs, corner radius, the three colours
/// and the five label-font keys) never reach this library: they go to the KWin
/// effect, which draws the indicator itself. See IScrollSettings.h for the
/// same split stated from the settings side.
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
    ///
    /// This seed is niri's own literal and is NOT the shipped default. The
    /// shipped one is ConfigDefaults::scrollingTabIndicatorGap(), which derives
    /// it from the inner gap and the bar thickness and currently comes out
    /// smaller. That accessor lives in the app tree, which this library cannot
    /// include, so the two cannot be single-sourced; the engine overwrites this
    /// field from settings on every refresh, which is what makes the divergence
    /// invisible outside direct constructions and test fixtures.
    int gap = 5;
    /// Indicator thickness (its short axis) in pixels, EXACT for every style.
    ///
    /// Load-bearing for @c reservedThickness: this library cannot measure
    /// text, so a style that sized itself to its own font would draw
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
    ///
    /// Which strip ROLE that thickness comes out of depends on the axis: a
    /// left/right indicator eats the column's MAIN extent on a horizontal
    /// strip and its CROSS extent on a vertical one, and the top/bottom pair
    /// inverts the same way. The screen-edge vocabulary above is deliberate
    /// and stays; the role is resolved by the caller that knows the axis (see
    /// the min-extent floor in scrollstrip_relayout.cpp, which states the same
    /// inversion).
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
        // Empty-first, the same guard contentRectFor and the preset resolvers
        // take: qBound asserts on an inverted range, and a zero-extent column
        // gives min 1 against max 0. There is nothing to draw an indicator on
        // in that case anyway.
        if (axisExtent <= 0) {
            return QRect();
        }
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
            // Falls out to the shared return below. No default label, so a
            // new enumerator is a compiler warning here rather than silently
            // inheriting Bottom's geometry.
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

/// Wire vocabulary of the DEFAULT-window-height KIND setting. The first
/// three members happen to share WindowHeight::Kind's values (Auto/Fixed/
/// Preset), but this space is NOT that enum and must never be cast to it:
/// ClientDecides has no WindowHeight::Kind counterpart at all, exactly like
/// its width twin. The engine translates with explicit ifs.
enum class DefaultHeightKind : int {
    Auto = 0,
    Fixed = 1,
    Preset = 2,
    /// New windows join their column at the client's own cross extent, the
    /// height twin of DefaultWidthKind::ClientDecides. Appended as 3 because
    /// the other three are load-bearing wire values in stored configs.
    ClientDecides = 3,
};

/// Where a fresh-opened window's new column enters the strip (config
/// default; the openColumnPlacement window rule outranks it). Wire/config
/// encoding is the int value; append only. RightOfActive must stay 0 so an
/// absent key preserves the historical behavior.
enum class ScrollInsertPosition : int {
    RightOfActive = 0,
    LeftOfActive = 1,
    /// The strip's FIRST column.
    First = 2,
    /// The strip's LAST column (niri's append).
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
        /// Fraction of the work area's MAIN extent. Proportions account for
        /// the inter-column gap: 0.5 + 0.5 tile edge-to-edge with one gap
        /// between.
        Proportion = 0,
        /// Absolute pixel MAIN extent.
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

/// Tile height INTENT within a column (its extent ACROSS the strip). Same
/// pixels-are-derived contract as ColumnWidth.
struct WindowHeight
{
    enum Kind : int {
        /// Share the column's CROSS extent left over after Fixed/Preset tiles,
        /// proportionally to weight (the default even split at weight 1).
        Auto = 0,
        /// Absolute pixel CROSS extent.
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

    static constexpr WindowHeight makeAuto(qreal w = 1.0)
    {
        WindowHeight h;
        h.kind = Auto;
        h.weight = w;
        return h;
    }
    static constexpr WindowHeight makeFixed(int px)
    {
        WindowHeight h;
        h.kind = Fixed;
        h.fixedPx = px;
        return h;
    }
    static constexpr WindowHeight makePreset(qreal fraction)
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

/// A proportion resolves against the work extent PLUS one gap so that
/// proportions summing to 1 tile edge-to-edge with the gaps between them
/// (0.5 + 0.5 across a 1000px area with a 10px gap → 495 + 10 + 495).
///
/// Lives here rather than in relayout's anonymous namespace because the
/// preset CYCLES have to resolve a vocabulary entry to the very pixels
/// relayout will lay it out at — a second copy of this rounding is exactly
/// the drift that makes a cycle press land one entry off.
inline int proportionalPx(qreal proportion, int workExtent, int gap)
{
    return qMax(1, qRound(proportion * (workExtent + gap)) - gap);
}

/// The vocabulary index a preset CYCLE press lands on, given what the target
/// currently measures on screen.
///
/// niri's rule (`Column::toggle_width` / `toggle_window_height`), ported: a
/// press moves the size the way the key says, so a forward press takes an
/// entry strictly WIDER/TALLER than the current extent and a backward press
/// one strictly smaller, wrapping at each end. The one-pixel allowance is
/// niri's too, for fractional-scale rounding. Which entry, and which end, are
/// resolved by EXTENT here (nearest on the pressed side; the vocabulary's own
/// smallest and largest for the wraps) rather than by position in the list,
/// which is the divergence the next two paragraphs are about.
///
/// Anchored on the resolved EXTENT rather than on a stored vocabulary index,
/// which is this engine's one deliberate divergence: a stored index lets a
/// short template vocabulary rewrite an anchor's original intent, so the
/// index was removed. For an ascending list (every shipped default, and
/// every list a user would plausibly type) the two rules agree entry for
/// entry — a list typed out of size order cycles in SIZE order here and in
/// TYPED order under niri.
///
/// Both the pick and the WRAP are by extent, not by position, and that is
/// what makes the SIZE-order claim above true. Order is NOT guaranteed: the
/// settings schema's canonicalProportionList deduplicates what a user types
/// but preserves their order, refreshConfigFromSettings' parser and the
/// per-screen override path neither sort nor deduplicate, and only the
/// template channel arrives sorted (phosphor-zones normalizePresetList). So a
/// list typed as e.g. 1/2, 1/3, 2/3 keeps its narrowest entry in the middle.
/// Wrapping to position 0 there would hand a forward press the MIDDLE entry
/// and leave the narrowest reachable only backwards.
///
/// @p resolve maps a vocabulary index to the pixel extent that entry would
/// render at. Returns -1 for an empty vocabulary.
template<typename Resolver>
int cyclePresetIndexByExtent(int count, int currentPx, int delta, Resolver resolve)
{
    if (count <= 0) {
        return -1;
    }
    const bool forward = delta >= 0;
    int best = -1; // nearest entry strictly on the pressed side
    int bestPx = 0; // its extent
    int wrap = -1; // the extreme entry the press wraps to
    int wrapPx = 0; // its extent
    for (int i = 0; i < count; ++i) {
        const int px = resolve(i);
        if (forward) {
            if (currentPx + 1 < px && (best < 0 || px < bestPx)) {
                best = i;
                bestPx = px;
            }
            if (wrap < 0 || px < wrapPx) {
                wrap = i;
                wrapPx = px;
            }
        } else {
            if (px + 1 < currentPx && (best < 0 || px > bestPx)) {
                best = i;
                bestPx = px;
            }
            if (wrap < 0 || px > wrapPx) {
                wrap = i;
                wrapPx = px;
            }
        }
    }
    return best >= 0 ? best : wrap;
}

/// One window in a column.
struct Tile
{
    QString windowId;
    WindowHeight height;
    /// The height this tile carried when toggleMaximizeActiveWindowHeight last
    /// maximized it, so the un-maximize press can put it back instead of
    /// falling to Auto. Empty means "not maximized by that verb", which is
    /// also the answer for a tile that reached full height by another route
    /// (an adjust clamped at the budget, a preset cycled to the top) — those
    /// still un-maximize to Auto, since there is no remembered height to
    /// restore and Auto is the height family's "the column decides".
    ///
    /// Held HERE rather than as a strip-level index the way the width axis
    /// holds m_preMaximizeColumnIdx: an index has to be re-clamped by every
    /// insert, removal, reorder, consume and expel (that bookkeeping is why
    /// this verb originally shipped without a slot at all), while a field on
    /// the tile travels with it through all of those for free, because every
    /// one of those paths moves the Tile struct whole.
    ///
    /// Set ONLY by that verb's maximize arm, and cleared by every other write
    /// that SETTLES a height on the tile. That includes an addressed re-state
    /// landing on the height the tile already held (setActiveWindowHeight and
    /// setWindowHeightIntent both clear above their equality bail: the caller
    /// named a height and got it), and it includes the write
    /// reconcileWindowSize makes when a user finishes an interactive resize,
    /// since the user has chosen a height of their own and there is no
    /// maximize left to undo.
    ///
    /// What does NOT clear it is a press the verb refuses. adjust, the preset
    /// cycle, minimize and expand bail on "this press moved no pixels", and a
    /// refusal is not a countermand — a held-down key sitting at its limit
    /// must not erase the memory. Nor does dropping a column's
    /// maximize-to-edges override on its own: that press reports a change, but
    /// it has not settled a height. Because of that the
    /// standing slot is also what the toggle READS to decide the tile is
    /// maximized: it cannot go stale the way the height itself does when the
    /// budget moves under it. Deliberately
    /// NOT serialized and not carried across a re-insert (migration, unfloat,
    /// stash restore, drag commit), which is the same session-and-place scope
    /// the width axis's slot has — a window that comes back from one of those
    /// un-maximizes to Auto.
    std::optional<WindowHeight> preMaximizeHeight;
    /// Minimized tiles keep their slot/order but are excluded from layout;
    /// unminimize restores the window into the same slot.
    bool minimized = false;
    /// Windowed fullscreen (niri toggle-windowed-fullscreen): the client is
    /// told it is fullscreen while the tile keeps its normal column slot.
    /// Layout ignores this flag entirely — it only rides the apply payload so
    /// the compositor side can flip the client's fullscreen state.
    bool windowedFullscreen = false;
    /// Client-reported minimum size (0 = unconstrained), stored PHYSICALLY as
    /// the compositor reports it. Relayout clamps the resolved rect to it; a
    /// window that cannot honour its slot at all is the engine's cue to float
    /// it instead.
    ///
    /// Layout wants this by ROLE, so read it through minMain/minCross rather
    /// than naming a field: on a vertical strip a client's minimum WIDTH is
    /// what constrains the tile ACROSS the strip, and its minimum HEIGHT is
    /// what constrains the column ALONG it. Picking by name silently applies
    /// each constraint to the wrong axis.
    int minWidth = 0;
    int minHeight = 0;

    /// The client minimum along the strip (a column-width floor).
    int minMain(StripAxis axis) const
    {
        return axis.isHorizontal() ? minWidth : minHeight;
    }
    /// The client minimum across the strip (a stacked-tile floor).
    int minCross(StripAxis axis) const
    {
        return axis.isHorizontal() ? minHeight : minWidth;
    }
};

/// One column: a stack of tiles ACROSS the strip (vertical on a horizontal
/// strip, horizontal on a vertical one).
struct Column
{
    QVector<Tile> tiles;
    int activeTileIdx = 0;
    ColumnWidth width;
    ColumnDisplay display = ColumnDisplay::Normal;
    /// Which tab's height intent decides a TABBED column's cross extent, by
    /// window id. Meaningless while the column is Normal, where every tile's
    /// own height governs its slice of the stack.
    ///
    /// A tabbed column shows one tab at a time, so its extent cannot be "the
    /// shown tab's height" — that would resize the column on every tab switch
    /// and break the compositor's cross-fade, which is built on the arriving
    /// tab occupying the rect the outgoing one vacated. One tab therefore owns
    /// the extent for as long as the column is tabbed.
    ///
    /// Held as an ID rather than an index because tiles are inserted, removed
    /// and reordered underneath it (an index would need re-clamping at every
    /// one of those sites, which is precisely the bookkeeping that rots), and
    /// because an id that no longer names a live tile is self-describing: the
    /// resolver falls back rather than pointing at whatever moved into the
    /// slot. Empty means "no tab has claimed it", which resolves to the
    /// deterministic scan tabbedColumnCrossPx documents.
    ///
    /// This is the ONE place the ownership lives. It used to be inferred —
    /// every height writer rewrote its siblings to Auto so a scan for the
    /// first non-Auto tab would find the right one. That destroyed the
    /// siblings' intents irrecoverably on a tab toggle, and it could not be
    /// maintained at all while the column was Normal, so a Normal column with
    /// several sized tiles handed its extent to whichever tile sat first in
    /// the stack the moment it was tabbed.
    QString heightOwnerId;
    /// Maximize-to-edges (niri parity): the column takes the RAW work area
    /// (ScrollLayoutParams::rawWorkArea) on BOTH axes, with inner gaps
    /// suppressed between its stacked tiles. Declared state, never inferred
    /// from rendered rects — the width intent below stays untouched while the
    /// flag holds, so clearing it is a plain "stop overriding". The user-facing
    /// width and height sizing verbs clear it (the width toggles and presets,
    /// expand, equalize, minimize-width, the interactive resize reconcile, and
    /// the height verbs when they actually change a height or move a tabbed
    /// column's extent owner); display (tab/stack) toggles do not, and neither
    /// do the restore paths that re-state a remembered height intent through
    /// setWindowHeightIntent. resetToDefaults is the deliberate split: it
    /// clears the flag when a default width is supplied and keeps it when the
    /// context's width default is "the client decides". This is the one state
    /// the effect mirrors onto KWin's maximize
    /// bit; toggleMaximizeColumn is a pure width verb with no mirror.
    bool maximizedToEdges = false;

    bool isEmpty() const
    {
        return tiles.isEmpty();
    }
    /// The tile whose height decides this column's cross extent while tabbed,
    /// or nullptr when no live non-minimized tab owns it. Minimized tabs are
    /// refused: they are dropped from the layout entirely, so a height they
    /// carry must not size a column they do not appear in.
    const Tile* heightOwner() const
    {
        if (heightOwnerId.isEmpty()) {
            return nullptr;
        }
        for (const Tile& tile : tiles) {
            if (tile.windowId == heightOwnerId) {
                return tile.minimized ? nullptr : &tile;
            }
        }
        return nullptr;
    }
    /// How many tiles are laid out, i.e. every tile that is not minimized.
    ///
    /// The tab indicator's reserved thickness scales with this (one segment per
    /// visible tab), and the relayout's own visible-tile walk shares the
    /// definition, so it lives beside isFullyMinimized rather than being
    /// re-counted at each site — the two answers must agree, and a count that
    /// drifted from the minimized predicate would size the reservation for
    /// tabs the column does not draw.
    int visibleTileCount() const
    {
        int visible = 0;
        for (const Tile& t : tiles) {
            if (!t.minimized) {
                ++visible;
            }
        }
        return visible;
    }
    /// True when every tile is minimized — the column occupies no strip MAIN
    /// extent.
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
    /// Surviving sibling to anchor a stack re-insert on: the nearest tile
    /// above @p tileIdx with a non-empty id, else the nearest below, else
    /// empty. Shared by the float capture, the minimize round trip and the
    /// drag-preview slot capture, which must agree on the anchor choice.
    QString anchorSiblingFor(int tileIdx) const
    {
        QString anchor;
        for (int i = tileIdx - 1; i >= 0 && anchor.isEmpty(); --i) {
            anchor = tiles.at(i).windowId;
        }
        for (int i = tileIdx + 1; i < tiles.size() && anchor.isEmpty(); ++i) {
            anchor = tiles.at(i).windowId;
        }
        return anchor;
    }
};

/// Inputs a relayout resolves pixel rects against. The work area is the
/// screen's available geometry already shrunk by the outer gaps; @c gap is the
/// inner gap between columns and between tiles in a column.
struct ScrollLayoutParams
{
    QRect workArea;
    /// The screen's available geometry BEFORE the outer-gap shrink and the
    /// smart-gaps zeroing — still strut-adjusted, so panels stay respected.
    /// Only a maximized-to-edges column resolves against it; everything else
    /// reads workArea. Clamped to null exactly like workArea when the screen
    /// is unknown or removed, so the degenerate-area bails cover both.
    QRect rawWorkArea;
    int gap = 0;
    /// Which way this screen's strip runs. Resolved per screen in
    /// layoutParamsForScreen, so a portrait and a landscape monitor in one
    /// session hold different axes at the same time. A resolved axis is never
    /// an INPUT to a new layout: resolve it per screen, per pass, from the
    /// tri-state setting and the live work area, so a rotation or a monitor
    /// swap cannot hand one screen the other's verdict.
    ///
    /// RECORDING the axis a past pass resolved is a different thing, and
    /// deliberate. ScrollState stamps both `lastAppliedAxis` (part of the
    /// view-delta baseline, which is meaningless without knowing which axis
    /// the offset was measured along) and `resolvedAxis` (what the flip sweep
    /// compares against). Those are history, not inputs, and must not be
    /// deleted in the name of the rule above.
    ///
    /// Defaulted to Horizontal so every existing construction, the test
    /// fixtures included, keeps the historical layout with no edit.
    StripAxis axis = StripAxis::horizontal();
    /// Preset proportion lists (the niri 1/3, 1/2, 2/3 plus 3/4 and full). Never empty —
    /// resolvers snap a Preset fraction anchor to the nearest entry.
    /// KEEP IN SYNC with FOUR other copies, not one:
    ///   1. ScrollEngine::m_presetColumnWidths / m_presetWindowHeights, the
    ///      member seeds these mirror (ScrollEngine.h);
    ///   2. ScrollEngine::refreshConfigFromSettings' fallback list
    ///      (engine_core.cpp), the empty-config fallback;
    ///   3. ConfigDefaults' "0.333,0.5,0.667,0.75,1" strings
    ///      (configdefaults_scrolling.h) — the one a CONFIGURED user actually
    ///      gets, and the only one expressed in decimal rather than as
    ///      thirds, so it is already very slightly different by construction;
    ///   4. EditorController::createNewScrollingTemplate's seed for a brand-new
    ///      template (src/editor/controller/scrollingtemplate.cpp). A template's
    ///      list REPLACES this one wholesale on the screens it covers, so a
    ///      stale seed there narrows the cycle rather than diverging quietly.
    /// The ops-suite assertion that pins THIS copy is the 800 one in
    /// reconcileLoneTileRecordsHeightIntent (test_scrollstrip_ops.cpp): a
    /// forward press from 720px only reaches the full-height entry if this list
    /// carries it. The neighbouring literal-260 assertion is the wrap-to-
    /// shortest answer and stays green under any vocabulary that starts at 1/3,
    /// so it pins nothing here. A change to any of the other four copies still
    /// leaves the suite green while the running engine shifts.
    QList<qreal> presetColumnWidths{1.0 / 3.0, 0.5, 2.0 / 3.0, 0.75, 1.0};
    QList<qreal> presetWindowHeights{1.0 / 3.0, 0.5, 2.0 / 3.0, 0.75, 1.0};
    CenterFocusedColumn centerFocusedColumn = CenterFocusedColumn::Never;
    bool alwaysCenterSingleColumn = false;
    /// Whether the strip's layout math honours client minimum sizes (the
    /// column MAIN floor, the tile CROSS floor and its rebalance, and the
    /// interactive-resize floor). Off, the resolved rects obey the user's
    /// intents and the compositor's own min-size enforcement decides what
    /// overhangs. The open-time work-area-oversized float escape ignores
    /// this flag.
    bool respectMinimumSize = true;
    /// Whether a column whose visible tiles resolve to less than the column's
    /// own CROSS extent is centred on that axis instead of hugging the start
    /// edge. Off (the niri behaviour) a solo window with an explicit
    /// Fixed/Preset height sits at the top of its column and the slack is
    /// left below it. A column that already fills the cross axis, the
    /// all-Auto case included, resolves identically either way.
    bool centerShortColumns = false;
    /// Whether main-axis straddlers keep their TRUE rect for the effect to
    /// crop (`true`) instead of being clamped at the screen edge. Resolved
    /// once in layoutParamsForScreen like every other per-screen bool, so
    /// the apply path does not fetch the override map a second time per
    /// batch for this one value.
    bool cropStraddlers = false;
    /// The context's default window height intent, seeded onto every
    /// fresh-created tile (restore paths overwrite it via
    /// setWindowHeightIntent). Default-constructed = Auto weight 1, the
    /// historical even split.
    WindowHeight defaultWindowHeight{};
    /// The context's default column width — the un-maximize fallback for a
    /// full-MAIN-extent column with no stored pre-maximize intent.
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
    /// Copied from Tile::windowedFullscreen so the apply payload can carry
    /// it; never influences the resolved rect.
    bool windowedFullscreen = false;
};

/// One column's resolved output.
struct ResolvedColumn
{
    int columnIndex = -1;
    /// The column's bounding rect. This is the column's FULL extent, before
    /// any within-column indicator reservation — the tiles carry the reduced
    /// rects, and @c tabIndicatorRect carries what was reserved.
    ///
    /// On the CROSS axis this rect always spans the whole work area, even when
    /// centerShortColumns has centred the tiles WITHIN it, so a stack's tiles
    /// may not start where this rect starts. A tabbed column is the exception:
    /// its tiles ride this rect, so centring moves the rect itself. Read the
    /// tile rects, never this one, to ask where a column's windows sit on the
    /// cross axis.
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
    TabIndicatorPosition tabIndicatorPosition = TabIndicatorPosition::Left;
    /// True when the column's main extent came from its MINIMUM rather than
    /// from its stored width intent — its tiles' declared minimum (plus any
    /// main-axis indicator reservation) is at least as wide as the intent.
    /// A consumer publishing "this column is maximized" must not do so off
    /// @c rect alone: a column whose floor already reaches the work area
    /// renders at full extent no matter what the user asks for, so it would
    /// read as permanently maximized and every toggle would report success
    /// while changing nothing the user can see.
    bool extentPinnedByMinimum = false;
    /// The column's declared maximize-to-edges state, copied through from
    /// Column::maximizedToEdges so the apply pass publishes the DECLARED
    /// state instead of measuring the rect (measuring is exactly what the
    /// extentPinnedByMinimum note above warns against).
    bool maximizedToEdges = false;
    QVector<ResolvedTile> tiles;
};

/// A full relayout pass over one strip.
struct ResolvedStrip
{
    QVector<ResolvedColumn> columns;
    /// The viewport's LEADING edge in strip coordinates — what the engine uses
    /// to decide which columns are visible vs parked off-screen.
    int viewOffset = 0;
    /// Total strip MAIN extent in pixels (all non-minimized columns + gaps).
    int stripExtent = 0;
};

/// The columns of @p resolved that intersect @p workArea, in strip order.
///
/// Single definition of "visible" on purpose. The drag-insert hit-test and
/// the edge auto-scroll's target writer both need it, and the auto-scroll's
/// owned target is only coherent with the hit-test while the two agree: if
/// one drifted, the owned target would name a column the hit-test does not
/// consider visible and the painted indicator would disagree with the drop.
/// The returned pointers alias @p resolved and are valid only as long as it is.
inline QVector<const ResolvedColumn*> visibleColumnsOf(const ResolvedStrip& resolved, const QRect& workArea)
{
    QVector<const ResolvedColumn*> visible;
    for (const ResolvedColumn& column : resolved.columns) {
        if (column.rect.intersects(workArea)) {
            visible.append(&column);
        }
    }
    return visible;
}

} // namespace PhosphorScrollEngine

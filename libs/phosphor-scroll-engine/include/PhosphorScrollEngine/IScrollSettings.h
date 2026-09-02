// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QObject>
#include <QStringList>

namespace PhosphorEngine {

/// Settings surface the scrolling engine reads, injected by the daemon via
/// PlacementEngineBase::setEngineSettings (qobject_cast at point of use,
/// mirroring IAutotileSettings). The gap accessors deliberately mirror the
/// shared Tiling.Gaps values — the implementing settings object forwards
/// them, so scrolling and autotile screens read one gap model without this
/// library depending on the tile-engine headers.
class IScrollSettings
{
public:
    virtual ~IScrollSettings() = default;

    virtual int scrollingInnerGap() const = 0;
    virtual bool scrollingUsePerSideOuterGap() const = 0;
    virtual int scrollingOuterGap() const = 0;
    virtual int scrollingOuterGapTop() const = 0;
    virtual int scrollingOuterGapBottom() const = 0;
    virtual int scrollingOuterGapLeft() const = 0;
    virtual int scrollingOuterGapRight() const = 0;
    /// Whether newly opened windows take focus (Scrolling.Behavior).
    virtual bool scrollingFocusNewWindows() const = 0;
    /// StickyWindowHandling as int (0 = treat as normal, 1 = restore only,
    /// 2 = ignore all) — the shared PhosphorEngine enum's wire values.
    /// RestoreOnly and IgnoreAll both keep sticky windows out of the strip
    /// (insertion is active management); the engine's desktop-pin logic is
    /// deliberately not gated on this.
    virtual int scrollingStickyWindowHandling() const = 0;
    /// Whether the strip's layout math honours client minimum sizes (column
    /// width floor, tile height floor, interactive-resize floor). The
    /// work-area-oversized float escape ignores this and always fires.
    virtual bool scrollingRespectMinimumSize() const = 0;
    /// Whether a column whose windows do not fill the cross axis is centred
    /// on it instead of hugging the start edge. See
    /// ScrollLayoutParams::centerShortColumns. DEFAULTED, not pure: false —
    /// the historical start-edge layout — is the answer for every implementor
    /// that has not heard of the option, including the test stubs.
    virtual bool scrollingCenterShortColumns() const
    {
        return false;
    }
    /// Zero the outer gaps when the strip holds a single column.
    ///
    /// Scrolling's OWN setting, not the tiling one: the gap VALUES above are
    /// shared and forwarded, but smart gaps is a per-mode behaviour and each
    /// mode keeps its own. The config default is OFF, unlike tiling's — a sole
    /// column sits at its own width rather than filling the screen, so the
    /// tiling rationale for stripping the gaps does not carry over. (Pure
    /// virtual, so there is no default HERE to speak of; the value lives in
    /// ConfigDefaults::scrollingSmartGaps.)
    virtual bool scrollingSmartGaps() const = 0;

    /// CenterFocusedColumn as int (0 = never, 1 = always, 2 = on-overflow).
    virtual int scrollingCenterFocusedColumn() const = 0;
    virtual bool scrollingAlwaysCenterSingleColumn() const = 0;
    /// Crop mode for partial edge columns (see ConfigDefaults). DEFAULTED,
    /// not pure: the safe clamp is the answer for every implementor that has
    /// not heard of the option, including the test stubs.
    virtual bool scrollingCropStraddlers() const
    {
        return false;
    }
    /// Which way the strip runs: 0 auto (from the work area), 1 horizontal,
    /// 2 vertical. DEFAULTED for the same reason cropStraddlers is — auto is
    /// the answer for every implementor that has not heard of the option,
    /// including the test stubs, and it reproduces the behaviour that existed
    /// before the setting.
    ///
    /// NOTE the numbering is the INTENT vocabulary, not the resolved
    /// PhosphorProtocol::ScrollAxis (whose Horizontal is 0). Never cast
    /// between them.
    virtual int scrollingStripAxis() const
    {
        return 0;
    }
    /// Default width for new columns: kind (0 = proportion, 1 = fixed px,
    /// 2 = client decides, 3 = preset index) + value (proportion in [0,1]
    /// or pixels) + the preset index the Preset kind resolves.
    /// Values 2 and 3 mean the OPPOSITE of what they mean in the height
    /// vocabulary below. Both numberings are load-bearing wire values in
    /// stored configs (each kind was appended when it was added, to a
    /// different existing set), so the mismatch is deliberate and neither
    /// side can be renumbered to match the other.
    virtual int scrollingDefaultColumnWidthKind() const = 0;
    virtual qreal scrollingDefaultColumnWidthValue() const = 0;
    virtual int scrollingDefaultColumnWidthPresetIndex() const = 0;
    /// Default height intent for fresh tiles: kind (0 = auto, 1 = fixed px,
    /// 2 = preset index, 3 = client decides) + fixed pixel value + preset
    /// index. The kind space is DefaultHeightKind, NOT WindowHeight::Kind —
    /// client-decides has no counterpart there.
    virtual int scrollingDefaultWindowHeightKind() const = 0;
    virtual qreal scrollingDefaultWindowHeightValue() const = 0;
    virtual int scrollingDefaultWindowHeightPresetIndex() const = 0;
    /// ScrollInsertPosition as int (0 = right of active, 1 = left of
    /// active, 2 = first, 3 = last, 4 = into active column).
    virtual int scrollingInsertPosition() const = 0;
    /// ColumnDisplay new columns open in (0 = normal, 1 = tabbed).
    virtual int scrollingDefaultColumnDisplay() const = 0;

    // ── Drag-insert edge auto-scroll (Scrolling.Behavior.DragScroll) ─────
    //
    // niri's dnd-edge-view-scroll: holding a dragged window near either
    // screen edge scrolls the strip so an off-screen column can be reached.
    // DEFAULTED, not pure, like scrollingCropStraddlers — niri's own
    // defaults are the right answer for every implementor that has not
    // heard of the option, the test stubs included.

    // niri's four figures, named so the defaulted bodies below and
    // ScrollEngine's pre-refresh member cache read the SAME constants rather
    // than three hand-copied sets. src/config/settings/scrolling.cpp
    // static_asserts ConfigDefaults against these, which is what keeps the
    // config layer and the engine layer agreeing.
    static constexpr bool kDragScrollEnabledDefault = true;
    static constexpr int kDragScrollTriggerWidthDefault = 30;
    static constexpr int kDragScrollDelayMsDefault = 100;
    static constexpr int kDragScrollMaxSpeedDefault = 1500;
    /// Upper bound the engine enforces on the speed. The config schema clamps
    /// there too, but the schema only governs the daemon's own settings
    /// object, and an unbounded speed teleports the strip in a single tick.
    static constexpr int kDragScrollMaxSpeedCeiling = 10000;

    /// Master switch. Off, the edge bands are inert and the drag behaves
    /// exactly as it did before the feature existed.
    virtual bool scrollingDragScrollEnabled() const
    {
        return kDragScrollEnabledDefault;
    }
    /// Width of the band inside each work-area edge that arms the scroll,
    /// in logical pixels. Speed ramps linearly from zero at the band's
    /// inner edge to the maximum at the work area's edge. The engine
    /// additionally clamps the value to a third of the work area's MAIN
    /// extent, so the two bands always leave a neutral zone to aim from —
    /// a configured width past that is silently narrowed.
    virtual int scrollingDragScrollTriggerWidth() const
    {
        return kDragScrollTriggerWidthDefault;
    }
    /// How long the cursor must sit inside the band before the strip starts
    /// moving. Stops a drag that merely passes near an edge from scrolling.
    virtual int scrollingDragScrollDelayMs() const
    {
        return kDragScrollDelayMsDefault;
    }
    /// Scroll speed at the very edge of the work area, in logical pixels
    /// per second.
    virtual int scrollingDragScrollMaxSpeed() const
    {
        return kDragScrollMaxSpeedDefault;
    }
    /// Preset proportion lists, serialized as decimal strings. These are the
    /// FALLBACK vocabulary: a screen whose context resolves a template layout
    /// gets a per-screen replacement list pushed through the TEMPLATE channel
    /// (ScrollPerScreenKeys::presetColumnWidths / presetWindowHeights), which
    /// wholesale replaces the matching list here for that screen.
    virtual QStringList scrollingPresetColumnWidths() const = 0;
    virtual QStringList scrollingPresetWindowHeights() const = 0;

    // ── Tab indicator geometry (Scrolling.TabIndicator) ──────────────────
    //
    // Only the subset that changes resolved rects lives here; it lands in
    // ScrollLayoutParams::tabIndicator (TabIndicatorParams, ScrollTypes.h,
    // where each field is documented in full). The indicator's PAINT settings
    // — style, gaps between tabs, corner radius, the three colours and the
    // five label-font keys (family, weight, italic, underline, strikeout) —
    // are deliberately absent: they never affect layout, so they go to the
    // KWin effect, which draws the indicator itself. The global keys travel
    // over the settings channel the effect pulls; per-screen rule overrides
    // are pushed by the daemon through
    // TilingAdaptor::setScrollTabPaintOverrides. Keep that split when adding a
    // knob, or this library grows a dependency on how the indicator happens to
    // be drawn.

    /// Master switch. Off, no indicator rect resolves and nothing is reserved.
    virtual bool scrollingTabIndicatorEnabled() const = 0;
    /// Skip the indicator for a tabbed column holding a single tile.
    virtual bool scrollingTabIndicatorHideWhenSingleTab() const = 0;
    /// Reserve the indicator out of the column instead of drawing beside it.
    virtual bool scrollingTabIndicatorPlaceWithinColumn() const = 0;
    /// Gap between indicator and window; NEGATIVE puts it over the window.
    virtual int scrollingTabIndicatorGap() const = 0;
    /// Indicator thickness. EXACT for every style, chips included: content
    /// that does not fit clips rather than growing the indicator, which is what
    /// keeps PlaceWithinColumn's reservation honest. See TabIndicatorParams.
    virtual int scrollingTabIndicatorWidth() const = 0;
    /// Indicator length as a proportion of the column extent beside it.
    virtual qreal scrollingTabIndicatorLengthProportion() const = 0;
    /// Which column edge the indicator runs along (TabIndicatorPosition).
    virtual int scrollingTabIndicatorPosition() const = 0;
};

} // namespace PhosphorEngine

Q_DECLARE_INTERFACE(PhosphorEngine::IScrollSettings, "org.plasmazones.IScrollSettings")

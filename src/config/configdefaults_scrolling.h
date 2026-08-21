// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "configdefaults_screens.h"

// Explicit rather than transitive: the drop indicator's corner-radius default
// IS the zone overlay's, so this header genuinely depends on that symbol.
#include <PhosphorZones/ZoneDefaults.h>

namespace PlasmaZones {

// Chain link 6: the scrolling engine's Scrolling defaults, including the
// Scrolling.ZoneSelector strip popup. Split out of configdefaults.h to keep
// that file inside the size ceiling; the Shortcuts.Scrolling chord defaults
// were split again into configdefaults_scrolling_shortcuts.h (link 7) for the
// same reason. Every accessor here reaches call sites through the
// ConfigDefaults leaf as before, so no consumer changes.
class ConfigDefaultsScrolling : public ConfigDefaultsScreens
{
public:
    // ═══════════════════════════════════════════════════════════════════════════
    // Scrolling (Scrolling)
    // ═══════════════════════════════════════════════════════════════════════════

    /// Master switch for the scrolling placement mode, the peer of
    /// snappingEnabled and autotileEnabled. Off, a Scrolling context
    /// downgrades to Snapping in the daemon's derive pass and the mode
    /// toggle skips the mode.
    static constexpr bool scrollingEnabled()
    {
        return true;
    }

    /// CenterFocusedColumn wire values. Named so the predicate below and any
    /// settings-layer branch read against the vocabulary instead of raw ints,
    /// matching the width-kind accessors further down. The schema declares the
    /// same three values through the engine's own enumerators, and
    /// settingsschema_scrolling.cpp static_asserts the two spellings agree.
    static constexpr int scrollingCenterFocusedColumnNever()
    {
        return 0;
    }
    static constexpr int scrollingCenterFocusedColumnAlways()
    {
        return 1;
    }
    static constexpr int scrollingCenterFocusedColumnOnOverflow()
    {
        return 2;
    }
    /// CenterFocusedColumn: 0 = never, 1 = always, 2 = on overflow.
    static constexpr int scrollingCenterFocusedColumn()
    {
        return scrollingCenterFocusedColumnNever();
    }
    /// Closed-set validity check for the D-Bus registry guard. The schema's
    /// validIntOr closed set is spelled SEPARATELY (settingsschema_scrolling
    /// builds its own list, in places from the engine enumerators), so the
    /// two lists are maintained IN PARALLEL and adding an enum value means
    /// editing both — forgetting one lets the D-Bus guard accept a value the
    /// schema snaps back to default, or the reverse. (A range check would be
    /// worse still: it silently accepts any hole a future enum leaves.) The
    /// same parallel-maintenance rule applies to every isValidScrolling*
    /// predicate below that names this comment.
    static constexpr bool isValidScrollingCenterFocusedColumn(int v)
    {
        return v == scrollingCenterFocusedColumnNever() || v == scrollingCenterFocusedColumnAlways()
            || v == scrollingCenterFocusedColumnOnOverflow();
    }
    /// Which way the strip runs on a screen. THREE values, and the numbering
    /// deliberately does NOT match PhosphorProtocol::ScrollAxis (whose
    /// Horizontal is 0): this is the INTENT, which has an Auto the resolved
    /// wire enum cannot express, and Auto must be 0 so an absent key reads as
    /// it. NEVER static_cast between the two — same trap, and the same
    /// discipline, as DefaultWidthKind vs ColumnWidth::Kind.
    static constexpr int scrollingStripAxisAuto()
    {
        return 0;
    }
    static constexpr int scrollingStripAxisHorizontal()
    {
        return 1;
    }
    static constexpr int scrollingStripAxisVertical()
    {
        return 2;
    }
    /// StripAxis: 0 = auto (from the work area), 1 = horizontal, 2 = vertical.
    static constexpr int scrollingStripAxis()
    {
        return scrollingStripAxisAuto();
    }
    /// Closed-set check, maintained in parallel with the schema's own list —
    /// see isValidScrollingCenterFocusedColumn's note above.
    static constexpr bool isValidScrollingStripAxis(int v)
    {
        return v == scrollingStripAxisAuto() || v == scrollingStripAxisHorizontal()
            || v == scrollingStripAxisVertical();
    }
    static constexpr bool scrollingAlwaysCenterSingleColumn()
    {
        return false;
    }
    /// Crop mode for partial edge columns: keep the TRUE column rect and let
    /// the compositor crop the overhang at the screen edge, instead of the
    /// default clamp that resizes the window at the boundary. Off by default
    /// because its safety depends on the effect forcing GL composition
    /// (blocksDirectScanout) actually covering the running hardware's
    /// scanout paths, which the clamp needs no assumption about.
    static constexpr bool scrollingCropStraddlers()
    {
        return false;
    }
    /// Width-kind wire values (0 = proportion, 1 = fixed px, 2 = client
    /// decides, 3 = preset index). Named so the settings layer's
    /// kind-aware branches read against the vocabulary instead of raw ints;
    /// the LGPL engine keeps its own interpretation of the same values
    /// (IScrollSettings docs).
    static constexpr int scrollingWidthKindProportion()
    {
        return 0;
    }
    static constexpr int scrollingWidthKindFixed()
    {
        return 1;
    }
    static constexpr int scrollingWidthKindClientDecides()
    {
        return 2;
    }
    /// Preset kind, appended as 3 (2 is taken by ClientDecides; the engine's
    /// DefaultWidthKind::Preset carries the same value and the schema
    /// static_asserts the pair).
    static constexpr int scrollingWidthKindPreset()
    {
        return 3;
    }
    /// Default column width kind: 0 = proportion, 1 = fixed px,
    /// 2 = client decides, 3 = preset index.
    static constexpr int scrollingDefaultColumnWidthKind()
    {
        return scrollingWidthKindProportion();
    }
    /// Closed-set validity check (see isValidScrollingCenterFocusedColumn).
    static constexpr bool isValidScrollingWidthKind(int v)
    {
        return v == scrollingWidthKindProportion() || v == scrollingWidthKindFixed()
            || v == scrollingWidthKindClientDecides() || v == scrollingWidthKindPreset();
    }
    /// Preset index the Preset width kind opens columns at, clamped by the
    /// schema into [0, presetIndexMax] and by the engine against the live
    /// preset list.
    static constexpr int scrollingDefaultColumnWidthPresetIndex()
    {
        return 0;
    }
    /// Schema-level ceiling for stored preset indices: the preset-list
    /// canonicalizer caps lists at 16 entries (settingsschema_p.h
    /// kMaxPresetEntries), so 15 is the largest index that can ever
    /// resolve. The engine re-clamps against the actual list length.
    static constexpr int scrollingPresetIndexMax()
    {
        return 15;
    }
    /// Value paired with the kind: a proportion in (0, 1] or a pixel width.
    static constexpr qreal scrollingDefaultColumnWidthValue()
    {
        return 0.5;
    }
    /// Proportion-kind floor, the twin of scrollingDefaultColumnWidthProportionMax
    /// below. It doubles as the schema clampDouble's lower bound for the shared
    /// value key, since a pixel width can never be smaller than a proportion,
    /// but it bounds the PROPORTION range — the fixed range has its own floor
    /// in scrollingDefaultColumnWidthFixedMin.
    /// NOTE the D-Bus DocStrings in dbus/org.plasmazones.Scrolling.xml spell
    /// these bounds (and the fixed/height ranges below) as literals, kept in
    /// sync BY HAND — retuning any of them means updating that XML too.
    static constexpr qreal scrollingDefaultColumnWidthProportionMin()
    {
        return 0.05;
    }
    /// Proportion-kind ceiling (100% of the work area). The QML slider and
    /// the schema's proportion-list canonicalizer bound against the same
    /// value conceptually; this accessor is the C++ home for it.
    static constexpr qreal scrollingDefaultColumnWidthProportionMax()
    {
        return 1.0;
    }
    /// Fixed-kind pixel floor. Enforced in two places, because the shared
    /// value key means the schema's clampDouble has to span both kinds and
    /// cannot enforce either: the hand-written setter (which the D-Bus
    /// registry routes through), and Settings::normalizeScrollingColumnWidthValue,
    /// called from load(), applyConfigOverlayStaged, discardKeys and resetKeys.
    /// Between them those cover every way a value reaches the store: a hand
    /// edit, a config import, the Discard reload, a per-page Reset, and
    /// profile staging. The QML SpinBox reads
    /// its bounds from here via SettingsController::scrollingConstants().
    /// The engine's qMax(1, …) keeps any bypass value renderable.
    static constexpr qreal scrollingDefaultColumnWidthFixedMin()
    {
        return 100.0;
    }
    /// Fixed-kind pixel ceiling. Also the schema clampDouble's upper bound
    /// (deliberately: the shared value key spans both kinds, so the schema
    /// clamp uses the WIDER fixed range and the kind-aware setter owns the
    /// real per-kind bounds).
    static constexpr qreal scrollingDefaultColumnWidthFixedMax()
    {
        return 10000.0;
    }
    /// Pixel width seeded when the kind flips to Fixed while a proportion
    /// is stored (the shared value key serves both kinds).
    static constexpr qreal scrollingDefaultColumnWidthFixedPx()
    {
        return 800.0;
    }
    /// Editing granularity for the two width controls, shipped to QML through
    /// SettingsController::scrollingConstants(). The proportion step is a
    /// work-area fraction (5% per slider notch); the fixed step is whole
    /// pixels. Both are doubles so the pair travels through one QVariantMap
    /// shape alongside the min/max accessors above.
    static constexpr qreal scrollingDefaultColumnWidthProportionStep()
    {
        return 0.05;
    }
    static constexpr qreal scrollingDefaultColumnWidthFixedStep()
    {
        return 10.0;
    }
    /// ColumnDisplay wire values (0 = normal, 1 = tabbed). Named for the same
    /// reason as the CenterFocusedColumn values above.
    static constexpr int scrollingColumnDisplayNormal()
    {
        return 0;
    }
    static constexpr int scrollingColumnDisplayTabbed()
    {
        return 1;
    }
    /// ColumnDisplay new columns open in: 0 = normal, 1 = tabbed.
    static constexpr int scrollingDefaultColumnDisplay()
    {
        return scrollingColumnDisplayNormal();
    }
    /// Closed-set validity check (see isValidScrollingCenterFocusedColumn).
    static constexpr bool isValidScrollingColumnDisplay(int v)
    {
        return v == scrollingColumnDisplayNormal() || v == scrollingColumnDisplayTabbed();
    }
    /// Preset proportion lists, comma-joined decimals (the niri defaults).
    /// KEEP IN SYNC with the other THREE copies of the {1/3, 1/2, 2/3}
    /// intent — ScrollLayoutParams' member seeds document the full four-copy
    /// map (ScrollTypes.h, presetColumnWidths). Spelled separately because
    /// the LGPL engine cannot include this GPL header.
    static QString scrollingPresetColumnWidths()
    {
        return QStringLiteral("0.333,0.5,0.667");
    }
    static QString scrollingPresetWindowHeights()
    {
        return QStringLiteral("0.333,0.5,0.667");
    }
    /// DEFAULT scrolling template id (braced uuid string), consulted when a
    /// Scrolling context's cascade entry names no template. Empty = none:
    /// the engine then runs on its compiled defaults.
    static QString scrollingDefaultTemplate()
    {
        return QString();
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Tab indicator (Scrolling.TabIndicator)
    //
    // The indicator drawn alongside a tabbed column. Numeric defaults follow
    // niri's tab-indicator section (gap 5, width 4, length 0.5,
    // gaps-between-tabs 0) so a user coming from niri lands on familiar
    // proportions. EVERY default here is niri's, including Style and Position:
    // the segment bar on the column's left edge is the only indicator niri
    // has, so it is what "default" should mean. The title-chip style is a
    // PlasmaZones addition with no upstream equivalent and is one setting
    // away; a user who picks it usually wants Position Top with it, since a
    // title reads better across than down.
    //
    // The geometry keys (Enabled, HideWhenSingleTab, PlaceWithinColumn, Gap,
    // Width, LengthProportion, Position) reach the LGPL engine through
    // IScrollSettings because they change the resolved column rect. The paint
    // keys (Style, GapsBetweenTabs, CornerRadius, the three colours) never
    // enter the engine — the daemon reads them straight onto the overlay.
    // ═══════════════════════════════════════════════════════════════════════════

    /// Master switch. Off, the indicator hides and stays hidden until
    /// re-enabled (tabbed columns still work; they just carry no on-screen
    /// indicator). niri's `tab-indicator { off }`.
    static constexpr bool scrollingTabIndicatorEnabled()
    {
        return true;
    }
    /// Style wire values: 0 = title chips (the PlasmaZones pill, one labelled
    /// chip per tab), 1 = segment bar (niri's thin run of coloured segments).
    /// Named for the same reason as the CenterFocusedColumn values above.
    static constexpr int scrollingTabIndicatorStyleChips()
    {
        return 0;
    }
    static constexpr int scrollingTabIndicatorStyleBar()
    {
        return 1;
    }
    /// The segment bar, which is niri's own indicator and the only one it has.
    /// Chips are a PlasmaZones addition with no upstream equivalent, so they
    /// are the opt-in rather than the default.
    static constexpr int scrollingTabIndicatorStyle()
    {
        return scrollingTabIndicatorStyleBar();
    }
    /// Closed-set validity check (see isValidScrollingCenterFocusedColumn).
    static constexpr bool isValidScrollingTabIndicatorStyle(int v)
    {
        return v == scrollingTabIndicatorStyleChips() || v == scrollingTabIndicatorStyleBar();
    }
    /// Position wire values, niri's TabIndicatorPosition 1:1 in its own
    /// declaration order. The engine mirrors these as TabIndicatorPosition and
    /// settingsschema_scrolling.cpp static_asserts the two spellings agree.
    static constexpr int scrollingTabIndicatorPositionLeft()
    {
        return 0;
    }
    static constexpr int scrollingTabIndicatorPositionRight()
    {
        return 1;
    }
    static constexpr int scrollingTabIndicatorPositionTop()
    {
        return 2;
    }
    static constexpr int scrollingTabIndicatorPositionBottom()
    {
        return 3;
    }
    /// Left, niri's own default. A user switching to the chips style will
    /// usually want Top with it, since a title reads better across than down.
    static constexpr int scrollingTabIndicatorPosition()
    {
        return scrollingTabIndicatorPositionLeft();
    }
    /// Closed-set validity check (see isValidScrollingCenterFocusedColumn).
    static constexpr bool isValidScrollingTabIndicatorPosition(int v)
    {
        return v == scrollingTabIndicatorPositionLeft() || v == scrollingTabIndicatorPositionRight()
            || v == scrollingTabIndicatorPositionTop() || v == scrollingTabIndicatorPositionBottom();
    }
    /// Hide the indicator for a tabbed column holding a single window. Off by
    /// default, matching niri, so a one-window tabbed column still advertises
    /// that it is tabbed.
    static constexpr bool scrollingTabIndicatorHideWhenSingleTab()
    {
        return false;
    }
    /// Draw the indicator INSIDE the column, shrinking the window rect by
    /// (width + gap) on the indicator's side, rather than outside it where it
    /// can overlay a neighbouring column or run off-screen. This is the one
    /// key in the family that moves windows, so it is read by the engine's
    /// relayout rather than by the overlay.
    static constexpr bool scrollingTabIndicatorPlaceWithinColumn()
    {
        return false;
    }
    /// Gap between the indicator and the window, in logical pixels. NEGATIVE
    /// IS MEANINGFUL and matches niri: it pulls the indicator on top of the
    /// window instead of away from it, so the floor is negative rather than 0.
    ///
    /// Placed OUTSIDE the column (the shipped PlaceWithinColumn is off), the
    /// indicator's far edge lands `gap + thickness` beyond the column edge, so
    /// the two together have to fit the gutter between columns or the bar ends
    /// up flush against — or over — the neighbouring window. niri's own 5 is
    /// sized for niri's 16 px gaps; ours are Defaults::InnerGap, so the figure
    /// is DERIVED rather than copied: half of whatever the gutter has left
    /// after the bar's thickness, which centres the bar in it and leaves the
    /// same air on both sides. Clamped at 0 so a gutter narrower than the bar
    /// still yields a placement gap rather than pulling the bar onto its own
    /// window. The chips style is far thicker than any gutter and is expected
    /// to be paired with PlaceWithinColumn or a Top/Bottom position, so it is
    /// not what this figure is sized for.
    static constexpr int scrollingTabIndicatorGap()
    {
        const int spare = Defaults::InnerGap - scrollingTabIndicatorWidthForBar();
        return spare > 0 ? spare / 2 : 0;
    }
    static constexpr int scrollingTabIndicatorGapMin()
    {
        return -64;
    }
    static constexpr int scrollingTabIndicatorGapMax()
    {
        return 64;
    }
    /// Thickness each STYLE wants, in logical pixels. One stored key serves
    /// both, so these are what the style setter re-seeds between (see
    /// Settings::setScrollingTabIndicatorStyle): a bar is a few pixels of
    /// colour, while a chip has to hold a title, and a value that suits one
    /// is unusable for the other.
    ///
    /// The bar figure is niri's. The chip figure is a comfortable line box at
    /// the system's small font size, which is what the painter starts its fit
    /// from. The label is fitted to this thickness rather than the other way
    /// round, so a user who wants bigger text raises the Width setting; there
    /// is no font scale to raise.
    static constexpr int scrollingTabIndicatorWidthForBar()
    {
        return 4;
    }
    static constexpr int scrollingTabIndicatorWidthForChips()
    {
        return 28;
    }
    /// The thickness @p style wants. Unknown styles answer with the shipped
    /// default rather than asserting: this is a re-seed hint, not a validator.
    /// Spelled as a test for CHIPS so that arm stays true — the fallback must
    /// be the shipped style's thickness, and Bar is what ships.
    static constexpr int scrollingTabIndicatorWidthForStyle(int style)
    {
        return style == scrollingTabIndicatorStyleChips() ? scrollingTabIndicatorWidthForChips()
                                                          : scrollingTabIndicatorWidthForBar();
    }
    /// Indicator thickness in logical pixels, its short axis. EXACT for every
    /// style: the chips honour it too, and content that does not fit clips.
    /// That is what makes PlaceWithinColumn correct — the engine reserves this
    /// many pixels out of the column, so an indicator that sized itself to its
    /// own font would draw outside the band it was given.
    ///
    /// Derived from the DEFAULT style rather than written out, so the two can
    /// never disagree, and re-seeded by the style setter when the user flips
    /// styles without having chosen a thickness of their own.
    static constexpr int scrollingTabIndicatorWidth()
    {
        return scrollingTabIndicatorWidthForStyle(scrollingTabIndicatorStyle());
    }
    static constexpr int scrollingTabIndicatorWidthMin()
    {
        return 1;
    }
    static constexpr int scrollingTabIndicatorWidthMax()
    {
        return 64;
    }
    /// Indicator length along its long axis, as a proportion of the column
    /// extent it runs beside — niri's `length total-proportion`.
    static constexpr qreal scrollingTabIndicatorLengthProportion()
    {
        return 0.5;
    }
    static constexpr qreal scrollingTabIndicatorLengthProportionMin()
    {
        return 0.05;
    }
    static constexpr qreal scrollingTabIndicatorLengthProportionMax()
    {
        return 1.0;
    }
    static constexpr qreal scrollingTabIndicatorLengthProportionStep()
    {
        return 0.05;
    }
    /// Gap between individual tabs, in logical pixels.
    static constexpr int scrollingTabIndicatorGapsBetweenTabs()
    {
        return 0;
    }
    static constexpr int scrollingTabIndicatorGapsBetweenTabsMin()
    {
        return 0;
    }
    static constexpr int scrollingTabIndicatorGapsBetweenTabsMax()
    {
        return 64;
    }
    /// Per-tab corner radius in logical pixels, with ONE sentinel: the floor
    /// value below means "fully rounded", i.e. half the tab's short extent.
    /// The sentinel exists because no fixed pixel radius tracks a chip whose
    /// height follows the user's overlay font, so "fully rounded" is a value
    /// only the renderer can resolve. The settings page spells it as a "Fully
    /// rounded" toggle rather than showing -1 in a spin box.
    static constexpr int scrollingTabIndicatorCornerRadiusPill()
    {
        return -1;
    }
    /// Square, niri's own default. The sentinel above is what a chips user
    /// reaches for; it is not the shipped value.
    static constexpr int scrollingTabIndicatorCornerRadius()
    {
        return 0;
    }
    static constexpr int scrollingTabIndicatorCornerRadiusMin()
    {
        return scrollingTabIndicatorCornerRadiusPill();
    }
    static constexpr int scrollingTabIndicatorCornerRadiusMax()
    {
        return 64;
    }
    /// Tab colours. EMPTY MEANS "follow the theme" — the overlay falls back to
    /// Kirigami.Theme (highlight for active, a translucent text colour for
    /// inactive, negative-text for urgent), which is niri's third resolution
    /// tier ("the colour matching the window border or focus ring") expressed
    /// in Plasma's vocabulary. A rule-level override outranks these, matching
    /// niri's order: window rule, then layout config, then theme.
    static QString scrollingTabIndicatorActiveColor()
    {
        return QString();
    }
    static QString scrollingTabIndicatorInactiveColor()
    {
        return QString();
    }
    /// Urgent tabs need the window-urgency channel to be live; with no urgent
    /// window this colour simply never resolves.
    static QString scrollingTabIndicatorUrgentColor()
    {
        return QString();
    }
    /// The font the tab labels are drawn in. EMPTY MEANS THE SYSTEM FONT: the
    /// painter asks the platform for its default family rather than naming one
    /// here, so a user who never touches this follows their desktop font.
    ///
    /// This family exists so the pills stop borrowing the snapping zone-label
    /// font, which is a different surface with different sizing pressure. That
    /// decoupling is not free for everyone: an install that CUSTOMISED the
    /// zone-label font loses that choice here in every dimension, because the
    /// pills no longer read it at all. Only an install that left it alone sees
    /// the same typeface and weight as before.
    ///
    /// THERE IS NO SIZE KEY, on purpose. The pill's thickness comes from the
    /// Width setting, and the painter fits the label to the band it was given.
    /// A size of its own would let the text draw outside that band, which is
    /// the same reasoning that makes Width exact for every style. This is the
    /// one dimension that changes for EVERY install: the labels no longer
    /// follow the zone labels' FontSizeScale, which now moves those labels
    /// alone.
    static QString scrollingTabIndicatorFontFamily()
    {
        return QString();
    }
    /// Bold, which is the weight the pills used to inherit from the snapping
    /// zone label, so an install that left that font alone keeps the weight it
    /// had. See the family accessor above for what does NOT carry over.
    ///
    /// Whether it renders as bold also depends on the platform font itself:
    /// the painter clears the inherited style name before applying this, so a
    /// system font that shipped one now takes a weight it previously ignored.
    static constexpr int scrollingTabIndicatorFontWeight()
    {
        return 700;
    }
    static constexpr int scrollingTabIndicatorFontWeightMin()
    {
        return 100;
    }
    static constexpr int scrollingTabIndicatorFontWeightMax()
    {
        return 900;
    }
    static constexpr bool scrollingTabIndicatorFontItalic()
    {
        return false;
    }
    static constexpr bool scrollingTabIndicatorFontUnderline()
    {
        return false;
    }
    static constexpr bool scrollingTabIndicatorFontStrikeout()
    {
        return false;
    }
    // ═══════════════════════════════════════════════════════════════════════════
    // Scrolling.DropIndicator — the drop-target highlight painted during a drag
    // re-insert. EVERY key here is PAINT-only: they never enter the engine, which
    // resolves the indicator's rect from the same layout math the drop uses.
    // ═══════════════════════════════════════════════════════════════════════════

    /// Master switch. Off, a scrolling drag re-insert runs exactly as before
    /// with no on-screen drop target. On by default because the scroll engine
    /// defers structure to the drop, so without the indicator nothing shows
    /// where the window is going.
    static constexpr bool scrollingDropIndicatorEnabled()
    {
        return true;
    }
    /// Fill and border colours. EMPTY MEANS "follow the theme", resolved the
    /// way the zone quartet resolves it: Settings::resolvedSystemColor reads
    /// the live palette and the getters hand the overlay a concrete colour, so
    /// the sentinel never leaves the config layer. Two colours rather than one
    /// so the highlight can be a tinted fill with a contrasting edge, which is
    /// how the snapping zone overlay spells the same idea (Highlight + Border).
    static QString scrollingDropIndicatorColor()
    {
        return QString();
    }
    static QString scrollingDropIndicatorBorderColor()
    {
        return QString();
    }
    /// The resolution fallback for both colours when there is no GUI
    /// application to take a palette from (headless config tools), the peer of
    /// the zone quartet's *FallbackColor accessors. NOT the stored default,
    /// which is the empty sentinel above.
    ///
    /// OPAQUE, unlike ZoneDefaults::HighlightColor's own half alpha: the fill's
    /// alpha comes from the opacity slider, which replaces whatever the colour
    /// carries, and the border has no slider at all, so an alpha here would
    /// only ever show up as an unset border quietly drawing half transparent.
    static QColor scrollingDropIndicatorFallbackColor()
    {
        QColor color = ::PhosphorZones::ZoneDefaults::HighlightColor;
        color.setAlpha(255);
        return color;
    }
    /// Fill opacity. Replaces the fill colour's own alpha, matching the
    /// snapping zone overlay's fill, so the fill can be faint enough to read
    /// the windows underneath. The border instead honours its colour's alpha
    /// channel directly; with an opaque theme fallback the edge stays crisp
    /// unless the user picks a translucent border colour on purpose.
    ///
    /// One opacity, not the snapping overlay's active/inactive pair: there is
    /// exactly one drop target at a time, so there is no inactive state to
    /// give a second value to.
    static constexpr qreal scrollingDropIndicatorOpacity()
    {
        return 0.25;
    }
    static constexpr qreal scrollingDropIndicatorOpacityMin()
    {
        return 0.0;
    }
    static constexpr qreal scrollingDropIndicatorOpacityMax()
    {
        return 1.0;
    }
    /// Border width and corner radius, in px. Bounds AND the radius default
    /// mirror the snapping zone overlay's (Snapping.Zones.Border, whose radius
    /// default is ZoneDefaults::BorderRadius) so the two highlights cannot be
    /// given visually incompatible ranges or land on different roundings out
    /// of the box. Zero width is legal and means a fill with no edge.
    static constexpr int scrollingDropIndicatorBorderWidth()
    {
        return ::PhosphorZones::ZoneDefaults::BorderWidth;
    }
    static constexpr int scrollingDropIndicatorBorderWidthMin()
    {
        return 0;
    }
    static constexpr int scrollingDropIndicatorBorderWidthMax()
    {
        return 10;
    }
    static constexpr int scrollingDropIndicatorBorderRadius()
    {
        return ::PhosphorZones::ZoneDefaults::BorderRadius;
    }
    static constexpr int scrollingDropIndicatorBorderRadiusMin()
    {
        return 0;
    }
    static constexpr int scrollingDropIndicatorBorderRadiusMax()
    {
        return 50;
    }

    /// Meta+wheel column focus in the KWin effect. Off, the axis chords are
    /// genuinely released back to the compositor for any later registrant
    /// (stock zoom binds its axis gesture to Meta+Ctrl, not plain Meta).
    /// Inverted flips the scroll direction.
    static constexpr bool scrollingWheelFocusEnabled()
    {
        return true;
    }
    static constexpr bool scrollingWheelFocusInverted()
    {
        return false;
    }
    /// DefaultWindowHeightKind wire values — the engine's WindowHeight::Kind
    /// vocabulary 1:1 (0 = auto split, 1 = fixed px, 2 = preset index); the
    /// schema static_asserts the pair via DefaultHeightKind.
    static constexpr int scrollingHeightKindAuto()
    {
        return 0;
    }
    static constexpr int scrollingHeightKindFixed()
    {
        return 1;
    }
    static constexpr int scrollingHeightKindPreset()
    {
        return 2;
    }
    /// Default window height kind for fresh tiles: auto (even split).
    static constexpr int scrollingDefaultWindowHeightKind()
    {
        return scrollingHeightKindAuto();
    }
    /// Closed-set validity check (see isValidScrollingCenterFocusedColumn).
    static constexpr bool isValidScrollingHeightKind(int v)
    {
        return v == scrollingHeightKindAuto() || v == scrollingHeightKindFixed() || v == scrollingHeightKindPreset();
    }
    /// Fixed-kind pixel height for fresh tiles, plus its range. Only read
    /// under the Fixed kind; Auto and Preset ignore it. The relayout
    /// renormalizes fixed heights into the column budget, so an oversized
    /// value degrades gracefully rather than overflowing.
    static constexpr qreal scrollingDefaultWindowHeightValue()
    {
        return 600.0;
    }
    static constexpr qreal scrollingDefaultWindowHeightMin()
    {
        return 100.0;
    }
    static constexpr qreal scrollingDefaultWindowHeightMax()
    {
        return 10000.0;
    }
    /// Height-PROPORTION floor: the D-Bus setWindowHeightProportion gate's
    /// alone (canonicalProportionList hardcodes its own <= 0.0 floor and
    /// never consults a Min). Delegates to the width proportion range today —
    /// both are work-area fractions sharing the 0-1 spelling — but exists as
    /// its own accessor so the height wire contract cannot be silently
    /// retargeted by a future retune of the WIDTH range.
    static constexpr qreal scrollingWindowHeightProportionMin()
    {
        return scrollingDefaultColumnWidthProportionMin();
    }
    /// Height-PROPORTION ceiling: the D-Bus gate AND the height preset-list
    /// canonicalizer read this one. Same delegation rationale as Min.
    static constexpr qreal scrollingWindowHeightProportionMax()
    {
        return scrollingDefaultColumnWidthProportionMax();
    }
    /// Editing granularity for the fixed-height spin box, in whole pixels.
    /// Its own accessor rather than a reuse of the width step: the two
    /// controls govern different dimensions and nothing pins them equal.
    static constexpr qreal scrollingDefaultWindowHeightStep()
    {
        return 10.0;
    }
    /// Preset index the Preset height kind seeds tiles at (see
    /// scrollingPresetIndexMax for the ceiling rationale).
    static constexpr int scrollingDefaultWindowHeightPresetIndex()
    {
        return 0;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Scrolling behavior (Scrolling.Behavior)
    //
    // The strip's window-handling and focus knobs, the peers of the
    // Tiling.Behavior and Snapping.Behavior.WindowHandling families. Key
    // NAMES are the shared leaf spellings (FocusNewWindows, StickyWindowHandling,
    // …) under the Scrolling.Behavior group. Defaults deliberately match the
    // autotile canonical so a screen flipped between the two engines starts
    // from identical behavior.
    // ═══════════════════════════════════════════════════════════════════════════

    static constexpr bool scrollingFocusNewWindows()
    {
        return true;
    }

    // ── Edge auto-scroll during a drag re-insert (Scrolling.Behavior.DragScroll) ──
    //
    // niri's dnd-edge-view-scroll defaults, verbatim. Holding a dragged
    // window near either edge of the work area scrolls the strip, so the
    // drop can reach a column that is off screen. Speed ramps linearly from
    // zero at the band's inner edge to the maximum at the work area's edge.
    // The engine holds the same four figures as the IScrollSettings
    // defaults, so a stub that never heard of this schema still scrolls the
    // way niri does. The static_asserts in settings/scrolling.cpp compare the
    // two sides directly rather than against copied literals, so an edit to
    // EITHER file fails the build and there is no manual sync to remember.
    static constexpr bool scrollingDragScrollEnabled()
    {
        return true;
    }
    static constexpr int scrollingDragScrollTriggerWidth()
    {
        return 30;
    }
    static constexpr int scrollingDragScrollTriggerWidthMin()
    {
        return 1;
    }
    /// UI range ceiling, not an engine limit: the engine re-clamps the width
    /// to a third of the work area's main extent at tick time, so values
    /// past that are silently narrowed on small screens either way.
    static constexpr int scrollingDragScrollTriggerWidthMax()
    {
        return 300;
    }
    static constexpr int scrollingDragScrollDelayMs()
    {
        return 100;
    }
    static constexpr int scrollingDragScrollDelayMsMin()
    {
        return 0;
    }
    static constexpr int scrollingDragScrollDelayMsMax()
    {
        return 2000;
    }
    static constexpr int scrollingDragScrollMaxSpeed()
    {
        return 1500;
    }
    /// UI usability floor, not the engine's: the engine floors the speed at
    /// 1 px/s (engine_core clamps independently), but a slider that can
    /// offer a sub-perceptible crawl reads as broken, so the settings range
    /// starts where the motion is visible.
    static constexpr int scrollingDragScrollMaxSpeedMin()
    {
        return 50;
    }
    static constexpr int scrollingDragScrollMaxSpeedMax()
    {
        return 10000;
    }
    /// ScrollInsertPosition wire values (0 = right of active, 1 = left of
    /// active, 2 = first, 3 = last, 4 = into active column); the schema
    /// static_asserts them against the engine enumerators. Right-of-active
    /// must stay 0 so an absent key preserves the historical behavior.
    static constexpr int scrollingInsertRightOfActive()
    {
        return 0;
    }
    static constexpr int scrollingInsertLeftOfActive()
    {
        return 1;
    }
    static constexpr int scrollingInsertFirst()
    {
        return 2;
    }
    static constexpr int scrollingInsertLast()
    {
        return 3;
    }
    static constexpr int scrollingInsertIntoActiveColumn()
    {
        return 4;
    }
    static constexpr int scrollingInsertPosition()
    {
        return scrollingInsertRightOfActive();
    }
    /// Closed-set validity check (see isValidScrollingCenterFocusedColumn).
    static constexpr bool isValidScrollingInsertPosition(int v)
    {
        return v == scrollingInsertRightOfActive() || v == scrollingInsertLeftOfActive() || v == scrollingInsertFirst()
            || v == scrollingInsertLast() || v == scrollingInsertIntoActiveColumn();
    }
    static constexpr bool scrollingFocusFollowsMouse()
    {
        return false;
    }
    /// StickyWindowHandling wire values, the shared PhosphorEngine enum's
    /// spelling (0 = treat as normal, 1 = restore only, 2 = ignore all).
    /// Named for the same reason as the CenterFocusedColumn values above;
    /// settingsschema_scrolling.cpp static_asserts them against the engine
    /// enumerators.
    static constexpr int scrollingStickyTreatAsNormal()
    {
        return 0;
    }
    static constexpr int scrollingStickyRestoreOnly()
    {
        return 1;
    }
    static constexpr int scrollingStickyIgnoreAll()
    {
        return 2;
    }
    static constexpr int scrollingStickyWindowHandling()
    {
        return scrollingStickyTreatAsNormal();
    }
    /// Closed-set validity check (see isValidScrollingCenterFocusedColumn).
    static constexpr bool isValidScrollingStickyWindowHandling(int v)
    {
        return v == scrollingStickyTreatAsNormal() || v == scrollingStickyRestoreOnly()
            || v == scrollingStickyIgnoreAll();
    }
    static constexpr bool scrollingRespectMinimumSize()
    {
        return true;
    }
    /// Restore the persisted strip snapshot (column order, widths, tab
    /// stacks, focus) when windows reopen after a restart. Gates only the
    /// cross-session restore; in-session mode round-trips always restore.
    static constexpr bool scrollingRestoreStripsOnLogin()
    {
        return true;
    }
    /// Restore a scroll-FLOATED window to its previous position on reopen.
    /// The scroll twin of autotileRestoreFloatedWindowsOnLogin, gating the
    /// engine's geometry move on the floating-reopen branch (the float flag
    /// itself is never gated). The literal matches the autotile canonical's
    /// value (it lives in the leaf configdefaults.h, which this chain link
    /// cannot reference).
    static constexpr bool scrollingRestoreFloatedWindowsOnLogin()
    {
        return true;
    }
    /// Percent of the work-area extent one increase/decrease shortcut press
    /// moves a column width or window height. Daemon-side only: the engine
    /// receives an already-computed delta, so these never enter
    /// IScrollSettings. Distinct from the DefaultColumnWidth*Step editor
    /// granularity accessors above, which size QML control notches.
    static constexpr int scrollingColumnWidthStepPercent()
    {
        return 10;
    }
    static constexpr int scrollingWindowHeightStepPercent()
    {
        return 10;
    }
    static constexpr int scrollingStepPercentMin()
    {
        return 1;
    }
    static constexpr int scrollingStepPercentMax()
    {
        return 50;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Scrolling.ZoneSelector
    //
    // The strip-mode drag selector popup. It mirrors the Snapping.ZoneSelector
    // family minus LayoutMode / GridColumns / MaxRows, because the strip popup
    // is always a single card row along the strip (so it follows the screen's
    // strip axis, but never becomes a grid). The values below hand-duplicate
    // the snapping twins in configdefaults.h (zoneSelectorEnabled(),
    // triggerDistance(), position(), sizeMode(), previewWidth(),
    // previewHeight(), previewLockAspect()): that file is the LEAF of the
    // ConfigDefaults chain and this header is link 6, so it cannot see them.
    // The two families are independent knobs and are free to diverge.
    // The ranges are NOT duplicated — the schema clamps these keys with the
    // shared triggerDistanceMin/Max and previewWidth/Height Min/Max accessors.
    // ═══════════════════════════════════════════════════════════════════════════

    /// On by default, like the snapping selector, and it needs no user
    /// opt-out to stay out of the way: the engine capability gate
    /// (providesDragInsertSelector) keeps it invisible on every screen that is
    /// not running the scrolling engine.
    static constexpr bool scrollingZoneSelectorEnabled()
    {
        return true;
    }
    static constexpr int scrollingZoneSelectorTriggerDistance()
    {
        return 50;
    }
    /// ZoneSelectorPosition::Top.
    static constexpr int scrollingZoneSelectorPosition()
    {
        return 1;
    }
    /// ZoneSelectorSizeMode::Auto.
    static constexpr int scrollingZoneSelectorSizeMode()
    {
        return 0;
    }
    static constexpr int scrollingZoneSelectorPreviewWidth()
    {
        return 180;
    }
    static constexpr int scrollingZoneSelectorPreviewHeight()
    {
        return 101;
    }
    static constexpr bool scrollingZoneSelectorPreviewLockAspect()
    {
        return true;
    }
};

// Compile-time bound checks for the scrolling defaults that declare min/max
// accessors — same guard the autotile and animation defaults carry at the end
// of configdefaults.h. The width VALUE default is a proportion (the default
// kind is Proportion), so it is checked against the proportion range; the
// pixel seed is checked against the fixed range.
static_assert(ConfigDefaultsScrolling::scrollingDefaultColumnWidthValue()
                      >= ConfigDefaultsScrolling::scrollingDefaultColumnWidthProportionMin()
                  && ConfigDefaultsScrolling::scrollingDefaultColumnWidthValue()
                      <= ConfigDefaultsScrolling::scrollingDefaultColumnWidthProportionMax(),
              "ConfigDefaults::scrollingDefaultColumnWidthValue() outside the declared proportion [min, max] range");
static_assert(ConfigDefaultsScrolling::scrollingDefaultColumnWidthFixedPx()
                      >= ConfigDefaultsScrolling::scrollingDefaultColumnWidthFixedMin()
                  && ConfigDefaultsScrolling::scrollingDefaultColumnWidthFixedPx()
                      <= ConfigDefaultsScrolling::scrollingDefaultColumnWidthFixedMax(),
              "ConfigDefaults::scrollingDefaultColumnWidthFixedPx() outside the declared fixed [min, max] range");
// The kind-ish defaults must satisfy their own closed-set predicates.
static_assert(ConfigDefaultsScrolling::isValidScrollingCenterFocusedColumn(
                  ConfigDefaultsScrolling::scrollingCenterFocusedColumn()),
              "ConfigDefaults::scrollingCenterFocusedColumn() is not in its own closed set");
static_assert(ConfigDefaultsScrolling::isValidScrollingStripAxis(ConfigDefaultsScrolling::scrollingStripAxis()),
              "ConfigDefaults::scrollingStripAxis() is not in its own closed set");
static_assert(
    ConfigDefaultsScrolling::isValidScrollingWidthKind(ConfigDefaultsScrolling::scrollingDefaultColumnWidthKind()),
    "ConfigDefaults::scrollingDefaultColumnWidthKind() is not in its own closed set");
static_assert(
    ConfigDefaultsScrolling::isValidScrollingColumnDisplay(ConfigDefaultsScrolling::scrollingDefaultColumnDisplay()),
    "ConfigDefaults::scrollingDefaultColumnDisplay() is not in its own closed set");
static_assert(ConfigDefaultsScrolling::isValidScrollingStickyWindowHandling(
                  ConfigDefaultsScrolling::scrollingStickyWindowHandling()),
              "ConfigDefaults::scrollingStickyWindowHandling() is not in its own closed set");
static_assert(
    ConfigDefaultsScrolling::isValidScrollingHeightKind(ConfigDefaultsScrolling::scrollingDefaultWindowHeightKind()),
    "ConfigDefaults::scrollingDefaultWindowHeightKind() is not in its own closed set");
static_assert(
    ConfigDefaultsScrolling::isValidScrollingInsertPosition(ConfigDefaultsScrolling::scrollingInsertPosition()),
    "ConfigDefaults::scrollingInsertPosition() is not in its own closed set");
static_assert(ConfigDefaultsScrolling::scrollingDefaultWindowHeightValue()
                      >= ConfigDefaultsScrolling::scrollingDefaultWindowHeightMin()
                  && ConfigDefaultsScrolling::scrollingDefaultWindowHeightValue()
                      <= ConfigDefaultsScrolling::scrollingDefaultWindowHeightMax(),
              "ConfigDefaults::scrollingDefaultWindowHeightValue() outside the declared [min, max] range");
static_assert(ConfigDefaultsScrolling::scrollingDefaultColumnWidthPresetIndex() >= 0
                  && ConfigDefaultsScrolling::scrollingDefaultColumnWidthPresetIndex()
                      <= ConfigDefaultsScrolling::scrollingPresetIndexMax(),
              "ConfigDefaults::scrollingDefaultColumnWidthPresetIndex() outside [0, presetIndexMax]");
static_assert(ConfigDefaultsScrolling::scrollingDefaultWindowHeightPresetIndex() >= 0
                  && ConfigDefaultsScrolling::scrollingDefaultWindowHeightPresetIndex()
                      <= ConfigDefaultsScrolling::scrollingPresetIndexMax(),
              "ConfigDefaults::scrollingDefaultWindowHeightPresetIndex() outside [0, presetIndexMax]");
static_assert(ConfigDefaultsScrolling::scrollingColumnWidthStepPercent()
                      >= ConfigDefaultsScrolling::scrollingStepPercentMin()
                  && ConfigDefaultsScrolling::scrollingColumnWidthStepPercent()
                      <= ConfigDefaultsScrolling::scrollingStepPercentMax(),
              "ConfigDefaults::scrollingColumnWidthStepPercent() outside the declared [min, max] range");
static_assert(ConfigDefaultsScrolling::scrollingWindowHeightStepPercent()
                      >= ConfigDefaultsScrolling::scrollingStepPercentMin()
                  && ConfigDefaultsScrolling::scrollingWindowHeightStepPercent()
                      <= ConfigDefaultsScrolling::scrollingStepPercentMax(),
              "ConfigDefaults::scrollingWindowHeightStepPercent() outside the declared [min, max] range");
// Tab-indicator family: the same closed-set and [min, max] guards the rest of
// the file carries, so a default edited out of its own declared range fails
// the build instead of being silently snapped by the schema on first read.
static_assert(
    ConfigDefaultsScrolling::isValidScrollingTabIndicatorStyle(ConfigDefaultsScrolling::scrollingTabIndicatorStyle()),
    "ConfigDefaults::scrollingTabIndicatorStyle() is not in its own closed set");
static_assert(ConfigDefaultsScrolling::isValidScrollingTabIndicatorPosition(
                  ConfigDefaultsScrolling::scrollingTabIndicatorPosition()),
              "ConfigDefaults::scrollingTabIndicatorPosition() is not in its own closed set");
static_assert(ConfigDefaultsScrolling::scrollingTabIndicatorGap()
                      >= ConfigDefaultsScrolling::scrollingTabIndicatorGapMin()
                  && ConfigDefaultsScrolling::scrollingTabIndicatorGap()
                      <= ConfigDefaultsScrolling::scrollingTabIndicatorGapMax(),
              "ConfigDefaults::scrollingTabIndicatorGap() outside the declared [min, max] range");
static_assert(ConfigDefaultsScrolling::scrollingTabIndicatorWidth()
                      >= ConfigDefaultsScrolling::scrollingTabIndicatorWidthMin()
                  && ConfigDefaultsScrolling::scrollingTabIndicatorWidth()
                      <= ConfigDefaultsScrolling::scrollingTabIndicatorWidthMax(),
              "ConfigDefaults::scrollingTabIndicatorWidth() outside the declared [min, max] range");
// Both per-style thickness figures explicitly, not just the default-style one
// the accessor above resolves to: the style setter re-seeds the ranged Width
// key from these, so an out-of-range edit would otherwise be silently snapped
// by the schema on the first style flip instead of failing the build.
static_assert(ConfigDefaultsScrolling::scrollingTabIndicatorWidthForBar()
                      >= ConfigDefaultsScrolling::scrollingTabIndicatorWidthMin()
                  && ConfigDefaultsScrolling::scrollingTabIndicatorWidthForBar()
                      <= ConfigDefaultsScrolling::scrollingTabIndicatorWidthMax(),
              "ConfigDefaults::scrollingTabIndicatorWidthForBar() outside the declared [min, max] range");
static_assert(ConfigDefaultsScrolling::scrollingTabIndicatorWidthForChips()
                      >= ConfigDefaultsScrolling::scrollingTabIndicatorWidthMin()
                  && ConfigDefaultsScrolling::scrollingTabIndicatorWidthForChips()
                      <= ConfigDefaultsScrolling::scrollingTabIndicatorWidthMax(),
              "ConfigDefaults::scrollingTabIndicatorWidthForChips() outside the declared [min, max] range");
static_assert(ConfigDefaultsScrolling::scrollingTabIndicatorLengthProportion()
                      >= ConfigDefaultsScrolling::scrollingTabIndicatorLengthProportionMin()
                  && ConfigDefaultsScrolling::scrollingTabIndicatorLengthProportion()
                      <= ConfigDefaultsScrolling::scrollingTabIndicatorLengthProportionMax(),
              "ConfigDefaults::scrollingTabIndicatorLengthProportion() outside the declared [min, max] range");
static_assert(ConfigDefaultsScrolling::scrollingTabIndicatorGapsBetweenTabs()
                      >= ConfigDefaultsScrolling::scrollingTabIndicatorGapsBetweenTabsMin()
                  && ConfigDefaultsScrolling::scrollingTabIndicatorGapsBetweenTabs()
                      <= ConfigDefaultsScrolling::scrollingTabIndicatorGapsBetweenTabsMax(),
              "ConfigDefaults::scrollingTabIndicatorGapsBetweenTabs() outside the declared [min, max] range");
static_assert(ConfigDefaultsScrolling::scrollingTabIndicatorCornerRadius()
                      >= ConfigDefaultsScrolling::scrollingTabIndicatorCornerRadiusMin()
                  && ConfigDefaultsScrolling::scrollingTabIndicatorCornerRadius()
                      <= ConfigDefaultsScrolling::scrollingTabIndicatorCornerRadiusMax(),
              "ConfigDefaults::scrollingTabIndicatorCornerRadius() outside the declared [min, max] range");
// The pill sentinel IS the floor, so nothing between it and 0 can be stored.
static_assert(ConfigDefaultsScrolling::scrollingTabIndicatorCornerRadiusMin()
                  == ConfigDefaultsScrolling::scrollingTabIndicatorCornerRadiusPill(),
              "The corner-radius floor must be the pill sentinel — see the accessor comment");
static_assert(ConfigDefaultsScrolling::scrollingTabIndicatorFontWeight()
                      >= ConfigDefaultsScrolling::scrollingTabIndicatorFontWeightMin()
                  && ConfigDefaultsScrolling::scrollingTabIndicatorFontWeight()
                      <= ConfigDefaultsScrolling::scrollingTabIndicatorFontWeightMax(),
              "ConfigDefaults::scrollingTabIndicatorFontWeight() outside the declared [min, max] range");
// The drop indicator's three ranged defaults, same guard as the tab
// indicator's above. The colour pair is unranged (empty means follow the
// scheme) and Enabled is a bool, so neither has anything to check.
static_assert(ConfigDefaultsScrolling::scrollingDropIndicatorOpacity()
                      >= ConfigDefaultsScrolling::scrollingDropIndicatorOpacityMin()
                  && ConfigDefaultsScrolling::scrollingDropIndicatorOpacity()
                      <= ConfigDefaultsScrolling::scrollingDropIndicatorOpacityMax(),
              "ConfigDefaults::scrollingDropIndicatorOpacity() outside the declared [min, max] range");
static_assert(ConfigDefaultsScrolling::scrollingDropIndicatorBorderWidth()
                      >= ConfigDefaultsScrolling::scrollingDropIndicatorBorderWidthMin()
                  && ConfigDefaultsScrolling::scrollingDropIndicatorBorderWidth()
                      <= ConfigDefaultsScrolling::scrollingDropIndicatorBorderWidthMax(),
              "ConfigDefaults::scrollingDropIndicatorBorderWidth() outside the declared [min, max] range");
// The radius default is ZoneDefaults::BorderRadius by reference, so this also
// catches the snapping overlay's own default drifting out of the range this
// file declares for the highlight that is meant to match it.
static_assert(ConfigDefaultsScrolling::scrollingDropIndicatorBorderRadius()
                      >= ConfigDefaultsScrolling::scrollingDropIndicatorBorderRadiusMin()
                  && ConfigDefaultsScrolling::scrollingDropIndicatorBorderRadius()
                      <= ConfigDefaultsScrolling::scrollingDropIndicatorBorderRadiusMax(),
              "ConfigDefaults::scrollingDropIndicatorBorderRadius() outside the declared [min, max] range");

// The strip axis' load-bearing invariant, pinned where the accessors live
// rather than only in the editor's own asserts: Auto MUST be zero. An absent
// Scrolling.StripAxis key reads back as 0 on every path (config, per-screen
// store, D-Bus), and that zero has to mean "resolve from the work area" rather
// than pinning a direction. It is also why this numbering cannot be cast to
// PhosphorProtocol::ScrollAxis, whose zero is Horizontal — the schema TU pins
// that half.
static_assert(ConfigDefaultsScrolling::scrollingStripAxisAuto() == 0,
              "StripAxis Auto must be 0 so an absent key resolves from the work area");
static_assert(ConfigDefaultsScrolling::scrollingStripAxis() == ConfigDefaultsScrolling::scrollingStripAxisAuto(),
              "the StripAxis default must be Auto");

// Edge auto-scroll, same guard as every other ranged default here. Without
// these a retuned default outside its own declared range would not fail the
// build; it would be silently snapped by the schema's clamp on first read.
static_assert(ConfigDefaultsScrolling::scrollingDragScrollTriggerWidth()
                      >= ConfigDefaultsScrolling::scrollingDragScrollTriggerWidthMin()
                  && ConfigDefaultsScrolling::scrollingDragScrollTriggerWidth()
                      <= ConfigDefaultsScrolling::scrollingDragScrollTriggerWidthMax(),
              "ConfigDefaults::scrollingDragScrollTriggerWidth() outside the declared [min, max] range");
static_assert(ConfigDefaultsScrolling::scrollingDragScrollDelayMs()
                      >= ConfigDefaultsScrolling::scrollingDragScrollDelayMsMin()
                  && ConfigDefaultsScrolling::scrollingDragScrollDelayMs()
                      <= ConfigDefaultsScrolling::scrollingDragScrollDelayMsMax(),
              "ConfigDefaults::scrollingDragScrollDelayMs() outside the declared [min, max] range");
static_assert(ConfigDefaultsScrolling::scrollingDragScrollMaxSpeed()
                      >= ConfigDefaultsScrolling::scrollingDragScrollMaxSpeedMin()
                  && ConfigDefaultsScrolling::scrollingDragScrollMaxSpeed()
                      <= ConfigDefaultsScrolling::scrollingDragScrollMaxSpeedMax(),
              "ConfigDefaults::scrollingDragScrollMaxSpeed() outside the declared [min, max] range");

} // namespace PlasmaZones

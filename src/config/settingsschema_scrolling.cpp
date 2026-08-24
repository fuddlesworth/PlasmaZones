// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The scrolling half of the settings schema: the Scrolling knobs and
// the Shortcuts.Scrolling chords. Split out of settingsschema.cpp for
// file-size; the shared validator helpers live in settingsschema_p.h and the
// three entry points (appendScrollingSchema, appendScrollingZoneSelectorSchema,
// appendScrollingShortcutsSchema) are declared alongside every other
// appendXxxSchema in settingsschema.h.

#include "settingsschema.h"

#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorProtocol/ScrollAxisEnum.h>
#include <PhosphorRules/ActionParams.h>
#include <PhosphorScrollEngine/ScrollTypes.h>

#include "configdefaults.h"
#include "core/types/enums.h"
#include "settingsschema_p.h"
#include "settingsschemachoices.h"

using namespace Qt::StringLiterals;

namespace PlasmaZones {

using SchemaValidators::canonicalFontFamily;
using SchemaValidators::canonicalProportionList;
using SchemaValidators::clampDouble;
using SchemaValidators::clampInt;
using SchemaValidators::validIntOr;

// The config-space enum vocabulary in ConfigDefaults and the engine's own
// enumerators are two spellings of one wire value. The schema below declares
// the engine spelling; the D-Bus registry guards and the settings-layer
// branches read the ConfigDefaults spelling. These asserts are what keeps the
// pair from drifting — without them the coupling is convention only.
//
// NOTE on the width KIND: it must never be cast to ColumnWidth::Kind, whose 2
// is Preset. The enum the engine actually static_casts the config value into
// is DefaultWidthKind, which IS an exact 1:1 match, so it gets the same
// lockstep asserts as its siblings below.
// The strip selector's two enum-valued defaults are the same class of
// hand-duplicated wire value; without the pins a reordering of
// ZoneSelectorPosition / ZoneSelectorSizeMode would silently retarget the
// popup's default corner with a build that still succeeds.
static_assert(ConfigDefaults::scrollingZoneSelectorPosition() == static_cast<int>(ZoneSelectorPosition::Top),
              "strip-selector default position drifted from ZoneSelectorPosition::Top");
static_assert(ConfigDefaults::scrollingZoneSelectorSizeMode() == static_cast<int>(ZoneSelectorSizeMode::Auto),
              "strip-selector default size mode drifted from ZoneSelectorSizeMode::Auto");
static_assert(ConfigDefaults::scrollingCenterFocusedColumnNever()
                  == static_cast<int>(PhosphorScrollEngine::CenterFocusedColumn::Never),
              "CenterFocusedColumn::Never wire value drifted from ConfigDefaults");
static_assert(ConfigDefaults::scrollingCenterFocusedColumnAlways()
                  == static_cast<int>(PhosphorScrollEngine::CenterFocusedColumn::Always),
              "CenterFocusedColumn::Always wire value drifted from ConfigDefaults");
static_assert(ConfigDefaults::scrollingCenterFocusedColumnOnOverflow()
                  == static_cast<int>(PhosphorScrollEngine::CenterFocusedColumn::OnOverflow),
              "CenterFocusedColumn::OnOverflow wire value drifted from ConfigDefaults");
static_assert(ConfigDefaults::scrollingColumnDisplayNormal()
                  == static_cast<int>(PhosphorScrollEngine::ColumnDisplay::Normal),
              "ColumnDisplay::Normal wire value drifted from ConfigDefaults");
static_assert(ConfigDefaults::scrollingColumnDisplayTabbed()
                  == static_cast<int>(PhosphorScrollEngine::ColumnDisplay::Tabbed),
              "ColumnDisplay::Tabbed wire value drifted from ConfigDefaults");
static_assert(ConfigDefaults::scrollingWidthKindProportion()
                  == static_cast<int>(PhosphorScrollEngine::DefaultWidthKind::Proportion),
              "DefaultWidthKind::Proportion wire value drifted from ConfigDefaults");
static_assert(ConfigDefaults::scrollingWidthKindFixed()
                  == static_cast<int>(PhosphorScrollEngine::DefaultWidthKind::Fixed),
              "DefaultWidthKind::Fixed wire value drifted from ConfigDefaults");
static_assert(ConfigDefaults::scrollingWidthKindClientDecides()
                  == static_cast<int>(PhosphorScrollEngine::DefaultWidthKind::ClientDecides),
              "DefaultWidthKind::ClientDecides wire value drifted from ConfigDefaults");
static_assert(ConfigDefaults::scrollingWidthKindPreset()
                  == static_cast<int>(PhosphorScrollEngine::DefaultWidthKind::Preset),
              "DefaultWidthKind::Preset wire value drifted from ConfigDefaults");
static_assert(ConfigDefaults::scrollingHeightKindAuto()
                  == static_cast<int>(PhosphorScrollEngine::DefaultHeightKind::Auto),
              "DefaultHeightKind::Auto wire value drifted from ConfigDefaults");
static_assert(ConfigDefaults::scrollingHeightKindFixed()
                  == static_cast<int>(PhosphorScrollEngine::DefaultHeightKind::Fixed),
              "DefaultHeightKind::Fixed wire value drifted from ConfigDefaults");
static_assert(ConfigDefaults::scrollingHeightKindPreset()
                  == static_cast<int>(PhosphorScrollEngine::DefaultHeightKind::Preset),
              "DefaultHeightKind::Preset wire value drifted from ConfigDefaults");
static_assert(ConfigDefaults::scrollingInsertRightOfActive()
                  == static_cast<int>(PhosphorScrollEngine::ScrollInsertPosition::RightOfActive),
              "ScrollInsertPosition::RightOfActive wire value drifted from ConfigDefaults");
static_assert(ConfigDefaults::scrollingInsertLeftOfActive()
                  == static_cast<int>(PhosphorScrollEngine::ScrollInsertPosition::LeftOfActive),
              "ScrollInsertPosition::LeftOfActive wire value drifted from ConfigDefaults");
static_assert(ConfigDefaults::scrollingInsertFirst()
                  == static_cast<int>(PhosphorScrollEngine::ScrollInsertPosition::First),
              "ScrollInsertPosition::First wire value drifted from ConfigDefaults");
static_assert(ConfigDefaults::scrollingInsertLast()
                  == static_cast<int>(PhosphorScrollEngine::ScrollInsertPosition::Last),
              "ScrollInsertPosition::Last wire value drifted from ConfigDefaults");
static_assert(ConfigDefaults::scrollingInsertIntoActiveColumn()
                  == static_cast<int>(PhosphorScrollEngine::ScrollInsertPosition::IntoActiveColumn),
              "ScrollInsertPosition::IntoActiveColumn wire value drifted from ConfigDefaults");
static_assert(ConfigDefaults::scrollingStickyTreatAsNormal()
                  == static_cast<int>(PhosphorEngine::StickyWindowHandling::TreatAsNormal),
              "StickyWindowHandling::TreatAsNormal wire value drifted from ConfigDefaults");
static_assert(ConfigDefaults::scrollingStickyRestoreOnly()
                  == static_cast<int>(PhosphorEngine::StickyWindowHandling::RestoreOnly),
              "StickyWindowHandling::RestoreOnly wire value drifted from ConfigDefaults");
static_assert(ConfigDefaults::scrollingStickyIgnoreAll()
                  == static_cast<int>(PhosphorEngine::StickyWindowHandling::IgnoreAll),
              "StickyWindowHandling::IgnoreAll wire value drifted from ConfigDefaults");
// TabIndicatorPosition. The engine static_casts the stored int straight into
// this enum after a range check (engine_core.cpp), so a drift would silently
// place the indicator on the wrong edge rather than failing to build. Two
// comments already CLAIMED this pinning existed before it did.
static_assert(ConfigDefaults::scrollingTabIndicatorPositionLeft()
                  == static_cast<int>(PhosphorScrollEngine::TabIndicatorPosition::Left),
              "TabIndicatorPosition::Left wire value drifted from ConfigDefaults");
static_assert(ConfigDefaults::scrollingTabIndicatorPositionRight()
                  == static_cast<int>(PhosphorScrollEngine::TabIndicatorPosition::Right),
              "TabIndicatorPosition::Right wire value drifted from ConfigDefaults");
static_assert(ConfigDefaults::scrollingTabIndicatorPositionTop()
                  == static_cast<int>(PhosphorScrollEngine::TabIndicatorPosition::Top),
              "TabIndicatorPosition::Top wire value drifted from ConfigDefaults");
static_assert(ConfigDefaults::scrollingTabIndicatorPositionBottom()
                  == static_cast<int>(PhosphorScrollEngine::TabIndicatorPosition::Bottom),
              "TabIndicatorPosition::Bottom wire value drifted from ConfigDefaults");

// The strip axis is the one enum here that must NOT line up with its engine
// counterpart: the config value is the three-valued INTENT (Auto is its zero),
// the wire value is the resolved two-valued PhosphorProtocol::ScrollAxis
// (Horizontal is its zero). Pin the mismatch so a future "tidy-up" that makes
// the two numberings agree has to delete this assert deliberately rather than
// making a silent static_cast between them start looking safe.
static_assert(ConfigDefaults::scrollingStripAxisHorizontal()
                  != static_cast<int>(PhosphorProtocol::ScrollAxis::Horizontal),
              "StripAxis is the config INTENT space and must not share ScrollAxis' numbering");

// The preset-index ceiling is a consequence of the preset-list length cap, not
// an independent number: an index past the last entry a canonicalized list can
// hold could never resolve.
static_assert(ConfigDefaults::scrollingPresetIndexMax() == SchemaValidators::kMaxPresetEntries - 1,
              "scrollingPresetIndexMax drifted from the preset-list length cap in settingsschema_p.h");

// ─── Scrolling (Scrolling) ───────────────────────────────────────────
// The niri-style scrolling engine's knobs. The strip reuses the shared Gaps
// group and Tiling.Behavior focus settings; only scroll-specific values live
// here. The preset lists are comma-joined decimal proportions.

void appendScrollingSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;

    schema.groups[CD::scrollingGroup()] = {
        // Master switch, the peer of Snapping/Tiling enabled: off, a
        // Scrolling context downgrades to Snapping in the daemon's derive
        // pass and the mode toggle skips the mode.
        {CD::enabledKey(), CD::scrollingEnabled(), QMetaType::Bool},
        // validIntOr (not clampInt) for every enum here: clamping snaps an
        // out-of-range stored value to the MAX enumerator, which both
        // contradicts the sibling file's documented reader-agreement
        // convention and disagrees with the engine's own snap-to-default
        // guard.
        {CD::centerFocusedColumnKey(),
         CD::scrollingCenterFocusedColumn(),
         QMetaType::Int,
         {},
         validIntOr({static_cast<int>(PhosphorScrollEngine::CenterFocusedColumn::Never),
                     static_cast<int>(PhosphorScrollEngine::CenterFocusedColumn::Always),
                     static_cast<int>(PhosphorScrollEngine::CenterFocusedColumn::OnOverflow)},
                    CD::scrollingCenterFocusedColumn()),
         intChoices({{static_cast<int>(PhosphorScrollEngine::CenterFocusedColumn::Never), "never"_L1},
                     {static_cast<int>(PhosphorScrollEngine::CenterFocusedColumn::Always), "always"_L1},
                     {static_cast<int>(PhosphorScrollEngine::CenterFocusedColumn::OnOverflow), "onOverflow"_L1}})},
        // StripAxis. Spelled from the ConfigDefaults accessors rather than an
        // engine enum, because this is the INTENT space and its Auto has no
        // engine enumerator — PhosphorProtocol::ScrollAxis is two-valued and
        // numbers Horizontal 0 where this space numbers it 1. Never cast
        // between them; the engine translates with an explicit switch.
        {CD::stripAxisKey(),
         CD::scrollingStripAxis(),
         QMetaType::Int,
         {},
         validIntOr(
             {CD::scrollingStripAxisAuto(), CD::scrollingStripAxisHorizontal(), CD::scrollingStripAxisVertical()},
             CD::scrollingStripAxis()),
         intChoices({{CD::scrollingStripAxisAuto(), "auto"_L1},
                     {CD::scrollingStripAxisHorizontal(), "horizontal"_L1},
                     {CD::scrollingStripAxisVertical(), "vertical"_L1}})},
        {CD::alwaysCenterSingleColumnKey(), CD::scrollingAlwaysCenterSingleColumn(), QMetaType::Bool},
        {CD::cropStraddlersKey(), CD::scrollingCropStraddlers(), QMetaType::Bool},
        // NOTE: the width-kind CONFIG space {0 proportion, 1 fixed,
        // 2 clientDecides, 3 preset} must never be static_cast to
        // ColumnWidth::Kind, whose 2 is Preset — the engine translates into
        // that enum with explicit ifs. DefaultWidthKind is the enum this
        // space does match 1:1; see the file-header note above it.
        {CD::defaultColumnWidthKindKey(),
         CD::scrollingDefaultColumnWidthKind(),
         QMetaType::Int,
         {},
         validIntOr({CD::scrollingWidthKindProportion(), CD::scrollingWidthKindFixed(),
                     CD::scrollingWidthKindClientDecides(), CD::scrollingWidthKindPreset()},
                    CD::scrollingDefaultColumnWidthKind()),
         intChoices({{CD::scrollingWidthKindProportion(), "proportion"_L1},
                     {CD::scrollingWidthKindFixed(), "fixed"_L1},
                     {CD::scrollingWidthKindClientDecides(), "clientDecides"_L1},
                     {CD::scrollingWidthKindPreset(), "preset"_L1}})},
        {CD::defaultColumnWidthPresetIndexKey(),
         CD::scrollingDefaultColumnWidthPresetIndex(),
         QMetaType::Int,
         {},
         clampInt(0, CD::scrollingPresetIndexMax())},
        {CD::defaultColumnWidthValueKey(),
         CD::scrollingDefaultColumnWidthValue(),
         QMetaType::Double,
         {},
         clampDouble(CD::scrollingDefaultColumnWidthProportionMin(), CD::scrollingDefaultColumnWidthFixedMax())},
        {CD::defaultColumnDisplayKey(),
         CD::scrollingDefaultColumnDisplay(),
         QMetaType::Int,
         {},
         validIntOr({static_cast<int>(PhosphorScrollEngine::ColumnDisplay::Normal),
                     static_cast<int>(PhosphorScrollEngine::ColumnDisplay::Tabbed)},
                    CD::scrollingDefaultColumnDisplay()),
         intChoices({{static_cast<int>(PhosphorScrollEngine::ColumnDisplay::Normal), "normal"_L1},
                     {static_cast<int>(PhosphorScrollEngine::ColumnDisplay::Tabbed), "tabbed"_L1}})},
        // Numeric canonicalizer, not the plain comma-list: entries must be
        // proportions the engine will actually honour, or it silently drops
        // the whole list and falls back to its built-ins while the page keeps
        // displaying the accepted-but-dead value. Both lists take the same
        // (0, 1] rule the engine applies to a preset entry — the scalar width
        // key's kind-aware floor governs that key alone and never reaches
        // preset entries.
        {CD::presetColumnWidthsKey(),
         CD::scrollingPresetColumnWidths(),
         QMetaType::QString,
         {},
         [](const QVariant& v) {
             return canonicalProportionList(v, CD::scrollingPresetColumnWidths(),
                                            CD::scrollingDefaultColumnWidthProportionMax());
         }},
        {CD::presetWindowHeightsKey(),
         CD::scrollingPresetWindowHeights(),
         QMetaType::QString,
         {},
         [](const QVariant& v) {
             // The HEIGHT proportion accessor (a delegating twin of the width
             // one), so a retune of the width ceiling cannot silently
             // retarget the height vocabulary.
             return canonicalProportionList(v, CD::scrollingPresetWindowHeights(),
                                            CD::scrollingWindowHeightProportionMax());
         }},
        // Default window height trio: kind + fixed pixel value + preset
        // index. Unlike the width pair, the value key serves ONE kind
        // (Fixed), so a plain clampDouble is the whole story — no kind-aware
        // setter needed.
        {CD::defaultWindowHeightKindKey(),
         CD::scrollingDefaultWindowHeightKind(),
         QMetaType::Int,
         {},
         validIntOr({CD::scrollingHeightKindAuto(), CD::scrollingHeightKindFixed(), CD::scrollingHeightKindPreset()},
                    CD::scrollingDefaultWindowHeightKind()),
         intChoices({{CD::scrollingHeightKindAuto(), "auto"_L1},
                     {CD::scrollingHeightKindFixed(), "fixed"_L1},
                     {CD::scrollingHeightKindPreset(), "preset"_L1}})},
        {CD::defaultWindowHeightValueKey(),
         CD::scrollingDefaultWindowHeightValue(),
         QMetaType::Double,
         {},
         clampDouble(CD::scrollingDefaultWindowHeightMin(), CD::scrollingDefaultWindowHeightMax())},
        {CD::defaultWindowHeightPresetIndexKey(),
         CD::scrollingDefaultWindowHeightPresetIndex(),
         QMetaType::Int,
         {},
         clampInt(0, CD::scrollingPresetIndexMax())},
        {CD::defaultTemplateKey(), CD::scrollingDefaultTemplate(), QMetaType::QString},
        {CD::wheelFocusEnabledKey(), CD::scrollingWheelFocusEnabled(), QMetaType::Bool},
        {CD::wheelFocusInvertedKey(), CD::scrollingWheelFocusInverted(), QMetaType::Bool},
    };

    // ─── Scrolling tab indicator (Scrolling.TabIndicator) ────────────────
    // The indicator drawn alongside a tabbed column. The two enums get
    // validIntOr closed sets built from the SAME ConfigDefaults enumerators
    // the D-Bus registry guards read, so neither can drift. The colours are
    // free-form strings because EMPTY is the meaningful "follow the theme"
    // value and no closed set can express that alongside arbitrary hex.
    schema.groups[CD::scrollingTabIndicatorGroup()] = {
        {CD::enabledKey(), CD::scrollingTabIndicatorEnabled(), QMetaType::Bool},
        {CD::styleKey(),
         CD::scrollingTabIndicatorStyle(),
         QMetaType::Int,
         {},
         validIntOr({CD::scrollingTabIndicatorStyleChips(), CD::scrollingTabIndicatorStyleBar()},
                    CD::scrollingTabIndicatorStyle()),
         intChoices(
             {{CD::scrollingTabIndicatorStyleChips(), "chips"_L1}, {CD::scrollingTabIndicatorStyleBar(), "bar"_L1}})},
        {CD::positionKey(),
         CD::scrollingTabIndicatorPosition(),
         QMetaType::Int,
         {},
         validIntOr({CD::scrollingTabIndicatorPositionLeft(), CD::scrollingTabIndicatorPositionRight(),
                     CD::scrollingTabIndicatorPositionTop(), CD::scrollingTabIndicatorPositionBottom()},
                    CD::scrollingTabIndicatorPosition()),
         intChoices({{CD::scrollingTabIndicatorPositionLeft(), "left"_L1},
                     {CD::scrollingTabIndicatorPositionRight(), "right"_L1},
                     {CD::scrollingTabIndicatorPositionTop(), "top"_L1},
                     {CD::scrollingTabIndicatorPositionBottom(), "bottom"_L1}})},
        {CD::hideWhenSingleTabKey(), CD::scrollingTabIndicatorHideWhenSingleTab(), QMetaType::Bool},
        {CD::placeWithinColumnKey(), CD::scrollingTabIndicatorPlaceWithinColumn(), QMetaType::Bool},
        // The gap floor is NEGATIVE on purpose: niri parity, where a negative
        // gap pulls the indicator on top of the window.
        {CD::gapKey(),
         CD::scrollingTabIndicatorGap(),
         QMetaType::Int,
         {},
         clampInt(CD::scrollingTabIndicatorGapMin(), CD::scrollingTabIndicatorGapMax())},
        {CD::widthKey(),
         CD::scrollingTabIndicatorWidth(),
         QMetaType::Int,
         {},
         clampInt(CD::scrollingTabIndicatorWidthMin(), CD::scrollingTabIndicatorWidthMax())},
        {CD::lengthProportionKey(),
         CD::scrollingTabIndicatorLengthProportion(),
         QMetaType::Double,
         {},
         clampDouble(CD::scrollingTabIndicatorLengthProportionMin(), CD::scrollingTabIndicatorLengthProportionMax())},
        {CD::gapsBetweenTabsKey(),
         CD::scrollingTabIndicatorGapsBetweenTabs(),
         QMetaType::Int,
         {},
         clampInt(CD::scrollingTabIndicatorGapsBetweenTabsMin(), CD::scrollingTabIndicatorGapsBetweenTabsMax())},
        // The floor IS the pill sentinel, so the clamp admits -1 and every
        // literal radius but nothing in between.
        {CD::cornerRadiusKey(),
         CD::scrollingTabIndicatorCornerRadius(),
         QMetaType::Int,
         {},
         clampInt(CD::scrollingTabIndicatorCornerRadiusMin(), CD::scrollingTabIndicatorCornerRadiusMax())},
        {CD::activeColorKey(),
         CD::scrollingTabIndicatorActiveColor(),
         QMetaType::QString,
         {},
         canonicalThemeFallbackColor},
        {CD::inactiveColorKey(),
         CD::scrollingTabIndicatorInactiveColor(),
         QMetaType::QString,
         {},
         canonicalThemeFallbackColor},
        {CD::urgentColorKey(),
         CD::scrollingTabIndicatorUrgentColor(),
         QMetaType::QString,
         {},
         canonicalThemeFallbackColor},
        // The label font. The family is free-form with no validator: EMPTY
        // means the system font, and no closed set can express that alongside
        // an arbitrary installed family. There is no size key — Width gives
        // the pill its thickness and the painter fits the label to it.
        {CD::fontFamilyKey(),
         CD::scrollingTabIndicatorFontFamily(),
         QMetaType::QString,
         {},
         canonicalFontFamily(PhosphorRules::MaxFontFamilyLength)},
        {CD::fontWeightKey(),
         CD::scrollingTabIndicatorFontWeight(),
         QMetaType::Int,
         {},
         clampInt(CD::scrollingTabIndicatorFontWeightMin(), CD::scrollingTabIndicatorFontWeightMax())},
        {CD::fontItalicKey(), CD::scrollingTabIndicatorFontItalic(), QMetaType::Bool},
        {CD::fontUnderlineKey(), CD::scrollingTabIndicatorFontUnderline(), QMetaType::Bool},
        {CD::fontStrikeoutKey(), CD::scrollingTabIndicatorFontStrikeout(), QMetaType::Bool},
    };

    // ─── Scrolling drop indicator (Scrolling.DropIndicator) ──────────────
    // The drop-target highlight painted during a drag re-insert. The colour is
    // a free-form string for the same reason as the tab colours: EMPTY is the
    // meaningful "follow the theme" value.
    schema.groups[CD::scrollingDropIndicatorGroup()] = {
        {CD::enabledKey(), CD::scrollingDropIndicatorEnabled(), QMetaType::Bool},
        {CD::colorKey(), CD::scrollingDropIndicatorColor(), QMetaType::QString, {}, canonicalThemeFallbackColor},
        {CD::borderColorKey(),
         CD::scrollingDropIndicatorBorderColor(),
         QMetaType::QString,
         {},
         canonicalThemeFallbackColor},
        {CD::opacityKey(),
         CD::scrollingDropIndicatorOpacity(),
         QMetaType::Double,
         {},
         clampDouble(CD::scrollingDropIndicatorOpacityMin(), CD::scrollingDropIndicatorOpacityMax())},
        {CD::widthKey(),
         CD::scrollingDropIndicatorBorderWidth(),
         QMetaType::Int,
         {},
         clampInt(CD::scrollingDropIndicatorBorderWidthMin(), CD::scrollingDropIndicatorBorderWidthMax())},
        {CD::radiusKey(),
         CD::scrollingDropIndicatorBorderRadius(),
         QMetaType::Int,
         {},
         clampInt(CD::scrollingDropIndicatorBorderRadiusMin(), CD::scrollingDropIndicatorBorderRadiusMax())},
    };

    // ─── Scrolling behavior (Scrolling.Behavior) ─────────────────────────
    // Window-handling and focus knobs, the peers of Tiling.Behavior and
    // Snapping.Behavior.WindowHandling. Shared leaf key names under the
    // scrolling group; smart gaps is deliberately absent (scrolling forwards
    // the shared Tiling.Gaps/SmartGaps value, see IScrollSettings).
    schema.groups[CD::scrollingBehaviorGroup()] = {
        {CD::focusNewWindowsKey(), CD::scrollingFocusNewWindows(), QMetaType::Bool},
        {CD::triggersKey(), CD::scrollingDragInsertTriggers(), QMetaType::QVariantList, {}, canonicalTriggerList},
        {CD::toggleActivationKey(), CD::scrollingDragInsertToggle(), QMetaType::Bool},
        {CD::releaseGraceMsKey(),
         CD::scrollingDragInsertGraceMs(),
         QMetaType::Int,
         {},
         clampInt(CD::triggerGraceMsMin(), CD::triggerGraceMsMax())},
        {CD::insertPositionKey(),
         CD::scrollingInsertPosition(),
         QMetaType::Int,
         {},
         validIntOr({CD::scrollingInsertRightOfActive(), CD::scrollingInsertLeftOfActive(), CD::scrollingInsertFirst(),
                     CD::scrollingInsertLast(), CD::scrollingInsertIntoActiveColumn()},
                    CD::scrollingInsertPosition()),
         intChoices({{CD::scrollingInsertRightOfActive(), "rightOfActive"_L1},
                     {CD::scrollingInsertLeftOfActive(), "leftOfActive"_L1},
                     {CD::scrollingInsertFirst(), "first"_L1},
                     {CD::scrollingInsertLast(), "last"_L1},
                     {CD::scrollingInsertIntoActiveColumn(), "intoActiveColumn"_L1}})},
        {CD::focusFollowsMouseKey(), CD::scrollingFocusFollowsMouse(), QMetaType::Bool},
        {CD::stickyWindowHandlingKey(),
         CD::scrollingStickyWindowHandling(),
         QMetaType::Int,
         {},
         validIntOr(
             {CD::scrollingStickyTreatAsNormal(), CD::scrollingStickyRestoreOnly(), CD::scrollingStickyIgnoreAll()},
             CD::scrollingStickyWindowHandling()),
         intChoices({{CD::scrollingStickyTreatAsNormal(), "treatAsNormal"_L1},
                     {CD::scrollingStickyRestoreOnly(), "restoreOnly"_L1},
                     {CD::scrollingStickyIgnoreAll(), "ignoreAll"_L1}})},
        {CD::respectMinimumSizeKey(), CD::scrollingRespectMinimumSize(), QMetaType::Bool},
        {CD::restoreOnLoginKey(), CD::scrollingRestoreStripsOnLogin(), QMetaType::Bool},
        {CD::restoreFloatedOnLoginKey(), CD::scrollingRestoreFloatedWindowsOnLogin(), QMetaType::Bool},
        {CD::columnWidthStepPercentKey(),
         CD::scrollingColumnWidthStepPercent(),
         QMetaType::Int,
         {},
         clampInt(CD::scrollingStepPercentMin(), CD::scrollingStepPercentMax())},
        {CD::windowHeightStepPercentKey(),
         CD::scrollingWindowHeightStepPercent(),
         QMetaType::Int,
         {},
         clampInt(CD::scrollingStepPercentMin(), CD::scrollingStepPercentMax())},
        {CD::viewScrollStepPercentKey(),
         CD::scrollingViewScrollStepPercent(),
         QMetaType::Int,
         {},
         clampInt(CD::scrollingStepPercentMin(), CD::scrollingStepPercentMax())},
    };

    // ─── Edge auto-scroll (Scrolling.Behavior.DragScroll) ────────────────
    // niri's dnd-edge-view-scroll. The three figures are clamped rather
    // than validated against a list: they are continuous quantities, and
    // the engine's own floors (a trigger width of at least one pixel, a
    // positive speed) are a second line of defence for a hand-edited file
    // that predates a range change.
    schema.groups[CD::scrollingDragScrollGroup()] = {
        {CD::enabledKey(), CD::scrollingDragScrollEnabled(), QMetaType::Bool},
        {CD::triggerWidthKey(),
         CD::scrollingDragScrollTriggerWidth(),
         QMetaType::Int,
         {},
         clampInt(CD::scrollingDragScrollTriggerWidthMin(), CD::scrollingDragScrollTriggerWidthMax())},
        {CD::delayMsKey(),
         CD::scrollingDragScrollDelayMs(),
         QMetaType::Int,
         {},
         clampInt(CD::scrollingDragScrollDelayMsMin(), CD::scrollingDragScrollDelayMsMax())},
        {CD::maxSpeedKey(),
         CD::scrollingDragScrollMaxSpeed(),
         QMetaType::Int,
         {},
         clampInt(CD::scrollingDragScrollMaxSpeedMin(), CD::scrollingDragScrollMaxSpeedMax())},
    };
}

// ─── Strip-mode selector (Scrolling.ZoneSelector) ───────────────────────────
// The drag popup on scrolling screens. Same key vocabulary as the snapping
// selector's group in settingsschema.cpp minus LayoutMode / GridColumns /
// MaxRows, and the ranges are the shared ones: the two selectors clamp their
// trigger distance and preview geometry identically.

void appendScrollingZoneSelectorSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;
    schema.groups[CD::scrollingZoneSelectorGroup()] = {
        {CD::enabledKey(), CD::scrollingZoneSelectorEnabled(), QMetaType::Bool},
        {CD::triggerDistanceKey(),
         CD::scrollingZoneSelectorTriggerDistance(),
         QMetaType::Int,
         {},
         clampInt(CD::triggerDistanceMin(), CD::triggerDistanceMax())},
        {CD::positionKey(),
         CD::scrollingZoneSelectorPosition(),
         QMetaType::Int,
         {},
         validIntOr({static_cast<int>(ZoneSelectorPosition::TopLeft), static_cast<int>(ZoneSelectorPosition::Top),
                     static_cast<int>(ZoneSelectorPosition::TopRight), static_cast<int>(ZoneSelectorPosition::Left),
                     static_cast<int>(ZoneSelectorPosition::Center), static_cast<int>(ZoneSelectorPosition::Right),
                     static_cast<int>(ZoneSelectorPosition::BottomLeft), static_cast<int>(ZoneSelectorPosition::Bottom),
                     static_cast<int>(ZoneSelectorPosition::BottomRight)},
                    CD::scrollingZoneSelectorPosition()),
         intChoices({{static_cast<int>(ZoneSelectorPosition::TopLeft), "topLeft"_L1},
                     {static_cast<int>(ZoneSelectorPosition::Top), "top"_L1},
                     {static_cast<int>(ZoneSelectorPosition::TopRight), "topRight"_L1},
                     {static_cast<int>(ZoneSelectorPosition::Left), "left"_L1},
                     {static_cast<int>(ZoneSelectorPosition::Center), "center"_L1},
                     {static_cast<int>(ZoneSelectorPosition::Right), "right"_L1},
                     {static_cast<int>(ZoneSelectorPosition::BottomLeft), "bottomLeft"_L1},
                     {static_cast<int>(ZoneSelectorPosition::Bottom), "bottom"_L1},
                     {static_cast<int>(ZoneSelectorPosition::BottomRight), "bottomRight"_L1}})},
        {CD::sizeModeKey(),
         CD::scrollingZoneSelectorSizeMode(),
         QMetaType::Int,
         {},
         validIntOr({static_cast<int>(ZoneSelectorSizeMode::Auto), static_cast<int>(ZoneSelectorSizeMode::Manual)},
                    CD::scrollingZoneSelectorSizeMode()),
         intChoices({{static_cast<int>(ZoneSelectorSizeMode::Auto), "auto"_L1},
                     {static_cast<int>(ZoneSelectorSizeMode::Manual), "manual"_L1}})},
        {CD::previewWidthKey(),
         CD::scrollingZoneSelectorPreviewWidth(),
         QMetaType::Int,
         {},
         clampInt(CD::previewWidthMin(), CD::previewWidthMax())},
        {CD::previewHeightKey(),
         CD::scrollingZoneSelectorPreviewHeight(),
         QMetaType::Int,
         {},
         clampInt(CD::previewHeightMin(), CD::previewHeightMax())},
        {CD::previewLockAspectKey(), CD::scrollingZoneSelectorPreviewLockAspect(), QMetaType::Bool},
    };
}

// ─── Scrolling shortcuts (Shortcuts.Scrolling) ──────────────────────────────
// Called from appendShortcutsSchema so the whole Shortcuts.* family is still
// declared by one entry point.
//
// Every BOUND chord in this group is bindable via the system Shortcuts KCM,
// because ShortcutManager registers it like every other action. The
// deliberately UNBOUND defaults (the edge-stop/wrap focus variants and the
// one-way float verbs) never register — the registry skips empty sequences —
// so they do not appear in the KCM; binding one means writing its
// Shortcuts.Scrolling key (config.json or the settings D-Bus surface). No
// count here on purpose: this family's hand-counts have drifted before, and
// the parity test that guards the group cannot see a number in a comment.
//
// The settings app has no page for editing the chords themselves. Scrolling
// does have a Quick Shortcuts page (ScrollingQuickShortcutsPage), but like
// its Snapping and Tiling siblings that page assigns templates to the
// numbered quick slots (wire mode 2, staged through the scrolling
// quick-slot map) rather than editing any of the chords declared below.

void appendScrollingShortcutsSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;
    schema.groups[CD::shortcutsScrollingGroup()] = {
        {CD::focusColumnFirstKey(), CD::scrollingFocusColumnFirstShortcut(), QMetaType::QString},
        {CD::focusColumnLastKey(), CD::scrollingFocusColumnLastShortcut(), QMetaType::QString},
        {CD::moveColumnToFirstKey(), CD::scrollingMoveColumnToFirstShortcut(), QMetaType::QString},
        {CD::moveColumnToLastKey(), CD::scrollingMoveColumnToLastShortcut(), QMetaType::QString},
        {CD::consumeWindowKey(), CD::scrollingConsumeWindowShortcut(), QMetaType::QString},
        {CD::expelWindowKey(), CD::scrollingExpelWindowShortcut(), QMetaType::QString},
        {CD::consumeOrExpelLeftKey(), CD::scrollingConsumeOrExpelLeftShortcut(), QMetaType::QString},
        {CD::consumeOrExpelRightKey(), CD::scrollingConsumeOrExpelRightShortcut(), QMetaType::QString},
        {CD::centerColumnKey(), CD::scrollingCenterColumnShortcut(), QMetaType::QString},
        {CD::toggleColumnTabbedKey(), CD::scrollingToggleColumnTabbedShortcut(), QMetaType::QString},
        {CD::toggleWindowedFullscreenKey(), CD::scrollingToggleWindowedFullscreenShortcut(), QMetaType::QString},
        {CD::cycleColumnWidthKey(), CD::scrollingCycleColumnWidthShortcut(), QMetaType::QString},
        {CD::cycleColumnWidthBackKey(), CD::scrollingCycleColumnWidthBackShortcut(), QMetaType::QString},
        {CD::increaseColumnWidthKey(), CD::scrollingIncreaseColumnWidthShortcut(), QMetaType::QString},
        {CD::decreaseColumnWidthKey(), CD::scrollingDecreaseColumnWidthShortcut(), QMetaType::QString},
        {CD::maximizeColumnKey(), CD::scrollingMaximizeColumnShortcut(), QMetaType::QString},
        {CD::expandColumnKey(), CD::scrollingExpandColumnShortcut(), QMetaType::QString},
        {CD::cycleWindowHeightKey(), CD::scrollingCycleWindowHeightShortcut(), QMetaType::QString},
        {CD::cycleWindowHeightBackKey(), CD::scrollingCycleWindowHeightBackShortcut(), QMetaType::QString},
        {CD::increaseWindowHeightKey(), CD::scrollingIncreaseWindowHeightShortcut(), QMetaType::QString},
        {CD::decreaseWindowHeightKey(), CD::scrollingDecreaseWindowHeightShortcut(), QMetaType::QString},
        {CD::resetWindowHeightsKey(), CD::scrollingResetWindowHeightsShortcut(), QMetaType::QString},
        {CD::centerVisibleColumnsKey(), CD::scrollingCenterVisibleColumnsShortcut(), QMetaType::QString},
        {CD::focusWindowTopKey(), CD::scrollingFocusWindowTopShortcut(), QMetaType::QString},
        {CD::focusWindowBottomKey(), CD::scrollingFocusWindowBottomShortcut(), QMetaType::QString},
        {CD::focusColumnLeftKey(), CD::scrollingFocusColumnLeftShortcut(), QMetaType::QString},
        {CD::focusColumnRightKey(), CD::scrollingFocusColumnRightShortcut(), QMetaType::QString},
        {CD::focusColumnLeftOrLastKey(), CD::scrollingFocusColumnLeftOrLastShortcut(), QMetaType::QString},
        {CD::focusColumnRightOrFirstKey(), CD::scrollingFocusColumnRightOrFirstShortcut(), QMetaType::QString},
        {CD::moveToFloatingKey(), CD::scrollingMoveToFloatingShortcut(), QMetaType::QString},
        {CD::moveToTilingKey(), CD::scrollingMoveToTilingShortcut(), QMetaType::QString},
        {CD::viewPageBackKey(), CD::scrollingViewPageBackShortcut(), QMetaType::QString},
        {CD::viewPageForwardKey(), CD::scrollingViewPageForwardShortcut(), QMetaType::QString},
        {CD::equalizeColumnWidthsKey(), CD::scrollingEqualizeColumnWidthsShortcut(), QMetaType::QString},
        {CD::minimizeColumnWidthKey(), CD::scrollingMinimizeColumnWidthShortcut(), QMetaType::QString},
    };
}

} // namespace PlasmaZones

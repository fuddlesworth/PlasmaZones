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
using SchemaValidators::clampIntOrDefault;
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
static_assert(ConfigDefaults::scrollingHeightKindClientDecides()
                  == static_cast<int>(PhosphorScrollEngine::DefaultHeightKind::ClientDecides),
              "DefaultHeightKind::ClientDecides wire value drifted from ConfigDefaults");
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
// The config layer's proportion FLOOR is hand-mirrored from the engine's own
// (ScrollTypes.h), which the config headers cannot include: the KWin effect
// includes those headers but does not link PhosphorScrollEngine. This
// translation unit does see both, so the pair is pinned here. The engine keeps
// the height floor as its own constant deliberately, so it gets its own
// assertion — note the CONFIG side currently delegates height to width, so a
// height-only retune of the engine needs a config-side split first.
static_assert(ConfigDefaults::scrollingDefaultColumnWidthProportionMin()
                  == PhosphorScrollEngine::MinColumnWidthFraction,
              "Config and engine disagree on the minimum column-width fraction");
static_assert(ConfigDefaults::scrollingWindowHeightProportionMin() == PhosphorScrollEngine::MinWindowHeightFraction,
              "Config and engine disagree on the minimum window-height fraction");

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
        {CD::enabledKey(), CD::scrollingEnabled(), QMetaType::Bool,
         QStringLiteral("Whether scrolling mode can be used at all. Off, it is skipped when cycling a screen's "
                        "placement mode.")},
        // validIntOr (not clampInt) for every enum here: clamping snaps an
        // out-of-range stored value to the MAX enumerator, which both
        // contradicts the sibling file's documented reader-agreement
        // convention and disagrees with the engine's own snap-to-default
        // guard.
        {CD::centerFocusedColumnKey(), CD::scrollingCenterFocusedColumn(), QMetaType::Int,
         QStringLiteral("When the strip re-centers on the focused column. Never leaves the strip still until the "
                        "column would leave the screen, always parks it in the middle, and on overflow centers it only "
                        "once the strip runs past the edge of the screen."),
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
        {CD::stripAxisKey(), CD::scrollingStripAxis(), QMetaType::Int,
         QStringLiteral("Which way the strip runs. Matching the screen shape runs it top to bottom when the usable "
                        "area is taller than it is wide, and side to side otherwise. Columns still divide across the "
                        "strip whichever way it runs."),
         validIntOr(
             {CD::scrollingStripAxisAuto(), CD::scrollingStripAxisHorizontal(), CD::scrollingStripAxisVertical()},
             CD::scrollingStripAxis()),
         intChoices({{CD::scrollingStripAxisAuto(), "auto"_L1},
                     {CD::scrollingStripAxisHorizontal(), "horizontal"_L1},
                     {CD::scrollingStripAxisVertical(), "vertical"_L1}})},
        {CD::alwaysCenterSingleColumnKey(), CD::scrollingAlwaysCenterSingleColumn(), QMetaType::Bool,
         QStringLiteral("When the strip holds a single column, center it even when the focused column is set never to "
                        "center.")},
        {CD::cropStraddlersKey(), CD::scrollingCropStraddlers(), QMetaType::Bool,
         QStringLiteral("Let a column at the screen edge keep its full size and be cut off there. Off, the column "
                        "shrinks to fit, or slides away once too little of it is left. Cropping costs some efficiency "
                        "in fullscreen video and games while any screen uses scrolling.")},
        // NOTE: the width-kind CONFIG space {0 proportion, 1 fixed,
        // 2 clientDecides, 3 preset} must never be static_cast to
        // ColumnWidth::Kind, whose 2 is Preset — the engine translates into
        // that enum with explicit ifs. DefaultWidthKind is the enum this
        // space does match 1:1; see the file-header note above it.
        {CD::defaultColumnWidthKindKey(), CD::scrollingDefaultColumnWidthKind(), QMetaType::Int,
         QStringLiteral("How the width of a new column is decided: as a share of the strip, a fixed number of pixels, "
                        "a preset from the screen's template, or whatever the application asks for."),
         validIntOr({CD::scrollingWidthKindProportion(), CD::scrollingWidthKindFixed(),
                     CD::scrollingWidthKindClientDecides(), CD::scrollingWidthKindPreset()},
                    CD::scrollingDefaultColumnWidthKind()),
         intChoices({{CD::scrollingWidthKindProportion(), "proportion"_L1},
                     {CD::scrollingWidthKindFixed(), "fixed"_L1},
                     {CD::scrollingWidthKindClientDecides(), "clientDecides"_L1},
                     {CD::scrollingWidthKindPreset(), "preset"_L1}})},
        {CD::defaultColumnWidthPresetIndexKey(), CD::scrollingDefaultColumnWidthPresetIndex(), QMetaType::Int,
         QStringLiteral("Which width a new column opens at, counted from zero into the widths of the screen's layout "
                        "template. Only applies when the width kind is preset."),
         clampInt(0, CD::scrollingPresetIndexMax())},
        {CD::defaultColumnWidthValueKey(), CD::scrollingDefaultColumnWidthValue(), QMetaType::Double,
         QStringLiteral("The width a new column opens at. Read as a fraction of the strip when the width kind is a "
                        "proportion, and as pixels when it is fixed."),
         clampDouble(CD::scrollingDefaultColumnWidthProportionMin(), CD::scrollingDefaultColumnWidthFixedMax())},
        {CD::defaultColumnDisplayKey(), CD::scrollingDefaultColumnDisplay(), QMetaType::Int,
         QStringLiteral("How a new column shows its windows. Normal stacks them above each other, tabbed shows one at "
                        "a time behind a tab strip. A screen with a layout template of its own takes this from the "
                        "template instead."),
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
        {CD::presetColumnWidthsKey(), CD::scrollingPresetColumnWidths(), QMetaType::QString,
         QStringLiteral("The column widths the cycle-width shortcuts step through, as a comma-separated list of "
                        "fractions of the work area along the strip."),
         [](const QVariant& v) {
             return canonicalProportionList(v, CD::scrollingPresetColumnWidths(),
                                            CD::scrollingDefaultColumnWidthProportionMax());
         }},
        {CD::presetWindowHeightsKey(), CD::scrollingPresetWindowHeights(), QMetaType::QString,
         QStringLiteral("The window heights the cycle-height shortcuts step through, as a comma-separated list of "
                        "fractions of the work area across the strip."),
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
        {CD::defaultWindowHeightKindKey(), CD::scrollingDefaultWindowHeightKind(), QMetaType::Int,
         QStringLiteral("How the height of a new window is decided: sharing the column evenly, a fixed number of "
                        "pixels, a preset from the screen's template, or whatever the application asks for."),
         validIntOr({CD::scrollingHeightKindAuto(), CD::scrollingHeightKindFixed(), CD::scrollingHeightKindPreset(),
                     CD::scrollingHeightKindClientDecides()},
                    CD::scrollingDefaultWindowHeightKind()),
         intChoices({{CD::scrollingHeightKindAuto(), "auto"_L1},
                     {CD::scrollingHeightKindFixed(), "fixed"_L1},
                     {CD::scrollingHeightKindPreset(), "preset"_L1},
                     {CD::scrollingHeightKindClientDecides(), "clientDecides"_L1}})},
        {CD::defaultWindowHeightValueKey(), CD::scrollingDefaultWindowHeightValue(), QMetaType::Double,
         QStringLiteral("How much space a new window takes inside its column, in pixels. Only applies when the height "
                        "kind is fixed."),
         clampDouble(CD::scrollingDefaultWindowHeightMin(), CD::scrollingDefaultWindowHeightMax())},
        {CD::defaultWindowHeightPresetIndexKey(), CD::scrollingDefaultWindowHeightPresetIndex(), QMetaType::Int,
         QStringLiteral("Which height a new window opens at, counted from zero into the heights of the screen's layout "
                        "template. Only applies when the height kind is preset."),
         clampInt(0, CD::scrollingPresetIndexMax())},
        {CD::defaultTemplateKey(), CD::scrollingDefaultTemplate(), QMetaType::QString,
         QStringLiteral("Layout template a screen uses until it is given one of its own. Empty uses the built-in width "
                        "and height steps.")},
        {CD::wheelFocusEnabledKey(), CD::scrollingWheelFocusEnabled(), QMetaType::Bool,
         QStringLiteral("Turn the wheel with a scroll key held to move along the strip. Off, both scroll keys are left "
                        "to the compositor.")},
        {CD::wheelFocusInvertedKey(), CD::scrollingWheelFocusInverted(), QMetaType::Bool,
         QStringLiteral("Scrolling down moves toward the start of the strip instead of the end, for both scroll "
                        "keys.")},
    };

    // ─── Scrolling wheel chords (Scrolling.Wheel.Focus / .View) ──────────
    // The two "scroll keys": hold this chord and turn the wheel to move
    // column focus along the strip, or to pan the view without moving focus.
    // Ordinary trigger lists: same generic Triggers leaf as every other
    // trigger-bearing node, so the settings editor, the profile diff and the
    // config file all speak one vocabulary. Nothing marks these as
    // wheel-driven, because the wheel is implicit in which GROUP the list
    // sits in, the same way the drag lists leave the drag implicit.
    //
    // The validator is the one thing that differs. canonicalWheelTriggerList
    // drops the AlwaysActive sentinel, which means "match whatever is held"
    // to the subset-matching drag readers but folds to "match only when
    // NOTHING is held" under the exact matcher these lists are read with.
    // Mouse buttons ARE stored: holding a button while turning the wheel is a
    // legal chord, the same shapes the drag lists take. See
    // canonicalWheelTriggerList for the whole argument.
    schema.groups[CD::scrollingWheelFocusGroup()] = {
        {CD::triggersKey(), CD::scrollingWheelFocusTriggers(), QMetaType::QVariantList,
         QStringLiteral("Modifier or mouse button held while turning the wheel to move focus from column to column. "
                        "Each entry is a {modifier, mouseButton} pair."),
         canonicalWheelTriggerList},
    };
    schema.groups[CD::scrollingWheelViewGroup()] = {
        {CD::triggersKey(), CD::scrollingWheelViewTriggers(), QMetaType::QVariantList,
         QStringLiteral("Modifier or mouse button held while turning the wheel to move the view along the strip "
                        "without changing which column has focus. Each entry is a {modifier, mouseButton} pair."),
         canonicalWheelTriggerList},
    };

    // ─── Scrolling tab indicator (Scrolling.TabIndicator) ────────────────
    // The indicator drawn alongside a tabbed column. The two enums get
    // validIntOr closed sets built from the SAME ConfigDefaults enumerators
    // the D-Bus registry guards read, so neither can drift. The colours are
    // free-form strings because EMPTY is the meaningful "follow the theme"
    // value and no closed set can express that alongside arbitrary hex.
    schema.groups[CD::scrollingTabIndicatorGroup()] = {
        {CD::enabledKey(), CD::scrollingTabIndicatorEnabled(), QMetaType::Bool,
         QStringLiteral("Mark a tabbed column's windows on screen. Tabbed columns keep working without it.")},
        {CD::styleKey(), CD::scrollingTabIndicatorStyle(), QMetaType::Int,
         QStringLiteral("Titled chips label each window. A segment bar is thinner and shows only how many there are."),
         validIntOr({CD::scrollingTabIndicatorStyleChips(), CD::scrollingTabIndicatorStyleBar()},
                    CD::scrollingTabIndicatorStyle()),
         intChoices(
             {{CD::scrollingTabIndicatorStyleChips(), "chips"_L1}, {CD::scrollingTabIndicatorStyleBar(), "bar"_L1}})},
        {CD::positionKey(), CD::scrollingTabIndicatorPosition(), QMetaType::Int,
         QStringLiteral("Which edge of the column the indicator runs along."),
         validIntOr({CD::scrollingTabIndicatorPositionLeft(), CD::scrollingTabIndicatorPositionRight(),
                     CD::scrollingTabIndicatorPositionTop(), CD::scrollingTabIndicatorPositionBottom()},
                    CD::scrollingTabIndicatorPosition()),
         intChoices({{CD::scrollingTabIndicatorPositionLeft(), "left"_L1},
                     {CD::scrollingTabIndicatorPositionRight(), "right"_L1},
                     {CD::scrollingTabIndicatorPositionTop(), "top"_L1},
                     {CD::scrollingTabIndicatorPositionBottom(), "bottom"_L1}})},
        {CD::hideWhenSingleTabKey(), CD::scrollingTabIndicatorHideWhenSingleTab(), QMetaType::Bool,
         QStringLiteral("Leave a tabbed column unmarked while it holds only one window.")},
        {CD::placeWithinColumnKey(), CD::scrollingTabIndicatorPlaceWithinColumn(), QMetaType::Bool,
         QStringLiteral("Shrink the windows to fit the indicator. Off, it is drawn beside the column and can overlap a "
                        "neighbour or run off screen.")},
        // The gap floor is NEGATIVE on purpose: niri parity, where a negative
        // gap pulls the indicator on top of the window.
        {CD::gapKey(), CD::scrollingTabIndicatorGap(), QMetaType::Int,
         QStringLiteral("Space between the indicator and the window. A negative gap draws it over the window instead."),
         clampInt(CD::scrollingTabIndicatorGapMin(), CD::scrollingTabIndicatorGapMax())},
        {CD::widthKey(), CD::scrollingTabIndicatorWidth(), QMetaType::Int,
         QStringLiteral("How thick the indicator is. When it makes room inside the column, this is exactly how much "
                        "room it takes. A segment bar reads well at a few pixels. Titled chips need enough for their "
                        "labels, which on a left or right edge means a lot."),
         clampInt(CD::scrollingTabIndicatorWidthMin(), CD::scrollingTabIndicatorWidthMax())},
        {CD::lengthProportionKey(), CD::scrollingTabIndicatorLengthProportion(), QMetaType::Double,
         QStringLiteral("How much of the column edge the indicator spans, centered on it."),
         clampDouble(CD::scrollingTabIndicatorLengthProportionMin(), CD::scrollingTabIndicatorLengthProportionMax())},
        {CD::gapsBetweenTabsKey(), CD::scrollingTabIndicatorGapsBetweenTabs(), QMetaType::Int,
         QStringLiteral("Space separating one tab from the next."),
         clampInt(CD::scrollingTabIndicatorGapsBetweenTabsMin(), CD::scrollingTabIndicatorGapsBetweenTabsMax())},
        // The floor IS the pill sentinel, so the clamp admits -1 and every
        // literal radius but nothing in between.
        {CD::cornerRadiusKey(), CD::scrollingTabIndicatorCornerRadius(), QMetaType::Int,
         QStringLiteral("How rounded each tab's corners are. On a segment bar with no gap between tabs, only the two "
                        "ends of the run are rounded."),
         clampInt(CD::scrollingTabIndicatorCornerRadiusMin(), CD::scrollingTabIndicatorCornerRadiusMax())},
        {CD::activeColorKey(), CD::scrollingTabIndicatorActiveColor(), QMetaType::QString,
         QStringLiteral("Colour of the tab for the window currently shown. Empty follows the colour scheme."),
         canonicalThemeFallbackColor},
        {CD::inactiveColorKey(), CD::scrollingTabIndicatorInactiveColor(), QMetaType::QString,
         QStringLiteral("Colour of the tabs for the windows not currently shown. Empty follows the colour scheme."),
         canonicalThemeFallbackColor},
        {CD::urgentColorKey(), CD::scrollingTabIndicatorUrgentColor(), QMetaType::QString,
         QStringLiteral("Colour of the tab for a window asking for attention. Empty follows the colour scheme."),
         canonicalThemeFallbackColor},
        // The label font. The family is free-form with no validator: EMPTY
        // means the system font, and no closed set can express that alongside
        // an arbitrary installed family. There is no size key — Width gives
        // the pill its thickness and the painter fits the label to it.
        {CD::fontFamilyKey(), CD::scrollingTabIndicatorFontFamily(), QMetaType::QString,
         QStringLiteral("Typeface for the tab labels. Their size comes from the indicator thickness. A segment bar "
                        "draws no labels, so it ignores this. Empty follows the system font."),
         canonicalFontFamily(PhosphorRules::MaxFontFamilyLength)},
        {CD::fontWeightKey(), CD::scrollingTabIndicatorFontWeight(), QMetaType::Int,
         QStringLiteral("Weight of the tab label text, on the usual 100 to 900 scale where 400 is regular and 700 is "
                        "bold."),
         clampInt(CD::scrollingTabIndicatorFontWeightMin(), CD::scrollingTabIndicatorFontWeightMax())},
        {CD::fontItalicKey(), CD::scrollingTabIndicatorFontItalic(), QMetaType::Bool,
         QStringLiteral("Italicize the tab label text.")},
        {CD::fontUnderlineKey(), CD::scrollingTabIndicatorFontUnderline(), QMetaType::Bool,
         QStringLiteral("Underline the tab label text.")},
        {CD::fontStrikeoutKey(), CD::scrollingTabIndicatorFontStrikeout(), QMetaType::Bool,
         QStringLiteral("Strike through the tab label text.")},
    };

    // ─── Scrolling drop indicator (Scrolling.DropIndicator) ──────────────
    // The drop-target highlight painted during a drag re-insert. The colour is
    // a free-form string for the same reason as the tab colours: EMPTY is the
    // meaningful "follow the theme" value.
    schema.groups[CD::scrollingDropIndicatorGroup()] = {
        {CD::enabledKey(), CD::scrollingDropIndicatorEnabled(), QMetaType::Bool,
         QStringLiteral("Show where a dragged window will land in the strip.")},
        {CD::colorKey(), CD::scrollingDropIndicatorColor(), QMetaType::QString,
         QStringLiteral("Colour filling the space the window will land in. Empty follows the colour scheme."),
         canonicalThemeFallbackColor},
        {CD::borderColorKey(), CD::scrollingDropIndicatorBorderColor(), QMetaType::QString,
         QStringLiteral("Colour of the indicator's edge. Empty follows the colour scheme."),
         canonicalThemeFallbackColor},
        {CD::opacityKey(), CD::scrollingDropIndicatorOpacity(), QMetaType::Double,
         QStringLiteral("How solid the fill is. This replaces any transparency carried by the fill colour."),
         clampDouble(CD::scrollingDropIndicatorOpacityMin(), CD::scrollingDropIndicatorOpacityMax())},
        {CD::widthKey(), CD::scrollingDropIndicatorBorderWidth(), QMetaType::Int,
         QStringLiteral("Thickness of the indicator's edge in pixels. Zero draws the fill with no edge."),
         clampInt(CD::scrollingDropIndicatorBorderWidthMin(), CD::scrollingDropIndicatorBorderWidthMax())},
        {CD::radiusKey(), CD::scrollingDropIndicatorBorderRadius(), QMetaType::Int,
         QStringLiteral("Corner rounding of the indicator in pixels."),
         clampInt(CD::scrollingDropIndicatorBorderRadiusMin(), CD::scrollingDropIndicatorBorderRadiusMax())},
    };

    // ─── Scrolling behavior (Scrolling.Behavior) ─────────────────────────
    // Window-handling and focus knobs, the peers of Tiling.Behavior and
    // Snapping.Behavior.WindowHandling. Shared leaf key names under the
    // scrolling group, smart gaps among them: scrolling owns its own SmartGaps
    // rather than forwarding the tiling value, because whether a lone column
    // drops its outer gaps is per-mode behaviour. See the entry below.
    schema.groups[CD::scrollingBehaviorGroup()] = {
        {CD::focusNewWindowsKey(), CD::scrollingFocusNewWindows(), QMetaType::Bool,
         QStringLiteral("Focus a window when it opens.")},
        {CD::triggersKey(), CD::scrollingDragInsertTriggers(), QMetaType::QVariantList,
         QStringLiteral("Modifier and mouse-button combinations that insert a dragged window into the strip under the "
                        "cursor. It becomes a new column, or stacks into the column it lands on. Each entry is a "
                        "{modifier, mouseButton} pair."),
         canonicalTriggerList},
        {CD::toggleActivationKey(), CD::scrollingDragInsertToggle(), QMetaType::Bool,
         QStringLiteral("Tap the re-insert trigger to turn the strip preview on, and tap again to turn it off, instead "
                        "of holding it down.")},
        {CD::releaseGraceMsKey(), CD::scrollingDragInsertGraceMs(), QMetaType::Int,
         QStringLiteral("How long the strip preview stays up after the trigger is released, so a brief slip does not "
                        "cancel the drop."),
         clampInt(CD::triggerGraceMsMin(), CD::triggerGraceMsMax())},
        {CD::insertPositionKey(), CD::scrollingInsertPosition(), QMetaType::Int,
         QStringLiteral("Where a new window's column enters the strip. Restored windows and per-window rules keep "
                        "their own position."),
         validIntOr({CD::scrollingInsertRightOfActive(), CD::scrollingInsertLeftOfActive(), CD::scrollingInsertFirst(),
                     CD::scrollingInsertLast(), CD::scrollingInsertIntoActiveColumn()},
                    CD::scrollingInsertPosition()),
         intChoices({{CD::scrollingInsertRightOfActive(), "rightOfActive"_L1},
                     {CD::scrollingInsertLeftOfActive(), "leftOfActive"_L1},
                     {CD::scrollingInsertFirst(), "first"_L1},
                     {CD::scrollingInsertLast(), "last"_L1},
                     {CD::scrollingInsertIntoActiveColumn(), "intoActiveColumn"_L1}})},
        {CD::focusFollowsMouseKey(), CD::scrollingFocusFollowsMouse(), QMetaType::Bool,
         QStringLiteral("Moving the mouse pointer over a window gives it focus.")},
        {CD::focusFollowsMouseMaxScrollKey(), CD::scrollingFocusFollowsMouseMaxScroll(), QMetaType::Int,
         QStringLiteral("Moving the pointer onto a column that is partly off screen scrolls the strip to bring it in. "
                        "When that scroll would be longer than this share of the work area along the strip, the "
                        "pointer is ignored and focus stays put. At 100 nothing is ignored."),
         // clampIntOrDefault, not clampInt: this key's minimum is 0, and 0 is
         // the most RESTRICTIVE setting (focus follows the pointer only onto
         // columns already fully in view). A corrupt entry read through
         // QVariant::toInt() answers 0, so a plain clamp would turn unreadable
         // config into the strictest possible behaviour instead of the default.
         clampIntOrDefault(CD::scrollingFocusFollowsMouseMaxScrollMin(), CD::scrollingFocusFollowsMouseMaxScrollMax(),
                           CD::scrollingFocusFollowsMouseMaxScroll())},
        {CD::stickyWindowHandlingKey(), CD::scrollingStickyWindowHandling(), QMetaType::Int,
         QStringLiteral("How to treat windows that appear on every desktop."),
         validIntOr(
             {CD::scrollingStickyTreatAsNormal(), CD::scrollingStickyRestoreOnly(), CD::scrollingStickyIgnoreAll()},
             CD::scrollingStickyWindowHandling()),
         intChoices({{CD::scrollingStickyTreatAsNormal(), "treatAsNormal"_L1},
                     {CD::scrollingStickyRestoreOnly(), "restoreOnly"_L1},
                     {CD::scrollingStickyIgnoreAll(), "ignoreAll"_L1}})},
        // Scrolling's OWN smart gaps. Shares the leaf key name with the tiling
        // twin under Tiling.Gaps; the group disambiguates, per the config-key
        // convention. Scrolling previously had no home for this and read the
        // tiling value, which is the mode leak this entry closes.
        {CD::smartGapsKey(), CD::scrollingSmartGaps(), QMetaType::Bool,
         QStringLiteral("Remove the outer gaps while the strip holds a single column, so that column sits against the "
                        "screen edge at its own width.")},
        {CD::respectMinimumSizeKey(), CD::scrollingRespectMinimumSize(), QMetaType::Bool,
         QStringLiteral("Keep columns at least as wide and tall as their windows' minimum size, which can push other "
                        "windows off screen.")},
        {CD::centerShortColumnsKey(), CD::scrollingCenterShortColumns(), QMetaType::Bool,
         QStringLiteral("Center the windows in a column that does not fill the screen, rather than leaving the unused "
                        "space at the end of the column.")},
        {CD::restoreOnLoginKey(), CD::scrollingRestoreStripsOnLogin(), QMetaType::Bool,
         QStringLiteral("When windows reopen after a restart, rebuild their columns with the same order, widths, and "
                        "tab groups.")},
        {CD::restoreFloatedOnLoginKey(), CD::scrollingRestoreFloatedWindowsOnLogin(), QMetaType::Bool,
         QStringLiteral("When a floated window reopens, return it to the position and size it had before rather than "
                        "letting the compositor place it. A rule can opt individual windows in or out.")},
        {CD::keepFloatingAboveKey(), CD::scrollingKeepFloatingAbove(), QMetaType::Bool,
         QStringLiteral("Keep the windows you float stacked above the columns of the strip. A rule that sets a window "
                        "layer takes precedence for the windows it matches.")},
        {CD::columnWidthStepPercentKey(), CD::scrollingColumnWidthStepPercent(), QMetaType::Int,
         QStringLiteral("How far the increase and decrease column width shortcuts resize a column per press, as a "
                        "share of the strip."),
         clampInt(CD::scrollingStepPercentMin(), CD::scrollingStepPercentMax())},
        {CD::windowHeightStepPercentKey(), CD::scrollingWindowHeightStepPercent(), QMetaType::Int,
         QStringLiteral("How far the increase and decrease window height shortcuts resize a window per press, as a "
                        "share of the work area across the strip."),
         clampInt(CD::scrollingStepPercentMin(), CD::scrollingStepPercentMax())},
        {CD::viewScrollStepPercentKey(), CD::scrollingViewScrollStepPercent(), QMetaType::Int,
         QStringLiteral("How far one notch of the view scroll key moves the strip without changing focus, as a share "
                        "of the work area along the strip."),
         clampInt(CD::scrollingStepPercentMin(), CD::scrollingStepPercentMax())},
    };

    // ─── Edge auto-scroll (Scrolling.Behavior.DragScroll) ────────────────
    // niri's dnd-edge-view-scroll. The three figures are clamped rather
    // than validated against a list: they are continuous quantities, and
    // the engine's own floors (a trigger width of at least one pixel, a
    // positive speed) are a second line of defence for a hand-edited file
    // that predates a range change.
    schema.groups[CD::scrollingDragScrollGroup()] = {
        {CD::enabledKey(), CD::scrollingDragScrollEnabled(), QMetaType::Bool,
         QStringLiteral("Scroll the strip when a dragged window is held near the edge of the work area.")},
        {CD::triggerWidthKey(), CD::scrollingDragScrollTriggerWidth(), QMetaType::Int,
         QStringLiteral("How close to the edge of the work area the pointer has to be before the strip can start "
                        "scrolling."),
         clampInt(CD::scrollingDragScrollTriggerWidthMin(), CD::scrollingDragScrollTriggerWidthMax())},
        {CD::delayMsKey(), CD::scrollingDragScrollDelayMs(), QMetaType::Int,
         QStringLiteral("How long the pointer has to stay near the edge before the strip moves. Stops a drag that only "
                        "passes by an edge from scrolling."),
         clampInt(CD::scrollingDragScrollDelayMsMin(), CD::scrollingDragScrollDelayMsMax())},
        {CD::maxSpeedKey(), CD::scrollingDragScrollMaxSpeed(), QMetaType::Int,
         QStringLiteral("How fast the strip scrolls with the pointer held right at the edge. It moves more slowly the "
                        "further from the edge the pointer sits."),
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
        {CD::enabledKey(), CD::scrollingZoneSelectorEnabled(), QMetaType::Bool,
         QStringLiteral("Show a template picker when a window is dragged to a screen edge.")},
        {CD::triggerDistanceKey(), CD::scrollingZoneSelectorTriggerDistance(), QMetaType::Int,
         QStringLiteral("How close to the screen edge a drag has to come before the picker opens."),
         clampInt(CD::triggerDistanceMin(), CD::triggerDistanceMax())},
        {CD::positionKey(), CD::scrollingZoneSelectorPosition(), QMetaType::Int,
         QStringLiteral("Where on the screen the picker appears."),
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
        {CD::sizeModeKey(), CD::scrollingZoneSelectorSizeMode(), QMetaType::Int,
         QStringLiteral("Whether preview size is chosen for you or taken from the width and height below."),
         validIntOr({static_cast<int>(ZoneSelectorSizeMode::Auto), static_cast<int>(ZoneSelectorSizeMode::Manual)},
                    CD::scrollingZoneSelectorSizeMode()),
         intChoices({{static_cast<int>(ZoneSelectorSizeMode::Auto), "auto"_L1},
                     {static_cast<int>(ZoneSelectorSizeMode::Manual), "manual"_L1}})},
        {CD::previewWidthKey(), CD::scrollingZoneSelectorPreviewWidth(), QMetaType::Int,
         QStringLiteral("Width of each template preview in the picker. Only applies when the size mode is manual."),
         clampInt(CD::previewWidthMin(), CD::previewWidthMax())},
        {CD::previewHeightKey(), CD::scrollingZoneSelectorPreviewHeight(), QMetaType::Int,
         QStringLiteral("Height of each template preview in the picker. Only applies when the size mode is manual and "
                        "the aspect ratio is unlocked."),
         clampInt(CD::previewHeightMin(), CD::previewHeightMax())},
        {CD::previewLockAspectKey(), CD::scrollingZoneSelectorPreviewLockAspect(), QMetaType::Bool,
         QStringLiteral("Derive the preview height from its width using the screen's aspect ratio, so previews match "
                        "the shape of the screen.")},
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
        {CD::focusColumnFirstKey(), CD::scrollingFocusColumnFirstShortcut(), QMetaType::QString,
         QStringLiteral("Moves focus to the first column.")},
        {CD::focusColumnLastKey(), CD::scrollingFocusColumnLastShortcut(), QMetaType::QString,
         QStringLiteral("Moves focus to the last column.")},
        {CD::moveColumnToFirstKey(), CD::scrollingMoveColumnToFirstShortcut(), QMetaType::QString,
         QStringLiteral("Moves the focused column to the first position.")},
        {CD::moveColumnToLastKey(), CD::scrollingMoveColumnToLastShortcut(), QMetaType::QString,
         QStringLiteral("Moves the focused column to the last position.")},
        {CD::consumeWindowKey(), CD::scrollingConsumeWindowShortcut(), QMetaType::QString,
         QStringLiteral("Pulls a window from the next column into the focused column, stacking them.")},
        {CD::expelWindowKey(), CD::scrollingExpelWindowShortcut(), QMetaType::QString,
         QStringLiteral("Moves the focused window out of a shared column into a new column after it.")},
        {CD::consumeOrExpelLeftKey(), CD::scrollingConsumeOrExpelLeftShortcut(), QMetaType::QString,
         QStringLiteral("Splits the focused window out of a shared column toward the start of the strip. A window "
                        "alone in its column merges into the previous column instead.")},
        {CD::consumeOrExpelRightKey(), CD::scrollingConsumeOrExpelRightShortcut(), QMetaType::QString,
         QStringLiteral("Splits the focused window out of a shared column toward the end of the strip. A window alone "
                        "in its column merges into the next column instead.")},
        {CD::centerColumnKey(), CD::scrollingCenterColumnShortcut(), QMetaType::QString,
         QStringLiteral("Scrolls the view so the focused column sits centered on the screen.")},
        {CD::toggleColumnTabbedKey(), CD::scrollingToggleColumnTabbedShortcut(), QMetaType::QString,
         QStringLiteral("Switches the focused column between stacked windows and tabs.")},
        {CD::toggleWindowedFullscreenKey(), CD::scrollingToggleWindowedFullscreenShortcut(), QMetaType::QString,
         QStringLiteral("Puts the focused window into its fullscreen presentation while it keeps its place in the "
                        "column, so it does not cover the screen. Press again to leave it.")},
        {CD::cycleColumnWidthKey(), CD::scrollingCycleColumnWidthShortcut(), QMetaType::QString,
         QStringLiteral("Steps the focused column through the screen's size presets along the strip.")},
        {CD::cycleColumnWidthBackKey(), CD::scrollingCycleColumnWidthBackShortcut(), QMetaType::QString,
         QStringLiteral("Steps the focused column through the screen's size presets along the strip, in reverse.")},
        {CD::increaseColumnWidthKey(), CD::scrollingIncreaseColumnWidthShortcut(), QMetaType::QString,
         QStringLiteral("Grows the focused column along the strip by the configured step.")},
        {CD::decreaseColumnWidthKey(), CD::scrollingDecreaseColumnWidthShortcut(), QMetaType::QString,
         QStringLiteral("Shrinks the focused column along the strip by the configured step.")},
        {CD::maximizeColumnKey(), CD::scrollingMaximizeColumnShortcut(), QMetaType::QString,
         QStringLiteral("Toggles the focused column between filling the work area and a smaller size.")},
        {CD::maximizeToEdgesKey(), CD::scrollingMaximizeToEdgesShortcut(), QMetaType::QString,
         QStringLiteral("Toggles the focused column between covering the whole work area with no gaps and its "
                        "normal size. This is the state the window's maximize button shows.")},
        {CD::expandColumnKey(), CD::scrollingExpandColumnShortcut(), QMetaType::QString,
         QStringLiteral("Grows the focused column to fill the empty space visible on screen. Other columns keep their "
                        "size.")},
        {CD::cycleWindowHeightKey(), CD::scrollingCycleWindowHeightShortcut(), QMetaType::QString,
         QStringLiteral("Steps the focused window through the screen's size presets within its column.")},
        {CD::cycleWindowHeightBackKey(), CD::scrollingCycleWindowHeightBackShortcut(), QMetaType::QString,
         QStringLiteral("Steps the focused window through the screen's size presets within its column, in reverse.")},
        {CD::increaseWindowHeightKey(), CD::scrollingIncreaseWindowHeightShortcut(), QMetaType::QString,
         QStringLiteral("Grows the focused window within its column by the configured step.")},
        {CD::decreaseWindowHeightKey(), CD::scrollingDecreaseWindowHeightShortcut(), QMetaType::QString,
         QStringLiteral("Shrinks the focused window within its column by the configured step.")},
        {CD::maximizeWindowHeightKey(), CD::scrollingMaximizeWindowHeightShortcut(), QMetaType::QString,
         QStringLiteral("Toggles the focused window between filling its column and sharing it evenly with the other "
                        "windows there.")},
        {CD::expandWindowKey(), CD::scrollingExpandWindowShortcut(), QMetaType::QString,
         QStringLiteral("Grows the focused window to fill the empty space left in its column. The other windows there "
                        "keep their size.")},
        {CD::centerVisibleColumnsKey(), CD::scrollingCenterVisibleColumnsShortcut(), QMetaType::QString,
         QStringLiteral("Scrolls the view so the fully visible columns sit centered as a group.")},
        {CD::focusWindowTopKey(), CD::scrollingFocusWindowTopShortcut(), QMetaType::QString,
         QStringLiteral("Moves focus to the first window of the focused column.")},
        {CD::focusWindowBottomKey(), CD::scrollingFocusWindowBottomShortcut(), QMetaType::QString,
         QStringLiteral("Moves focus to the last window of the focused column.")},
        {CD::focusColumnLeftKey(), CD::scrollingFocusColumnLeftShortcut(), QMetaType::QString,
         QStringLiteral("Moves focus one column toward the start of the strip and stops at the edge. The regular focus "
                        "shortcut continues onto the next monitor instead.")},
        {CD::focusColumnRightKey(), CD::scrollingFocusColumnRightShortcut(), QMetaType::QString,
         QStringLiteral("Moves focus one column toward the end of the strip and stops at the edge. The regular focus "
                        "shortcut continues onto the next monitor instead.")},
        {CD::focusColumnLeftOrLastKey(), CD::scrollingFocusColumnLeftOrLastShortcut(), QMetaType::QString,
         QStringLiteral("Moves focus one column toward the start of the strip, wrapping to the last column at the "
                        "edge.")},
        {CD::focusColumnRightOrFirstKey(), CD::scrollingFocusColumnRightOrFirstShortcut(), QMetaType::QString,
         QStringLiteral("Moves focus one column toward the end of the strip, wrapping to the first column at the "
                        "edge.")},
        {CD::moveToFloatingKey(), CD::scrollingMoveToFloatingShortcut(), QMetaType::QString,
         QStringLiteral("Makes the focused window float. Unlike the float toggle, it never re-tiles.")},
        {CD::moveToTilingKey(), CD::scrollingMoveToTilingShortcut(), QMetaType::QString,
         QStringLiteral("Returns the focused floating window to its column. Unlike the float toggle, it never "
                        "floats.")},
        {CD::viewPageBackKey(), CD::scrollingViewPageBackShortcut(), QMetaType::QString,
         QStringLiteral("Scrolls the view toward the start of the strip by a whole screen. Focus stays where it is.")},
        {CD::viewPageForwardKey(), CD::scrollingViewPageForwardShortcut(), QMetaType::QString,
         QStringLiteral("Scrolls the view toward the end of the strip by a whole screen. Focus stays where it is.")},
        {CD::equalizeColumnWidthsKey(), CD::scrollingEqualizeColumnWidthsShortcut(), QMetaType::QString,
         QStringLiteral("Gives every column fully on screen an equal share of the screen. Columns clipped at an edge "
                        "are left alone.")},
        {CD::minimizeColumnWidthKey(), CD::scrollingMinimizeColumnWidthShortcut(), QMetaType::QString,
         QStringLiteral("Shrinks the focused column to the smallest size preset.")},
        {CD::equalizeWindowHeightsKey(), CD::scrollingEqualizeWindowHeightsShortcut(), QMetaType::QString,
         QStringLiteral("Gives every window in the focused column an equal share of it.")},
        {CD::minimizeWindowHeightKey(), CD::scrollingMinimizeWindowHeightShortcut(), QMetaType::QString,
         QStringLiteral("Shrinks the focused window to the smallest size preset.")},
    };
}

} // namespace PlasmaZones

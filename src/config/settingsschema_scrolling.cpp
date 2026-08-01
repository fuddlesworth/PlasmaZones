// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The scrolling half of the settings schema: the Scrolling knobs and
// the Shortcuts.Scrolling chords. Split out of settingsschema.cpp for
// file-size; the shared validator helpers live in settingsschema_p.h and the
// two entry points are declared alongside every other appendXxxSchema in
// settingsschema.h.

#include "settingsschema.h"

#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorScrollEngine/ScrollTypes.h>

#include "settingsschemachoices.h"
#include "settingsschema_p.h"

#include "configdefaults.h"

using namespace Qt::StringLiterals;

namespace PlasmaZones {

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
        {CD::alwaysCenterSingleColumnKey(), CD::scrollingAlwaysCenterSingleColumn(), QMetaType::Bool},
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
             return canonicalProportionList(v, CD::scrollingPresetWindowHeights(),
                                            CD::scrollingDefaultColumnWidthProportionMax());
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
        {CD::tabIndicatorStyleKey(),
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
        {CD::activeColorKey(), CD::scrollingTabIndicatorActiveColor(), QMetaType::QString},
        {CD::inactiveColorKey(), CD::scrollingTabIndicatorInactiveColor(), QMetaType::QString},
        {CD::urgentColorKey(), CD::scrollingTabIndicatorUrgentColor(), QMetaType::QString},
    };

    // ─── Scrolling behavior (Scrolling.Behavior) ─────────────────────────
    // Window-handling and focus knobs, the peers of Tiling.Behavior and
    // Snapping.Behavior.WindowHandling. Shared leaf key names under the
    // scrolling group; smart gaps is deliberately absent (scrolling forwards
    // the shared Tiling.Gaps/SmartGaps value, see IScrollSettings).
    schema.groups[CD::scrollingBehaviorGroup()] = {
        {CD::focusNewWindowsKey(), CD::scrollingFocusNewWindows(), QMetaType::Bool},
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
    };
}

// ─── Scrolling shortcuts (Shortcuts.Scrolling) ──────────────────────────────
// Called from appendShortcutsSchema so the whole Shortcuts.* family is still
// declared by one entry point.
//
// These 21 chords are bindable via the system Shortcuts KCM, because
// ShortcutManager registers them like every other action. The settings app has
// no page of its own for them, and the Snapping / Tiling quick-shortcut pages
// are not a counter-example: those assign layouts and algorithms to the
// numbered quick slots rather than editing chords, and scrolling has no
// numbered-slot family to assign.

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
    };
}

} // namespace PlasmaZones

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The scrolling half of the settings schema: the Scrolling knobs and
// the Shortcuts.Scrolling chords. Split out of settingsschema.cpp for
// file-size; the shared validator helpers live in settingsschema_p.h and the
// two entry points are declared alongside every other appendXxxSchema in
// settingsschema.h.

#include "settingsschema.h"

#include <PhosphorScrollEngine/ScrollTypes.h>

#include "settingsschemachoices.h"
#include "settingsschema_p.h"

#include "configdefaults.h"

using namespace Qt::StringLiterals;

namespace PlasmaZones {

using SchemaValidators::canonicalProportionList;
using SchemaValidators::clampDouble;
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
        // 2 clientDecides} deliberately does NOT map onto
        // ColumnWidth::Kind (whose 2 is Preset) — never static_cast between
        // them; the engine translates with explicit ifs.
        {CD::defaultColumnWidthKindKey(),
         CD::scrollingDefaultColumnWidthKind(),
         QMetaType::Int,
         {},
         validIntOr(
             {CD::scrollingWidthKindProportion(), CD::scrollingWidthKindFixed(), CD::scrollingWidthKindClientDecides()},
             CD::scrollingDefaultColumnWidthKind()),
         intChoices({{CD::scrollingWidthKindProportion(), "proportion"_L1},
                     {CD::scrollingWidthKindFixed(), "fixed"_L1},
                     {CD::scrollingWidthKindClientDecides(), "clientDecides"_L1}})},
        {CD::defaultColumnWidthValueKey(),
         CD::scrollingDefaultColumnWidthValue(),
         QMetaType::Double,
         {},
         clampDouble(CD::scrollingDefaultColumnWidthValueMin(), CD::scrollingDefaultColumnWidthFixedMax())},
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
             return canonicalProportionList(v, CD::scrollingPresetColumnWidths());
         }},
        {CD::presetWindowHeightsKey(),
         CD::scrollingPresetWindowHeights(),
         QMetaType::QString,
         {},
         [](const QVariant& v) {
             return canonicalProportionList(v, CD::scrollingPresetWindowHeights());
         }},
    };
}

// ─── Scrolling shortcuts (Shortcuts.Scrolling) ──────────────────────────────
// Called from appendShortcutsSchema so the whole Shortcuts.* family is still
// declared by one entry point.
//
// These 20 chords are bindable via the system Shortcuts KCM, because
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
        {CD::increaseWindowHeightKey(), CD::scrollingIncreaseWindowHeightShortcut(), QMetaType::QString},
        {CD::decreaseWindowHeightKey(), CD::scrollingDecreaseWindowHeightShortcut(), QMetaType::QString},
        {CD::resetWindowHeightsKey(), CD::scrollingResetWindowHeightsShortcut(), QMetaType::QString},
    };
}

} // namespace PlasmaZones

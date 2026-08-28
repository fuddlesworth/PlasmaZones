// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The tiling half of the settings schema: the Tiling.* groups (Algorithm,
// Behavior, Gaps) and the Tiling.Enabled toggle. Split out of
// settingsschema.cpp for file-size the way the scrolling TU was; the shared
// validator helpers live in settingsschema_p.h and the entry point
// (appendAutotilingSchema) is declared alongside every other appendXxxSchema
// in settingsschema.h. sanitizePerAlgorithmSettings stays file-local: the
// per-algorithm settings map is the one key that round-trips through
// AutotileConfig, so this is its only consumer.

#include "settingsschema.h"

#include <PhosphorTileEngine/AutotileConfig.h>

#include "configdefaults.h"
#include "core/types/enums.h"
#include "settingsschema_p.h"
#include "settingsschemachoices.h"

using namespace Qt::StringLiterals;

namespace PlasmaZones {

using SchemaValidators::canonicalCommaList;
using SchemaValidators::clampDouble;
using SchemaValidators::clampInt;
using SchemaValidators::validIntOr;

namespace {

/// Canonicalize a per-algorithm settings map: round-trip through
/// @c AutotileConfig so each algorithm's settings are validated against
/// its schema and unknown keys are dropped. Idempotent:
/// @c perAlgoToVariantMap(perAlgoFromVariantMap(x)) == perAlgoToVariantMap(perAlgoFromVariantMap(it)).
QVariant sanitizePerAlgorithmSettings(const QVariant& v)
{
    return QVariant(PhosphorTileEngine::AutotileConfig::perAlgoToVariantMap(
        PhosphorTileEngine::AutotileConfig::perAlgoFromVariantMap(v.toMap())));
}

} // namespace

// ─── Autotiling ─────────────────────────────────────────────────────────────
// Tiling.* has three sub-groups: Algorithm, Behavior, Gaps. Plus the top-level
// Tiling.Enabled toggle. (The Appearance.{Colors,Decorations,Borders} groups
// that used to live here are gone — window border and title-bar appearance moved
// to the top-level mode-neutral Windows config group, see appendWindowsSchema.)
// PerAlgorithmSettings is a JSON-encoded QVariantMap;
// LockedScreens is a comma list; DragInsert triggers are a JSON list.

void appendAutotilingSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;

    schema.groups[CD::tilingGroup()] = {
        {CD::enabledKey(), CD::autotileEnabled(), QMetaType::Bool,
         QStringLiteral("Whether autotile mode can be used at all. Off, it is skipped when cycling a screen's "
                        "placement mode.")},
    };

    schema.groups[CD::tilingAlgorithmGroup()] = {
        {CD::defaultKey(), CD::defaultAutotileAlgorithm(), QMetaType::QString,
         QStringLiteral("Algorithm a screen tiles with until it is given one of its own.")},
        {CD::splitRatioKey(), CD::autotileSplitRatio(), QMetaType::Double,
         QStringLiteral("Share of the space the master area takes, as a fraction. Algorithms without a master area "
                        "ignore it."),
         clampDouble(CD::autotileSplitRatioMin(), CD::autotileSplitRatioMax())},
        {CD::splitRatioStepKey(), CD::autotileSplitRatioStep(), QMetaType::Double,
         QStringLiteral("Amount the split ratio changes per keyboard shortcut press."),
         clampDouble(CD::autotileSplitRatioStepMin(), CD::autotileSplitRatioStepMax())},
        {CD::masterCountKey(), CD::autotileMasterCount(), QMetaType::Int,
         QStringLiteral("How many windows the master area holds."),
         clampInt(CD::autotileMasterCountMin(), CD::autotileMasterCountMax())},
        {CD::maxWindowsKey(), CD::autotileMaxWindows(), QMetaType::Int,
         QStringLiteral("Maximum number of windows to tile."),
         clampInt(CD::autotileMaxWindowsMin(), CD::autotileMaxWindowsMax())},
        {CD::perAlgorithmSettingsKey(), CD::autotilePerAlgorithmSettings(), QMetaType::QVariantMap,
         QStringLiteral("Custom parameter values per algorithm, keyed by algorithm id. The tiling page writes this; it "
                        "is not meant to be edited by hand."),
         sanitizePerAlgorithmSettings},
    };

    schema.groups[CD::tilingBehaviorGroup()] = {
        {CD::insertPositionKey(), CD::autotileInsertPosition(), QMetaType::Int,
         QStringLiteral("Where a newly opened window lands in the tiling order."),
         validIntOr({static_cast<int>(AutotileInsertPosition::End),
                     static_cast<int>(AutotileInsertPosition::AfterFocused),
                     static_cast<int>(AutotileInsertPosition::AsMaster)},
                    CD::autotileInsertPosition()),
         intChoices({{static_cast<int>(AutotileInsertPosition::End), "end"_L1},
                     {static_cast<int>(AutotileInsertPosition::AfterFocused), "afterFocused"_L1},
                     {static_cast<int>(AutotileInsertPosition::AsMaster), "asMaster"_L1}})},
        {CD::focusNewWindowsKey(), CD::autotileFocusNewWindows(), QMetaType::Bool,
         QStringLiteral("Focus a window when it opens.")},
        {CD::focusFollowsMouseKey(), CD::autotileFocusFollowsMouse(), QMetaType::Bool,
         QStringLiteral("Moving the mouse pointer over a window gives it focus.")},
        {CD::respectMinimumSizeKey(), CD::autotileRespectMinimumSize(), QMetaType::Bool,
         QStringLiteral("Stop windows being resized below their minimum size, which may leave gaps in the layout.")},
        {CD::restoreFloatedOnLoginKey(), CD::autotileRestoreFloatedWindowsOnLogin(), QMetaType::Bool,
         QStringLiteral("Return a floated window to the position and monitor it was on when it reopens after a "
                        "logout. A rule can opt individual windows in or out.")},
        {CD::keepFloatingAboveKey(), CD::autotileKeepFloatingAbove(), QMetaType::Bool,
         QStringLiteral("Keep the windows you float stacked above the tiled windows. A rule that sets a window "
                        "layer takes precedence for the windows it matches.")},
        {CD::stickyWindowHandlingKey(), CD::autotileStickyWindowHandling(), QMetaType::Int,
         QStringLiteral("How to treat windows that appear on every desktop."),
         validIntOr({static_cast<int>(StickyWindowHandling::TreatAsNormal),
                     static_cast<int>(StickyWindowHandling::RestoreOnly),
                     static_cast<int>(StickyWindowHandling::IgnoreAll)},
                    CD::autotileStickyWindowHandling()),
         intChoices({{static_cast<int>(StickyWindowHandling::TreatAsNormal), "treatAsNormal"_L1},
                     {static_cast<int>(StickyWindowHandling::RestoreOnly), "restoreOnly"_L1},
                     {static_cast<int>(StickyWindowHandling::IgnoreAll), "ignoreAll"_L1}})},
        {CD::dragBehaviorKey(), CD::autotileDragBehavior(), QMetaType::Int,
         QStringLiteral("Float converts a dragged tile to free-floating. Reorder keeps it tiled and swaps it into the "
                        "drop slot."),
         validIntOr({static_cast<int>(AutotileDragBehavior::Float), static_cast<int>(AutotileDragBehavior::Reorder)},
                    CD::autotileDragBehavior()),
         intChoices({{static_cast<int>(AutotileDragBehavior::Float), "float"_L1},
                     {static_cast<int>(AutotileDragBehavior::Reorder), "reorder"_L1}})},
        {CD::overflowBehaviorKey(), CD::autotileOverflowBehavior(), QMetaType::Int,
         QStringLiteral("Float leaves windows beyond the max-windows cap floating. Unlimited tiles every window "
                        "regardless of count."),
         validIntOr(
             {static_cast<int>(AutotileOverflowBehavior::Float), static_cast<int>(AutotileOverflowBehavior::Unlimited)},
             CD::autotileOverflowBehavior()),
         intChoices({{static_cast<int>(AutotileOverflowBehavior::Float), "float"_L1},
                     {static_cast<int>(AutotileOverflowBehavior::Unlimited), "unlimited"_L1}})},
        {CD::lockedScreensKey(), CD::autotileLockedScreens(), QMetaType::QString,
         QStringLiteral("Screens whose tiling layout is locked, as a comma-separated list of screen "
                        "ids."),
         canonicalCommaList},
        {CD::triggersKey(), CD::autotileDragInsertTriggers(), QMetaType::QVariantList,
         QStringLiteral("Modifier and mouse-button combinations that re-insert a dragged window into the stack at "
                        "the cursor. Each entry is a {modifier, mouseButton} pair."),
         canonicalTriggerList},
        {CD::toggleActivationKey(), CD::autotileDragInsertToggle(), QMetaType::Bool,
         QStringLiteral("Tap the re-insert trigger to turn the stack preview on, and tap again to turn it off, "
                        "instead of holding it down.")},
        {CD::releaseGraceMsKey(), CD::autotileDragInsertGraceMs(), QMetaType::Int,
         QStringLiteral("How long the stack preview stays up after the trigger is released, so a brief slip does not "
                        "cancel the drop."),
         clampInt(CD::triggerGraceMsMin(), CD::triggerGraceMsMax())},
    };

    // Tiling.Gaps keeps only the tiling-specific SmartGaps toggle. The shared
    // inner/outer gaps live in the top-level Gaps group (appendGapsSchema).
    schema.groups[CD::tilingGapsGroup()] = {
        {CD::smartGapsKey(), CD::autotileSmartGaps(), QMetaType::Bool,
         QStringLiteral("Remove all gaps when only one window is tiled.")},
    };
}

} // namespace PlasmaZones

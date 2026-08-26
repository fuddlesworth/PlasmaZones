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
        {CD::enabledKey(), CD::autotileEnabled(), QMetaType::Bool},
    };

    schema.groups[CD::tilingAlgorithmGroup()] = {
        {CD::defaultKey(), CD::defaultAutotileAlgorithm(), QMetaType::QString},
        {CD::splitRatioKey(),
         CD::autotileSplitRatio(),
         QMetaType::Double,
         {},
         clampDouble(CD::autotileSplitRatioMin(), CD::autotileSplitRatioMax())},
        {CD::splitRatioStepKey(),
         CD::autotileSplitRatioStep(),
         QMetaType::Double,
         {},
         clampDouble(CD::autotileSplitRatioStepMin(), CD::autotileSplitRatioStepMax())},
        {CD::masterCountKey(),
         CD::autotileMasterCount(),
         QMetaType::Int,
         {},
         clampInt(CD::autotileMasterCountMin(), CD::autotileMasterCountMax())},
        {CD::maxWindowsKey(),
         CD::autotileMaxWindows(),
         QMetaType::Int,
         {},
         clampInt(CD::autotileMaxWindowsMin(), CD::autotileMaxWindowsMax())},
        {CD::perAlgorithmSettingsKey(),
         CD::autotilePerAlgorithmSettings(),
         QMetaType::QVariantMap,
         {},
         sanitizePerAlgorithmSettings},
    };

    schema.groups[CD::tilingBehaviorGroup()] = {
        {CD::insertPositionKey(),
         CD::autotileInsertPosition(),
         QMetaType::Int,
         {},
         validIntOr({static_cast<int>(AutotileInsertPosition::End),
                     static_cast<int>(AutotileInsertPosition::AfterFocused),
                     static_cast<int>(AutotileInsertPosition::AsMaster)},
                    CD::autotileInsertPosition()),
         intChoices({{static_cast<int>(AutotileInsertPosition::End), "end"_L1},
                     {static_cast<int>(AutotileInsertPosition::AfterFocused), "afterFocused"_L1},
                     {static_cast<int>(AutotileInsertPosition::AsMaster), "asMaster"_L1}})},
        {CD::focusNewWindowsKey(), CD::autotileFocusNewWindows(), QMetaType::Bool},
        {CD::focusFollowsMouseKey(), CD::autotileFocusFollowsMouse(), QMetaType::Bool},
        {CD::respectMinimumSizeKey(), CD::autotileRespectMinimumSize(), QMetaType::Bool},
        {CD::restoreFloatedOnLoginKey(), CD::autotileRestoreFloatedWindowsOnLogin(), QMetaType::Bool},
        {CD::keepFloatingAboveKey(), CD::autotileKeepFloatingAbove(), QMetaType::Bool},
        {CD::stickyWindowHandlingKey(),
         CD::autotileStickyWindowHandling(),
         QMetaType::Int,
         {},
         validIntOr({static_cast<int>(StickyWindowHandling::TreatAsNormal),
                     static_cast<int>(StickyWindowHandling::RestoreOnly),
                     static_cast<int>(StickyWindowHandling::IgnoreAll)},
                    CD::autotileStickyWindowHandling()),
         intChoices({{static_cast<int>(StickyWindowHandling::TreatAsNormal), "treatAsNormal"_L1},
                     {static_cast<int>(StickyWindowHandling::RestoreOnly), "restoreOnly"_L1},
                     {static_cast<int>(StickyWindowHandling::IgnoreAll), "ignoreAll"_L1}})},
        {CD::dragBehaviorKey(),
         CD::autotileDragBehavior(),
         QMetaType::Int,
         {},
         validIntOr({static_cast<int>(AutotileDragBehavior::Float), static_cast<int>(AutotileDragBehavior::Reorder)},
                    CD::autotileDragBehavior()),
         intChoices({{static_cast<int>(AutotileDragBehavior::Float), "float"_L1},
                     {static_cast<int>(AutotileDragBehavior::Reorder), "reorder"_L1}})},
        {CD::overflowBehaviorKey(),
         CD::autotileOverflowBehavior(),
         QMetaType::Int,
         {},
         validIntOr(
             {static_cast<int>(AutotileOverflowBehavior::Float), static_cast<int>(AutotileOverflowBehavior::Unlimited)},
             CD::autotileOverflowBehavior()),
         intChoices({{static_cast<int>(AutotileOverflowBehavior::Float), "float"_L1},
                     {static_cast<int>(AutotileOverflowBehavior::Unlimited), "unlimited"_L1}})},
        {CD::lockedScreensKey(), CD::autotileLockedScreens(), QMetaType::QString, {}, canonicalCommaList},
        {CD::triggersKey(), CD::autotileDragInsertTriggers(), QMetaType::QVariantList, {}, canonicalTriggerList},
        {CD::toggleActivationKey(), CD::autotileDragInsertToggle(), QMetaType::Bool},
        {CD::releaseGraceMsKey(),
         CD::autotileDragInsertGraceMs(),
         QMetaType::Int,
         {},
         clampInt(CD::triggerGraceMsMin(), CD::triggerGraceMsMax())},
    };

    // Tiling.Gaps keeps only the tiling-specific SmartGaps toggle. The shared
    // inner/outer gaps live in the top-level Gaps group (appendGapsSchema).
    schema.groups[CD::tilingGapsGroup()] = {
        {CD::smartGapsKey(), CD::autotileSmartGaps(), QMetaType::Bool},
    };
}

} // namespace PlasmaZones

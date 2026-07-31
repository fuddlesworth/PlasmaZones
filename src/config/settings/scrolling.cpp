// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config/settings.h"
#include "config/settings/settings_detail.h"
#include "config/configdefaults.h"
#include "core/platform/logging.h"

namespace PlasmaZones {

// ── Scrolling (PhosphorConfig::Store-backed) ────────────────────────────────
// Scalars live in m_store under Scrolling; the schema validators own
// the enum/list validation (validIntOr / canonicalProportionList). The width
// value's REAL clamp is kind-aware, because the schema's clampDouble alone
// spans both kinds' ranges and so cannot reject a value that is out of range
// for the kind actually in force. Two places apply it: the hand-written value
// setter below, and normalizeScrollingColumnWidthValue for anything that
// reached the store without passing that setter.

namespace {
/// Clamp into the kind's range. What the SETTER applies: a user dragging a
/// slider or typing in the SpinBox wants the nearest legal value, not a jump
/// back to a default.
qreal clampColumnWidthForKind(qreal value, bool isFixed)
{
    return isFixed ? qBound<qreal>(ConfigDefaults::scrollingDefaultColumnWidthFixedMin(), value,
                                   ConfigDefaults::scrollingDefaultColumnWidthFixedMax())
                   : qBound<qreal>(ConfigDefaults::scrollingDefaultColumnWidthValueMin(), value,
                                   ConfigDefaults::scrollingDefaultColumnWidthProportionMax());
}

/// Repair a value that cannot belong to @p isFixed at all, the way the KIND
/// setter's arms do: re-seed rather than clamp.
///
/// Clamping is wrong for this case and quietly produces the exact failure the
/// kind setter exists to avoid. A proportion of 0.5 left behind under Fixed
/// clamps to the 100px floor; worse, a pixel count of 800 left behind under
/// Proportion clamps to 1.0, which opens every column at 100% of the work
/// area. The two repair paths have to agree, or the same broken pair heals
/// differently depending on whether a kind flip or a config load found it.
qreal reseedColumnWidthForKind(qreal value, bool isFixed)
{
    if (isFixed && value < ConfigDefaults::scrollingDefaultColumnWidthFixedMin()) {
        return ConfigDefaults::scrollingDefaultColumnWidthFixedPx();
    }
    if (!isFixed && value > ConfigDefaults::scrollingDefaultColumnWidthProportionMax()) {
        return ConfigDefaults::scrollingDefaultColumnWidthValue();
    }
    // In-kind but out of range: a clamp is the right repair, since the value
    // is the right SORT of thing.
    //
    // UNREACHABLE in practice, and deliberately kept as defence rather than
    // removed: the schema's clampDouble(ValueMin, FixedMax) validator runs on
    // the READ path too, so both callers receive an already-bounded value and
    // this tail is an identity. It only starts mattering if that clamp is
    // widened for a future kind, or if a caller ever reads the raw backend.
    // Not unit-testable through either caller for the same reason — see the
    // note in test_scrolling_settings.cpp's data table.
    return clampColumnWidthForKind(value, isFixed);
}
} // namespace

P_STORE_GET(bool, scrollingEnabled, scrollingGroup, enabledKey, bool)
P_STORE_SET_BOOL(setScrollingEnabled, scrollingGroup, enabledKey, scrollingEnabledChanged)

P_STORE_GET(int, scrollingCenterFocusedColumn, scrollingGroup, centerFocusedColumnKey, int)
P_STORE_SET_INT(setScrollingCenterFocusedColumn, scrollingGroup, centerFocusedColumnKey,
                scrollingCenterFocusedColumnChanged)

P_STORE_GET(bool, scrollingAlwaysCenterSingleColumn, scrollingGroup, alwaysCenterSingleColumnKey, bool)
P_STORE_SET_BOOL(setScrollingAlwaysCenterSingleColumn, scrollingGroup, alwaysCenterSingleColumnKey,
                 scrollingAlwaysCenterSingleColumnChanged)

P_STORE_GET(int, scrollingDefaultColumnWidthKind, scrollingGroup, defaultColumnWidthKindKey, int)

// Hand-written kind setter: the shared value key serves two kinds under one
// schema clamp, so a kind flip must coerce the stored value into the new
// kind's range — otherwise Fixed→Proportion leaves 800 stored (engine clamps
// to 100%) or Proportion→Fixed leaves 0.5 stored (the engine's qMax(1, …)
// backstop renders it one pixel wide) while the page displays something
// else entirely.
void Settings::setScrollingDefaultColumnWidthKind(int value)
{
    const int before =
        m_store->read<int>(ConfigDefaults::scrollingGroup(), ConfigDefaults::defaultColumnWidthKindKey());
    m_store->write(ConfigDefaults::scrollingGroup(), ConfigDefaults::defaultColumnWidthKindKey(), value);
    const int after = m_store->read<int>(ConfigDefaults::scrollingGroup(), ConfigDefaults::defaultColumnWidthKindKey());
    if (after == before) {
        return;
    }
    // Both arms are preservation tests, not kind sniffs: each re-seeds only
    // when the stored value cannot belong to the kind being entered. Fixed
    // re-seeds below the pixel floor, Proportion above 1.0. A legitimate
    // value of either kind parked through a ClientDecides hop therefore
    // survives the round trip. A hand-edited config with an inconsistent pair
    // (kind=Fixed, value=0.5) never reaches this setter at all; that case is
    // caught once by normalizeScrollingColumnWidthValue at load.
    // The kind flip is announced FIRST: a QML handler keyed on the kind
    // NOTIFY must observe the new kind before (not after) the value
    // coercion and the engine refresh it triggers.
    Q_EMIT scrollingDefaultColumnWidthKindChanged();
    const qreal stored =
        m_store->read<double>(ConfigDefaults::scrollingGroup(), ConfigDefaults::defaultColumnWidthValueKey());
    const bool isFixed = after == ConfigDefaults::scrollingWidthKindFixed();
    const bool isProportion = after == ConfigDefaults::scrollingWidthKindProportion();
    // No before/after kind comparison is needed inside the arms: the
    // early-return above guarantees after != before, so entering Fixed
    // implies the previous kind was not Fixed.
    if (isFixed && stored < ConfigDefaults::scrollingDefaultColumnWidthFixedMin()) {
        // Entering Fixed with something that is not a plausible pixel width —
        // a proportion arriving straight from Proportion, or one parked
        // through a ClientDecides hop. Seed a sane pixel width. A pixel count
        // already sitting there (the user's retained width across a
        // Fixed→ClientDecides→Fixed round trip) is left alone.
        setScrollingDefaultColumnWidthValue(ConfigDefaults::scrollingDefaultColumnWidthFixedPx());
    } else if (isProportion && stored > 1.0) {
        // Entering Proportion with pixels stored — whether directly from
        // Fixed or via a ClientDecides hop (ClientDecides ignores the value
        // and deliberately leaves it untouched, so pixels can arrive here
        // two transitions later). A pixel count fed to the engine's
        // qBound(0.05, …, 1.0) would open every column at 100% width.
        setScrollingDefaultColumnWidthValue(ConfigDefaults::scrollingDefaultColumnWidthValue());
    }
    // One aggregate emit per kind flip: when the value coercion above ran,
    // its nested setter already emitted settingsChanged — a second emit here
    // would run the engine's refresh+retile sweep twice for one user action.
    const qreal storedNow =
        m_store->read<double>(ConfigDefaults::scrollingGroup(), ConfigDefaults::defaultColumnWidthValueKey());
    if (qFuzzyCompare(1.0 + storedNow, 1.0 + stored)) {
        Q_EMIT settingsChanged();
    }
}

P_STORE_GET(qreal, scrollingDefaultColumnWidthValue, scrollingGroup, defaultColumnWidthValueKey, double)

// Post-load repair for the shared width VALUE key. The key serves both kinds,
// so the schema's clampDouble has to span their union (0.05 proportion up to
// 10000 px) and cannot reject a Fixed=5px that reached the store without
// passing the setter below.
//
// SCOPE: called from Settings::load, so it catches whatever the reparse
// brought in — a hand-edited config, a config import, the Discard reload —
// and from applyConfigOverlayStaged, which writes the store through
// importFromJson without a load(). Profile staging needs it just as much: a
// shared blob carrying kind=Fixed with value 0.5 would otherwise make the
// engine open every column ONE PIXEL wide for the whole session.
//
// Read-time coercion is deliberately NOT how this is done: the kind setter
// announces the flip BEFORE coercing the value, and a clamping getter would
// report the new kind's bounds against the old kind's value in that window,
// breaking the emit ordering a test pins.
void Settings::normalizeScrollingColumnWidthValue()
{
    const int kind = scrollingDefaultColumnWidthKind();
    // ClientDecides and Preset store no width of their own — both
    // deliberately leave whatever the previous kind wrote in place (Preset
    // resolves through its index key), so there is nothing to validate
    // against.
    if (kind == ConfigDefaults::scrollingWidthKindClientDecides()
        || kind == ConfigDefaults::scrollingWidthKindPreset()) {
        return;
    }
    const qreal stored = scrollingDefaultColumnWidthValue();
    const qreal coerced = reseedColumnWidthForKind(stored, kind == ConfigDefaults::scrollingWidthKindFixed());
    if (qFuzzyCompare(1.0 + stored, 1.0 + coerced)) {
        return;
    }
    qCWarning(lcConfig) << "scrolling: stored column width" << stored << "is out of range for the current kind" << kind
                        << "— using" << coerced << "in memory; it reaches disk on the next save";
    m_store->write(ConfigDefaults::scrollingGroup(), ConfigDefaults::defaultColumnWidthValueKey(), coerced);
    // NO Q_EMIT here. BOTH callers — load() and applyConfigOverlayStaged —
    // snapshot every Q_PROPERTY before mutating the store and re-emit each
    // changed NOTIFY after this returns. Emitting here would double-fire on
    // a coercing load or a coercing staged apply, and
    // fire spuriously on a Discard reload where the in-memory value was
    // already coerced (disk still holds the bad pair, so this coerces again,
    // but the property never changed from any consumer's point of view).
}

// Hand-written value setter: kind-aware clamp (Proportion values live in
// [ValueMin, ProportionMax]; Fixed in pixels with a FixedMin floor, rounded
// to whole pixels by the engine on load) — the schema clamp alone spans
// both ranges.
void Settings::setScrollingDefaultColumnWidthValue(qreal value)
{
    value =
        clampColumnWidthForKind(value, scrollingDefaultColumnWidthKind() == ConfigDefaults::scrollingWidthKindFixed());
    const qreal before =
        m_store->read<double>(ConfigDefaults::scrollingGroup(), ConfigDefaults::defaultColumnWidthValueKey());
    m_store->write(ConfigDefaults::scrollingGroup(), ConfigDefaults::defaultColumnWidthValueKey(), value);
    const qreal after =
        m_store->read<double>(ConfigDefaults::scrollingGroup(), ConfigDefaults::defaultColumnWidthValueKey());
    if (qFuzzyCompare(1.0 + before, 1.0 + after)) {
        return;
    }
    Q_EMIT scrollingDefaultColumnWidthValueChanged();
    Q_EMIT settingsChanged();
}

P_STORE_GET(int, scrollingDefaultColumnDisplay, scrollingGroup, defaultColumnDisplayKey, int)
P_STORE_SET_INT(setScrollingDefaultColumnDisplay, scrollingGroup, defaultColumnDisplayKey,
                scrollingDefaultColumnDisplayChanged)

P_STORE_GET(int, scrollingDefaultColumnWidthPresetIndex, scrollingGroup, defaultColumnWidthPresetIndexKey, int)
P_STORE_SET_INT(setScrollingDefaultColumnWidthPresetIndex, scrollingGroup, defaultColumnWidthPresetIndexKey,
                scrollingDefaultColumnWidthPresetIndexChanged)

// Height trio: unlike the width pair, the value key serves one kind (Fixed)
// so the schema clamp is the whole story and the plain macros suffice.
P_STORE_GET(int, scrollingDefaultWindowHeightKind, scrollingGroup, defaultWindowHeightKindKey, int)
P_STORE_SET_INT(setScrollingDefaultWindowHeightKind, scrollingGroup, defaultWindowHeightKindKey,
                scrollingDefaultWindowHeightKindChanged)
P_STORE_GET(qreal, scrollingDefaultWindowHeightValue, scrollingGroup, defaultWindowHeightValueKey, double)
P_STORE_SET_DOUBLE(setScrollingDefaultWindowHeightValue, scrollingGroup, defaultWindowHeightValueKey,
                   scrollingDefaultWindowHeightValueChanged)
P_STORE_GET(int, scrollingDefaultWindowHeightPresetIndex, scrollingGroup, defaultWindowHeightPresetIndexKey, int)
P_STORE_SET_INT(setScrollingDefaultWindowHeightPresetIndex, scrollingGroup, defaultWindowHeightPresetIndexKey,
                scrollingDefaultWindowHeightPresetIndexChanged)

// Preset lists: comma-joined QString on disk, QStringList through
// IScrollSettings (the engine parses the decimals), raw string for QML.
QStringList Settings::scrollingPresetColumnWidths() const
{
    return settings_detail::parseCommaList(
        m_store->read<QString>(ConfigDefaults::scrollingGroup(), ConfigDefaults::presetColumnWidthsKey()));
}

P_STORE_GET(QString, scrollingPresetColumnWidthsString, scrollingGroup, presetColumnWidthsKey, QString)
P_STORE_SET_STRING(setScrollingPresetColumnWidths, scrollingGroup, presetColumnWidthsKey,
                   scrollingPresetColumnWidthsChanged)

QStringList Settings::scrollingPresetWindowHeights() const
{
    return settings_detail::parseCommaList(
        m_store->read<QString>(ConfigDefaults::scrollingGroup(), ConfigDefaults::presetWindowHeightsKey()));
}

P_STORE_GET(QString, scrollingPresetWindowHeightsString, scrollingGroup, presetWindowHeightsKey, QString)
P_STORE_SET_STRING(setScrollingPresetWindowHeights, scrollingGroup, presetWindowHeightsKey,
                   scrollingPresetWindowHeightsChanged)

// ── Scrolling behavior (Scrolling.Behavior) ─────────────────────────────────
// Shared leaf key names under the scrolling behavior group; the schema
// validators own enum validation (validIntOr snaps a bad sticky value back
// to the default on read, like every other stored enum) and range clamping
// (clampInt on the step percents).

P_STORE_GET(int, scrollingInsertPosition, scrollingBehaviorGroup, insertPositionKey, int)
P_STORE_SET_INT(setScrollingInsertPosition, scrollingBehaviorGroup, insertPositionKey, scrollingInsertPositionChanged)

P_STORE_GET(bool, scrollingFocusNewWindows, scrollingBehaviorGroup, focusNewWindowsKey, bool)
P_STORE_SET_BOOL(setScrollingFocusNewWindows, scrollingBehaviorGroup, focusNewWindowsKey,
                 scrollingFocusNewWindowsChanged)

P_STORE_GET(bool, scrollingFocusFollowsMouse, scrollingBehaviorGroup, focusFollowsMouseKey, bool)
P_STORE_SET_BOOL(setScrollingFocusFollowsMouse, scrollingBehaviorGroup, focusFollowsMouseKey,
                 scrollingFocusFollowsMouseChanged)

P_STORE_GET(int, scrollingStickyWindowHandling, scrollingBehaviorGroup, stickyWindowHandlingKey, int)
P_STORE_SET_INT(setScrollingStickyWindowHandling, scrollingBehaviorGroup, stickyWindowHandlingKey,
                scrollingStickyWindowHandlingChanged)

P_STORE_GET(bool, scrollingRespectMinimumSize, scrollingBehaviorGroup, respectMinimumSizeKey, bool)
P_STORE_SET_BOOL(setScrollingRespectMinimumSize, scrollingBehaviorGroup, respectMinimumSizeKey,
                 scrollingRespectMinimumSizeChanged)

P_STORE_GET(bool, scrollingRestoreStripsOnLogin, scrollingBehaviorGroup, restoreOnLoginKey, bool)
P_STORE_SET_BOOL(setScrollingRestoreStripsOnLogin, scrollingBehaviorGroup, restoreOnLoginKey,
                 scrollingRestoreStripsOnLoginChanged)

P_STORE_GET(int, scrollingColumnWidthStepPercent, scrollingBehaviorGroup, columnWidthStepPercentKey, int)
P_STORE_SET_INT(setScrollingColumnWidthStepPercent, scrollingBehaviorGroup, columnWidthStepPercentKey,
                scrollingColumnWidthStepPercentChanged)

P_STORE_GET(int, scrollingWindowHeightStepPercent, scrollingBehaviorGroup, windowHeightStepPercentKey, int)
P_STORE_SET_INT(setScrollingWindowHeightStepPercent, scrollingBehaviorGroup, windowHeightStepPercentKey,
                scrollingWindowHeightStepPercentChanged)

// ── Scrolling shortcuts ─────────────────────────────────────────────────────

P_STORE_GET(QString, scrollingFocusColumnFirstShortcut, shortcutsScrollingGroup, focusColumnFirstKey, QString)
P_STORE_SET_STRING(setScrollingFocusColumnFirstShortcut, shortcutsScrollingGroup, focusColumnFirstKey,
                   scrollingFocusColumnFirstShortcutChanged)
P_STORE_GET(QString, scrollingFocusColumnLastShortcut, shortcutsScrollingGroup, focusColumnLastKey, QString)
P_STORE_SET_STRING(setScrollingFocusColumnLastShortcut, shortcutsScrollingGroup, focusColumnLastKey,
                   scrollingFocusColumnLastShortcutChanged)
P_STORE_GET(QString, scrollingMoveColumnToFirstShortcut, shortcutsScrollingGroup, moveColumnToFirstKey, QString)
P_STORE_SET_STRING(setScrollingMoveColumnToFirstShortcut, shortcutsScrollingGroup, moveColumnToFirstKey,
                   scrollingMoveColumnToFirstShortcutChanged)
P_STORE_GET(QString, scrollingMoveColumnToLastShortcut, shortcutsScrollingGroup, moveColumnToLastKey, QString)
P_STORE_SET_STRING(setScrollingMoveColumnToLastShortcut, shortcutsScrollingGroup, moveColumnToLastKey,
                   scrollingMoveColumnToLastShortcutChanged)
P_STORE_GET(QString, scrollingConsumeWindowShortcut, shortcutsScrollingGroup, consumeWindowKey, QString)
P_STORE_SET_STRING(setScrollingConsumeWindowShortcut, shortcutsScrollingGroup, consumeWindowKey,
                   scrollingConsumeWindowShortcutChanged)
P_STORE_GET(QString, scrollingExpelWindowShortcut, shortcutsScrollingGroup, expelWindowKey, QString)
P_STORE_SET_STRING(setScrollingExpelWindowShortcut, shortcutsScrollingGroup, expelWindowKey,
                   scrollingExpelWindowShortcutChanged)
P_STORE_GET(QString, scrollingConsumeOrExpelLeftShortcut, shortcutsScrollingGroup, consumeOrExpelLeftKey, QString)
P_STORE_SET_STRING(setScrollingConsumeOrExpelLeftShortcut, shortcutsScrollingGroup, consumeOrExpelLeftKey,
                   scrollingConsumeOrExpelLeftShortcutChanged)
P_STORE_GET(QString, scrollingConsumeOrExpelRightShortcut, shortcutsScrollingGroup, consumeOrExpelRightKey, QString)
P_STORE_SET_STRING(setScrollingConsumeOrExpelRightShortcut, shortcutsScrollingGroup, consumeOrExpelRightKey,
                   scrollingConsumeOrExpelRightShortcutChanged)
P_STORE_GET(QString, scrollingCenterColumnShortcut, shortcutsScrollingGroup, centerColumnKey, QString)
P_STORE_SET_STRING(setScrollingCenterColumnShortcut, shortcutsScrollingGroup, centerColumnKey,
                   scrollingCenterColumnShortcutChanged)
P_STORE_GET(QString, scrollingToggleColumnTabbedShortcut, shortcutsScrollingGroup, toggleColumnTabbedKey, QString)
P_STORE_SET_STRING(setScrollingToggleColumnTabbedShortcut, shortcutsScrollingGroup, toggleColumnTabbedKey,
                   scrollingToggleColumnTabbedShortcutChanged)
P_STORE_GET(QString, scrollingCycleColumnWidthShortcut, shortcutsScrollingGroup, cycleColumnWidthKey, QString)
P_STORE_SET_STRING(setScrollingCycleColumnWidthShortcut, shortcutsScrollingGroup, cycleColumnWidthKey,
                   scrollingCycleColumnWidthShortcutChanged)
P_STORE_GET(QString, scrollingCycleColumnWidthBackShortcut, shortcutsScrollingGroup, cycleColumnWidthBackKey, QString)
P_STORE_SET_STRING(setScrollingCycleColumnWidthBackShortcut, shortcutsScrollingGroup, cycleColumnWidthBackKey,
                   scrollingCycleColumnWidthBackShortcutChanged)
P_STORE_GET(QString, scrollingIncreaseColumnWidthShortcut, shortcutsScrollingGroup, increaseColumnWidthKey, QString)
P_STORE_SET_STRING(setScrollingIncreaseColumnWidthShortcut, shortcutsScrollingGroup, increaseColumnWidthKey,
                   scrollingIncreaseColumnWidthShortcutChanged)
P_STORE_GET(QString, scrollingDecreaseColumnWidthShortcut, shortcutsScrollingGroup, decreaseColumnWidthKey, QString)
P_STORE_SET_STRING(setScrollingDecreaseColumnWidthShortcut, shortcutsScrollingGroup, decreaseColumnWidthKey,
                   scrollingDecreaseColumnWidthShortcutChanged)
P_STORE_GET(QString, scrollingMaximizeColumnShortcut, shortcutsScrollingGroup, maximizeColumnKey, QString)
P_STORE_SET_STRING(setScrollingMaximizeColumnShortcut, shortcutsScrollingGroup, maximizeColumnKey,
                   scrollingMaximizeColumnShortcutChanged)
P_STORE_GET(QString, scrollingExpandColumnShortcut, shortcutsScrollingGroup, expandColumnKey, QString)
P_STORE_SET_STRING(setScrollingExpandColumnShortcut, shortcutsScrollingGroup, expandColumnKey,
                   scrollingExpandColumnShortcutChanged)
P_STORE_GET(QString, scrollingCycleWindowHeightShortcut, shortcutsScrollingGroup, cycleWindowHeightKey, QString)
P_STORE_SET_STRING(setScrollingCycleWindowHeightShortcut, shortcutsScrollingGroup, cycleWindowHeightKey,
                   scrollingCycleWindowHeightShortcutChanged)
P_STORE_GET(QString, scrollingCycleWindowHeightBackShortcut, shortcutsScrollingGroup, cycleWindowHeightBackKey, QString)
P_STORE_SET_STRING(setScrollingCycleWindowHeightBackShortcut, shortcutsScrollingGroup, cycleWindowHeightBackKey,
                   scrollingCycleWindowHeightBackShortcutChanged)
P_STORE_GET(QString, scrollingIncreaseWindowHeightShortcut, shortcutsScrollingGroup, increaseWindowHeightKey, QString)
P_STORE_SET_STRING(setScrollingIncreaseWindowHeightShortcut, shortcutsScrollingGroup, increaseWindowHeightKey,
                   scrollingIncreaseWindowHeightShortcutChanged)
P_STORE_GET(QString, scrollingDecreaseWindowHeightShortcut, shortcutsScrollingGroup, decreaseWindowHeightKey, QString)
P_STORE_SET_STRING(setScrollingDecreaseWindowHeightShortcut, shortcutsScrollingGroup, decreaseWindowHeightKey,
                   scrollingDecreaseWindowHeightShortcutChanged)
P_STORE_GET(QString, scrollingResetWindowHeightsShortcut, shortcutsScrollingGroup, resetWindowHeightsKey, QString)
P_STORE_SET_STRING(setScrollingResetWindowHeightsShortcut, shortcutsScrollingGroup, resetWindowHeightsKey,
                   scrollingResetWindowHeightsShortcutChanged)

} // namespace PlasmaZones

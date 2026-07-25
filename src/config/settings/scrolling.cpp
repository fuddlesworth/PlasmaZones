// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config/settings.h"
#include "config/settings/settings_detail.h"
#include "config/configdefaults.h"
#include "core/platform/logging.h"

namespace PlasmaZones {

// ── Scrolling (PhosphorConfig::Store-backed) ────────────────────────────────
// Scalars live in m_store under Tiling.Scrolling; the schema validators own
// the clamping (clampInt / validIntOr / clampDouble / canonicalCommaList).

P_STORE_GET(int, scrollingCenterFocusedColumn, tilingScrollingGroup, centerFocusedColumnKey, int)
P_STORE_SET_INT(setScrollingCenterFocusedColumn, tilingScrollingGroup, centerFocusedColumnKey,
                scrollingCenterFocusedColumnChanged)

P_STORE_GET(bool, scrollingAlwaysCenterSingleColumn, tilingScrollingGroup, alwaysCenterSingleColumnKey, bool)
P_STORE_SET_BOOL(setScrollingAlwaysCenterSingleColumn, tilingScrollingGroup, alwaysCenterSingleColumnKey,
                 scrollingAlwaysCenterSingleColumnChanged)

P_STORE_GET(int, scrollingDefaultColumnWidthKind, tilingScrollingGroup, defaultColumnWidthKindKey, int)
P_STORE_SET_INT(setScrollingDefaultColumnWidthKind, tilingScrollingGroup, defaultColumnWidthKindKey,
                scrollingDefaultColumnWidthKindChanged)

P_STORE_GET(qreal, scrollingDefaultColumnWidthValue, tilingScrollingGroup, defaultColumnWidthValueKey, double)
P_STORE_SET_DOUBLE(setScrollingDefaultColumnWidthValue, tilingScrollingGroup, defaultColumnWidthValueKey,
                   scrollingDefaultColumnWidthValueChanged)

P_STORE_GET(int, scrollingDefaultColumnDisplay, tilingScrollingGroup, defaultColumnDisplayKey, int)
P_STORE_SET_INT(setScrollingDefaultColumnDisplay, tilingScrollingGroup, defaultColumnDisplayKey,
                scrollingDefaultColumnDisplayChanged)

// Preset lists: comma-joined QString on disk, QStringList through
// IScrollSettings (the engine parses the decimals), raw string for QML.
QStringList Settings::scrollingPresetColumnWidths() const
{
    return settings_detail::parseCommaList(
        m_store->read<QString>(ConfigDefaults::tilingScrollingGroup(), ConfigDefaults::presetColumnWidthsKey()));
}

P_STORE_GET(QString, scrollingPresetColumnWidthsString, tilingScrollingGroup, presetColumnWidthsKey, QString)
P_STORE_SET_STRING(setScrollingPresetColumnWidths, tilingScrollingGroup, presetColumnWidthsKey,
                   scrollingPresetColumnWidthsChanged)

QStringList Settings::scrollingPresetWindowHeights() const
{
    return settings_detail::parseCommaList(
        m_store->read<QString>(ConfigDefaults::tilingScrollingGroup(), ConfigDefaults::presetWindowHeightsKey()));
}

P_STORE_GET(QString, scrollingPresetWindowHeightsString, tilingScrollingGroup, presetWindowHeightsKey, QString)
P_STORE_SET_STRING(setScrollingPresetWindowHeights, tilingScrollingGroup, presetWindowHeightsKey,
                   scrollingPresetWindowHeightsChanged)

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

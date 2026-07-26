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

// Hand-written kind setter: the shared value key serves two kinds under one
// schema clamp, so a kind flip must coerce the stored value into the new
// kind's range — otherwise Fixed→Proportion leaves 800 stored (engine clamps
// to 100%) or Proportion→Fixed leaves 0.5 stored (a 1px column) while the
// page displays something else entirely.
void Settings::setScrollingDefaultColumnWidthKind(int value)
{
    const int before =
        m_store->read<int>(ConfigDefaults::tilingScrollingGroup(), ConfigDefaults::defaultColumnWidthKindKey());
    m_store->write(ConfigDefaults::tilingScrollingGroup(), ConfigDefaults::defaultColumnWidthKindKey(), value);
    const int after =
        m_store->read<int>(ConfigDefaults::tilingScrollingGroup(), ConfigDefaults::defaultColumnWidthKindKey());
    if (after == before) {
        return;
    }
    const qreal stored =
        m_store->read<double>(ConfigDefaults::tilingScrollingGroup(), ConfigDefaults::defaultColumnWidthValueKey());
    if (after == 1 && stored <= 1.0) {
        // Entering Fixed with a proportion stored: seed a sane pixel width.
        setScrollingDefaultColumnWidthValue(ConfigDefaults::scrollingDefaultColumnWidthFixedPx());
    } else if (after != 1 && stored > 1.0) {
        // Leaving Fixed with pixels stored: fall back to the proportion default.
        setScrollingDefaultColumnWidthValue(ConfigDefaults::scrollingDefaultColumnWidthValue());
    }
    Q_EMIT scrollingDefaultColumnWidthKindChanged();
    Q_EMIT settingsChanged();
}

P_STORE_GET(qreal, scrollingDefaultColumnWidthValue, tilingScrollingGroup, defaultColumnWidthValueKey, double)

// Hand-written value setter: kind-aware clamp (Proportion values live in
// (0.05, 1.0]; Fixed in pixels) — the schema clamp alone spans both ranges.
void Settings::setScrollingDefaultColumnWidthValue(qreal value)
{
    const bool isFixed = scrollingDefaultColumnWidthKind() == 1;
    value = isFixed ? qBound<qreal>(ConfigDefaults::scrollingDefaultColumnWidthValueMin(), value,
                                    ConfigDefaults::scrollingDefaultColumnWidthValueMax())
                    : qBound<qreal>(ConfigDefaults::scrollingDefaultColumnWidthValueMin(), value, 1.0);
    const qreal before =
        m_store->read<double>(ConfigDefaults::tilingScrollingGroup(), ConfigDefaults::defaultColumnWidthValueKey());
    m_store->write(ConfigDefaults::tilingScrollingGroup(), ConfigDefaults::defaultColumnWidthValueKey(), value);
    const qreal after =
        m_store->read<double>(ConfigDefaults::tilingScrollingGroup(), ConfigDefaults::defaultColumnWidthValueKey());
    if (qFuzzyCompare(1.0 + before, 1.0 + after)) {
        return;
    }
    Q_EMIT scrollingDefaultColumnWidthValueChanged();
    Q_EMIT settingsChanged();
}

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

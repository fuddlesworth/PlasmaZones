// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

// Shortcut id vocabulary shared by the registration table
// (shortcutmanager.cpp) and the cheatsheet catalog
// (shortcutmanager_catalog.cpp).

namespace PlasmaZones {
namespace ShortcutIds {

// Stable string ids are documented contract: they appear in
// ~/.config/kglobalshortcutsrc under the "plasmazonesd" component and in
// XDG Portal settings UIs. Changing one is an on-disk rename that users pay
// for, so add new ones at the bottom; never rename existing.
inline constexpr auto kIdOpenEditor = "open_editor";
inline constexpr auto kIdOpenSettings = "open_settings";
inline constexpr auto kIdPreviousLayout = "previous_layout";
inline constexpr auto kIdNextLayout = "next_layout";
inline constexpr auto kIdMoveWindowLeft = "move_window_left";
inline constexpr auto kIdMoveWindowRight = "move_window_right";
inline constexpr auto kIdMoveWindowUp = "move_window_up";
inline constexpr auto kIdMoveWindowDown = "move_window_down";
inline constexpr auto kIdFocusZoneLeft = "focus_zone_left";
inline constexpr auto kIdFocusZoneRight = "focus_zone_right";
inline constexpr auto kIdFocusZoneUp = "focus_zone_up";
inline constexpr auto kIdFocusZoneDown = "focus_zone_down";
inline constexpr auto kIdPushToEmptyZone = "push_to_empty_zone";
inline constexpr auto kIdRestoreWindowSize = "restore_window_size";
inline constexpr auto kIdToggleWindowFloat = "toggle_window_float";
inline constexpr auto kIdSwapWindowLeft = "swap_window_left";
inline constexpr auto kIdSwapWindowRight = "swap_window_right";
inline constexpr auto kIdSwapWindowUp = "swap_window_up";
inline constexpr auto kIdSwapWindowDown = "swap_window_down";
inline constexpr auto kIdSwapVirtualScreenLeft = "swap_virtual_screen_left";
inline constexpr auto kIdSwapVirtualScreenRight = "swap_virtual_screen_right";
inline constexpr auto kIdSwapVirtualScreenUp = "swap_virtual_screen_up";
inline constexpr auto kIdSwapVirtualScreenDown = "swap_virtual_screen_down";
inline constexpr auto kIdRotateVirtualScreensCW = "rotate_virtual_screens_clockwise";
inline constexpr auto kIdRotateVirtualScreensCCW = "rotate_virtual_screens_counterclockwise";
inline constexpr auto kIdRotateWindowsCW = "rotate_windows_clockwise";
inline constexpr auto kIdRotateWindowsCCW = "rotate_windows_counterclockwise";
inline constexpr auto kIdCycleWindowForward = "cycle_window_forward";
inline constexpr auto kIdCycleWindowBackward = "cycle_window_backward";
inline constexpr auto kIdResnapToNewLayout = "resnap_to_new_layout";
inline constexpr auto kIdSnapAllWindows = "snap_all_windows";
inline constexpr auto kIdLayoutPicker = "layout_picker";
inline constexpr auto kIdToggleLayoutLock = "toggle_layout_lock";
inline constexpr auto kIdToggleAutotile = "toggle_autotile";
inline constexpr auto kIdFocusMaster = "focus_master";
inline constexpr auto kIdSwapMaster = "swap_master";
inline constexpr auto kIdIncreaseMasterRatio = "increase_master_ratio";
inline constexpr auto kIdDecreaseMasterRatio = "decrease_master_ratio";
inline constexpr auto kIdIncreaseMasterCount = "increase_master_count";
inline constexpr auto kIdDecreaseMasterCount = "decrease_master_count";
inline constexpr auto kIdRetile = "retile";
inline constexpr auto kIdToggleCheatsheet = "toggle_cheatsheet";
inline constexpr auto kIdSpanWindowLeft = "span_window_left";
inline constexpr auto kIdSpanWindowRight = "span_window_right";
inline constexpr auto kIdSpanWindowUp = "span_window_up";
inline constexpr auto kIdSpanWindowDown = "span_window_down";
inline constexpr auto kIdScrollFocusColumnFirst = "scroll_focus_column_first";
inline constexpr auto kIdScrollFocusColumnLast = "scroll_focus_column_last";
inline constexpr auto kIdScrollMoveColumnToFirst = "scroll_move_column_to_first";
inline constexpr auto kIdScrollMoveColumnToLast = "scroll_move_column_to_last";
inline constexpr auto kIdScrollConsumeWindow = "scroll_consume_window";
inline constexpr auto kIdScrollExpelWindow = "scroll_expel_window";
inline constexpr auto kIdScrollConsumeOrExpelLeft = "scroll_consume_or_expel_left";
inline constexpr auto kIdScrollConsumeOrExpelRight = "scroll_consume_or_expel_right";
inline constexpr auto kIdScrollCenterColumn = "scroll_center_column";
inline constexpr auto kIdScrollToggleColumnTabbed = "scroll_toggle_column_tabbed";
inline constexpr auto kIdScrollCycleColumnWidth = "scroll_cycle_column_width";
inline constexpr auto kIdScrollCycleColumnWidthBack = "scroll_cycle_column_width_back";
inline constexpr auto kIdScrollIncreaseColumnWidth = "scroll_increase_column_width";
inline constexpr auto kIdScrollDecreaseColumnWidth = "scroll_decrease_column_width";
inline constexpr auto kIdScrollMaximizeColumn = "scroll_maximize_column";
inline constexpr auto kIdScrollExpandColumn = "scroll_expand_column";
inline constexpr auto kIdScrollCycleWindowHeight = "scroll_cycle_window_height";
inline constexpr auto kIdScrollCycleWindowHeightBack = "scroll_cycle_window_height_back";
inline constexpr auto kIdScrollIncreaseWindowHeight = "scroll_increase_window_height";
inline constexpr auto kIdScrollDecreaseWindowHeight = "scroll_decrease_window_height";
inline constexpr auto kIdScrollResetWindowHeights = "scroll_reset_window_heights";

inline QString quickLayoutId(int slotZeroBased)
{
    return QStringLiteral("quick_layout_%1").arg(slotZeroBased + 1);
}

inline QString snapToZoneId(int slotZeroBased)
{
    return QStringLiteral("snap_to_zone_%1").arg(slotZeroBased + 1);
}

} // namespace ShortcutIds
} // namespace PlasmaZones

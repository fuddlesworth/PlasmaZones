// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "configkeys.h"

// Local copy of configkeys.h's accessor macro (that header #undefs it at end
// of file); same expansion, undef'd again below. A tweak to the original
// (e.g. attribute annotation) must be applied HERE by hand too — both
// expansions compile either way, so nothing catches the drift.
#define P_CONFIG_KEY(name, str)                                                                                        \
    static QString name()                                                                                              \
    {                                                                                                                  \
        return QStringLiteral(str);                                                                                    \
    }

namespace PlasmaZones {

/**
 * @brief The Shortcuts.Scrolling config key accessors.
 *
 * A link in the ConfigKeys → … → ConfigDefaults inheritance chain (the same
 * shape configdefaults_scrolling.h uses), split out of configkeys.h by
 * concern when that file hit its size ceiling. Call sites keep addressing
 * everything through ConfigDefaults::.
 */
class ConfigKeysScrolling : public ConfigKeys
{
public:
    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Shortcuts.Scrolling
    // ═══════════════════════════════════════════════════════════════════════════

    P_CONFIG_KEY(focusColumnFirstKey, "FocusColumnFirst")
    P_CONFIG_KEY(focusColumnLastKey, "FocusColumnLast")
    P_CONFIG_KEY(moveColumnToFirstKey, "MoveColumnToFirst")
    P_CONFIG_KEY(moveColumnToLastKey, "MoveColumnToLast")
    P_CONFIG_KEY(consumeWindowKey, "ConsumeWindow")
    P_CONFIG_KEY(expelWindowKey, "ExpelWindow")
    P_CONFIG_KEY(consumeOrExpelLeftKey, "ConsumeOrExpelLeft")
    P_CONFIG_KEY(consumeOrExpelRightKey, "ConsumeOrExpelRight")
    P_CONFIG_KEY(centerColumnKey, "CenterColumn")
    P_CONFIG_KEY(centerVisibleColumnsKey, "CenterVisibleColumns")
    P_CONFIG_KEY(toggleColumnTabbedKey, "ToggleColumnTabbed")
    P_CONFIG_KEY(toggleWindowedFullscreenKey, "ToggleWindowedFullscreen")
    P_CONFIG_KEY(cycleColumnWidthKey, "CycleColumnWidth")
    P_CONFIG_KEY(cycleColumnWidthBackKey, "CycleColumnWidthBack")
    P_CONFIG_KEY(increaseColumnWidthKey, "IncreaseColumnWidth")
    P_CONFIG_KEY(decreaseColumnWidthKey, "DecreaseColumnWidth")
    P_CONFIG_KEY(maximizeColumnKey, "MaximizeColumn")
    P_CONFIG_KEY(expandColumnKey, "ExpandColumn")
    P_CONFIG_KEY(cycleWindowHeightKey, "CycleWindowHeight")
    P_CONFIG_KEY(cycleWindowHeightBackKey, "CycleWindowHeightBack")
    P_CONFIG_KEY(increaseWindowHeightKey, "IncreaseWindowHeight")
    P_CONFIG_KEY(decreaseWindowHeightKey, "DecreaseWindowHeight")
    P_CONFIG_KEY(resetWindowHeightsKey, "ResetWindowHeights")
    P_CONFIG_KEY(focusWindowTopKey, "FocusWindowTop")
    P_CONFIG_KEY(focusWindowBottomKey, "FocusWindowBottom")
    P_CONFIG_KEY(focusColumnLeftKey, "FocusColumnLeft")
    P_CONFIG_KEY(focusColumnRightKey, "FocusColumnRight")
    P_CONFIG_KEY(focusColumnLeftOrLastKey, "FocusColumnLeftOrLast")
    P_CONFIG_KEY(focusColumnRightOrFirstKey, "FocusColumnRightOrFirst")
    P_CONFIG_KEY(moveToFloatingKey, "MoveToFloating")
    P_CONFIG_KEY(moveToTilingKey, "MoveToTiling")
    P_CONFIG_KEY(viewBackKey, "ViewBack")
    P_CONFIG_KEY(viewForwardKey, "ViewForward")
    P_CONFIG_KEY(viewPageBackKey, "ViewPageBack")
    P_CONFIG_KEY(viewPageForwardKey, "ViewPageForward")
    P_CONFIG_KEY(equalizeColumnWidthsKey, "EqualizeColumnWidths")
    P_CONFIG_KEY(minimizeColumnWidthKey, "MinimizeColumnWidth")
};

} // namespace PlasmaZones

#undef P_CONFIG_KEY

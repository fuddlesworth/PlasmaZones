// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

namespace PlasmaZones::RuleAuthoring {

/// One picker category: a translated label + a stable sort order. The field
/// and action pickers group their (otherwise long, flat) entry lists into
/// fly-out submenus keyed by this. Shared by the match-side field picker
/// (ruleauthoring.cpp) and the action-side type picker (ruleauthoring_actions.cpp).
struct PickerCategory
{
    QString label;
    int order;
};

} // namespace PlasmaZones::RuleAuthoring

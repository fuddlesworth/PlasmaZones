// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QVariantList>

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

/// The parameter schema for @p type, derived from the LGPL ActionDescriptor's
/// structural `params` and supplemented by GPL-side translated labels. Each
/// entry is a `{ key, kind, label, ... }` map; the QML editor's per-param
/// Loader dispatches on `kind`, so the wire shape here is the contract between
/// the descriptor and the editor. Defined in ruleauthoring_actionparams.cpp
/// alongside the label / hint / default-seeding it feeds, and consumed by the
/// action-type picker in ruleauthoring_actions.cpp.
QVariantList paramsForActionType(const QString& type);

} // namespace PlasmaZones::RuleAuthoring

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace PlasmaZones::RuleTemplates {

/// Default priority bases by section. They seed the starting priority of the
/// seeded templates / empty rules, and `RuleController::bandBaseForSection`
/// reuses them to seed where a newly added rule inserts (so a new Advanced rule
/// starts high). Priority renormalization itself is flat global list-order, not
/// banded (see `RuleController::renormalizePriorities`); these only set sensible
/// defaults, the user reorders freely afterwards.
constexpr int kContextBandBase = 300;
constexpr int kApplicationBandBase = 200;
constexpr int kAnimationBandBase = 100;
constexpr int kAdvancedBandBase = 500;

/// Build a fresh, never-yet-stored rule for the given guided @p subject and
/// return it as a JSON map ready for the editor sheet. See
/// `RuleController::newEmptyRule` for the subject contract — the
/// controller delegates here. The returned rule has a fresh UUID, a sensible
/// starting match for the subject, and an empty action list.
QVariantMap newEmptyRule(const QString& subject);

/// Catalogue of pre-fab rule templates surfaced as quick-starts in the
/// AddRuleSheet. Each entry: `{ id, label, description, icon }`. Use
/// `newRuleFromTemplate(id)` to materialise the rule.
QVariantList ruleTemplates();

/// Build a fully-seeded rule for @p templateId (one of the ids returned by
/// `ruleTemplates()`). Returns an empty map for an unknown id. The rule is
/// NOT added — the editor sheet commits it after the user fills in the
/// remaining match values.
QVariantMap newRuleFromTemplate(const QString& templateId);

} // namespace PlasmaZones::RuleTemplates

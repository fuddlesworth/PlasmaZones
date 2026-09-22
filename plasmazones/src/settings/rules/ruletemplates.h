// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace PlasmaZones::RuleTemplates {

/// Default priority bases by section.
///
/// Two separate uses, and only the second one reaches the stored rule.
/// The `rule.priority` each builder below stamps is what the editor sheet's
/// Priority row SHOWS while the rule is being created; it does not survive the
/// commit, because `RuleController::addRuleFromJson` calls
/// `renormalizePriorities()` unconditionally and that re-stamps every
/// non-managed rule from its list position. Editing an EXISTING rule goes
/// through `updateRuleFromJson` and does keep the value.
///
/// What actually places a new rule is `RuleController::bandBaseForSection`,
/// which `bandSeededInsertIndex` derives from the rule's SECTION (its match and
/// action shape), never from the priority field. Renormalization itself is a
/// flat global list-order, not banded — see
/// `RuleController::renormalizePriorities`. The user reorders freely
/// afterwards.
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

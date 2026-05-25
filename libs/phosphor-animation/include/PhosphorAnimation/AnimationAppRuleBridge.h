// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/AnimationAppRule.h>

#include <PhosphorWindowRule/IdentityKey.h>
#include <PhosphorWindowRule/MatchExpression.h>
#include <PhosphorWindowRule/MatchTypes.h>
#include <PhosphorWindowRule/RuleAction.h>
#include <PhosphorWindowRule/WindowRule.h>
#include <PhosphorWindowRule/WindowRuleSet.h>

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUuid>
#include <QVariant>

/**
 * @file AnimationAppRuleBridge.h
 * @brief Header-only in-memory bridge: `PhosphorAnimationShaders::
 *        AnimationAppRuleList` → `WindowRuleSet`.
 *
 * The animation App Rules are an ordered list of per-window-class overrides.
 * Each entry names one `classPattern` (substring, case-insensitive match
 * against `windowClass()`), one `eventPath` (exact match), and carries EITHER
 * a `Shader` payload (`effectId` + `shaderParams`) OR a `Timing` payload
 * (`curve` + `durationMs`). Resolution is first-match per axis: the first
 * `Shader` rule wins the shader axis, the first `Timing` rule wins the timing
 * axis, and the two axes resolve independently.
 *
 * This bridge converts that list into a `WindowRuleSet` that resolves
 * byte-identically through `RuleEvaluator`:
 *
 *   - `classPattern` → a leaf `WindowClass Contains <pattern>` predicate.
 *     `Contains` is case-insensitive, matching the App Rule contract.
 *   - `eventPath` → an event-scoped action slot. A `Shader` rule produces an
 *     `OverrideAnimationShader` action whose `event` param is the `eventPath`;
 *     the action registry derives the slot `anim-shader:<eventPath>`. A
 *     `Timing` rule likewise produces `OverrideAnimationTiming` →
 *     `anim-timing:<eventPath>`. Event-scoped slots reproduce the exact
 *     `eventPath` match and keep the shader and timing axes independent.
 *   - List order → descending priority. The first list entry gets the
 *     highest priority, so `RuleEvaluator`'s descending-priority walk lands
 *     the same first-match-per-slot result the App Rule resolver produced.
 *   - The Shader rule's engaged-empty `effectId` ("block the per-event
 *     default for matching windows") is carried verbatim — the slot is still
 *     filled, so a `ResolvedActions::slot()` lookup yields a present-but-empty
 *     `effectId`, distinct from an unfilled slot ("no rule matched").
 *   - Rules with an empty `classPattern` or empty `eventPath` are dropped: a
 *     `Contains ""` predicate matches everything and an empty event maps to
 *     no slot. The `AnimationAppRuleList` loader already rejects these, but
 *     dropping defensively keeps the bridge total-functional.
 *
 * Header-only: this bridge declares only `inline` free functions, so neither
 * `phosphor-animation` nor `phosphor-windowrule` gains a link edge on the
 * other library. The header sources `<PhosphorAnimation/AnimationAppRule.h>`
 * (the INPUT type) and `<PhosphorWindowRule/...>` (the OUTPUT types), so
 * consumers that include it must `find_package(PhosphorAnimation)` AND
 * `find_package(PhosphorWindowRule)` themselves and link both libraries —
 * neither library's `Config.cmake.in` resolves the other transitively.
 *
 * Ships in `PhosphorAnimation/` (not `PhosphorWindowRule/`) so the windowrule
 * library's Config.cmake.in carries no transitive dependency on
 * `PhosphorAnimation`; the dep graph stays `phosphor-windowrule →
 * phosphor-protocol + phosphor-identity` with no edge into phosphor-animation.
 * The namespace remains `PhosphorWindowRule::AnimationAppRuleBridge` because
 * the bridge's output type is `PhosphorWindowRule::WindowRuleSet`; only the
 * include path moves.
 */

namespace PhosphorWindowRule {

namespace AnimationAppRuleBridge {

/// Canonical wire keys for the animation action params — kept private to the
/// bridge so the shape stays consistent with the action-registry validators.
namespace detail {
inline constexpr QLatin1StringView kKeyEvent{"event"};
inline constexpr QLatin1StringView kKeyEffectId{"effectId"};
inline constexpr QLatin1StringView kKeyParams{"params"};
inline constexpr QLatin1StringView kKeyCurve{"curve"};
inline constexpr QLatin1StringView kKeyDurationMs{"durationMs"};

/// Fixed v5-UUID namespace for animation App-Rule identities. Deriving each
/// rule's id deterministically from its source identity (classPattern,
/// eventPath, kind) makes the conversion idempotent: converting the same
/// `AnimationAppRuleList` twice yields rule sets that compare equal, so
/// `WindowRuleStore::setAllRules` can keep its no-op fast path.
inline const QUuid& namespaceUuid()
{
    static const QUuid ns(QStringLiteral("{b3f2c1a0-7d4e-5f6a-8b9c-0d1e2f3a4b5c}"));
    return ns;
}

/// Length-prefix a segment so concatenated identity keys are unambiguous —
/// re-exported from the shared @c PhosphorWindowRule/IdentityKey.h.
using PhosphorWindowRule::Detail::encodeSegment;

/// Stable per-rule key for the v5-UUID derivation. Segments are length-prefixed
/// so no two distinct (classPattern, eventPath, kind) tuples can collide.
inline QString ruleIdentityKey(const PhosphorAnimationShaders::AnimationAppRule& source)
{
    const QString kind = source.kind == PhosphorAnimationShaders::AnimationAppRule::Kind::Shader
        ? QStringLiteral("shader")
        : QStringLiteral("timing");
    return encodeSegment(source.classPattern) + encodeSegment(source.eventPath) + encodeSegment(kind);
}
} // namespace detail

/**
 * @brief Build the `RuleAction` for a single `AnimationAppRule`.
 *
 * Returns the action carrying the rule's payload. The action's `type` selects
 * `OverrideAnimationShader` or `OverrideAnimationTiming`; its params carry the
 * `event` (always) plus the kind-specific payload. The caller guarantees the
 * source rule has a non-empty `eventPath`.
 */
inline RuleAction makeAnimationAction(const PhosphorAnimationShaders::AnimationAppRule& source)
{
    RuleAction action;
    QJsonObject params;
    params.insert(detail::kKeyEvent, source.eventPath);

    if (source.kind == PhosphorAnimationShaders::AnimationAppRule::Kind::Shader) {
        action.type = QString(ActionType::OverrideAnimationShader);
        // `effectId` is always written — the empty string is the meaningful
        // engaged-blocking sentinel, not an absent value.
        params.insert(detail::kKeyEffectId, source.effectId);
        if (!source.shaderParams.isEmpty()) {
            params.insert(detail::kKeyParams, QJsonObject::fromVariantMap(source.shaderParams));
        }
    } else {
        action.type = QString(ActionType::OverrideAnimationTiming);
        if (!source.curve.isEmpty()) {
            params.insert(detail::kKeyCurve, source.curve);
        }
        // `durationMs <= 0` is the "inherit per-event default" sentinel —
        // omit the key entirely, mirroring `AnimationAppRule::toJson()`.
        if (source.durationMs > 0) {
            params.insert(detail::kKeyDurationMs, source.durationMs);
        }
    }
    action.params = params;
    return action;
}

/**
 * @brief Convert an `AnimationAppRuleList` into a `WindowRuleSet`.
 *
 * Entries with an empty `classPattern` or empty `eventPath` are dropped. List
 * order is preserved as descending priority so the evaluator's descending
 * walk reproduces the App Rule resolver's first-match-per-axis behaviour.
 */
inline WindowRuleSet toRuleSet(const PhosphorAnimationShaders::AnimationAppRuleList& list)
{
    const QList<PhosphorAnimationShaders::AnimationAppRule> entries = list.entries();
    QList<WindowRule> rules;
    rules.reserve(entries.size());

    const int count = entries.size();
    for (int i = 0; i < count; ++i) {
        const PhosphorAnimationShaders::AnimationAppRule& source = entries.at(i);
        if (source.classPattern.isEmpty() || source.eventPath.isEmpty()) {
            continue;
        }

        WindowRule rule;
        // Deterministic id — identical source entries yield identical rules,
        // keeping the conversion idempotent at the rule-identity level.
        rule.id = QUuid::createUuidV5(detail::namespaceUuid(), detail::ruleIdentityKey(source));
        rule.enabled = true;
        // First entry → highest priority. Descending integers preserve the
        // list order across the evaluator's priority sort; the stable sort
        // would also hold ties, but distinct priorities make the intent
        // explicit and robust against a future non-stable sort.
        //
        // `count - i` floors at 1 (the last entry, i == count - 1, gets
        // priority 1) and never reaches 0 — the value reserved for the
        // provider-default catch-all. Every animation App Rule therefore
        // stays strictly above the catch-all band.
        rule.priority = count - i;
        rule.match = MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, source.classPattern);
        rule.actions.append(makeAnimationAction(source));
        rules.append(rule);
    }

    WindowRuleSet set;
    set.setRules(rules);
    return set;
}

} // namespace AnimationAppRuleBridge

} // namespace PhosphorWindowRule

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/AnimationAppRule.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLoggingCategory>

namespace PhosphorAnimationShaders {

namespace {
Q_LOGGING_CATEGORY(lcRules, "phosphoranimation.apprule")

// `constexpr QLatin1StringView` lets call sites use the constants as
// QLatin1String directly without per-call wrapping (e.g.
// `o.insert(kKeyClassPattern, ...)` instead of
// `o.insert(QLatin1String(kKeyClassPattern), ...)`). The view is the
// C++20 type alias for QLatin1String — same memory layout, same
// runtime cost.
constexpr QLatin1StringView kKeyClassPattern{"classPattern"};
constexpr QLatin1StringView kKeyEventPath{"eventPath"};
constexpr QLatin1StringView kKeyKind{"kind"};
constexpr QLatin1StringView kKeyEffectId{"effectId"};
constexpr QLatin1StringView kKeyShaderParams{"shaderParams"};
constexpr QLatin1StringView kKeyCurve{"curve"};
constexpr QLatin1StringView kKeyDurationMs{"durationMs"};

constexpr QLatin1StringView kKindShaderStr{"shader"};
constexpr QLatin1StringView kKindTimingStr{"timing"};

QString kindToString(AnimationAppRule::Kind kind)
{
    switch (kind) {
    case AnimationAppRule::Kind::Shader:
        return kKindShaderStr;
    case AnimationAppRule::Kind::Timing:
        return kKindTimingStr;
    }
    Q_UNREACHABLE();
}

/// Strict parse: an unknown kind string returns nullopt so the caller
/// (`fromJson`) can drop the rule rather than silently coercing every
/// typo / future-kind into `Shader`. `kindFromString("xyz")` previously
/// produced an engaged-blocking shader rule, which would disable
/// animations for matching windows without the user's consent.
std::optional<AnimationAppRule::Kind> kindFromString(const QString& s)
{
    if (s.compare(kKindShaderStr, Qt::CaseInsensitive) == 0)
        return AnimationAppRule::Kind::Shader;
    if (s.compare(kKindTimingStr, Qt::CaseInsensitive) == 0)
        return AnimationAppRule::Kind::Timing;
    return std::nullopt;
}

bool patternMatches(const QString& pattern, const QString& windowClass)
{
    // Belt-and-braces empty-pattern guard. `append` and `fromJson`
    // both reject empty patterns at insertion time AND the resolvers
    // short-circuit on empty windowClass before calling here, so this
    // can only fire if a future call site bypasses both layers — but
    // a "match every window" rule is dangerous enough that the cost
    // of the extra branch is worth the safety property.
    if (pattern.isEmpty())
        return false;
    return windowClass.contains(pattern, Qt::CaseInsensitive);
}

} // namespace

bool AnimationAppRule::operator==(const AnimationAppRule& other) const noexcept
{
    if (classPattern != other.classPattern || eventPath != other.eventPath || kind != other.kind)
        return false;
    switch (kind) {
    case Kind::Shader:
        return effectId == other.effectId && shaderParams == other.shaderParams;
    case Kind::Timing:
        return curve == other.curve && durationMs == other.durationMs;
    }
    Q_UNREACHABLE();
}

QJsonObject AnimationAppRule::toJson() const
{
    QJsonObject o;
    o.insert(kKeyClassPattern, classPattern);
    o.insert(kKeyEventPath, eventPath);
    o.insert(kKeyKind, kindToString(kind));
    switch (kind) {
    case Kind::Shader:
        // Always emit `effectId` even when empty — it is a meaningful
        // value (the engaged-blocking sentinel) and consumers compare
        // by presence-vs-absence to distinguish "rule disables shader"
        // from "rule field missing in malformed JSON."
        o.insert(kKeyEffectId, effectId);
        if (!shaderParams.isEmpty())
            o.insert(kKeyShaderParams, QJsonObject::fromVariantMap(shaderParams));
        break;
    case Kind::Timing:
        if (!curve.isEmpty())
            o.insert(kKeyCurve, curve);
        if (durationMs > 0)
            o.insert(kKeyDurationMs, durationMs);
        break;
    }
    return o;
}

std::optional<AnimationAppRule> AnimationAppRule::fromJson(const QJsonObject& obj)
{
    AnimationAppRule r;
    r.classPattern = obj.value(kKeyClassPattern).toString();
    r.eventPath = obj.value(kKeyEventPath).toString();
    if (r.classPattern.isEmpty() || r.eventPath.isEmpty())
        return std::nullopt;
    // Strict kind whitelist. Direct callers (tests, ad-hoc round-trips)
    // need the drop-on-malformed contract: silently coercing an unknown
    // kind to Shader produces an engaged-empty-effectId rule that would
    // block animations for matching windows without the user's consent.
    const auto parsedKind = kindFromString(obj.value(kKeyKind).toString());
    if (!parsedKind)
        return std::nullopt;
    r.kind = *parsedKind;
    switch (r.kind) {
    case Kind::Shader: {
        r.effectId = obj.value(kKeyEffectId).toString();
        // Distinguish "field absent" (legitimate — no params override)
        // from "field present but malformed" so the latter surfaces a
        // diagnostic instead of silently absorbing a config bug. We
        // still coerce malformed payloads to an empty map (rather than
        // dropping the rule) because `effectId` may carry a meaningful
        // override on its own — the rule is functional with empty
        // params, just without per-effect tuning. `toObject()` returns
        // an empty object for any non-object JSON value, so the
        // explicit type check happens on the QJsonValue.
        const auto paramsValue = obj.value(kKeyShaderParams);
        if (!paramsValue.isUndefined() && !paramsValue.isNull() && !paramsValue.isObject()) {
            qCWarning(lcRules) << "shaderParams present but not an object — coercing to empty map."
                               << "classPattern:" << r.classPattern << "eventPath:" << r.eventPath;
        }
        r.shaderParams = paramsValue.toObject().toVariantMap();
        break;
    }
    case Kind::Timing:
        r.curve = obj.value(kKeyCurve).toString();
        r.durationMs = obj.value(kKeyDurationMs).toInt(0);
        break;
    }
    return r;
}

// ============================================================================
// AnimationAppRuleList
// ============================================================================

AnimationAppRule AnimationAppRuleList::at(int index) const
{
    if (index < 0 || index >= m_rules.size()) {
        qCWarning(lcRules) << "AnimationAppRuleList::at out-of-range index:" << index << "size:" << m_rules.size();
        return {};
    }
    return m_rules.at(index);
}

bool AnimationAppRuleList::append(const AnimationAppRule& rule)
{
    if (rule.classPattern.isEmpty() || rule.eventPath.isEmpty()) {
        qCWarning(lcRules) << "Rejecting rule with empty classPattern or eventPath:" << rule.classPattern
                           << rule.eventPath;
        return false;
    }
    m_rules.append(rule);
    return true;
}

void AnimationAppRuleList::removeAt(int index)
{
    if (index < 0 || index >= m_rules.size())
        return;
    m_rules.removeAt(index);
}

int AnimationAppRuleList::setEntries(const QList<AnimationAppRule>& rules)
{
    QList<AnimationAppRule> validated;
    validated.reserve(rules.size());
    for (const auto& rule : rules) {
        if (rule.classPattern.isEmpty() || rule.eventPath.isEmpty()) {
            qCWarning(lcRules) << "setEntries: dropping rule with empty classPattern or eventPath:" << rule.classPattern
                               << rule.eventPath;
            continue;
        }
        validated.append(rule);
    }
    const int accepted = validated.size();
    m_rules = std::move(validated);
    return accepted;
}

void AnimationAppRuleList::move(int from, int to)
{
    if (from < 0 || from >= m_rules.size())
        return;
    if (to < 0 || to >= m_rules.size())
        return;
    if (from == to)
        return;
    m_rules.move(from, to);
}

std::optional<AnimationAppRule> AnimationAppRuleList::firstMatchOfKind(AnimationAppRule::Kind kind,
                                                                       const QString& windowClass,
                                                                       const QString& eventPath) const
{
    if (windowClass.isEmpty() || eventPath.isEmpty())
        return std::nullopt;
    for (const auto& rule : m_rules) {
        if (rule.kind != kind)
            continue;
        if (rule.eventPath != eventPath)
            continue;
        if (patternMatches(rule.classPattern, windowClass))
            return rule;
    }
    return std::nullopt;
}

std::optional<AnimationAppRule> AnimationAppRuleList::resolveShader(const QString& windowClass,
                                                                    const QString& eventPath) const
{
    return firstMatchOfKind(AnimationAppRule::Kind::Shader, windowClass, eventPath);
}

std::optional<AnimationAppRule> AnimationAppRuleList::resolveTiming(const QString& windowClass,
                                                                    const QString& eventPath) const
{
    return firstMatchOfKind(AnimationAppRule::Kind::Timing, windowClass, eventPath);
}

QJsonArray AnimationAppRuleList::toJson() const
{
    QJsonArray arr;
    for (const auto& rule : m_rules)
        arr.append(rule.toJson());
    return arr;
}

AnimationAppRuleList AnimationAppRuleList::fromJson(const QJsonArray& arr)
{
    AnimationAppRuleList list;
    for (const auto& v : arr) {
        if (!v.isObject())
            continue;
        // The rule-level loader is strict (returns nullopt on unknown
        // kind / empty pattern / empty eventPath), so a single drop
        // gate covers every malformed entry. Successful parses route
        // through `append()` so any future strengthening of append's
        // validation (dedup, normalisation) automatically applies to
        // the JSON load path.
        const auto raw = v.toObject();
        const auto rule = AnimationAppRule::fromJson(raw);
        if (!rule) {
            // Surface the offending tuple so an operator triaging a
            // user-reported "rules vanished after restart" can grep
            // their journal for the dropped pattern instead of
            // diffing JSON snapshots by hand.
            qCWarning(lcRules) << "Dropping malformed rule from JSON. classPattern:"
                               << raw.value(kKeyClassPattern).toString()
                               << "eventPath:" << raw.value(kKeyEventPath).toString()
                               << "kind:" << raw.value(kKeyKind).toString();
            continue;
        }
        // Honour append's bool return so any future strengthening of
        // its validation (dedup, normalisation) surfaces a journal
        // entry on the JSON-load path. Today's append never rejects a
        // post-fromJson rule (the strict rule-level loader already
        // catches every classPattern/eventPath/kind failure), but
        // checking the return future-proofs the loader.
        if (!list.append(*rule)) {
            qCWarning(lcRules) << "JSON-load: append() rejected a rule that passed rule-level fromJson —"
                                  " the validation gates have drifted apart";
        }
    }
    return list;
}

} // namespace PhosphorAnimationShaders

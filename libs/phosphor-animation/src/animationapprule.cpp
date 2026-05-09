// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/AnimationAppRule.h>

#include <QJsonValue>
#include <QLoggingCategory>

namespace PhosphorAnimationShaders {

namespace {
Q_LOGGING_CATEGORY(lcRules, "phosphoranimation.apprule")

constexpr auto kKeyClassPattern = "classPattern";
constexpr auto kKeyEventPath = "eventPath";
constexpr auto kKeyKind = "kind";
constexpr auto kKeyEffectId = "effectId";
constexpr auto kKeyShaderParams = "shaderParams";
constexpr auto kKeyCurve = "curve";
constexpr auto kKeyDurationMs = "durationMs";

constexpr auto kKindShaderStr = "shader";
constexpr auto kKindTimingStr = "timing";

QString kindToString(AnimationAppRule::Kind kind)
{
    switch (kind) {
    case AnimationAppRule::Kind::Shader:
        return QString::fromLatin1(kKindShaderStr);
    case AnimationAppRule::Kind::Timing:
        return QString::fromLatin1(kKindTimingStr);
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
    if (s.compare(QLatin1String(kKindShaderStr), Qt::CaseInsensitive) == 0)
        return AnimationAppRule::Kind::Shader;
    if (s.compare(QLatin1String(kKindTimingStr), Qt::CaseInsensitive) == 0)
        return AnimationAppRule::Kind::Timing;
    return std::nullopt;
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
    o.insert(QLatin1String(kKeyClassPattern), classPattern);
    o.insert(QLatin1String(kKeyEventPath), eventPath);
    o.insert(QLatin1String(kKeyKind), kindToString(kind));
    switch (kind) {
    case Kind::Shader:
        // Always emit `effectId` even when empty — it is a meaningful
        // value (the engaged-blocking sentinel) and consumers compare
        // by presence-vs-absence to distinguish "rule disables shader"
        // from "rule field missing in malformed JSON."
        o.insert(QLatin1String(kKeyEffectId), effectId);
        if (!shaderParams.isEmpty())
            o.insert(QLatin1String(kKeyShaderParams), QJsonObject::fromVariantMap(shaderParams));
        break;
    case Kind::Timing:
        if (!curve.isEmpty())
            o.insert(QLatin1String(kKeyCurve), curve);
        if (durationMs > 0)
            o.insert(QLatin1String(kKeyDurationMs), durationMs);
        break;
    }
    return o;
}

AnimationAppRule AnimationAppRule::fromJson(const QJsonObject& obj)
{
    AnimationAppRule r;
    r.classPattern = obj.value(QLatin1String(kKeyClassPattern)).toString();
    r.eventPath = obj.value(QLatin1String(kKeyEventPath)).toString();
    // Default to Shader when the `kind` field is absent or unknown —
    // the list-level `AnimationAppRuleList::fromJson` re-parses the
    // raw kind string against `kindFromString`'s strict whitelist and
    // drops entries that fail it, so silent "unknown → Shader"
    // coercion at this layer never reaches resolve time.
    r.kind = kindFromString(obj.value(QLatin1String(kKeyKind)).toString()).value_or(Kind::Shader);
    switch (r.kind) {
    case Kind::Shader:
        r.effectId = obj.value(QLatin1String(kKeyEffectId)).toString();
        r.shaderParams = obj.value(QLatin1String(kKeyShaderParams)).toObject().toVariantMap();
        break;
    case Kind::Timing:
        r.curve = obj.value(QLatin1String(kKeyCurve)).toString();
        r.durationMs = obj.value(QLatin1String(kKeyDurationMs)).toInt(0);
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

void AnimationAppRuleList::append(const AnimationAppRule& rule)
{
    if (rule.classPattern.isEmpty() || rule.eventPath.isEmpty()) {
        qCWarning(lcRules) << "Rejecting rule with empty classPattern or eventPath";
        return;
    }
    m_rules.append(rule);
}

void AnimationAppRuleList::removeAt(int index)
{
    if (index < 0 || index >= m_rules.size())
        return;
    m_rules.removeAt(index);
}

void AnimationAppRuleList::setEntries(const QList<AnimationAppRule>& rules)
{
    QList<AnimationAppRule> validated;
    validated.reserve(rules.size());
    for (const auto& rule : rules) {
        if (rule.classPattern.isEmpty() || rule.eventPath.isEmpty()) {
            qCWarning(lcRules) << "setEntries: dropping rule with empty classPattern or eventPath";
            continue;
        }
        validated.append(rule);
    }
    m_rules = std::move(validated);
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

namespace {
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

std::optional<AnimationAppRule> AnimationAppRuleList::resolveShader(const QString& windowClass,
                                                                    const QString& eventPath) const
{
    if (windowClass.isEmpty() || eventPath.isEmpty())
        return std::nullopt;
    for (const auto& rule : m_rules) {
        if (rule.kind != AnimationAppRule::Kind::Shader)
            continue;
        if (rule.eventPath != eventPath)
            continue;
        if (patternMatches(rule.classPattern, windowClass))
            return rule;
    }
    return std::nullopt;
}

std::optional<AnimationAppRule> AnimationAppRuleList::resolveTiming(const QString& windowClass,
                                                                    const QString& eventPath) const
{
    if (windowClass.isEmpty() || eventPath.isEmpty())
        return std::nullopt;
    for (const auto& rule : m_rules) {
        if (rule.kind != AnimationAppRule::Kind::Timing)
            continue;
        if (rule.eventPath != eventPath)
            continue;
        if (patternMatches(rule.classPattern, windowClass))
            return rule;
    }
    return std::nullopt;
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
        const auto obj = v.toObject();
        // Strict kind validation BEFORE constructing the rule —
        // `AnimationAppRule::fromJson` silently coerces unknown kind
        // strings to Shader so the value type round-trips without
        // optional plumbing, but a malformed kind in a JSON load is a
        // user-data error we'd rather drop than silently reinterpret
        // as an engaged-blocking shader rule.
        if (!kindFromString(obj.value(QLatin1String(kKeyKind)).toString()).has_value()) {
            qCWarning(lcRules) << "Dropping rule with unknown kind:" << obj.value(QLatin1String(kKeyKind));
            continue;
        }
        // Route through `append()` rather than re-implementing its
        // empty-pattern / empty-eventPath gate inline — that way a
        // future strengthening of `append()` validation (e.g. a
        // dedup pass) automatically applies to the JSON load path.
        // `append()` is a no-op (with warning) on rejected entries.
        list.append(AnimationAppRule::fromJson(obj));
    }
    return list;
}

} // namespace PhosphorAnimationShaders

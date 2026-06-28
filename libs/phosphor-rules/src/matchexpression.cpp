// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorRules/MatchExpression.h>

#include <PhosphorIdentity/WindowId.h>

#include <QJsonArray>
#include <QJsonValue>

#include <cmath>
#include <limits>

#include "rulelogging.h"

namespace PhosphorRules {

namespace {

// Canonical JSON keys. `constexpr QLatin1StringView` lets call sites use the
// constants directly as QLatin1String without per-call wrapping.
constexpr QLatin1StringView kKeyField{"field"};
constexpr QLatin1StringView kKeyOp{"op"};
constexpr QLatin1StringView kKeyValue{"value"};
constexpr QLatin1StringView kKeyAll{"all"};
constexpr QLatin1StringView kKeyAny{"any"};
constexpr QLatin1StringView kKeyNone{"none"};

/// True if @p op is a string-only operator (substring family + Regex).
bool operatorIsStringOnly(Operator op)
{
    switch (op) {
    case Operator::Contains:
    case Operator::StartsWith:
    case Operator::EndsWith:
    case Operator::Regex:
        return true;
    case Operator::Equals:
    case Operator::AppIdMatches:
    case Operator::GreaterThan:
    case Operator::LessThan:
        return false;
    }
    return false;
}

/// True if @p op is a numeric-comparison operator.
bool operatorIsNumeric(Operator op)
{
    return op == Operator::GreaterThan || op == Operator::LessThan;
}

/// String comparison honouring the operator family. Case-insensitive
/// throughout, matching the design's string-match contract.
bool stringMatch(const QString& subject, Operator op, const QString& pattern)
{
    switch (op) {
    case Operator::Equals:
        return subject.compare(pattern, Qt::CaseInsensitive) == 0;
    case Operator::Contains:
        // An empty pattern would otherwise match everything — a dangerous
        // "match every window" rule. Reject it explicitly.
        return !pattern.isEmpty() && subject.contains(pattern, Qt::CaseInsensitive);
    case Operator::StartsWith:
        return !pattern.isEmpty() && subject.startsWith(pattern, Qt::CaseInsensitive);
    case Operator::EndsWith:
        return !pattern.isEmpty() && subject.endsWith(pattern, Qt::CaseInsensitive);
    case Operator::AppIdMatches:
        return PhosphorIdentity::WindowId::appIdMatches(subject, pattern);
    default:
        return false;
    }
}

} // namespace

// ── Construction ────────────────────────────────────────────────────────

MatchExpression MatchExpression::makeLeaf(const Predicate& predicate)
{
    MatchExpression expr;
    expr.m_kind = Kind::Leaf;
    expr.m_predicate = predicate;
    // Compile the regex eagerly so evaluate() never mutates the expression
    // itself — evaluate() is then reentrant. Note this does NOT make copies
    // sharing the regex leaf concurrently evaluable: QRegularExpression::match()
    // mutates the shared instance. See the MatchExpression class doc.
    expr.ensureRegex();
    return expr;
}

MatchExpression MatchExpression::makeLeaf(Field field, Operator op, const QVariant& value)
{
    return makeLeaf(Predicate{field, op, value});
}

MatchExpression MatchExpression::makeAll(const QList<MatchExpression>& children)
{
    MatchExpression expr;
    expr.m_kind = Kind::All;
    expr.m_children = children;
    return expr;
}

MatchExpression MatchExpression::makeAny(const QList<MatchExpression>& children)
{
    MatchExpression expr;
    expr.m_kind = Kind::Any;
    expr.m_children = children;
    return expr;
}

MatchExpression MatchExpression::makeNone(const QList<MatchExpression>& children)
{
    MatchExpression expr;
    expr.m_kind = Kind::None;
    expr.m_children = children;
    return expr;
}

// ── Introspection ───────────────────────────────────────────────────────

bool MatchExpression::isContextOnly() const
{
    if (m_kind == Kind::Leaf) {
        switch (m_predicate.field) {
        case Field::ScreenId:
        case Field::VirtualDesktop:
        case Field::Activity:
        case Field::Mode:
            return true;
        default:
            return false;
        }
    }
    // A composite is context-only iff every child is context-only AND the
    // composite carries at least one child. An empty `Any{}` is always-false
    // (vacuous OR over zero terms), an empty `None{}` is always-true
    // (vacuous NOT-any), and an empty `All{}` is the catch-all the default
    // ctor builds. The first two are not "context-only" in any useful
    // sense — they encode rule shapes that fire (or fail to fire) based on
    // no window context at all. The catch-all is genuinely context-only
    // by the same vacuous semantics but we keep the narrow `isCatchAll()`
    // accessor for it (declared in the header); returning false here
    // for empty Any/None lets `validationIssues()` flag those shapes as
    // sus rather than coercing them into context-only behaviour.
    if (m_children.isEmpty()) {
        return m_kind == Kind::All;
    }
    for (const auto& child : m_children) {
        if (!child.isContextOnly()) {
            return false;
        }
    }
    return true;
}

bool MatchExpression::referencesAnyField(const QSet<Field>& fields) const
{
    if (m_kind == Kind::Leaf) {
        return fields.contains(m_predicate.field);
    }
    // Recurse through every child regardless of composite kind — a field
    // mentioned inside a `none{}` negation still counts as "the rule talks
    // about this field". An empty composite has no children and references
    // nothing.
    for (const auto& child : m_children) {
        if (child.referencesAnyField(fields)) {
            return true;
        }
    }
    return false;
}

void MatchExpression::ensureRegex()
{
    if (m_kind != Kind::Leaf || m_predicate.op != Operator::Regex || m_compiledRegex) {
        return;
    }
    m_compiledRegex =
        std::make_shared<QRegularExpression>(m_predicate.value.toString(), QRegularExpression::CaseInsensitiveOption);
}

// ── Validation ──────────────────────────────────────────────────────────

bool MatchExpression::isValid() const
{
    if (m_kind != Kind::Leaf) {
        for (const auto& child : m_children) {
            if (!child.isValid()) {
                return false;
            }
        }
        return true;
    }

    const Field field = m_predicate.field;
    const Operator op = m_predicate.op;

    // AppIdMatches only applies to AppId.
    if (op == Operator::AppIdMatches && field != Field::AppId) {
        return false;
    }
    // String operators require a string field.
    if (operatorIsStringOnly(op) && !fieldIsString(field)) {
        return false;
    }
    // A context string field (Mode / ScreenId / Activity) with an empty `Equals`
    // value matches a window whose context value is empty — e.g. `Mode Equals ""`
    // matches every FLOATING window (a floating window carries an empty mode).
    // That is never an authored rule (the pickers only offer real values), so
    // reject it rather than let a hand-edited or corrupt store silently match
    // everything in that empty-context state. (The Contains/StartsWith/EndsWith/
    // Regex branches already reject empty patterns for the same reason.)
    if (op == Operator::Equals && fieldIsContext(field) && fieldIsString(field)
        && m_predicate.value.toString().isEmpty()) {
        return false;
    }
    // Numeric comparisons require a numeric field.
    if (operatorIsNumeric(op) && !fieldIsNumeric(field)) {
        return false;
    }
    // A Regex pattern must compile. The program was compiled eagerly by
    // ensureRegex() at construction — reuse it rather than recompiling.
    // Also reject an empty pattern: QRegularExpression("") is a valid
    // empty regex that matches at every position, turning the leaf into
    // an unintentional "match anything" rule. The sibling `stringMatch`
    // branches (Contains / StartsWith / EndsWith) reject empty patterns
    // for the same reason; the Regex path closes the symmetric door.
    if (op == Operator::Regex) {
        if (!m_compiledRegex || !m_compiledRegex->isValid()) {
            return false;
        }
        if (m_predicate.value.toString().isEmpty()) {
            return false;
        }
    }
    // A numeric field's value must be an integer. JSON's only numeric type is
    // double, so an authored `"value": 12.7` would otherwise be silently
    // truncated by `subject.toInt()` to 12 — a Pid 12.7 leaf would match pid
    // 12. Reject the leaf rather than accepting a value the user almost
    // certainly did not intend.
    if (fieldIsNumeric(field)) {
        const int valueTypeId = m_predicate.value.metaType().id();
        if (valueTypeId == QMetaType::Double) {
            const double d = m_predicate.value.toDouble();
            // Numeric leaves evaluate via subject.toInt() / value.toInt() (int),
            // so the value must be a finite, integer-valued number inside int
            // range. Reject non-finite or out-of-range values FIRST: casting
            // such a double to an integer is undefined behaviour, and toInt()
            // would otherwise overflow at evaluation and match the wrong subject
            // (e.g. a value above INT_MAX collapsing to 0, which reads as "pid 0").
            if (!std::isfinite(d) || d < static_cast<double>(std::numeric_limits<int>::min())
                || d > static_cast<double>(std::numeric_limits<int>::max())) {
                qCWarning(lcRule) << "MatchExpression: rejecting numeric leaf with out-of-range value" << d
                                  << "— numeric fields require an integer within int range.";
                return false;
            }
            // A bit-for-bit integer-valued double is fine (JSON has no int);
            // only a genuine fractional part is the authoring mistake (a Pid 12.7
            // leaf would otherwise match pid 12).
            if (d != std::trunc(d)) {
                qCWarning(lcRule) << "MatchExpression: rejecting numeric leaf with fractional value" << d
                                  << "— numeric fields require integer values.";
                return false;
            }
        }
    }
    return true;
}

// ── Evaluation ──────────────────────────────────────────────────────────

bool MatchExpression::evaluate(const WindowQuery& query) const
{
    switch (m_kind) {
    case Kind::Leaf:
        return evaluateLeaf(query);
    case Kind::All:
        // Empty All{} is the always-true catch-all (fold identity for AND).
        for (const auto& child : m_children) {
            if (!child.evaluate(query)) {
                return false;
            }
        }
        return true;
    case Kind::Any:
        // Empty Any{} is always-false (fold identity for OR).
        for (const auto& child : m_children) {
            if (child.evaluate(query)) {
                return true;
            }
        }
        return false;
    case Kind::None:
        // None{} matches iff no child matches; empty None{} is always-true.
        for (const auto& child : m_children) {
            if (child.evaluate(query)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

bool MatchExpression::evaluateLeaf(const WindowQuery& query) const
{
    const std::optional<QVariant> resolved = query.valueForField(m_predicate.field);
    // An absent window attribute can never match — this is what makes
    // window-property predicates inert during windowless context resolution.
    if (!resolved) {
        return false;
    }
    const QVariant& subject = *resolved;
    const Operator op = m_predicate.op;
    const QVariant& value = m_predicate.value;

    // ── Boolean fields ──
    if (fieldIsBool(m_predicate.field)) {
        // Only Equals is meaningful on a flag.
        if (op != Operator::Equals) {
            return false;
        }
        return subject.toBool() == value.toBool();
    }

    // ── Numeric fields (Pid / VirtualDesktop / Width / Height / PositionX / PositionY) ──
    if (fieldIsNumeric(m_predicate.field)) {
        const int lhs = subject.toInt();
        const int rhs = value.toInt();
        switch (op) {
        case Operator::Equals:
            return lhs == rhs;
        case Operator::GreaterThan:
            return lhs > rhs;
        case Operator::LessThan:
            return lhs < rhs;
        default:
            return false;
        }
    }

    // ── WindowType — compared by its underlying int (Equals only) ──
    if (m_predicate.field == Field::WindowType) {
        if (op != Operator::Equals) {
            return false;
        }
        return subject.toInt() == value.toInt();
    }

    // ── String fields ──
    if (op == Operator::Regex) {
        // The program was compiled eagerly at construction by ensureRegex().
        // A leaf that reaches evaluate() has passed isValid(), so the program
        // is present and valid; the guard only defends a hand-built leaf that
        // skipped validation.
        //
        // `match()` mutates m_compiledRegex's internal state, and that
        // instance is shared across value-copies of this expression — so this
        // branch is reentrant but NOT safe to run concurrently against a copy
        // sharing this leaf. Callers must serialize. See the class doc.
        if (!m_compiledRegex || !m_compiledRegex->isValid()) {
            return false;
        }
        return m_compiledRegex->match(subject.toString()).hasMatch();
    }
    return stringMatch(subject.toString(), op, value.toString());
}

// ── Serialization ───────────────────────────────────────────────────────

QJsonObject MatchExpression::toJson() const
{
    QJsonObject o;
    switch (m_kind) {
    case Kind::Leaf:
        o.insert(kKeyField, fieldToString(m_predicate.field));
        o.insert(kKeyOp, operatorToString(m_predicate.op));
        o.insert(kKeyValue, QJsonValue::fromVariant(m_predicate.value));
        break;
    case Kind::All:
    case Kind::Any:
    case Kind::None: {
        QJsonArray arr;
        for (const auto& child : m_children) {
            arr.append(child.toJson());
        }
        const QLatin1StringView key = (m_kind == Kind::All) ? kKeyAll : (m_kind == Kind::Any) ? kKeyAny : kKeyNone;
        o.insert(key, arr);
        break;
    }
    }
    return o;
}

std::optional<MatchExpression> MatchExpression::fromJson(const QJsonObject& obj)
{
    return fromJsonAtDepth(obj, 0);
}

std::optional<MatchExpression> MatchExpression::fromJsonAtDepth(const QJsonObject& obj, int depth)
{
    // Hard cap on composite nesting. A hand-authored rule never approaches
    // 32 levels — only a malicious or corrupt store does. Reject the whole
    // expression before the recursion can blow the stack (the `kMaxFileBytes`
    // cap on the rule store does NOT protect against this: a few KB of
    // nested `{"all":[…]}` already exceeds any reasonable stack budget).
    if (depth > kMaxParseDepth) {
        qCWarning(lcRule) << "Match expression exceeds the parse-depth cap of" << kMaxParseDepth
                          << "— refusing to parse.";
        return std::nullopt;
    }

    // Composite — exactly one of all/any/none must be present and an array.
    const auto loadComposite = [depth](const QJsonValue& arrayValue, Kind kind) -> std::optional<MatchExpression> {
        if (!arrayValue.isArray()) {
            qCWarning(lcRule) << "Composite match node has a non-array body — dropping expression.";
            return std::nullopt;
        }
        QList<MatchExpression> children;
        for (const QJsonValue& v : arrayValue.toArray()) {
            if (!v.isObject()) {
                qCWarning(lcRule) << "Composite child is not an object — dropping expression.";
                return std::nullopt;
            }
            const auto child = MatchExpression::fromJsonAtDepth(v.toObject(), depth + 1);
            if (!child) {
                return std::nullopt;
            }
            children.append(*child);
        }
        switch (kind) {
        case Kind::All:
            return MatchExpression::makeAll(children);
        case Kind::Any:
            return MatchExpression::makeAny(children);
        case Kind::None:
            return MatchExpression::makeNone(children);
        case Kind::Leaf:
            break;
        }
        return std::nullopt;
    };

    const bool hasAll = obj.contains(kKeyAll);
    const bool hasAny = obj.contains(kKeyAny);
    const bool hasNone = obj.contains(kKeyNone);
    const int compositeCount = (hasAll ? 1 : 0) + (hasAny ? 1 : 0) + (hasNone ? 1 : 0);

    if (compositeCount > 1) {
        qCWarning(lcRule) << "Match node carries more than one composite key — dropping expression.";
        return std::nullopt;
    }
    // Strict-key discipline — a composite must not also carry leaf keys
    // (`field`/`op`/`value`). A hand-edited or stale-schema object like
    // `{"all":[…], "field":"appId"}` would otherwise silently behave as a
    // composite while the leaf keys are dropped — masking the authoring
    // mistake. RuleAction::fromJson uses the same strict-key shape; this
    // loader mirrors it.
    if (compositeCount == 1 && (obj.contains(kKeyField) || obj.contains(kKeyOp) || obj.contains(kKeyValue))) {
        qCWarning(lcRule)
            << "Match node mixes a composite key with leaf keys (`field`/`op`/`value`) — dropping expression.";
        return std::nullopt;
    }
    if (hasAll) {
        return loadComposite(obj.value(kKeyAll), Kind::All);
    }
    if (hasAny) {
        return loadComposite(obj.value(kKeyAny), Kind::Any);
    }
    if (hasNone) {
        return loadComposite(obj.value(kKeyNone), Kind::None);
    }

    // Leaf — must carry a valid field + operator.
    const auto field = fieldFromString(obj.value(kKeyField).toString());
    const auto op = operatorFromString(obj.value(kKeyOp).toString());
    if (!field || !op) {
        qCWarning(lcRule) << "Leaf match predicate has an unknown field/operator — dropping expression. field:"
                          << obj.value(kKeyField).toString() << "op:" << obj.value(kKeyOp).toString();
        return std::nullopt;
    }
    // Every leaf operator compares against a value — a leaf with no `value`
    // key is structurally incomplete. Drop it rather than silently treating
    // the absent value as an empty string / zero, which would produce a
    // surprising "matches everything with an empty attribute" rule.
    if (!obj.contains(kKeyValue)) {
        qCWarning(lcRule) << "Leaf match predicate has no `value` key — dropping expression. field:"
                          << fieldToString(*field) << "op:" << operatorToString(*op);
        return std::nullopt;
    }
    MatchExpression leaf = makeLeaf(*field, *op, obj.value(kKeyValue).toVariant());
    if (!leaf.isValid()) {
        qCWarning(lcRule) << "Leaf match predicate is structurally invalid (field/operator mismatch or bad"
                             " regex) — dropping expression. field:"
                          << fieldToString(*field) << "op:" << operatorToString(*op);
        return std::nullopt;
    }
    return leaf;
}

bool MatchExpression::operator==(const MatchExpression& other) const
{
    if (m_kind != other.m_kind) {
        return false;
    }
    if (m_kind == Kind::Leaf) {
        return m_predicate == other.m_predicate;
    }
    return m_children == other.m_children;
}

} // namespace PhosphorRules

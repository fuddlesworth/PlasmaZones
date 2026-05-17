// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QJsonObject>
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QVariant>

#include <memory>
#include <optional>

#include "MatchTypes.h"
#include "WindowQuery.h"
#include "phosphorwindowrule_export.h"

namespace PhosphorWindowRule {

/**
 * @brief A composable predicate tree evaluated against a WindowQuery.
 *
 * An expression is either:
 *   - a **leaf** — a `Predicate { field, op, value }`, or
 *   - a **composite** — `All` (AND), `Any` (OR), or `None` (NOT-any) over
 *     a list of child expressions.
 *
 * MatchExpression is a copyable value type — it lives inside `QList<WindowRule>`.
 * The compiled regex for a `Regex` leaf is lazily built and cached in a
 * `mutable shared_ptr` so copies share the compiled program.
 *
 * An empty `All{}` is the **always-true catch-all** — the migrated provider
 * default. An empty `Any{}` / `None{}` are always-false / always-true
 * respectively, following the standard fold identities.
 */
class PHOSPHORWINDOWRULE_EXPORT MatchExpression
{
public:
    /// Tree node kind.
    enum class Kind {
        Leaf, ///< a single field/op/value predicate
        All, ///< AND — every child must match
        Any, ///< OR — at least one child must match
        None, ///< NOT-any — no child may match
    };

    /// Leaf predicate payload.
    struct Predicate
    {
        Field field = Field::AppId;
        Operator op = Operator::Equals;
        QVariant value;

        bool operator==(const Predicate& other) const
        {
            return field == other.field && op == other.op && value == other.value;
        }
    };

    /// Default-constructs an empty `All{}` — the always-true catch-all.
    MatchExpression()
        : m_kind(Kind::All)
    {
    }

    /// Constructs a leaf predicate expression.
    static MatchExpression makeLeaf(const Predicate& predicate);
    static MatchExpression makeLeaf(Field field, Operator op, const QVariant& value);

    /// Constructs a composite expression.
    static MatchExpression makeAll(const QList<MatchExpression>& children);
    static MatchExpression makeAny(const QList<MatchExpression>& children);
    static MatchExpression makeNone(const QList<MatchExpression>& children);

    Kind kind() const
    {
        return m_kind;
    }
    bool isLeaf() const
    {
        return m_kind == Kind::Leaf;
    }

    /// Leaf accessor — only meaningful when `isLeaf()`.
    const Predicate& predicate() const
    {
        return m_predicate;
    }

    /// Composite accessor — empty for a leaf.
    const QList<MatchExpression>& children() const
    {
        return m_children;
    }

    /// True if this is the empty catch-all `All{}` — matches every query.
    bool isCatchAll() const
    {
        return m_kind == Kind::All && m_children.isEmpty();
    }

    /// True if this expression references only context fields (ScreenId /
    /// VirtualDesktop / Activity) — i.e. a windowless context rule. An empty
    /// catch-all is context-only by this definition.
    bool isContextOnly() const;

    /**
     * @brief Evaluate this expression against @p query.
     *
     * A leaf predicate over an absent window field evaluates `false`.
     * Composites fold per their kind. The empty `All{}` returns `true`.
     */
    bool evaluate(const WindowQuery& query) const;

    /// True if this expression is structurally well-formed: every leaf has a
    /// field/operator combination that can actually match (a string operator
    /// on a numeric field is rejected), and `Regex` patterns compile.
    bool isValid() const;

    QJsonObject toJson() const;

    /// Strict loader — returns nullopt on a malformed / invalid expression.
    static std::optional<MatchExpression> fromJson(const QJsonObject& obj);

    bool operator==(const MatchExpression& other) const;
    bool operator!=(const MatchExpression& other) const
    {
        return !(*this == other);
    }

private:
    Kind m_kind = Kind::All;
    Predicate m_predicate;
    QList<MatchExpression> m_children;

    // Lazily compiled & cached regex for a `Regex` leaf. `shared_ptr` so
    // value-copies of the expression share the compiled program rather than
    // recompiling. `mutable` because `evaluate()` is logically const.
    mutable std::shared_ptr<QRegularExpression> m_compiledRegex;

    bool evaluateLeaf(const WindowQuery& query) const;
    const QRegularExpression& compiledRegex() const;
};

} // namespace PhosphorWindowRule

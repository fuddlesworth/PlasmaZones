// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QJsonObject>
#include <QList>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QVariant>

#include <memory>
#include <optional>

#include "MatchTypes.h"
#include "WindowQuery.h"
#include "phosphorrules_export.h"

namespace PhosphorRules {

/**
 * @brief A composable predicate tree evaluated against a WindowQuery.
 *
 * An expression is either:
 *   - a **leaf** — a `Predicate { field, op, value }`, or
 *   - a **composite** — `All` (AND), `Any` (OR), or `None` (NOT-any) over
 *     a list of child expressions.
 *
 * MatchExpression is a copyable value type — it lives inside `QList<Rule>`.
 * The compiled regex for a `Regex` leaf is built **eagerly** at construction
 * time (in `makeLeaf` / `fromJson`) and stored in a `shared_ptr` so
 * value-copies share the *same* compiled program rather than recompiling.
 *
 * **Thread-safety.** `evaluate()` performs no lazy mutation of the
 * MatchExpression itself — it only reads the already-compiled program. It is
 * therefore *reentrant*. It is **NOT**, however, safe to call concurrently on
 * two MatchExpression copies that share a `Regex` leaf: the shared
 * `shared_ptr<QRegularExpression>` means both copies dispatch through one
 * `QRegularExpression` instance, and `QRegularExpression::match()` mutates that
 * instance's internal state (it is not thread-safe for concurrent `match()`
 * calls on the same object). Callers that evaluate copies of a regex-bearing
 * expression from multiple threads must serialize those calls themselves.
 *
 * An empty `All{}` is the **always-true catch-all** — the migrated provider
 * default. An empty `Any{}` / `None{}` are always-false / always-true
 * respectively, following the standard fold identities.
 */
class PHOSPHORRULES_EXPORT MatchExpression
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

    /// True if any leaf predicate anywhere in the tree (including inside
    /// `none{}` negations) tests one of @p fields. This is a purely
    /// structural query about what the expression *mentions*, independent of
    /// whether it matches any particular window — callers use it to ask
    /// "does this rule deliberately target a window type?" (e.g. a rule that
    /// references IsTransient / WindowType is opting into transient windows,
    /// whereas a class-only rule that merely happens to match a tooltip is
    /// not). An empty composite references nothing and returns false.
    bool referencesAnyField(const QSet<Field>& fields) const;

    /**
     * @brief Evaluate this expression against @p query.
     *
     * A leaf predicate over an absent window field evaluates `false`.
     * Composites fold per their kind. The empty `All{}` returns `true`.
     *
     * Reentrant, but **not** safe to call concurrently on copies that share a
     * `Regex` leaf — see the class-level thread-safety note. Callers must
     * serialize concurrent evaluation of regex-bearing expressions.
     */
    bool evaluate(const WindowQuery& query) const;

    /// True if this expression is structurally well-formed: every leaf has a
    /// field/operator combination that can actually match (a string operator
    /// on a numeric field is rejected), and `Regex` patterns compile.
    bool isValid() const;

    QJsonObject toJson() const;

    /// Strict loader — returns nullopt on a malformed / invalid expression.
    ///
    /// A pathologically deep composite tree (`All{All{All{…}}}`) is rejected:
    /// nesting deeper than @ref kMaxParseDepth levels short-circuits to
    /// `nullopt` with a logged diagnostic, before the recursion would blow
    /// the stack. The cap is generous — a hand-authored rule never approaches
    /// it; only a malicious or corrupt store can.
    static std::optional<MatchExpression> fromJson(const QJsonObject& obj);

    /// Hard cap on the JSON parser's recursion depth. A composite at depth
    /// N may contain children at depth N + 1 — the cap rejects any tree whose
    /// composite nesting would exceed this limit.
    static constexpr int kMaxParseDepth = 32;

    bool operator==(const MatchExpression& other) const;
    bool operator!=(const MatchExpression& other) const
    {
        return !(*this == other);
    }

private:
    Kind m_kind = Kind::All;
    Predicate m_predicate;
    QList<MatchExpression> m_children;

    // Eagerly compiled regex for a `Regex` leaf — built once by ensureRegex()
    // at construction / load time, never mutated by evaluate(). `shared_ptr`
    // so value-copies of the expression share the compiled program rather
    // than recompiling.
    std::shared_ptr<QRegularExpression> m_compiledRegex;

    bool evaluateLeaf(const WindowQuery& query) const;

    // Compiles the `Regex` leaf's pattern into m_compiledRegex if this is a
    // Regex leaf and the program is not yet built. Idempotent; called from
    // every construction path so evaluate() never has to compile.
    void ensureRegex();

    /// Internal: depth-tracked JSON parser. The public @ref fromJson is the
    /// depth=0 entry point; composites recurse through here so a pathological
    /// tree is rejected before it blows the stack.
    static std::optional<MatchExpression> fromJsonAtDepth(const QJsonObject& obj, int depth);
};

} // namespace PhosphorRules

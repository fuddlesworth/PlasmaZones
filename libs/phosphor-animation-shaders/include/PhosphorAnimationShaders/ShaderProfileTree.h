// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimationShaders/ShaderProfile.h>
#include <PhosphorAnimationShaders/phosphoranimationshaders_export.h>

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace PhosphorAnimationShaders {

/**
 * @brief Hierarchical ShaderProfile storage with walk-up inheritance.
 *
 * Same inheritance semantics as `PhosphorAnimation::ProfileTree` but
 * carrying `ShaderProfile` payloads instead of `Profile`. The two trees
 * share the same dot-path namespace (ProfilePaths) so a consumer can
 * resolve both motion and shader config for the same event path.
 *
 * ## Walk-up inheritance
 *
 * For `"window.open"`, resolve() walks:
 *   1. `"window.open"`  (leaf override)
 *   2. `"window"`       (category)
 *   3. `"global"`       (baseline)
 *   4. library default  (empty ShaderProfile — no effect)
 *
 * At each step, engaged optionals replace the accumulator. Unset fields
 * pass through.
 *
 * ## Thread safety
 *
 * Value type, not internally synchronized. Same as ProfileTree.
 */
class PHOSPHORANIMATIONSHADERS_EXPORT ShaderProfileTree
{
public:
    ShaderProfileTree() = default;

    ShaderProfileTree(const ShaderProfileTree&) = default;
    ShaderProfileTree& operator=(const ShaderProfileTree&) = default;
    ShaderProfileTree(ShaderProfileTree&&) = default;
    ShaderProfileTree& operator=(ShaderProfileTree&&) = default;

    // ─────── Lookup ───────

    ShaderProfile resolve(const QString& path) const;
    ShaderProfile directOverride(const QString& path) const;
    bool hasOverride(const QString& path) const;
    QStringList overriddenPaths() const;

    // ─────── Mutation ───────

    void setOverride(const QString& path, const ShaderProfile& profile);
    bool clearOverride(const QString& path);
    void clearAllOverrides();

    // ─────── Baseline ───────

    ShaderProfile baseline() const
    {
        return m_baseline;
    }
    void setBaseline(const ShaderProfile& profile);

    // ─────── Serialization ───────

    QJsonObject toJson() const;
    static ShaderProfileTree fromJson(const QJsonObject& obj);

    // ─────── Equality ───────

    bool operator==(const ShaderProfileTree& other) const;
    bool operator!=(const ShaderProfileTree& other) const
    {
        return !(*this == other);
    }

private:
    ShaderProfile m_baseline;
    QHash<QString, ShaderProfile> m_overrides;
    QStringList m_insertionOrder;
};

} // namespace PhosphorAnimationShaders

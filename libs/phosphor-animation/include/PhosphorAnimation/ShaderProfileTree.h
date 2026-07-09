// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/ShaderProfile.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

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
 * For `"window.appearance.open"`, resolve() walks:
 *   1. `"window.appearance.open"`  (leaf override)
 *   2. `"window.appearance"`       (appearance contract group — the "All Appearance" node)
 *   3. `"window"`                  (category — the all-windows node)
 *   4. `"global"`                  (baseline)
 *   5. library default             (empty ShaderProfile — no effect)
 *
 * At each step, engaged optionals replace the accumulator. Unset fields
 * pass through.
 *
 * ## Thread safety
 *
 * Value type, not internally synchronized. Same as ProfileTree.
 */
class PHOSPHORANIMATION_EXPORT ShaderProfileTree
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

/// Resolve @p path against @p tree, applying the built-in per-event default
/// shader (ProfilePaths::defaultShaderEffectIdForPath, e.g. "window-morph" for
/// window-move events) when the path is TRULY UNSET — i.e. neither it nor any
/// ancestor carries an override. An explicit "None" (an engaged-empty
/// override) IS an override, so it is respected and the default is NOT applied.
///
/// SSOT for "what shader does this event use", shared by the kwin-effect
/// resolution and the settings UI so the built-in default both plays at runtime
/// and shows as the current value in settings — without persisting the default
/// into the user's config (it's computed, not stored).
PHOSPHORANIMATION_EXPORT ShaderProfile resolveShaderWithDefault(const ShaderProfileTree& tree, const QString& path);

} // namespace PhosphorAnimationShaders

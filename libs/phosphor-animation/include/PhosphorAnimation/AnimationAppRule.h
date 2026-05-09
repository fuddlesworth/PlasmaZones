// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QList>
#include <QString>
#include <QVariantMap>
#include <QtCore/qcontainerfwd.h>

#include <optional>

class QJsonArray;
class QJsonObject;

namespace PhosphorAnimationShaders {

/**
 * @brief One per-window animation override entry.
 *
 * Sits alongside the per-event `ShaderProfileTree` in the same
 * `org.plasmazones.settings` config namespace. Where the tree provides
 * a global default per event path (e.g. "every window-open uses
 * pop-in"), an `AnimationAppRule` overrides that default for a
 * specific window-class pattern (e.g. "firefox uses dissolve, but
 * everything else stays pop-in").
 *
 * ## Schema decisions (settled)
 *
 *   - Per-event scope: each rule names one `eventPath` only. To
 *     override the same pattern across multiple events, write a
 *     separate rule per event.
 *   - Single-axis: each rule carries EITHER a `Shader` override OR a
 *     `Timing` override, not both. Override both for the same
 *     (pattern, eventPath) by writing two rules.
 *   - Substring, case-insensitive matching (mirrors snap App Rules).
 *   - First-match per axis (the resolver walks the list and returns
 *     the first matching rule whose `kind` matches the requested
 *     axis).
 *   - Empty `effectId` in a `Shader` rule = engaged-blocking
 *     sentinel: "no animation for this app on this event," winning
 *     over the per-event default.
 *
 * ## Cascade
 *
 * Resolution at window-event time is:
 *
 *   1. `AnimationAppRuleList::resolve{Shader,Timing}(windowClass,
 *      eventPath)` — first matching rule of that axis. If hit, done.
 *   2. `Settings::shaderProfileTree.resolve(eventPath)` — the user's
 *      per-event override, if engaged.
 *   3. The library default (empty `ShaderProfile`).
 *
 * The rule layer wins over the per-event override. If a user has
 * configured both, they intended the per-app rule to take precedence
 * on matching windows.
 */
struct PHOSPHORANIMATION_EXPORT AnimationAppRule
{
    enum class Kind {
        Shader, ///< Override the shader effect (`effectId` + `shaderParams`)
        Timing, ///< Override the motion curve (`curve` + `durationMs`)
    };

    /// Substring matched case-insensitively against `EffectWindow::windowClass()`
    /// (or the equivalent in the daemon path). Empty pattern matches nothing
    /// — the resolver short-circuits, so an accidentally-empty pattern can't
    /// accidentally swallow every window.
    QString classPattern;

    /// Animation event path from `PhosphorAnimation::ProfilePaths::Window*`
    /// (e.g. `"window.open"`, `"window.close"`). Exact equality match — rule
    /// only applies to the named event.
    QString eventPath;

    Kind kind = Kind::Shader;

    // ─── Shader-kind payload (kind == Shader only) ────────────────
    /// Shader effect id from the registry. Empty string means "engaged-
    /// blocking sentinel" — the rule disables the per-event default
    /// shader for matching windows without falling through to it.
    QString effectId;
    /// Per-effect parameter map (same shape as the per-event tree). Stored
    /// as `QVariantMap` so the JSON round-trip is straightforward; consumers
    /// translate via `AnimationShaderRegistry::translateAnimationParams` at
    /// resolution time.
    QVariantMap shaderParams;

    // ─── Timing-kind payload (kind == Timing only) ────────────────
    /// Easing curve string in `Profile`'s wire format
    /// (`"x1,y1,x2,y2"`, `"elastic-out:amp,per"`, `"spring:omega,zeta"`,
    /// etc.). Empty string means "use per-event default."
    QString curve;
    /// Duration override in milliseconds. Zero (or negative) means
    /// "use per-event default."
    int durationMs = 0;

    bool operator==(const AnimationAppRule& other) const noexcept;
    bool operator!=(const AnimationAppRule& other) const noexcept
    {
        return !(*this == other);
    }

    QJsonObject toJson() const;

    /// Strict rule-level loader. Returns @c nullopt when the JSON is
    /// missing required fields, names an unknown `kind`, or carries
    /// an empty `classPattern` / `eventPath` — none of which can
    /// produce a valid rule. Direct callers (tests, ad-hoc round-trips,
    /// future scripting hooks) get the same drop-on-malformed contract
    /// the list-level loader already applies, so a typo in `kind`
    /// can't silently materialise as an engaged-empty-effectId Shader
    /// rule that would block animations for matching windows.
    static std::optional<AnimationAppRule> fromJson(const QJsonObject& obj);
};

/**
 * @brief Ordered list of `AnimationAppRule` entries with first-match resolver.
 *
 * The list is the storage shape — it preserves insertion order across
 * round-trips so the user's drag-reorder choice survives. Resolution is
 * O(N) per window event, which is fine for the expected rule count
 * (dozens at most; the lookup happens once per window-lifecycle event,
 * not per frame).
 */
class PHOSPHORANIMATION_EXPORT AnimationAppRuleList
{
public:
    AnimationAppRuleList() = default;

    int size() const noexcept
    {
        return m_rules.size();
    }
    bool isEmpty() const noexcept
    {
        return m_rules.isEmpty();
    }
    AnimationAppRule at(int index) const;
    QList<AnimationAppRule> entries() const
    {
        return m_rules;
    }

    /// Append @p rule. Returns @c true on success, @c false when the
    /// rule fails validation (empty `classPattern` or `eventPath` —
    /// "match every window" rules are silently dangerous, so the gate
    /// is mandatory). Callers writing UI flow can surface the rejection
    /// through the return value rather than re-checking after the call.
    bool append(const AnimationAppRule& rule);
    void removeAt(int index);
    void move(int from, int to);
    void clear() noexcept
    {
        m_rules.clear();
    }
    /// Replace the entire list (used by setters that take a full
    /// already-built list, e.g. drag-reorder commits). Each entry is
    /// validated through the same gate as `append()` — entries with
    /// empty `classPattern` or `eventPath` are dropped rather than
    /// silently swallowing every window, and a warning is logged so
    /// the call-site bug is visible in the journal. Returns the number
    /// of entries actually accepted (so callers can detect drops).
    int setEntries(const QList<AnimationAppRule>& rules);

    /// First matching `Kind::Shader` rule for the given (windowClass,
    /// eventPath). Match is substring case-insensitive on
    /// `classPattern` and exact on `eventPath`. Returns `nullopt` when
    /// no rule of that axis matches — the caller falls through to the
    /// per-event tree.
    std::optional<AnimationAppRule> resolveShader(const QString& windowClass, const QString& eventPath) const;

    /// First matching `Kind::Timing` rule for the given (windowClass,
    /// eventPath). Same matching rules as `resolveShader`.
    std::optional<AnimationAppRule> resolveTiming(const QString& windowClass, const QString& eventPath) const;

    QJsonArray toJson() const;
    static AnimationAppRuleList fromJson(const QJsonArray& arr);

    bool operator==(const AnimationAppRuleList& other) const noexcept
    {
        return m_rules == other.m_rules;
    }
    bool operator!=(const AnimationAppRuleList& other) const noexcept
    {
        return !(*this == other);
    }

private:
    /// Shared body for `resolveShader` / `resolveTiming` — they only
    /// differ on the kind filter.
    std::optional<AnimationAppRule> firstMatchOfKind(AnimationAppRule::Kind kind, const QString& windowClass,
                                                     const QString& eventPath) const;

    QList<AnimationAppRule> m_rules;
};

} // namespace PhosphorAnimationShaders

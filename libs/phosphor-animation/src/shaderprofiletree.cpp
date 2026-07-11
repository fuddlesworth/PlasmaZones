// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/ShaderProfileTree.h>

#include <PhosphorAnimation/ProfilePaths.h>

#include <QJsonArray>
#include <QJsonValue>

namespace PhosphorAnimationShaders {

// ═══════════════════════════════════════════════════════════════════════════════
// Lookup
// ═══════════════════════════════════════════════════════════════════════════════

ShaderProfile ShaderProfileTree::resolve(const QString& path) const
{
    // The interactive-drag leaf takes NO inherited shader. Every pack a user
    // can assign on an ancestor ("window.movement", "window", the baseline)
    // is a single-surface crossfade — the pickers refuse move-class packs
    // everywhere but this leaf — and a crossfade cannot drive the held drag
    // transition (no from/to plays while the pointer is down). Inheriting one
    // here would install a dead transition that pins full-output repaints for
    // the whole drag, and would show a "current shader" in settings that
    // never visibly runs. Only a direct override at the leaf applies; timing
    // inheritance is unaffected (that lives in the motion ProfileTree).
    // Membership is defined by shaderPathResolvesInIsolation (below) so UI
    // helpers that reason about shadowing share the resolver's definition.
    if (shaderPathResolvesInIsolation(path)) {
        ShaderProfile effective;
        auto it = m_overrides.constFind(path);
        if (it != m_overrides.constEnd())
            ShaderProfile::overlay(effective, it.value());
        return effective.withDefaults();
    }

    QStringList chain;
    QString cursor = path;
    while (!cursor.isEmpty()) {
        chain.prepend(cursor);
        cursor = PhosphorAnimation::ProfilePaths::parentPath(cursor);
    }

    ShaderProfile effective = m_baseline;

    for (const QString& step : chain) {
        auto it = m_overrides.constFind(step);
        if (it == m_overrides.constEnd())
            continue;
        ShaderProfile::overlay(effective, it.value());
    }

    return effective.withDefaults();
}

ShaderProfile ShaderProfileTree::directOverride(const QString& path) const
{
    return m_overrides.value(path);
}

bool ShaderProfileTree::hasOverride(const QString& path) const
{
    return m_overrides.contains(path);
}

QStringList ShaderProfileTree::overriddenPaths() const
{
    return m_insertionOrder;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Mutation
// ═══════════════════════════════════════════════════════════════════════════════

void ShaderProfileTree::setOverride(const QString& path, const ShaderProfile& profile)
{
    if (path.isEmpty())
        return;
    if (!m_overrides.contains(path))
        m_insertionOrder.append(path);
    m_overrides.insert(path, profile);
}

bool ShaderProfileTree::clearOverride(const QString& path)
{
    if (!m_overrides.remove(path))
        return false;
    m_insertionOrder.removeAll(path);
    return true;
}

void ShaderProfileTree::clearAllOverrides()
{
    m_overrides.clear();
    m_insertionOrder.clear();
}

void ShaderProfileTree::setBaseline(const ShaderProfile& profile)
{
    m_baseline = profile;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Serialization
// ═══════════════════════════════════════════════════════════════════════════════

QJsonObject ShaderProfileTree::toJson() const
{
    QJsonObject root;
    root.insert(QLatin1String("baseline"), m_baseline.toJson());

    QJsonArray overrides;
    for (const QString& path : m_insertionOrder) {
        auto it = m_overrides.constFind(path);
        if (it == m_overrides.constEnd())
            continue;
        QJsonObject entry;
        entry.insert(QLatin1String("path"), path);
        entry.insert(QLatin1String("profile"), it.value().toJson());
        overrides.append(entry);
    }
    root.insert(QLatin1String("overrides"), overrides);

    return root;
}

ShaderProfileTree ShaderProfileTree::fromJson(const QJsonObject& obj)
{
    ShaderProfileTree tree;

    if (obj.contains(QLatin1String("baseline")))
        tree.m_baseline = ShaderProfile::fromJson(obj.value(QLatin1String("baseline")).toObject());

    const QJsonArray arr = obj.value(QLatin1String("overrides")).toArray();
    for (const QJsonValue& v : arr) {
        const QJsonObject entry = v.toObject();
        const QString path = entry.value(QLatin1String("path")).toString();
        if (path.isEmpty())
            continue;
        tree.setOverride(path, ShaderProfile::fromJson(entry.value(QLatin1String("profile")).toObject()));
    }

    return tree;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Equality
// ═══════════════════════════════════════════════════════════════════════════════

bool ShaderProfileTree::operator==(const ShaderProfileTree& other) const
{
    if (m_baseline != other.m_baseline)
        return false;
    if (m_insertionOrder != other.m_insertionOrder)
        return false;
    if (m_overrides.size() != other.m_overrides.size())
        return false;
    for (auto it = m_overrides.constBegin(); it != m_overrides.constEnd(); ++it) {
        auto otherIt = other.m_overrides.constFind(it.key());
        if (otherIt == other.m_overrides.constEnd())
            return false;
        if (it.value() != otherIt.value())
            return false;
    }
    return true;
}

bool shaderPathResolvesInIsolation(const QString& path)
{
    // Exactly the interactive-drag leaf today — see the resolve() note above.
    // Any future leaf that opts out of the walk-up overlay joins this
    // predicate so resolve() and every shadowing-aware consumer move in
    // lockstep.
    return path == PhosphorAnimation::ProfilePaths::WindowMove;
}

ShaderProfile resolveShaderWithDefault(const ShaderProfileTree& tree, const QString& path)
{
    ShaderProfile resolved = tree.resolve(path);
    // A real shader resolved — a direct override or an inherited NON-EMPTY
    // ancestor (e.g. "window" → slide). Inheritance of a chosen shader wins.
    if (!resolved.effectiveEffectId().isEmpty()) {
        return resolved;
    }
    // Empty effectId. Apply the built-in per-event default UNLESS the user
    // CHOSE a shader for THIS exact event — including an explicit engaged-empty
    // "None". The gate is the leaf's own `effectId` engagement, not merely
    // `hasOverride(path)`: a params-only override (parameters set, effectId
    // unset) is "no shader chosen", so the default still applies and the user's
    // params overlay onto it.
    //
    // Deliberately checks only the leaf, not ancestors: an ancestor "None"
    // (e.g. a category-level "window" → "") means "no shader chosen for the
    // category", which must NOT suppress a built-in default for a specific
    // event like a snap/move. (An ancestor that chose a real shader was
    // already returned above; only an ancestor "None" reaches here, and it
    // should not win over the event default.) A per-event "None" still wins.
    if (tree.directOverride(path).effectId.has_value()) {
        return resolved;
    }
    const QString def = PhosphorAnimation::ProfilePaths::defaultShaderEffectIdForPath(path);
    if (!def.isEmpty()) {
        resolved.effectId = def;
    }
    return resolved;
}

} // namespace PhosphorAnimationShaders

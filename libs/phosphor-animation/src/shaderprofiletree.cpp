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
    // Some leaves take NO inherited shader: everything either could inherit
    // from its ancestors is provably wrong for it, so only a direct override
    // at the leaf applies (timing inheritance is unaffected — that lives in
    // the motion ProfileTree). Membership is defined by
    // shaderPathResolvesInIsolation below, which carries each member's own
    // rationale; UI helpers that reason about shadowing share that predicate
    // so the definitions cannot drift.
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

    // A subtree that inherits from its own root and from nothing above it —
    // see shaderPathIsolationRoot. Two edits to the ordinary walk, and BOTH
    // are needed: the baseline is dropped, and the chain is cut back to the
    // root. Dropping the baseline alone would leave the `global` node in the
    // chain, which is a node the user can assign a pack to, so the subtree
    // would still inherit from outside itself through it.
    const QString isolationRoot = shaderPathIsolationRoot(path);
    ShaderProfile effective = isolationRoot.isEmpty() ? m_baseline : ShaderProfile{};
    if (!isolationRoot.isEmpty()) {
        while (!chain.isEmpty() && chain.constFirst() != isolationRoot) {
            chain.removeFirst();
        }
        // The trim assumes the root is an ANCESTOR of the path, which holds for
        // every predicate whose membership test is "the root, or the root plus a
        // dot" — the only shape there is today. A root that ever answers for a path outside
        // its own chain would empty the list here and silently apply nothing, not
        // even the direct override at the path. Fail closed to the path itself
        // rather than to no shader at all, so the leaf still resolves what the
        // user explicitly put on it.
        if (chain.isEmpty()) {
            chain.append(path);
        }
    }

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
    // The two members, each with its own reason. Any future leaf that opts
    // out of the walk-up overlay joins this predicate so resolve() and every
    // shadowing-aware consumer move in lockstep.
    //
    // The DRAG leaf: every pack a user can assign on an ancestor
    // ("window.movement", "window", the baseline) is a single-surface
    // crossfade — the pickers refuse move-class packs everywhere but this
    // leaf — and a crossfade cannot drive the held drag transition (no
    // from/to plays while the pointer is down). Inheriting one would install
    // a dead transition that pins full-output repaints for the whole drag.
    //
    // The tab leaf is here for the drag leaf's reason in a different shape: its
    // class is OPT-IN rather than universal-permissive (a pack must declare
    // `appliesTo: ["tab"]` to be offered), so every pack a picker offers on an
    // ancestor for that ancestor's OWN sake is refused here. All three levels are
    // above it — parentPath("scrolling") is `global`, not empty, and the baseline
    // sits above that — so "its only ancestor is the strip-classed root" would be
    // the wrong reason as well as the wrong count. The one pack that survives the
    // gate is a HYBRID declaring `tab` beside the ancestor's class
    // (`["tab","appearance"]` on `global`, `["tab","strip"]` on `scrolling`), so
    // this predicate is a policy choice for that case and a structural refusal for
    // every other: a hybrid engaged for window appearance must not silently start
    // driving tab swaps. Inheriting one is worse than inheriting nothing, because
    // shaderEffectAppliesToEventPath refuses it when the transition BEGINS (not at
    // install) and the leaf then animates nothing. Only a direct override at the
    // leaf applies; timing inheritance is unaffected (that lives in the motion
    // ProfileTree, where the scrolling root's curve and duration ARE meaningful
    // for both children).
    return path == PhosphorAnimation::ProfilePaths::WindowMove
        || path == PhosphorAnimation::ProfilePaths::ScrollingTabSwitch;
}

QString shaderPathIsolationRoot(const QString& path)
{
    namespace PP = PhosphorAnimation::ProfilePaths;
    // The shell family. Its surfaces belong to plasmashell, so every ancestor
    // above the root describes something else entirely: `global` and the
    // baseline are where a user says what THEIR WINDOWS do, and inheriting
    // that would start playing a window pack on the system tray the moment
    // anyone set one, with no way to say no short of overriding every shell
    // leg with an explicit None. Cutting the chain at the root is the same
    // answer the decoration tree gives the same family, and it is what makes
    // "engage a pack on the Shell page" the entire opt-in.
    //
    // Inside the subtree inheritance is ordinary: a pack on `shell` cascades
    // to every leg, and a leg overrides it.
    static const QString shellPrefix = PP::Shell + QLatin1Char('.');
    if (path == PP::Shell || path.startsWith(shellPrefix)) {
        return PP::Shell;
    }
    return QString();
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

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/ProfileTree.h>

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/ProfilePaths.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

namespace PhosphorAnimation {

// ═══════════════════════════════════════════════════════════════════════════════
// Lookup
// ═══════════════════════════════════════════════════════════════════════════════

Profile ProfileTree::overlayChain(const QString& path, Profile seed) const
{
    // Build the path chain from closest-to-root down to the leaf and overlay
    // each matching override onto @p seed in that order — later (deeper)
    // overlays win. Every overlay copies only the fields explicitly set in the
    // source, so a child with only `duration` engaged inherits curve / stagger
    // / mode from its parent, and a child with `duration = DefaultDuration`
    // still wins over a parent's different duration (the point of optionals).
    // No library-default fill: fields no override in the chain engages keep
    // their @p seed value, and a chain with no override returns @p seed
    // unchanged. resolve() and overlayChainOnto() differ only in the seed
    // (m_baseline vs a caller-owned base) and whether they apply withDefaults().
    QStringList chain;
    QString cursor = path;
    while (!cursor.isEmpty()) {
        chain.prepend(cursor);
        cursor = ProfilePaths::parentPath(cursor);
    }

    for (const QString& step : chain) {
        auto it = m_overrides.constFind(step);
        if (it == m_overrides.constEnd()) {
            continue;
        }
        overlay(seed, it.value());
    }
    return seed;
}

Profile ProfileTree::resolve(const QString& path) const
{
    // Seed from the baseline (logical "global" level), overlay the chain, then
    // fill any still-unset field with the library default so consumers get a
    // fully-populated Profile and never have to check the optionals — including
    // `curve`, which withDefaults() backfills with a default Easing.
    return overlayChain(path, m_baseline).withDefaults();
}

Profile ProfileTree::overlayChainOnto(const QString& path, Profile base) const
{
    // Same chain overlay as resolve(), but seeded with the caller's @p base
    // instead of m_baseline and with NO withDefaults() fill — so a consumer
    // holding the authoritative baseline elsewhere can gate the whole overlay
    // on hasOverride/overriddenPaths and keep its fast path (a no-override
    // chain returns @p base unchanged).
    return overlayChain(path, std::move(base));
}

Profile ProfileTree::directOverride(const QString& path) const
{
    return m_overrides.value(path);
}

bool ProfileTree::hasOverride(const QString& path) const
{
    return m_overrides.contains(path);
}

QStringList ProfileTree::overriddenPaths() const
{
    return m_insertionOrder;
}

bool ProfileTree::hasAnyOverride() const
{
    return !m_insertionOrder.isEmpty();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Mutation
// ═══════════════════════════════════════════════════════════════════════════════

void ProfileTree::setOverride(const QString& path, const Profile& profile)
{
    if (path.isEmpty()) {
        return;
    }
    if (!m_overrides.contains(path)) {
        m_insertionOrder.append(path);
    }
    m_overrides.insert(path, profile);
}

bool ProfileTree::clearOverride(const QString& path)
{
    if (!m_overrides.remove(path)) {
        return false;
    }
    m_insertionOrder.removeAll(path);
    return true;
}

void ProfileTree::clearAllOverrides()
{
    m_overrides.clear();
    m_insertionOrder.clear();
}

void ProfileTree::setBaseline(const Profile& profile)
{
    m_baseline = profile;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Overlay helper
// ═══════════════════════════════════════════════════════════════════════════════

void ProfileTree::overlay(Profile& dst, const Profile& src)
{
    // Every engaged optional in src replaces dst. Unset (nullopt) fields
    // in src leave dst alone — that's the inheritance mechanism.
    if (src.curve) {
        dst.curve = src.curve;
    }
    if (src.duration) {
        dst.duration = src.duration;
    }
    if (src.minDistance) {
        dst.minDistance = src.minDistance;
    }
    if (src.sequenceMode) {
        dst.sequenceMode = src.sequenceMode;
    }
    if (src.staggerInterval) {
        dst.staggerInterval = src.staggerInterval;
    }
    if (src.presetName) {
        dst.presetName = src.presetName;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Serialization
// ═══════════════════════════════════════════════════════════════════════════════

QJsonObject ProfileTree::toJson() const
{
    QJsonObject root;
    root.insert(QLatin1String("baseline"), m_baseline.toJson());

    // Array shape preserves user-visible ordering — QJsonObject keys are
    // alphabetically sorted on serialization.
    QJsonArray overrides;
    for (const QString& path : m_insertionOrder) {
        auto it = m_overrides.constFind(path);
        if (it == m_overrides.constEnd()) {
            continue;
        }
        QJsonObject entry;
        entry.insert(QLatin1String("path"), path);
        entry.insert(QLatin1String("profile"), it.value().toJson());
        overrides.append(entry);
    }
    root.insert(QLatin1String("overrides"), overrides);

    return root;
}

ProfileTree ProfileTree::fromJson(const QJsonObject& obj, const CurveRegistry& registry)
{
    ProfileTree tree;

    if (obj.contains(QLatin1String("baseline"))) {
        tree.m_baseline = Profile::fromJson(obj.value(QLatin1String("baseline")).toObject(), registry);
    }

    const QJsonArray arr = obj.value(QLatin1String("overrides")).toArray();
    for (const QJsonValue& v : arr) {
        const QJsonObject entry = v.toObject();
        const QString path = entry.value(QLatin1String("path")).toString();
        if (path.isEmpty()) {
            continue;
        }
        tree.setOverride(path, Profile::fromJson(entry.value(QLatin1String("profile")).toObject(), registry));
    }

    return tree;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Equality
// ═══════════════════════════════════════════════════════════════════════════════

bool ProfileTree::operator==(const ProfileTree& other) const
{
    if (m_baseline != other.m_baseline) {
        return false;
    }
    if (m_insertionOrder != other.m_insertionOrder) {
        return false;
    }
    if (m_overrides.size() != other.m_overrides.size()) {
        return false;
    }
    for (auto it = m_overrides.constBegin(); it != m_overrides.constEnd(); ++it) {
        auto otherIt = other.m_overrides.constFind(it.key());
        if (otherIt == other.m_overrides.constEnd()) {
            return false;
        }
        if (it.value() != otherIt.value()) {
            return false;
        }
    }
    return true;
}

} // namespace PhosphorAnimation

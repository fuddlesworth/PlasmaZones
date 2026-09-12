// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/ProfileTree.h>

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/ProfilePaths.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QVarLengthArray>

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
    // (the baseline vs a caller-owned base) and whether they apply withDefaults().
    // Built leaf-first then walked in reverse, rather than prepending into a
    // QStringList. Prepend shifts every element already there, and this is on
    // the snap path, which calls it once per retiled item. Depth is 2-3, so the
    // inline capacity covers the usual case with no allocation at all.
    QVarLengthArray<QString, 4> chain;
    QString cursor = path;
    while (!cursor.isEmpty()) {
        chain.append(cursor);
        cursor = ProfilePaths::parentPath(cursor);
    }

    for (auto it = chain.crbegin(); it != chain.crend(); ++it) {
        // findOverride, not hasOverride + directOverride: one hash lookup per step
        // and no Profile copy. This runs once per retiled item on the snap path.
        if (const Profile* own = m_store.findOverride(*it)) {
            overlay(seed, *own);
        }
    }
    return seed;
}

Profile ProfileTree::resolve(const QString& path) const
{
    // Seed from the baseline (logical "global" level), overlay the chain, then
    // fill any still-unset field with the library default so consumers get a
    // fully-populated Profile and never have to check the optionals — including
    // `curve`, which withDefaults() backfills with a default Easing.
    return overlayChain(path, m_store.baseline()).withDefaults();
}

Profile ProfileTree::overlayChainOnto(const QString& path, Profile base) const
{
    // Same chain overlay as resolve(), but seeded with the caller's @p base
    // instead of the baseline and with NO withDefaults() fill — so a consumer
    // holding the authoritative baseline elsewhere can gate the whole overlay
    // on hasOverride/overriddenPaths and keep its fast path (a no-override
    // chain returns @p base unchanged).
    return overlayChain(path, std::move(base));
}

Profile ProfileTree::directOverride(const QString& path) const
{
    return m_store.directOverride(path);
}

bool ProfileTree::hasOverride(const QString& path) const
{
    return m_store.hasOverride(path);
}

QStringList ProfileTree::overriddenPaths() const
{
    return m_store.keys();
}

bool ProfileTree::hasAnyOverride() const
{
    return !m_store.hasNoOverrides();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Mutation
// ═══════════════════════════════════════════════════════════════════════════════

void ProfileTree::setOverride(const QString& path, const Profile& profile)
{
    // The empty-path refusal lives in the container now, where all four trees
    // share one copy of it rather than each writing out the same guard.
    m_store.setOverride(path, profile);
}

bool ProfileTree::clearOverride(const QString& path)
{
    return m_store.clearOverride(path);
}

void ProfileTree::clearAllOverrides()
{
    m_store.clearAllOverrides();
}

void ProfileTree::setBaseline(const Profile& profile)
{
    m_store.setBaseline(profile);
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
    // Omitted when it carries nothing. fromJson treats an absent baseline the
    // same as an empty one, and always emitting it wrote a dead `"baseline":{}`
    // into every stored tree, which is then copied on every read and joins
    // every settings-profile delta. Config's own canonicalisation strips it
    // afterwards, but only when `overrides` is empty too, so the usual tree
    // kept it.
    const QJsonObject baseline = m_store.baseline().toJson();
    if (!baseline.isEmpty()) {
        root.insert(QLatin1String("baseline"), baseline);
    }

    // Array shape preserves user-visible ordering — QJsonObject keys are
    // alphabetically sorted on serialization.
    QJsonArray overrides;
    m_store.forEachInOrder([&overrides](const QString& path, const Profile& profile) {
        QJsonObject entry;
        entry.insert(QLatin1String("path"), path);
        entry.insert(QLatin1String("profile"), profile.toJson());
        overrides.append(entry);
    });
    root.insert(QLatin1String("overrides"), overrides);

    return root;
}

ProfileTree ProfileTree::fromJson(const QJsonObject& obj, const CurveRegistry& registry)
{
    ProfileTree tree;

    if (obj.contains(QLatin1String("baseline"))) {
        tree.m_store.setBaseline(Profile::fromJson(obj.value(QLatin1String("baseline")).toObject(), registry));
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
    // Order-SENSITIVE, like the animation and decoration shader trees and unlike
    // the overlay one: this tree serialises its overrides as a JSON ARRAY, so the
    // insertion order is user-visible and round-trips, and two trees that differ
    // only in it are genuinely different documents.
    return m_store.sameBaseline(other.m_store) && m_store.sameKeyOrder(other.m_store)
        && m_store.sameOverrides(other.m_store);
}

} // namespace PhosphorAnimation

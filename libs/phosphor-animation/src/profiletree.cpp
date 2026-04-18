// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/ProfileTree.h>

#include <PhosphorAnimation/ProfilePaths.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

namespace PhosphorAnimation {

// ═══════════════════════════════════════════════════════════════════════════════
// Lookup
// ═══════════════════════════════════════════════════════════════════════════════

Profile ProfileTree::resolve(const QString& path) const
{
    // Start from defaults, fill from the deepest explicit override upward.
    // This order makes "child wins for fields it specifies; parent fills
    // gaps" natural: we walk parent-chain top-down into an empty Profile,
    // then overlay the more-specific overrides last.
    //
    // Build the chain first (root → leaf) so we can apply in order.
    QStringList chain;
    QString cursor = path;
    while (!cursor.isEmpty()) {
        chain.prepend(cursor);
        cursor = ProfilePaths::parentPath(cursor);
    }

    Profile effective; // library defaults

    // Baseline (global) is implicit — always the first thing overlaid.
    // It lives outside m_overrides so callers can edit it without having
    // to explicitly insert a "global" key into the override map.
    mergeFromParent(effective, m_baseline);
    // Note: because m_baseline is a user-visible non-default, we treat it
    // as always-present — its fields overwrite library defaults below.
    effective = m_baseline;

    for (const QString& step : chain) {
        auto it = m_overrides.constFind(step);
        if (it == m_overrides.constEnd()) {
            continue;
        }
        // Overlay this step's override onto effective: each non-default
        // field in the override wins; defaults inherit from effective.
        const Profile& ov = it.value();
        if (ov.curve) {
            effective.curve = ov.curve;
        }
        if (!qFuzzyCompare(1.0 + ov.duration, 1.0 + 150.0) || !effective.curve) {
            // Sentinel: only overwrite duration if override explicitly set
            // something other than the library default. This keeps "I only
            // changed the curve" overrides from stamping duration back to
            // 150 when the parent had 300.
            if (!qFuzzyCompare(1.0 + ov.duration, 1.0 + 150.0)) {
                effective.duration = ov.duration;
            }
        }
        if (ov.minDistance != 0) {
            effective.minDistance = ov.minDistance;
        }
        if (ov.sequenceMode != 0) {
            effective.sequenceMode = ov.sequenceMode;
        }
        if (ov.staggerInterval != 30) {
            effective.staggerInterval = ov.staggerInterval;
        }
        if (!ov.presetName.isEmpty()) {
            effective.presetName = ov.presetName;
        }
    }

    return effective;
}

Profile ProfileTree::override_(const QString& path) const
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
// Merge helper
// ═══════════════════════════════════════════════════════════════════════════════

void ProfileTree::mergeFromParent(Profile& dst, const Profile& src)
{
    // Fill fields in @p dst that still hold the library default from
    // values in @p src. Child wins; parent fills gaps.
    if (!dst.curve && src.curve) {
        dst.curve = src.curve;
    }
    if (qFuzzyCompare(1.0 + dst.duration, 1.0 + 150.0)) {
        dst.duration = src.duration;
    }
    if (dst.minDistance == 0) {
        dst.minDistance = src.minDistance;
    }
    if (dst.sequenceMode == 0) {
        dst.sequenceMode = src.sequenceMode;
    }
    if (dst.staggerInterval == 30) {
        dst.staggerInterval = src.staggerInterval;
    }
    if (dst.presetName.isEmpty()) {
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

    // QJsonObject keys are alphabetically sorted on serialization — using
    // an object for `overrides` would silently reshuffle user-visible
    // ordering. Emit as an array of {path, profile} pairs so iteration
    // order round-trips losslessly. This matches the order the settings
    // UI shows when users add/remove overrides.
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

ProfileTree ProfileTree::fromJson(const QJsonObject& obj)
{
    ProfileTree tree;

    if (obj.contains(QLatin1String("baseline"))) {
        tree.m_baseline = Profile::fromJson(obj.value(QLatin1String("baseline")).toObject());
    }

    // Accept either the current array shape (order-preserving) or the
    // legacy object shape (alphabetically sorted) for read compatibility
    // with any pre-array configs. New writes always produce the array.
    const QJsonValue overridesValue = obj.value(QLatin1String("overrides"));
    if (overridesValue.isArray()) {
        const QJsonArray arr = overridesValue.toArray();
        for (const QJsonValue& v : arr) {
            const QJsonObject entry = v.toObject();
            const QString path = entry.value(QLatin1String("path")).toString();
            if (path.isEmpty()) {
                continue;
            }
            tree.setOverride(path, Profile::fromJson(entry.value(QLatin1String("profile")).toObject()));
        }
    } else if (overridesValue.isObject()) {
        const QJsonObject legacy = overridesValue.toObject();
        for (auto it = legacy.constBegin(); it != legacy.constEnd(); ++it) {
            const QString path = it.key();
            if (path.isEmpty()) {
                continue;
            }
            tree.setOverride(path, Profile::fromJson(it.value().toObject()));
        }
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

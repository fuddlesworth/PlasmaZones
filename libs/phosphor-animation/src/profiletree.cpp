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
    // Build the path chain from closest-to-root down to the leaf, so we
    // can overlay in that order — later overlays win.
    QStringList chain;
    QString cursor = path;
    while (!cursor.isEmpty()) {
        chain.prepend(cursor);
        cursor = ProfilePaths::parentPath(cursor);
    }

    // Start from the baseline (logical "global" level) and overlay each
    // matching override in chain order. Every overlay copies only the
    // fields that are explicitly set in the source, so a child with only
    // `duration` engaged correctly inherits curve / stagger / mode from
    // its parent — and a child with `duration = DefaultDuration` still
    // wins over a parent's different duration (the point of optionals).
    Profile effective = m_baseline;

    for (const QString& step : chain) {
        auto it = m_overrides.constFind(step);
        if (it == m_overrides.constEnd()) {
            continue;
        }
        overlay(effective, it.value());
    }

    // Final pass: fill any still-unset field with the library default
    // so consumers get a fully-populated Profile and never have to check
    // the optionals themselves. `curve` stays null if no chain member
    // supplied one; runtime callers that need a concrete curve should
    // substitute a default Easing at that point.
    return effective.withDefaults();
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

ProfileTree ProfileTree::fromJson(const QJsonObject& obj)
{
    ProfileTree tree;

    if (obj.contains(QLatin1String("baseline"))) {
        tree.m_baseline = Profile::fromJson(obj.value(QLatin1String("baseline")).toObject());
    }

    const QJsonArray arr = obj.value(QLatin1String("overrides")).toArray();
    for (const QJsonValue& v : arr) {
        const QJsonObject entry = v.toObject();
        const QString path = entry.value(QLatin1String("path")).toString();
        if (path.isEmpty()) {
            continue;
        }
        tree.setOverride(path, Profile::fromJson(entry.value(QLatin1String("profile")).toObject()));
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

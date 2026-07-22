// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Dot-path JSON helpers shared across the per-version migration steps. These
// walk a nested QJsonObject by a dot-separated group path, preserving sibling
// sub-groups at every ancestor. Inline so each migration TU
// (configmigration_v2/v4/v5.cpp) gets one definition without an ODR clash.

#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace PlasmaZones {

// Helper: move a key from one JSON object to another, renaming it.
inline void moveKey(const QJsonObject& src, const QString& srcKey, QJsonObject& dst, const QString& dstKey)
{
    if (src.contains(srcKey)) {
        dst[dstKey] = src.value(srcKey);
    }
}

/// Resolve a dot-path config group accessor (e.g. "Snapping.Behavior.WindowHandling")
/// against a nested JSON root and return the leaf-group object. Walking the
/// accessor's own segments keeps the migration in lockstep with the schema
/// instead of duplicating segment names as bare literals — the v1*-migration
/// literal exemption does NOT cover live v4 config keys.
inline QJsonObject groupObjectAtPath(const QJsonObject& root, const QString& dotPath)
{
    QJsonObject obj = root;
    for (const QString& segment : dotPath.split(QLatin1Char('.'), Qt::SkipEmptyParts)) {
        obj = obj.value(segment).toObject();
    }
    return obj;
}

/// Set the nested object at @p segments within @p root to @p value, reading
/// out, mutating, and writing back each level so sibling sub-groups at any
/// ancestor are preserved and intermediate objects are created on demand.
inline void setGroupAtSegments(QJsonObject& root, const QStringList& segments, const QJsonObject& value)
{
    // Materialise the chain of ancestor objects top-down so each can be
    // rewritten bottom-up with its mutated child (QJsonObject is a value type;
    // value() returns copies, so we must reassign back up the chain).
    QList<QJsonObject> chain;
    chain.reserve(segments.size());
    QJsonObject node = root;
    for (int i = 0; i < segments.size() - 1; ++i) {
        chain.append(node);
        node = node.value(segments.at(i)).toObject();
    }
    chain.append(node);

    // Bottom-up rebuild: place the value at the leaf, then fold each level
    // back into its parent.
    chain.last()[segments.last()] = value;
    for (int i = segments.size() - 2; i >= 0; --i) {
        chain[i][segments.at(i)] = chain.at(i + 1);
    }
    root = chain.first();
}

/// Remove the nested object at @p segments within @p root, then prune any
/// ancestor object that became empty as a result — but never an ancestor that
/// still holds other sub-groups (e.g. don't drop "Snapping" while
/// Behavior/Effects remain).
inline void removeGroupAtSegments(QJsonObject& root, const QStringList& segments)
{
    QList<QJsonObject> chain;
    chain.reserve(segments.size());
    QJsonObject node = root;
    for (int i = 0; i < segments.size() - 1; ++i) {
        chain.append(node);
        node = node.value(segments.at(i)).toObject();
    }
    chain.append(node);

    chain.last().remove(segments.last());
    for (int i = segments.size() - 2; i >= 0; --i) {
        if (chain.at(i + 1).isEmpty()) {
            chain[i].remove(segments.at(i));
        } else {
            chain[i][segments.at(i)] = chain.at(i + 1);
        }
    }
    root = chain.first();
}

/// Move the nested object at @p fromDotPath to @p toDotPath within @p root,
/// creating destination ancestors and pruning now-empty source ancestors.
/// No-op when the source object is absent/empty. Used by migrateV3ToV4 to
/// rename Snapping.Appearance.* zone-overlay groups to Snapping.Zones.*.
/// If the destination already exists it is overwritten wholesale (source
/// wins) — this cannot arise for a genuine v3 config since the destination
/// namespace did not exist before v4.
inline void moveGroupAtPath(QJsonObject& root, const QString& fromDotPath, const QString& toDotPath)
{
    const QStringList fromSegments = fromDotPath.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    const QStringList toSegments = toDotPath.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (fromSegments.isEmpty() || toSegments.isEmpty()) {
        return;
    }

    // Read the source leaf via segment navigation. An absent or empty object
    // contributes nothing and (per the no-op contract) must not create a
    // husk at the destination — bail before touching anything.
    const QJsonObject leaf = groupObjectAtPath(root, fromDotPath);
    if (leaf.isEmpty()) {
        return;
    }

    setGroupAtSegments(root, toSegments, leaf);
    removeGroupAtSegments(root, fromSegments);
}

} // namespace PlasmaZones

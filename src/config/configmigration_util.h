// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Helpers shared across the per-version migration steps. The dot-path JSON
// helpers walk a nested QJsonObject by a dot-separated group path, preserving
// sibling sub-groups at every ancestor; withRuleSet at the end is the one
// rules.json read-modify-write scaffold every finalizer that touches the rule
// store goes through. Inline so each consumer TU (the migration steps
// configmigration_v2/v4/v5/v6.cpp, the v4 and v8 finalizers, plus
// ProfileStore's delta translation) gets one definition without an ODR clash.

#pragma once

#include "configdefaults.h"

#include <PhosphorRules/RuleSet.h>

#include <QFile>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

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
    // Empty-segment guard: the chain logic below assumes at least one
    // segment (chain.last()/segments.last() on an empty list read out of
    // bounds in release builds). ProfileStore feeds this user-editable
    // dot-path keys, so the contract must be defensive, not assert-only.
    if (segments.isEmpty()) {
        return;
    }
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
    // Same empty-segment guard as setGroupAtSegments, for the same reason.
    if (segments.isEmpty()) {
        return;
    }
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

/// Load rules.json, hand the mutable set to @p mutate, and save only when it
/// reports a change.
///
/// Shared by every finalizer that rewrites the rule store: the two v4 sidecar
/// fix-ups to rows that code seeded, and the v8 relocation of overlay-shader
/// rules onto the tree's node shape. All of them need the same scaffold: gate
/// on the conversion having happened, tolerate a missing or unloadable store
/// as "nothing of ours to fix", and never write unless something actually
/// changed. Each caller still loads the store for itself — this is a DRY
/// extraction, not a perf one.
///
/// Not serialised against a RUNNING daemon that already holds the rule set in
/// memory: `ensureJsonConfig` runs once at process startup, so launching the
/// settings app beside a live daemon can rewrite rules.json underneath it. The
/// exposure is the same one `pruneRetiredProviderDefaultRule` has always had,
/// and it converges — a fix-up only fires while the shape it repairs is still
/// on disk, so a clobbering save is repaired again on the next daemon start
/// and stops firing for good once it sticks.
///
/// @param jsonPath config.json; only gates on the conversion having happened.
/// @param what     fix-up name, for the failure warning.
/// @return true on success or a clean no-op; false only on a write failure.
inline bool withRuleSet(const QString& jsonPath, const char* what,
                        const std::function<bool(PhosphorRules::RuleSet&)>& mutate)
{
    if (!QFile::exists(jsonPath)) {
        return true;
    }
    const QString rulesPath = ConfigDefaults::rulesFilePath();
    auto setOpt = PhosphorRules::RuleSet::loadFromFile(rulesPath);
    if (!setOpt.has_value()) {
        // No rules.json yet (conversion not established), or a store this
        // build cannot load. Either way there is nothing of ours to repair,
        // and prevalidateRulesFile owns the unloadable-store case.
        return true;
    }
    PhosphorRules::RuleSet ruleSet = *setOpt;
    if (!mutate(ruleSet)) {
        return true; // nothing to change — no write
    }
    if (!ruleSet.saveToFile(rulesPath)) {
        qWarning("ConfigMigration::%s: failed to write %s", what, qPrintable(rulesPath));
        return false;
    }
    qInfo("ConfigMigration::%s: rewrote %s", what, qPrintable(rulesPath));
    return true;
}

} // namespace PlasmaZones

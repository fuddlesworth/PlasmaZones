// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorSurface/DecorationProfileTree.h>

#include <PhosphorSurface/DecorationSupportedPaths.h>

#include <QJsonArray>
#include <QJsonValue>
#include <QLoggingCategory>

namespace {
Q_LOGGING_CATEGORY(lcDecorationTree, "phosphorsurfaceshaders.decorationtree")
} // namespace

namespace PhosphorSurfaceShaders {

namespace {

// Tree serialization keys, hoisted so toJson / fromJson share one source of
// truth (mirroring DecorationProfile::JsonField*) — a future rename touches
// one place instead of two unguarded literals that must be kept in lockstep.
constexpr auto kJsonFieldBaseline = "baseline";
constexpr auto kJsonFieldOverrides = "overrides";
constexpr auto kJsonFieldPath = "path";
constexpr auto kJsonFieldProfile = "profile";

} // namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Lookup
// ═══════════════════════════════════════════════════════════════════════════════

DecorationProfile DecorationProfileTree::resolve(const QString& surfacePath) const
{
    QStringList chain;
    QString cursor = surfacePath;
    while (!cursor.isEmpty()) {
        chain.prepend(cursor);
        cursor = decorationParentPath(cursor);
    }

    // Baseline-isolated subtrees (shell.*) start from an empty profile: the
    // baseline is the user's look for surfaces WE own, and a foreign shell
    // surface must stay undecorated until a chain is engaged in its own
    // subtree. See decorationPathIsBaselineIsolated.
    DecorationProfile effective = decorationPathIsBaselineIsolated(surfacePath) ? DecorationProfile{} : m_baseline;

    for (const QString& step : chain) {
        auto it = m_overrides.constFind(step);
        if (it == m_overrides.constEnd())
            continue;
        DecorationProfile::overlay(effective, it.value());
    }

    return effective.withDefaults();
}

DecorationProfileTree DecorationProfileTree::withSeedDefaults(const DecorationProfileTree& seeds) const
{
    // True when this tree engages @p member at @p surfacePath or anywhere on
    // its walk-up (baseline included). Chain engagement anywhere on the walk
    // is the MASTER gate (the user built their own look there); parameters and
    // disabledPacks additionally run the same walk for their OWN field, so a
    // seed's leaf map can never shadow an engaged ancestor map — resolve()
    // overlays deepest-last, and an injected leaf field would silently win
    // over the user's category-level engagement.
    const auto fieldEngagedOnWalk = [this](const QString& surfacePath, auto member) {
        // Baseline-isolated paths (shell.*) never resolve against the
        // baseline, so a baseline engagement must not block a seed there —
        // mirror resolve()'s isolation or a global user chain would silently
        // veto every shell seed a decoration set ships.
        if (!decorationPathIsBaselineIsolated(surfacePath) && (m_baseline.*member).has_value())
            return true;
        QString cursor = surfacePath;
        while (!cursor.isEmpty()) {
            const auto it = m_overrides.constFind(cursor);
            if (it != m_overrides.constEnd() && (it.value().*member).has_value())
                return true;
            cursor = decorationParentPath(cursor);
        }
        return false;
    };

    DecorationProfileTree merged = *this;

    // Baseline: the empty path's walk collapses to the baseline's own slots,
    // so the SAME per-field gates apply — chain as the master gate, then each
    // field checks its own engagement. Sharing the lambda keeps the baseline
    // and per-path gate expressions from ever drifting apart.
    if (!fieldEngagedOnWalk(QString(), &DecorationProfile::chain)) {
        DecorationProfile baseline = merged.m_baseline;
        bool changed = false;
        if (seeds.m_baseline.chain) {
            baseline.chain = seeds.m_baseline.chain;
            changed = true;
        }
        if (seeds.m_baseline.parameters && !fieldEngagedOnWalk(QString(), &DecorationProfile::parameters)) {
            baseline.parameters = seeds.m_baseline.parameters;
            changed = true;
        }
        if (seeds.m_baseline.disabledPacks && !fieldEngagedOnWalk(QString(), &DecorationProfile::disabledPacks)) {
            baseline.disabledPacks = seeds.m_baseline.disabledPacks;
            changed = true;
        }
        if (changed)
            merged.m_baseline = baseline;
    }

    for (const QString& path : seeds.m_insertionOrder) {
        // Master gate: an engaged chain at the path or any ancestor blocks the
        // whole seed for this path. The walk reads *this, not `merged`, so an
        // already-injected sibling seed never blocks another seed path.
        if (fieldEngagedOnWalk(path, &DecorationProfile::chain))
            continue;
        const DecorationProfile seed = seeds.m_overrides.value(path);
        DecorationProfile target = merged.m_overrides.value(path);
        bool changed = false;
        // The chain slot at `path` is unengaged (the master gate covers the
        // path itself), so a seed chain always lands here.
        if (seed.chain) {
            target.chain = seed.chain;
            changed = true;
        }
        if (seed.parameters && !fieldEngagedOnWalk(path, &DecorationProfile::parameters)) {
            target.parameters = seed.parameters;
            changed = true;
        }
        if (seed.disabledPacks && !fieldEngagedOnWalk(path, &DecorationProfile::disabledPacks)) {
            target.disabledPacks = seed.disabledPacks;
            changed = true;
        }
        if (changed)
            merged.setOverride(path, target);
    }

    return merged;
}

DecorationProfile DecorationProfileTree::directOverride(const QString& surfacePath) const
{
    return m_overrides.value(surfacePath);
}

bool DecorationProfileTree::hasOverride(const QString& surfacePath) const
{
    return m_overrides.contains(surfacePath);
}

QStringList DecorationProfileTree::overriddenPaths() const
{
    return m_insertionOrder;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Mutation
// ═══════════════════════════════════════════════════════════════════════════════

void DecorationProfileTree::setOverride(const QString& surfacePath, const DecorationProfile& profile)
{
    if (surfacePath.isEmpty())
        return;
    if (!m_overrides.contains(surfacePath))
        m_insertionOrder.append(surfacePath);
    m_overrides.insert(surfacePath, profile);
}

bool DecorationProfileTree::clearOverride(const QString& surfacePath)
{
    if (!m_overrides.remove(surfacePath))
        return false;
    // removeOne, not removeAll: setOverride's contains-check keeps the order
    // list duplicate-free, so at most one entry exists and the scan can stop
    // at the hit.
    m_insertionOrder.removeOne(surfacePath);
    return true;
}

void DecorationProfileTree::clearAllOverrides()
{
    m_overrides.clear();
    m_insertionOrder.clear();
}

void DecorationProfileTree::setBaseline(const DecorationProfile& profile)
{
    m_baseline = profile;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Serialization
// ═══════════════════════════════════════════════════════════════════════════════

QJsonObject DecorationProfileTree::toJson() const
{
    QJsonObject root;
    root.insert(QLatin1String(kJsonFieldBaseline), m_baseline.toJson());

    QJsonArray overrides;
    for (const QString& path : m_insertionOrder) {
        // Unreachable in practice: every mutator writes m_overrides and
        // m_insertionOrder together (setOverride / clearOverride /
        // clearAllOverrides are the complete set), so the two cannot desync.
        // Kept as a cheap guard rather than an assert — a desync here would
        // only drop the orphaned entry from the serialized form.
        auto it = m_overrides.constFind(path);
        if (it == m_overrides.constEnd())
            continue;
        QJsonObject entry;
        entry.insert(QLatin1String(kJsonFieldPath), path);
        entry.insert(QLatin1String(kJsonFieldProfile), it.value().toJson());
        overrides.append(entry);
    }
    root.insert(QLatin1String(kJsonFieldOverrides), overrides);

    return root;
}

DecorationProfileTree DecorationProfileTree::fromJson(const QJsonObject& obj)
{
    DecorationProfileTree tree;

    if (obj.contains(QLatin1String(kJsonFieldBaseline)))
        tree.m_baseline = DecorationProfile::fromJson(obj.value(QLatin1String(kJsonFieldBaseline)).toObject());

    const QJsonArray arr = obj.value(QLatin1String(kJsonFieldOverrides)).toArray();
    for (const QJsonValue& v : arr) {
        const QJsonObject entry = v.toObject();
        const QString path = entry.value(QLatin1String(kJsonFieldPath)).toString();
        if (path.isEmpty())
            continue;
        // Defence-in-depth at the persistence boundary: drop overrides for paths
        // that name no decorable surface (junk / a path retired in a newer
        // version). The drop is DESTRUCTIVE at the next save: toJson writes
        // only the surviving entries, so once any decoration setting is
        // touched the dropped override is gone from the stored config too —
        // consistent with the project's no-ad-hoc-back-compat rule (old
        // values silently drop), but worth a diagnostic for the hand-editing
        // case (the likeliest mistype is the wire token `appletpopup` for the
        // camelCase leaf `shell.appletPopup`). Pack ids are intentionally NOT
        // validated here — the registry
        // loads packs asynchronously and is the catalogue, not the gate, so an
        // override for a not-yet-loaded pack must survive and resolve to a no-op
        // at render time rather than be silently dropped on load. The settings
        // UI (DecorationPageController) remains the primary path validator.
        if (!decorationSurfaceSupported(path)) {
            qCWarning(lcDecorationTree) << "Dropping decoration override for unsupported surface path" << path
                                        << "- it will be removed from the stored configuration on the next save";
            continue;
        }
        // setOverride de-dups: a malformed file with duplicate entries for the
        // same path resolves to last-value-wins (keeping the first-seen position),
        // and the next save() normalises it back to a single entry.
        tree.setOverride(path, DecorationProfile::fromJson(entry.value(QLatin1String(kJsonFieldProfile)).toObject()));
    }

    return tree;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Equality
// ═══════════════════════════════════════════════════════════════════════════════

bool DecorationProfileTree::operator==(const DecorationProfileTree& other) const
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

} // namespace PhosphorSurfaceShaders

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config/settings.h"
#include "config/configdefaults.h"
#include "core/platform/logging.h"
#include "core/types/animationshadersupportedpaths.h"

#include <PhosphorAnimation/ShaderProfileTree.h>
#include <PhosphorSurface/DecorationProfileTree.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace PlasmaZones {

PhosphorAnimationShaders::ShaderProfileTree Settings::shaderProfileTree() const
{
    const QVariantMap map =
        m_store->read<QVariantMap>(ConfigDefaults::animationsGroup(), ConfigDefaults::shaderProfileTreeKey());
    // Prune on read so a config that contains stale overrides on paths
    // the daemon's overlay service doesn't consume (left over from an
    // earlier UI revision that exposed the picker on every event row)
    // can never shadow a user-intended parent override at runtime. The
    // resolver walks deeper-leaf-wins, so an unsupported leaf entry
    // would otherwise silently beat the supported parent entry the user
    // can actually edit. See `src/core/animationshadersupportedpaths.h`
    // for the rationale + the full SSOT.
    return pruneShaderProfileTreeToSupportedPaths(
        PhosphorAnimationShaders::ShaderProfileTree::fromJson(QJsonObject::fromVariantMap(map)));
}

PhosphorAnimationShaders::ShaderProfileTree Settings::committedShaderProfileTree() const
{
    // Baseline snapshot, not the live store — mirrors isKeyModified()'s
    // m_baseline lookup and the shaderProfileTree() prune so the two trees
    // compare prune-for-prune. No empty→ConfigDefaults fallback: the shader
    // tree's schema default IS the empty tree (unlike the decoration tree).
    const QVariantMap map =
        m_baseline.value(ConfigDefaults::animationsGroup()).value(ConfigDefaults::shaderProfileTreeKey()).toMap();
    return pruneShaderProfileTreeToSupportedPaths(
        PhosphorAnimationShaders::ShaderProfileTree::fromJson(QJsonObject::fromVariantMap(map)));
}

void Settings::setShaderProfileTree(const PhosphorAnimationShaders::ShaderProfileTree& tree)
{
    refreshCleanBackendFromDisk();
    // Prune incoming tree at the persistence boundary — same rationale
    // as the read-side prune in shaderProfileTree(). Belt-and-braces:
    // the QML UI gates the picker via supportsShaderLeg(), but a
    // Q_INVOKABLE write coming from elsewhere (future scripting hooks,
    // tests) cannot stamp unsupported-path entries onto disk.
    const auto pruned = pruneShaderProfileTreeToSupportedPaths(tree);

    // Value-equality compare so a same-tree write doesn't fire a spurious
    // changed signal (e.g. discard-changes path that calls
    // setShaderProfileTree(currentTree)). Compare AFTER pruning so the
    // first save against a stale-on-disk config still produces a write
    // that drops the unsupported entries.
    const QVariantMap prevMap =
        m_store->read<QVariantMap>(ConfigDefaults::animationsGroup(), ConfigDefaults::shaderProfileTreeKey());
    PhosphorAnimationShaders::ShaderProfileTree prevTree;
    if (!prevMap.isEmpty())
        prevTree = PhosphorAnimationShaders::ShaderProfileTree::fromJson(QJsonObject::fromVariantMap(prevMap));
    const auto prevPruned = pruneShaderProfileTreeToSupportedPaths(prevTree);
    if (pruned == prevPruned)
        return;
    m_store->write(ConfigDefaults::animationsGroup(), ConfigDefaults::shaderProfileTreeKey(),
                   pruned.toJson().toVariantMap());
    Q_EMIT shaderProfileTreeChanged();
    Q_EMIT settingsChanged();
}

QString Settings::shaderProfileTreeJson() const
{
    return QString::fromUtf8(QJsonDocument(shaderProfileTree().toJson()).toJson(QJsonDocument::Compact));
}

void Settings::setShaderProfileTreeJson(const QString& json)
{
    if (json.isEmpty()) {
        setShaderProfileTree(PhosphorAnimationShaders::ShaderProfileTree{});
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) {
        qCWarning(lcConfig) << "setShaderProfileTreeJson: malformed JSON, ignoring";
        return;
    }
    setShaderProfileTree(PhosphorAnimationShaders::ShaderProfileTree::fromJson(doc.object()));
}

// ── Decorations tree (PhosphorConfig::Store-backed) ─────────────────────────
// Persisted as one nested JSON entry under Decorations/DecorationProfileTree,
// mirroring how the animation shaderProfileTree persists under
// Animations/ShaderProfileTree. The STORE holds only user edits (schema
// default: the empty tree); the built-in card chrome for the OSD and the
// PopupFrame popups (ConfigDefaults::decorationProfileTree) is overlaid as a
// lowest-precedence seed layer on every read — the same model as the
// animation seeds (PhosphorProfileRegistry's low-precedence owner tag), so
// shipped default improvements reach users who never customized those
// surfaces, and a user config that predates (or was written without) the
// defaults still renders them. A user edit at a seeded path becomes a real
// override and wins; an engaged-but-empty chain keeps a surface explicitly
// undecorated (see DecorationProfileTree::withSeedDefaults).

PhosphorSurfaceShaders::DecorationProfileTree Settings::decorationProfileTree() const
{
    const QVariantMap map =
        m_store->read<QVariantMap>(ConfigDefaults::decorationsGroup(), ConfigDefaults::decorationProfileTreeKey());
    return PhosphorSurfaceShaders::DecorationProfileTree::fromJson(QJsonObject::fromVariantMap(map))
        .withSeedDefaults(ConfigDefaults::decorationProfileTree());
}

PhosphorSurfaceShaders::DecorationProfileTree Settings::committedDecorationProfileTree() const
{
    // Read the baseline snapshot, not the live store — mirrors isKeyModified()'s
    // m_baseline.value(group).value(key) lookup so the two stay in lockstep. The
    // same seed overlay as decorationProfileTree() so a never-modified key
    // compares equal to the live tree (both canonicalise to the same merged
    // view) instead of spuriously reporting a diff.
    const QVariantMap map =
        m_baseline.value(ConfigDefaults::decorationsGroup()).value(ConfigDefaults::decorationProfileTreeKey()).toMap();
    return PhosphorSurfaceShaders::DecorationProfileTree::fromJson(QJsonObject::fromVariantMap(map))
        .withSeedDefaults(ConfigDefaults::decorationProfileTree());
}

void Settings::setDecorationProfileTree(const PhosphorSurfaceShaders::DecorationProfileTree& tree)
{
    refreshCleanBackendFromDisk();
    // Prune the incoming tree at the persistence boundary — same
    // belt-and-braces rationale as setShaderProfileTree. fromJson is the
    // tree's canonical unsupported-path filter (setOverride itself does not
    // validate), so a toJson→fromJson round trip IS the prune: a Q_INVOKABLE
    // write from scripting/tests cannot stamp unsupported-path entries onto
    // disk. The read side (decorationProfileTree) passes through the same
    // filter, so the comparison below is pruned-vs-pruned.
    auto pruned = PhosphorSurfaceShaders::DecorationProfileTree::fromJson(tree.toJson());
    // Strip the parts of an override the read-side seed overlay regenerates:
    // callers read the MERGED tree (seed defaults injected), mutate, and write
    // the whole tree back, so without this the injected card chrome would
    // freeze into the stored blob on the first unrelated edit — and a later
    // shipped default improvement would never reach this config. The strip is
    // PER FIELD, not per override: a parameters-only retune of a seeded
    // surface arrives as {chain: seed chain, parameters: user map}, and
    // storing only the parameters keeps the chain seed-owned so shipped chain
    // improvements still flow. Each strip is validated by regenerating the
    // candidate through withSeedDefaults and requiring the WHOLE merged view
    // back unchanged — not just this path's override. The whole-view check
    // honours the overlay's injection gates everywhere: a field that merely
    // LOOKS like a seed but whose removal would change the resolved result
    // (a chain-only override whose parameters would then inject, a map
    // shadowed by an engaged ancestor, or a stripped chain re-opening the
    // master gate for a descendant seed path) is left alone.
    const auto seeds = ConfigDefaults::decorationProfileTree();
    // Order-insensitive merged-view equality: the tree's operator== also
    // compares insertion order, and a strip-then-reinject legitimately moves
    // the reinjected path to the end, while resolve() ignores order entirely.
    const auto mergedEquivalent = [](const PhosphorSurfaceShaders::DecorationProfileTree& a,
                                     const PhosphorSurfaceShaders::DecorationProfileTree& b) {
        if (!(a.baseline() == b.baseline()))
            return false;
        const QStringList aPaths = a.overriddenPaths();
        QSet<QString> paths(aPaths.cbegin(), aPaths.cend());
        const QStringList bPaths = b.overriddenPaths();
        for (const QString& p : bPaths)
            paths.insert(p);
        for (const QString& p : paths) {
            if (a.hasOverride(p) != b.hasOverride(p))
                return false;
            if (a.hasOverride(p) && !(a.directOverride(p) == b.directOverride(p)))
                return false;
        }
        return true;
    };
    for (const QString& path : seeds.overriddenPaths()) {
        if (!pruned.hasOverride(path))
            continue;
        PhosphorSurfaceShaders::DecorationProfileTree without = pruned;
        without.clearOverride(path);
        const auto regenerated = without.withSeedDefaults(seeds);
        if (!regenerated.hasOverride(path))
            continue;
        const auto seedView = regenerated.directOverride(path);
        auto candidate = pruned.directOverride(path);
        bool strippedAny = false;
        const auto stripField = [&](auto member) {
            auto& slot = candidate.*member;
            const auto& regen = seedView.*member;
            if (slot.has_value() && regen.has_value() && *slot == *regen) {
                slot.reset();
                strippedAny = true;
            }
        };
        stripField(&PhosphorSurfaceShaders::DecorationProfile::chain);
        stripField(&PhosphorSurfaceShaders::DecorationProfile::parameters);
        stripField(&PhosphorSurfaceShaders::DecorationProfile::disabledPacks);
        if (!strippedAny)
            continue;
        PhosphorSurfaceShaders::DecorationProfileTree candidateTree = pruned;
        if (!candidate.chain && !candidate.parameters && !candidate.disabledPacks)
            candidateTree.clearOverride(path);
        else
            candidateTree.setOverride(path, candidate);
        if (mergedEquivalent(candidateTree.withSeedDefaults(seeds), pruned.withSeedDefaults(seeds)))
            pruned = candidateTree;
    }
    // Value-equality compare against the STORED (raw, pre-overlay) tree so a
    // same-tree write doesn't fire a spurious changed signal — writing the
    // merged default view back over an empty store normalises to empty and is
    // correctly a no-op.
    const QVariantMap storedMap =
        m_store->read<QVariantMap>(ConfigDefaults::decorationsGroup(), ConfigDefaults::decorationProfileTreeKey());
    if (pruned == PhosphorSurfaceShaders::DecorationProfileTree::fromJson(QJsonObject::fromVariantMap(storedMap)))
        return;
    m_store->write(ConfigDefaults::decorationsGroup(), ConfigDefaults::decorationProfileTreeKey(),
                   pruned.toJson().toVariantMap());
    Q_EMIT decorationProfileTreeChanged();
    Q_EMIT settingsChanged();
}

QString Settings::decorationProfileTreeJson() const
{
    return QString::fromUtf8(QJsonDocument(decorationProfileTree().toJson()).toJson(QJsonDocument::Compact));
}

void Settings::setDecorationProfileTreeJson(const QString& json)
{
    if (json.isEmpty()) {
        // Empty string = reset to the canonical default, exactly like the
        // animation shaderProfileTree facade: drop every user edit (store the
        // empty tree). The read side re-injects the built-in seed defaults
        // (ConfigDefaults::decorationProfileTree card chrome for the OSD and
        // PopupFrame popups); everything else returns to "no decoration"
        // (border and titlebar visuals are rule-owned).
        setDecorationProfileTree(PhosphorSurfaceShaders::DecorationProfileTree{});
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) {
        qCWarning(lcConfig) << "setDecorationProfileTreeJson: malformed JSON, ignoring";
        return;
    }
    setDecorationProfileTree(PhosphorSurfaceShaders::DecorationProfileTree::fromJson(doc.object()));
}

} // namespace PlasmaZones

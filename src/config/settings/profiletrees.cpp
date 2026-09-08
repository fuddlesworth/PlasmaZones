// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config/settings.h"
#include "config/configdefaults.h"
#include "core/platform/logging.h"
#include "core/types/animationshadersupportedpaths.h"

#include <PhosphorAnimation/Profile.h>
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
    // can actually edit. See `src/core/types/animationshadersupportedpaths.h`
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
    // setShaderProfileTree(currentTree)). Compare AFTER pruning, so a caller
    // writing back a value it just read (both sides already pruned) is a no-op
    // rather than a spurious write.
    //
    // This does NOT self-heal a stale on-disk config: both sides of the compare
    // are pruned, so an unsupported entry sitting in the file is invisible here
    // and survives until some other edit forces a write. Acceptable because the
    // READ side prunes unconditionally, so a stale entry can never reach a
    // consumer — it just lingers in the file.
    // The getter is exactly "read + parse + prune", so reuse it instead of
    // duplicating its body inline. (fromJson({}) already yields the
    // default-constructed tree, so the old !isEmpty() guard was dead.)
    const auto prevPruned = shaderProfileTree();
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

// ── Motion (timing) tree (PhosphorConfig::Store-backed) ─────────────────────
// The timing sibling of the shader tree above, persisted under
// Animations/MotionProfileTree in `PhosphorAnimation::ProfileTree`'s own
// serialized shape. Before schema v8 this lived in loose per-event JSON files
// under `<data>/plasmazones/profiles`, which split one animation event across
// two stores: its pack in config, its timing on disk. That split is what made
// a settings profile capture the pack and lose the timing, and what forced the
// motion-set domain to carry a file-staging layer the decoration domain, whose
// single tree holds everything, never needed.
//
// Carried as a raw QVariantMap, not a parsed tree. Parsing a ProfileTree needs
// a CurveRegistry, and the config layer owning one would drag curve loading
// into every process that reads a setting. Consumers that animate parse with
// the registry they already have; consumers that only read or rewrite a path's
// fields work on the map.
//
// No seed layer here, unlike the decoration tree. The animation timing seeds
// live in PhosphorProfileRegistry at its low-precedence owner tag, where they
// have always lived, and the schema default for this key is the empty tree.

QVariantMap Settings::motionProfileTree() const
{
    return m_store->read<QVariantMap>(ConfigDefaults::animationsGroup(), ConfigDefaults::motionProfileTreeKey());
}

QVariantMap Settings::committedMotionProfileTree() const
{
    // The baseline snapshot, not the live store — mirrors isKeyModified()'s
    // m_baseline lookup so a per-page Discard and the dirty check agree.
    return m_baseline.value(ConfigDefaults::animationsGroup()).value(ConfigDefaults::motionProfileTreeKey()).toMap();
}

namespace {

/// The keys a motion-tree entry's `profile` may carry, and how long a string in
/// one may be.
///
/// This is a PERSISTENCE boundary, not a UI convenience. The tree is one shared
/// config key that every read of per-event timing copies whole, the file is
/// hand-editable, and several writers reach it: the animations page, a settings
/// profile being applied, `setMotionProfileTreeJson` from QML, and the v7→v8
/// migration. The page filtered its own writes and the migration filtered its
/// own import, which left every other door unguarded — a stray key or a 64 KB
/// string entering through one of them would then stay, because
/// `Profile::fromJson` ignores what it does not recognise rather than pruning
/// it.
///
/// Deliberately does NOT parse. Judging a curve would need a CurveRegistry this
/// layer must not grow, for the reason the setter's own comment gives.
QVariantMap boundedProfileMap(const QVariantMap& profile, const QString& path)
{
    using P = PhosphorAnimation::Profile;
    static const QSet<QString> kKnownFields = {
        QLatin1String(P::JsonFieldCurve),           QLatin1String(P::JsonFieldDuration),
        QLatin1String(P::JsonFieldMinDistance),     QLatin1String(P::JsonFieldSequenceMode),
        QLatin1String(P::JsonFieldStaggerInterval), QLatin1String(P::JsonFieldPresetName),
    };
    // Above any legitimate curve spec or preset name and far below the cost of
    // letting an unbounded string reach the key.
    constexpr int kMaxStringChars = 1024;

    QVariantMap out;
    for (auto it = profile.cbegin(); it != profile.cend(); ++it) {
        if (!kKnownFields.contains(it.key())) {
            qCWarning(lcConfig) << "setMotionProfileTree: dropping unknown field" << it.key() << "at" << path;
            continue;
        }
        if (it.value().typeId() == QMetaType::QString && it.value().toString().size() > kMaxStringChars) {
            qCWarning(lcConfig) << "setMotionProfileTree: dropping over-long" << it.key() << "at" << path;
            continue;
        }
        out.insert(it.key(), it.value());
    }
    return out;
}

} // namespace

void Settings::setMotionProfileTree(const QVariantMap& tree)
{
    refreshCleanBackendFromDisk();
    // Stored VERBATIM, with no round trip through ProfileTree. That round trip
    // looks like harmless canonicalisation and is not: Profile stores its curve
    // as a resolved object, so re-serializing one parsed against a registry
    // that has not loaded the user's curve packs drops the `curve` key
    // outright, silently retiming every event that names a curve by name. The
    // config layer has no curve registry and must not grow one, so it does not
    // parse. Callers assemble the tree from what they read here, and the write
    // side of the animations page is the only thing that builds one.
    //
    // The ONE normalisation applied here is dropping an empty `overrides` list
    // (and, with it, an empty `baseline`). A tree carrying no overrides is the
    // schema default, and storing it as `{"overrides": []}` would leave the key
    // permanently unequal to its default: sparse persistence would never prune
    // it, `isKeyModified` would report the page dirty forever, and the key would
    // join every settings-profile delta captured afterwards. This touches only
    // the empty case and never inspects a profile body, so the curve hazard
    // above does not apply. Doing it here rather than only at the page's helper
    // makes the persistence boundary canonical whoever builds the map —
    // `setMotionProfileTreeJson`, a profile apply, or a future writer.
    // Filter each entry's profile body before anything else looks at the map,
    // so the comparison below and the stored value are the same shape.
    QVariantMap canonical = tree;
    {
        const QVariantList entries = canonical.value(QLatin1String("overrides")).toList();
        QVariantList filtered;
        filtered.reserve(entries.size());
        for (const QVariant& entryVar : entries) {
            QVariantMap entry = entryVar.toMap();
            const QString path = entry.value(QLatin1String("path")).toString();
            entry.insert(QLatin1String("profile"),
                         boundedProfileMap(entry.value(QLatin1String("profile")).toMap(), path));
            filtered.append(entry);
        }
        if (!filtered.isEmpty()) {
            canonical.insert(QLatin1String("overrides"), filtered);
        }
    }
    if (canonical.value(QLatin1String("overrides")).toList().isEmpty()) {
        canonical.remove(QLatin1String("overrides"));
        if (canonical.value(QLatin1String("baseline")).toMap().isEmpty()) {
            canonical.remove(QLatin1String("baseline"));
        }
    }
    if (canonical == motionProfileTree())
        return;
    m_store->write(ConfigDefaults::animationsGroup(), ConfigDefaults::motionProfileTreeKey(), canonical);
    Q_EMIT motionProfileTreeChanged();
    Q_EMIT settingsChanged();
}

QString Settings::motionProfileTreeJson() const
{
    return QString::fromUtf8(
        QJsonDocument(QJsonObject::fromVariantMap(motionProfileTree())).toJson(QJsonDocument::Compact));
}

void Settings::setMotionProfileTreeJson(const QString& json)
{
    if (json.isEmpty()) {
        // Empty string = drop every per-event timing override, the same
        // "reset to canonical default" the shader facade gives.
        setMotionProfileTree({});
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) {
        qCWarning(lcConfig) << "setMotionProfileTreeJson: malformed JSON, ignoring";
        return;
    }
    setMotionProfileTree(doc.object().toVariantMap());
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

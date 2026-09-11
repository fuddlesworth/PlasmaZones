// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config/settings.h"
#include "config/configdefaults.h"
#include "core/platform/logging.h"
#include "core/types/animationshadersupportedpaths.h"
#include "core/types/overlayshadertree.h"

#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ShaderProfileTree.h>
#include <PhosphorConfig/Schema.h>
#include <PhosphorConfig/Store.h>
#include <PhosphorSurface/DecorationProfileTree.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace PlasmaZones {

namespace {

/// Run a blob through the same schema validator `Store::write` applies, so a
/// setter's changed-check compares like with like.
///
/// The two setters below compare the CALLER's tree against a store READ, and a
/// store read has already been sanitized. Any input the validator would alter
/// (a parameter past its bound, a non-canonical UUID spelling, an override
/// count over the cap) therefore never equals what comes back, so the setter
/// writes, the store canonicalizes to the value already on disk, and the
/// changed signals fire for a value that did not move — on every repeat call.
/// The overlay tree's signal drives the daemon's overlay recreate, so the
/// repeat is not free. The two arms above this one canonicalize their input
/// in-setter and need no help.
QVariantMap sanitizedThroughSchema(const PhosphorConfig::Store* store, const QString& group, const QString& key,
                                   const QVariantMap& map)
{
    const PhosphorConfig::KeyDef* def = store->schema().findKey(group, key);
    if (!def || !def->validator)
        return map;
    return def->validator(QVariant(map)).toMap();
}

} // namespace

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
    //
    // Both sides go through sanitizedThroughSchema, because this key gained a
    // schema validator and the getter's value comes from a store READ, which the
    // validator has already run on. Comparing the caller's unsanitized tree
    // against a sanitized one means any input the validator alters — an
    // over-long id or preset id, a 65th parameter, a map-valued parameter, more
    // than 1024 overrides — can never compare equal, so the early return never
    // fires and every repeat call writes and emits for a value that did not
    // move. Same shape the decoration setter below already guards against.
    const auto sanitized = PhosphorAnimationShaders::ShaderProfileTree::fromJson(QJsonObject::fromVariantMap(
        sanitizedThroughSchema(m_store.get(), ConfigDefaults::animationsGroup(), ConfigDefaults::shaderProfileTreeKey(),
                               pruned.toJson().toVariantMap())));
    const auto prevPruned = shaderProfileTree();
    if (sanitized == prevPruned)
        return;
    // The sanitized tree, for the reason given on the overlay setter below.
    m_store->write(ConfigDefaults::animationsGroup(), ConfigDefaults::shaderProfileTreeKey(),
                   sanitized.toJson().toVariantMap());
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
    // Compared against the canonical READ. A blob that is not a map at all
    // reads back as an empty map, so this short-circuits against it, and the
    // key is nonetheless repaired: at that point the value equals its default,
    // and sparse persistence deletes a default-equal key on save. The test
    // `aMalformedTreeBlobIsRepairedRatherThanLeftInPlace` pins that route,
    // which is the only one that removes it.
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

namespace {

/// Size bounds for a decoration-tree profile at the persistence boundary, the
/// decoration twin of boundedProfileMap above and there for the same reason:
/// one shared config key, hand-editable, reached by several writers (the
/// decoration pages, a set import, a settings profile, the D-Bus setter), and
/// nothing downstream prunes what it does not understand.
///
/// The FIELD SET is not whitelisted here, unlike the motion twin, because it
/// cannot be: `parameters` is keyed by pack id and then by the parameter ids
/// each pack declares in its own metadata, so the legitimate key set is
/// whatever packs are installed, which this layer does not know and must not
/// load a registry to learn. What can be bounded regardless of pack is the
/// SIZE of each container and of each string in it, so that is what is
/// bounded: chain and disabledPacks length, parameter-map key count at both
/// levels, and every string's length. Excess is dropped with one warning per
/// field, and a tree written through here can never grow past these no
/// matter which door it came in by.
/// TWO bounders, deliberately, and the names repeat on purpose.
///
/// `settingsschema_shaderbounds.cpp` has a `boundedIdList` and a
/// `boundedDecorationProfile` of its own, and the duplication is not drift left
/// over from unifying them. They bound different DIRECTIONS: the schema
/// sanitizer runs on every read and write of the key, which is what covers a
/// hand-edited `config.json`, while these run inside the setter, which is what
/// covers every writer reaching the tree through it with a per-field warning
/// naming the path. Each needs context the other does not have (these take the
/// path, to name it in the warning; that one takes none), which is why the
/// signatures differ rather than one calling the other.
///
/// What MUST stay in step is the numbers, and only the numbers. A sanitizer that
/// trimmed harder than the setter would rewrite a tree the setter had just
/// accepted, so the value would vanish on the next read instead of at the write
/// that produced it. These three are the authoritative pair of
/// `kMaxChainPacks` / `kMaxShaderStringChars` over there; change one and change
/// the other.
constexpr int kMaxDecorationListEntries = 64;
constexpr int kMaxDecorationMapKeys = 64;
constexpr int kMaxDecorationStringChars = 1024;

QStringList boundedIdList(const QStringList& ids, const char* field, const QString& path)
{
    QStringList out;
    bool warnedLength = false;
    for (const QString& id : ids) {
        if (id.size() > kMaxDecorationStringChars) {
            qCWarning(lcConfig) << "setDecorationProfileTree: dropping over-long id in" << field << "at" << path;
            continue;
        }
        if (out.size() >= kMaxDecorationListEntries) {
            if (!warnedLength) {
                qCWarning(lcConfig) << "setDecorationProfileTree: dropping" << field << "entries past"
                                    << kMaxDecorationListEntries << "at" << path;
                warnedLength = true;
            }
            continue;
        }
        out.append(id);
    }
    return out;
}

/// One level of a parameters map: key count capped, over-long strings and
/// over-long keys dropped, nested maps bounded the same way. Values that are
/// neither strings nor maps (numbers, bools, colours) are fixed-size already.
QVariantMap boundedParameterMap(const QVariantMap& map, const QString& path, int depth)
{
    QVariantMap out;
    bool warnedCount = false;
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        if (it.key().size() > kMaxDecorationStringChars) {
            qCWarning(lcConfig) << "setDecorationProfileTree: dropping over-long parameter key at" << path;
            continue;
        }
        if (out.size() >= kMaxDecorationMapKeys) {
            if (!warnedCount) {
                qCWarning(lcConfig) << "setDecorationProfileTree: dropping parameter keys past" << kMaxDecorationMapKeys
                                    << "at" << path;
                warnedCount = true;
            }
            continue;
        }
        const QVariant& v = it.value();
        if (v.typeId() == QMetaType::QString && v.toString().size() > kMaxDecorationStringChars) {
            qCWarning(lcConfig) << "setDecorationProfileTree: dropping over-long parameter" << it.key() << "at" << path;
            continue;
        }
        // A pack's own map sits one level under the pack id, and a bounded
        // depth keeps a hand-crafted nest from recursing without end.
        if (v.typeId() == QMetaType::QVariantMap) {
            if (depth >= 2) {
                qCWarning(lcConfig) << "setDecorationProfileTree: dropping over-nested parameter" << it.key() << "at"
                                    << path;
                continue;
            }
            out.insert(it.key(), boundedParameterMap(v.toMap(), path, depth + 1));
            continue;
        }
        out.insert(it.key(), v);
    }
    return out;
}

/// `presetIds`, which is `{packId -> presetId}` and so is flat and
/// string-valued at both levels, unlike `parameters`.
///
/// Not routed through boundedParameterMap, which would accept a nested map and
/// a numeric value here. Every value is a preset id the resolver calls
/// `.toString()` on, so anything else is inert, and keeping a key whose value
/// can never resolve only preserves a hand-edit that does nothing.
QVariantMap boundedPresetIdMap(const QVariantMap& map, const QString& path)
{
    QVariantMap out;
    bool warnedCount = false;
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        if (it.key().size() > kMaxDecorationStringChars) {
            qCWarning(lcConfig) << "setDecorationProfileTree: dropping over-long presetIds pack id at" << path;
            continue;
        }
        if (out.size() >= kMaxDecorationMapKeys) {
            if (!warnedCount) {
                qCWarning(lcConfig) << "setDecorationProfileTree: dropping presetIds entries past"
                                    << kMaxDecorationMapKeys << "at" << path;
                warnedCount = true;
            }
            continue;
        }
        if (it.value().typeId() != QMetaType::QString || it.value().toString().size() > kMaxDecorationStringChars) {
            qCWarning(lcConfig) << "setDecorationProfileTree: dropping unusable presetIds value for" << it.key() << "at"
                                << path;
            continue;
        }
        out.insert(it.key(), it.value());
    }
    return out;
}

PhosphorSurfaceShaders::DecorationProfile boundedDecorationProfile(const PhosphorSurfaceShaders::DecorationProfile& p,
                                                                   const QString& path)
{
    PhosphorSurfaceShaders::DecorationProfile out = p;
    if (out.chain)
        out.chain = boundedIdList(*out.chain, "chain", path);
    if (out.disabledPacks)
        out.disabledPacks = boundedIdList(*out.disabledPacks, "disabledPacks", path);
    // Bounded here as well as in the schema sanitizer, for the reason the whole
    // function exists: the schema runs on read and write of THIS key, and the
    // setter covers every writer reaching the tree through it. A field bounded
    // on only one of the two is a field whose bound depends on which door the
    // write came in by.
    if (out.presetIds)
        out.presetIds = boundedPresetIdMap(*out.presetIds, path);
    if (out.parameters)
        out.parameters = boundedParameterMap(*out.parameters, path, 0);
    return out;
}

} // namespace

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
    // Size-bound every profile body before anything else looks at the tree,
    // so the seed strip below and the stored value see the same shape (the
    // motion twin filters before its comparison for the same reason).
    pruned.setBaseline(boundedDecorationProfile(pruned.baseline(), QString()));
    for (const QString& path : pruned.overriddenPaths())
        pruned.setOverride(path, boundedDecorationProfile(pruned.directOverride(path), path));
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
    const QVariantMap prunedMap =
        sanitizedThroughSchema(m_store.get(), ConfigDefaults::decorationsGroup(),
                               ConfigDefaults::decorationProfileTreeKey(), pruned.toJson().toVariantMap());
    if (PhosphorSurfaceShaders::DecorationProfileTree::fromJson(QJsonObject::fromVariantMap(prunedMap))
        == PhosphorSurfaceShaders::DecorationProfileTree::fromJson(QJsonObject::fromVariantMap(storedMap)))
        return;
    // The sanitized map, for the reason given on the overlay setter below.
    m_store->write(ConfigDefaults::decorationsGroup(), ConfigDefaults::decorationProfileTreeKey(), prunedMap);
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

// ── Overlay shader tree (PhosphorConfig::Store-backed) ──────────────────────
// Persisted as one nested JSON entry under Overlays/OverlayShaderTree,
// mirroring the two trees above. No prune-to-supported-paths step (the paths
// are layout UUIDs, and a stale UUID for a deleted layout is inert, never
// resolved) and no seed overlay (the schema default is the bare empty tree,
// like the animation tree).
//
// Overrides are NOT reclaimed when a layout is deleted, and that is deliberate.
// A layout UUID is per-installation, so "this machine has no layout with that
// id" is exactly the state a shared overlay set or an imported settings profile
// produces on a second machine, and it is not evidence the entry is dead. Auto
// reclaiming would silently eat those the first time the config was saved. The
// growth is bounded by the schema's override cap, and the assignments page
// appends every unmatched override as a clearable row (see
// OverlaysPageController::assignableLayouts), so a genuinely dead entry still
// has a way out.

OverlayShaderTree Settings::overlayShaderTree() const
{
    const QVariantMap map =
        m_store->read<QVariantMap>(ConfigDefaults::overlaysGroup(), ConfigDefaults::overlayShaderTreeKey());
    return OverlayShaderTree::fromJson(QJsonObject::fromVariantMap(map));
}

void Settings::setOverlayShaderTree(const OverlayShaderTree& tree)
{
    refreshCleanBackendFromDisk();
    // Value-equality compare so a same-tree write doesn't fire a spurious
    // changed signal (discard-changes writes back the tree it just read).
    // Sanitized first, because the read side already is: see
    // sanitizedThroughSchema above.
    const QVariantMap sanitizedMap =
        sanitizedThroughSchema(m_store.get(), ConfigDefaults::overlaysGroup(), ConfigDefaults::overlayShaderTreeKey(),
                               tree.toJson().toVariantMap());
    if (OverlayShaderTree::fromJson(QJsonObject::fromVariantMap(sanitizedMap)) == overlayShaderTree())
        return;
    // The SANITIZED map, not the caller's. Writing the raw one worked only
    // because Store::write runs the same validator again, so the value that
    // landed matched what was compared — by repetition rather than by
    // construction. Two lines apart and one of them would have to be noticed.
    m_store->write(ConfigDefaults::overlaysGroup(), ConfigDefaults::overlayShaderTreeKey(), sanitizedMap);
    Q_EMIT overlayShaderTreeChanged();
    Q_EMIT settingsChanged();
}

QString Settings::overlayShaderTreeJson() const
{
    return QString::fromUtf8(QJsonDocument(overlayShaderTree().toJson()).toJson(QJsonDocument::Compact));
}

void Settings::setOverlayShaderTreeJson(const QString& json)
{
    if (json.isEmpty()) {
        setOverlayShaderTree(OverlayShaderTree{});
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) {
        qCWarning(lcConfig) << "setOverlayShaderTreeJson: malformed JSON, ignoring";
        return;
    }
    setOverlayShaderTree(OverlayShaderTree::fromJson(doc.object()));
}

} // namespace PlasmaZones

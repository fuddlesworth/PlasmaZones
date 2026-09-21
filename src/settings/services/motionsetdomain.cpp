// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Motion-set domain closures for the shared ShaderSetStore. A motion set
// captures both halves of a per-event animation: the timing from
// `Animations/MotionProfileTree` and the pack assignment from
// `Animations/ShaderProfileTree`. Both are config keys since schema v8, so the
// snapshot reads two config values and opens no file. The generic store handles
// the envelope (name / description / version), the coverage summary, and the
// set FILES themselves.

#include "motionsetdomain.h"

#include "core/platform/logging.h"

#include "core/types/animationshadersupportedpaths.h"

#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfilePaths.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QLoggingCategory>
#include <QSet>

#include <QStringList>

#include <utility>

namespace PlasmaZones::motionset {

namespace {

/// Current on-disk motion-set format. The store stamps it on save and refuses
/// a NEWER file on apply / import.
///
/// 2 adds the `shader` half of each entry (see makeConfig's header). Reading a
/// format-1 set still works — it simply carries no shader key — but the bump
/// is what stops an OLDER build from opening a format-2 set, dropping the
/// shader half on parse and applying a silently halved look.
constexpr int kSetFormatVersion = 2;

constexpr QLatin1String kOverridesKey{"overrides"};
constexpr QLatin1String kPathKey{"path"};
constexpr QLatin1String kProfileKey{"profile"};
constexpr QLatin1String kBaselineKey{"baseline"};
/// The nested shader half of one entry's profile, and its two fields. Named
/// apart from the timing fields so the two halves can never collide as the
/// Profile schema grows.
constexpr QLatin1String kShaderKey{"shader"};
constexpr QLatin1String kEffectIdKey{"effectId"};
constexpr QLatin1String kParametersKey{"parameters"};
constexpr QLatin1String kPresetIdKey{"presetId"};
/// The same figure AnimationsPageController's writer refuses above, mirrored rather than
/// shared because that header is not in this file's include set. If one moves, both move.
constexpr int kMaxPresetIdChars = 1024;

/// The timing fields `Profile` actually round-trips. A timing half whose keys
/// are all unrecognised is a wrong-shaped or hand-edited entry: it is a
/// non-empty object that changes nothing, and staging it writes a tree entry
/// every reader then ignores. Decoration refuses that class by parsing its payload
/// into a typed profile and judging the RESULT (decorationpagecontroller_sets.cpp);
/// `Profile::fromJson` needs a CurveRegistry this domain has no handle on, so
/// the same guarantee is had here by requiring a recognised field rather than
/// by duplicating the type rules.
const QSet<QString>& knownTimingFields()
{
    static const QSet<QString> fields = {
        QLatin1String(PhosphorAnimation::Profile::JsonFieldCurve),
        QLatin1String(PhosphorAnimation::Profile::JsonFieldDuration),
        QLatin1String(PhosphorAnimation::Profile::JsonFieldMinDistance),
        QLatin1String(PhosphorAnimation::Profile::JsonFieldSequenceMode),
        QLatin1String(PhosphorAnimation::Profile::JsonFieldStaggerInterval),
        QLatin1String(PhosphorAnimation::Profile::JsonFieldPresetName),
    };
    return fields;
}

struct StagedEntry
{
    QString path;
    /// The timing half, already stripped of the nested shader key, so it is
    /// exactly the shape one entry's `profile` takes in
    /// `Animations/MotionProfileTree`. Empty when the entry carries a shader
    /// assignment only.
    QVariantMap timing;
    /// True when the entry carried a `shader` object at all. Absent is not the
    /// same as empty: absent means "do not touch this event's pack", which is
    /// what every format-1 entry means, while present carries one of the three
    /// states the shader tree models (see below).
    bool hasShader = false;
    /// The shader half verbatim, in the same map shape
    /// `AnimationsPageController::rawShaderProfile()` returns. Passed straight
    /// through rather than pre-split, because which of the three states it
    /// represents decides WHICH controller API applies it, and that dispatch
    /// belongs next to those APIs rather than here.
    QVariantMap shader;
};

/// The event-path taxonomy is fixed for the process, so build it once. Shared by
/// the validator and the snapshot walk, both of which run on every set refresh.
const QSet<QString>& knownEventPaths()
{
    static const QSet<QString> paths = [] {
        const QStringList list = PhosphorAnimation::ProfilePaths::allBuiltInPaths();
        return QSet<QString>(list.cbegin(), list.cend());
    }();
    return paths;
}

/// Validate + stage every entry in @p root. Whole-set discipline: one
/// malformed entry rejects the set rather than committing partial state.
/// Shared by validate (import / apply gate) and apply (the commit), so the
/// two can never drift apart on what counts as a valid entry.
bool stageEntries(const QJsonObject& root, QList<StagedEntry>* staged,
                  const std::function<bool(const QString&)>& knowsEffectId)
{
    staged->clear();
    // Motion has no baseline: there is no global default profile to apply one
    // to. A file carrying the key at all is a foreign or hand-edited set, and
    // accepting it would half-apply the set. Refuse the KEY, not just a
    // non-empty value — the same rule the decoration validator applies, so the
    // two domains cannot drift on what the shared envelope may carry.
    if (root.contains(kBaselineKey)) {
        qCWarning(lcConfig) << "motionset: rejecting a set that carries a baseline";
        return false;
    }
    if (root.contains(kOverridesKey) && !root.value(kOverridesKey).isArray()) {
        qCWarning(lcConfig) << "motionset: rejecting a set whose overrides are not an array";
        return false;
    }
    const QJsonArray overrides = root.value(kOverridesKey).toArray();
    staged->reserve(overrides.size());
    for (const QJsonValue& v : overrides) {
        if (!v.isObject()) {
            qCWarning(lcConfig) << "motionset: non-object entry in set";
            return false;
        }
        const QJsonObject entry = v.toObject();
        const QString path = entry.value(kPathKey).toString();
        // Membership in `allBuiltInPaths()` is the single source of truth —
        // the same rule the controller's setOverride enforces. It also
        // rejects empty and traversal-attempting paths.
        if (path.isEmpty() || !knownEventPaths().contains(path)) {
            qCWarning(lcConfig) << "motionset: rejecting unknown / invalid path" << path;
            return false;
        }
        if (!entry.value(kProfileKey).isObject()) {
            qCWarning(lcConfig) << "motionset: missing profile object for path" << path;
            return false;
        }
        QJsonObject profile = entry.value(kProfileKey).toObject();

        // Split the entry's two halves. `shader` is optional: a format-1 set
        // has none and means "leave this event's pack alone", which is why its
        // absence is not an error and is not the same as an empty one.
        StagedEntry out;
        out.path = path;
        if (profile.contains(kShaderKey)) {
            const QJsonValue shaderVal = profile.take(kShaderKey);
            if (!shaderVal.isObject()) {
                qCWarning(lcConfig) << "motionset: shader half is not an object for path" << path;
                return false;
            }
            const QJsonObject shader = shaderVal.toObject();
            // The shader tree is a THREE-state model and a set has to round-trip
            // all of it, so both fields are optional and only their types are
            // pinned here:
            //   effectId "pack"  — this event runs that pack
            //   effectId ""      — engaged-empty: explicitly NO pack, which also
            //                      blocks inheritance from an ancestor. Distinct
            //                      from having no override at all.
            //   no effectId, but parameters — the event inherits its pack from an
            //                      ancestor and overrides only the parameter map.
            // An object carrying neither is a no-op override the tree itself
            // refuses to store, so it is malformed here too.
            if (shader.contains(kEffectIdKey) && !shader.value(kEffectIdKey).isString()) {
                qCWarning(lcConfig) << "motionset: shader effectId is not a string for path" << path;
                return false;
            }
            if (shader.contains(kParametersKey) && !shader.value(kParametersKey).isObject()) {
                qCWarning(lcConfig) << "motionset: shader parameters are not an object for path" << path;
                return false;
            }
            if (shader.contains(kPresetIdKey) && !shader.value(kPresetIdKey).isString()) {
                qCWarning(lcConfig) << "motionset: shader presetId is not a string for path" << path;
                return false;
            }
            // BOUNDED here, where a refusal still fails the whole set. The controller's
            // writer refuses an over-long id and returns -1, and an imported set that got
            // that far would commit its timing and pack halves and silently lose the
            // preset — the mid-commit partial this validator exists to prevent.
            if (shader.value(kPresetIdKey).toString().size() > kMaxPresetIdChars) {
                qCWarning(lcConfig) << "motionset: shader presetId is too long for path" << path;
                return false;
            }
            // A presetId counts as content. An event can inherit its pack and its
            // values and own nothing but a preset reference, which the capture writes
            // out as a presetId-only half; refusing that dropped the one thing the
            // entry was about, with a warning, at parse time.
            if (!shader.contains(kEffectIdKey) && !shader.contains(kParametersKey) && !shader.contains(kPresetIdKey)) {
                qCWarning(lcConfig) << "motionset: shader half carries no effectId, parameters or presetId for path"
                                    << path;
                return false;
            }
            // The shader half has its OWN taxonomy, narrower than the motion
            // one: allBuiltInPaths() covers widget.*, cursor.*, panel.* and
            // editor.*, none of which has a shader leg. setShaderOverride
            // refuses those paths, so accepting one here passed validation and
            // then failed mid-commit, after earlier entries had already written
            // their timing files. Gate it at validate time instead, which is
            // what keeps the whole-set promise the header makes.
            if (!eventPathSupportsShaderLeg(path)) {
                qCWarning(lcConfig) << "motionset: path carries a shader half but supports no shader leg" << path;
                return false;
            }
            // Same argument one step further out: the WRITE refuses an effectId
            // this build does not have installed, so a set naming a pack the
            // sender has and the recipient does not passed validation and then
            // failed mid-commit — the entries before it already written, the
            // event that failed left carrying the set's new timing over its old
            // pack. Refuse the whole set here instead, which is the promise the
            // header makes and the only outcome a user can act on.
            const QString effectId = shader.value(kEffectIdKey).toString();
            if (!effectId.isEmpty() && knowsEffectId && !knowsEffectId(effectId)) {
                qCWarning(lcConfig) << "motionset: set names a pack this build does not have" << effectId << "for path"
                                    << path;
                return false;
            }
            out.hasShader = true;
            out.shader = shader.toVariantMap();
        }
        // Whatever is left is the timing half, in exactly the shape one
        // entry's `profile` takes in the timing tree, pruned to the recognised
        // fields rather than taken verbatim. What is
        // left after this is exactly what the write stores, because the
        // persistence boundary applies the same allowlist — so a hand-edited
        // set carrying junk alongside a real field no longer has that junk
        // survive into the staged entry, get compared against a stored profile
        // that never had it, and read as permanently inactive.
        {
            const QVariantMap raw = profile.toVariantMap();
            for (auto it = raw.cbegin(); it != raw.cend(); ++it) {
                if (knownTimingFields().contains(it.key())) {
                    out.timing.insert(it.key(), it.value());
                } else {
                    qCWarning(lcConfig) << "motionset: dropping unrecognised timing field" << it.key() << "at" << path;
                }
            }
            if (!raw.isEmpty()) {
                // Judge what the timing half actually SAYS, not merely that it
                // is non-empty — decoration's rule, adapted.
                const bool anyRecognised = !out.timing.isEmpty();
                if (!anyRecognised) {
                    qCWarning(lcConfig) << "motionset: timing half carries no recognised field for path" << path
                                        << "— keys:" << raw.keys();
                    return false;
                }
            }
        }
        if (out.timing.isEmpty() && !out.hasShader) {
            qCWarning(lcConfig) << "motionset: entry carries neither timing nor shader for path" << path;
            return false;
        }
        staged->push_back(std::move(out));
    }
    return !staged->isEmpty();
}

} // namespace

ShaderSetStore::Config makeConfig(std::function<QVariantMap()> readTimings, std::function<QString()> setsDir,
                                  std::function<bool(const QList<QPair<QString, QVariantMap>>&)> writeOverrides,
                                  std::function<QVariantMap()> readShaders,
                                  std::function<bool(const QString&, const QVariantMap&)> writeShader,
                                  std::function<QVariantMap()> resolvedShaderIds,
                                  std::function<bool(const QString&)> knowsEffectId)
{
    // The domain cannot function without these: a missing callable is a wiring
    // bug, not a runtime condition. Assert in debug; the lambdas below still
    // check, so a release build degrades to "nothing to snapshot / refuse the
    // write" instead of throwing std::bad_function_call.
    Q_ASSERT(readTimings);
    Q_ASSERT(writeOverrides);
    Q_ASSERT(readShaders);
    Q_ASSERT(writeShader);
    // Both of these degrade SILENTLY rather than loudly, which is why they are
    // asserted alongside the rest: a null resolvedShaderIds skips the entire
    // self-containment sweep, so the set saves with almost nothing in it, and a
    // null knowsEffectId drops the pack-installed gate, putting mid-batch
    // refusals back. Neither surfaces as an obvious failure at the callsite.
    Q_ASSERT(resolvedShaderIds);
    Q_ASSERT(knowsEffectId);

    ShaderSetStore::Config config;
    // Named rather than left to the default, so a future format bump is a
    // one-line change here instead of an easy-to-miss omission.
    config.formatVersion = kSetFormatVersion;
    config.setsDir = std::move(setsDir);

    // ── Snapshot: read both halves of every event out of config. The timing
    //    half comes from `Animations/MotionProfileTree`, the pack half from
    //    `Animations/ShaderProfileTree`. Insertion order is preserved, which
    //    keeps the on-disk set stable across saves and so diffable.
    //    Active-detection does NOT depend on it: the store indexes live
    //    overrides by path into a hash.
    config.snapshot = [readTimings = std::move(readTimings), readShaders = std::move(readShaders),
                       resolvedShaderIds = std::move(resolvedShaderIds)]() -> QJsonObject {
        if (!readTimings) {
            return QJsonObject{};
        }
        // The shader half, read ONCE for the whole snapshot. Consumed by the
        // timing walk below and then swept for any path that has a pack but no
        // timing override, which would otherwise be missing from the set.
        QVariantMap shaders = readShaders ? readShaders() : QVariantMap{};
        QJsonArray overrides;

        const QVariantMap tree = readTimings();
        const QVariantList stored = tree.value(kOverridesKey).toList();
        for (const QVariant& value : stored) {
            const QVariantMap entryMap = value.toMap();
            const QString entryName = entryMap.value(kPathKey).toString();
            if (!knownEventPaths().contains(entryName)) {
                continue;
            }
            QJsonObject profile = QJsonObject::fromVariantMap(entryMap.value(kProfileKey).toMap());
            // Fold in this event's pack assignment, and take it out of the map
            // so the sweep below only sees paths this walk never reached.
            const auto shaderIt = shaders.find(entryName);
            if (shaderIt != shaders.end()) {
                // Gated on the SHADER taxonomy, which is narrower than the
                // motion one this loop walks: widget.*, cursor.*, panel.* and
                // editor.* are built-in event paths with no shader leg. Without
                // this the snapshot could emit a shader half that stageEntries
                // then refuses, so the set would save and never validate again.
                // It only held because a prune two layers away kept such
                // entries out of the map.
                if (eventPathSupportsShaderLeg(entryName)) {
                    profile.insert(kShaderKey, QJsonObject::fromVariantMap(shaderIt.value().toMap()));
                }
                shaders.erase(shaderIt);
            }
            QJsonObject entry;
            entry.insert(kPathKey, entryName);
            entry.insert(kProfileKey, profile);
            overrides.append(entry);
        }

        // Paths carrying a pack but NO timing override. Without this sweep an event
        // whose only override is its shader would be dropped from the set, and
        // the set would then read as "clean" against a live state that is not.
        QSet<QString> emitted;
        for (const QJsonValue& v : std::as_const(overrides)) {
            emitted.insert(v.toObject().value(kPathKey).toString());
        }
        for (auto it = shaders.cbegin(); it != shaders.cend(); ++it) {
            if (!knownEventPaths().contains(it.key()) || !eventPathSupportsShaderLeg(it.key())) {
                continue;
            }
            QJsonObject profile;
            profile.insert(kShaderKey, QJsonObject::fromVariantMap(it.value().toMap()));
            QJsonObject entry;
            entry.insert(kPathKey, it.key());
            entry.insert(kProfileKey, profile);
            overrides.append(entry);
            emitted.insert(it.key());
        }

        // ── Built-in per-path defaults, so a set is SELF-CONTAINED. ──
        //
        // This is the analogue of decoration reading its seed-merged tree: a
        // decoration set captures every surface that has a look, whether the
        // user chose it or the build shipped it, which is what lets the set
        // reproduce that look on the recipient. Motion has per-path defaults of
        // exactly the same kind — window-morph on the geometry legs, a fade on
        // the OSD and popup legs — supplied at resolve time rather than stored.
        //
        // Capturing only stored overrides meant a user on defaults saved a set
        // that was nearly empty (or was told there was nothing to capture on a
        // setup that visibly animates), and applying it left a recipient's own
        // customisation of those legs standing, so the set did not reproduce
        // the sender's look at all.
        //
        // A path that already emitted above is skipped, so a deliberate
        // "no pack here" (the engaged-empty sentinel) is never overwritten by
        // the default it was chosen to suppress.
        // Resolved ONCE for the whole sweep, like readShaders above. Resolving
        // a single path rebuilds the entire ShaderProfileTree out of the store
        // — a read, a fromVariantMap, a fromJson and a prune walk — so asking
        // per path made this a full rebuild per supported path, on the GUI
        // thread, on every setsChanged.
        const QVariantMap resolved = resolvedShaderIds ? resolvedShaderIds() : QVariantMap{};
        for (const QString& path : shaderSupportedEventPaths()) {
            if (emitted.contains(path)) {
                continue;
            }
            // The RESOLVED pack, not the built-in default: a leaf inheriting
            // from a category ancestor renders that ancestor's pack, and
            // capturing the default instead would describe a look the sender
            // is not using while still reading as active, because the live
            // side of the comparison is this same snapshot.
            const QString resolvedId = resolved.value(path).toString();
            if (resolvedId.isEmpty()) {
                continue;
            }
            QJsonObject shader;
            shader.insert(kEffectIdKey, resolvedId);
            QJsonObject profile;
            profile.insert(kShaderKey, shader);
            QJsonObject entry;
            entry.insert(kPathKey, path);
            entry.insert(kProfileKey, profile);
            overrides.append(entry);
        }

        QJsonObject root;
        root.insert(kOverridesKey, overrides);
        return root;
    };

    // ── Active: an entry is satisfied when the halves it CARRIES match. An
    //    entry may hold a timing half, a pack half, or both, and `apply` below
    //    writes only the ones present — a pack-only entry deliberately leaves
    //    the path's timing alone, and a timing-only entry leaves its pack
    //    alone. The store's default check is exact equality, which is right
    //    for a domain whose apply replaces the whole profile (decoration) and
    //    wrong here: a pack-only entry would never match a path that also
    //    carries timing, so one field the set does not own would keep the
    //    whole set reading as inactive.
    //
    //    Each half is compared WHOLE, because apply replaces a half it writes
    //    rather than merging into it.
    config.entrySatisfied = [](const QJsonObject& setProfile, const QJsonObject& live) -> bool {
        if (setProfile.contains(kShaderKey) && setProfile.value(kShaderKey) != live.value(kShaderKey)) {
            return false;
        }
        QJsonObject setTiming = setProfile;
        setTiming.remove(kShaderKey);
        if (setTiming.isEmpty()) {
            return true; // carries no timing, so the path's timing is not its business
        }
        QJsonObject liveTiming = live;
        liveTiming.remove(kShaderKey);
        return setTiming == liveTiming;
    };

    config.validate = [knowsEffectId](const QJsonObject& root) -> bool {
        QList<StagedEntry> staged;
        return stageEntries(root, &staged, knowsEffectId);
    };

    // ── Apply: validate everything up-front, then write each entry. Both
    //    halves are config keys, so a mid-batch failure leaves the settings
    //    baseline untouched and Discard reverts the whole page in one
    //    `Settings::load()` — the same guarantee decoration has.
    config.apply = [writeOverrides = std::move(writeOverrides), writeShader = std::move(writeShader),
                    knowsEffectId](const QJsonObject& root) -> bool {
        if (!writeOverrides || !writeShader) {
            return false;
        }
        QList<StagedEntry> staged;
        if (!stageEntries(root, &staged, knowsEffectId)) {
            return false;
        }

        // The whole timing half in ONE write. Per-path writes were observable
        // half-applied: each one emits motionProfileTreeChanged synchronously,
        // so every card rebound and the set-row active sweep re-ran once per
        // path against an intermediate tree. Only entries that actually CARRY
        // timing are included, so a shader-only entry does not write an empty
        // override over timing the set never mentioned. Merge semantics apply
        // within an entry as well as across paths.
        QList<QPair<QString, QVariantMap>> timingEdits;
        timingEdits.reserve(staged.size());
        for (const StagedEntry& e : staged) {
            if (!e.timing.isEmpty()) {
                timingEdits.append({e.path, e.timing});
            }
        }
        if (!timingEdits.isEmpty() && !writeOverrides(timingEdits)) {
            qCWarning(lcConfig) << "motionset apply: timing write failed; nothing was committed";
            return false;
        }

        QStringList committedPaths;
        for (const StagedEntry& e : staged) {
            // Which of the three states this is decides which controller API
            // applies it; the closure owns that dispatch.
            if (e.hasShader && !writeShader(e.path, e.shader)) {
                qCWarning(lcConfig) << "motionset apply: shader write failed for path" << e.path;
                if (!committedPaths.isEmpty()) {
                    qCWarning(lcConfig) << "motionset apply: partial apply committed" << committedPaths.size()
                                        << "paths before failure:" << committedPaths;
                }
                return false;
            }
            committedPaths.append(e.path);
        }
        return true;
    };

    return config;
}

} // namespace PlasmaZones::motionset

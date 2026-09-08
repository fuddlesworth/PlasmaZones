// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Shared helpers for the five TUs that split AnimationsPageController across
// files (animationspagecontroller.cpp and its _overrides / _shaders / _paths /
// _groupwrites siblings). Covers the shader-effect / parameter / shader-profile conversions
// those TUs hand to QML, the override-file read and normalisation
// (JsonNameKey, JsonEffectIdKey, JsonShaderParametersKey,
// sanitizedProfileMap, profileToVariantMap,
// mergeMissingFields, fillLibraryDefaults), the Q_INVOKABLE-boundary bound on
// what a caller's map may carry to disk (kMaxWrittenMap*, boundedWrittenMap),
// and the two path helpers
// (humanizeSegment, collectShaderOverrideDescendants). Inline definitions here ensure every TU
// gets its own copy without relying on unity-build TU merging for cross-TU
// linkage.

#include "core/platform/logging.h"
#include "settings/utils/animationfileutils.h"

#include <PhosphorAnimation/AnimationShaderEffect.h>
#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ShaderProfile.h>
#include <PhosphorAnimation/ShaderProfileTree.h>

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1Char>
#include <QLatin1String>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace PlasmaZones {
namespace animations_controller_detail {

inline QVariantMap parameterInfoToMap(const PhosphorAnimationShaders::AnimationShaderEffect::ParameterInfo& p)
{
    // Keys mirror PhosphorRendering::ShaderRegistry::parameterInfoToVariantMap
    // so animation packs and overlay packs share QML editor components.
    // Optional fields are emitted only when valid/non-empty.
    QVariantMap m;
    m.insert(QLatin1String("id"), p.id);
    m.insert(QLatin1String("name"), p.name);
    m.insert(QLatin1String("type"), p.type);
    if (!p.description.isEmpty())
        m.insert(QLatin1String("description"), p.description);
    if (!p.group.isEmpty())
        m.insert(QLatin1String("group"), p.group);
    if (p.defaultValue.isValid())
        m.insert(QLatin1String("default"), p.defaultValue);
    if (p.minValue.isValid())
        m.insert(QLatin1String("min"), p.minValue);
    if (p.maxValue.isValid())
        m.insert(QLatin1String("max"), p.maxValue);
    if (p.stepValue.isValid())
        m.insert(QLatin1String("step"), p.stepValue);
    return m;
}

inline QVariantMap effectToMap(const PhosphorAnimationShaders::AnimationShaderEffect& effect)
{
    QVariantMap m;
    m.insert(QLatin1String("id"), effect.id);
    m.insert(QLatin1String("name"), effect.name);
    m.insert(QLatin1String("description"), effect.description);
    m.insert(QLatin1String("author"), effect.author);
    m.insert(QLatin1String("version"), effect.version);
    m.insert(QLatin1String("category"), effect.category);
    // Declared event-class capability (empty = universal). Surfaced so the
    // shader gallery can show a capability badge; the per-event picker uses
    // the controller's path-aware `availableShaderEffectsForPath` instead,
    // which folds this into ready-made `dimmed`/`dimReason` flags.
    m.insert(QLatin1String("appliesTo"), QVariant::fromValue(effect.appliesTo));
    m.insert(QLatin1String("isUserEffect"), effect.isUserEffect);
    // No `previewPath`: the browser previews live shaders now, and no QML
    // reads the key. The registry-level field stays (a user pack may still
    // ship a preview.png for other consumers); it just isn't ferried to
    // this page's rows.
    QVariantList params;
    params.reserve(effect.parameters.size());
    for (const auto& p : effect.parameters) {
        params.append(parameterInfoToMap(p));
    }
    m.insert(QLatin1String("parameters"), params);
    return m;
}

/// Keys of the map `shaderProfileToMap` produces. `JsonEffectIdKey` is
/// consumed by name in another TU (the group-write comparison in
/// animationspagecontroller_groupwrites.cpp) as well as by QML;
/// `JsonShaderParametersKey` currently has no consumer outside this header
/// and is kept as a named constant for symmetry, so a future consumer cannot
/// introduce a second independently-spelled literal.
inline constexpr QLatin1String JsonEffectIdKey{"effectId"};
inline constexpr QLatin1String JsonShaderParametersKey{"parameters"};

inline QVariantMap shaderProfileToMap(const PhosphorAnimationShaders::ShaderProfile& profile)
{
    QVariantMap m;
    if (profile.effectId)
        m.insert(JsonEffectIdKey, *profile.effectId);
    if (profile.parameters)
        m.insert(JsonShaderParametersKey, *profile.parameters);
    return m;
}

/// Collect every override path strictly DEEPER than @p path
/// (i.e. starting with `<path>.`) that SHADOWS @p path in the resolver's
/// deeper-leaf-wins overlay. Centralises the prefix-match math
/// so shaderOverrideDescendantCount and clearShaderOverrideDescendants
/// share one definition of "shadowing descendant" — the trailing `.`
/// boundary is what excludes both the path itself ("popup") and unrelated
/// names with shared character-prefix ("popups"). Inline in this
/// header so sibling helpers in this namespace can call it without
/// depending on unity-build TU merging.
///
/// Leaf-isolated paths (shaderPathResolvesInIsolation — the drag and
/// tab-switch leaves) are EXCLUDED even though
/// they are prefix-descendants: their resolve() never walks the ancestor,
/// so an override there cannot shadow @p path. Counting one would show a
/// false "shadowing children" warning on the ancestor card, and the
/// paired clear action would silently wipe a setting the user made on the
/// Window Dragging page.
///
/// PARAMS-ONLY overrides are excluded, but for a narrower reason than the
/// leaf-isolated ones above, and the difference matters. An override whose
/// `effectId` is not engaged still inherits the ancestor's PACK, so change
/// the pack above and the descendant follows. Only an ENGAGED effectId
/// pins a pack at the descendant and makes it stop following, which is
/// what "shadows this parent" means to a user reading the warning. Note an
/// engaged effectId counts even when it currently equals what the ancestor
/// resolves: the two are the same pack today and independent tomorrow, and
/// it is that pin the warning exists to surface.
///
/// It does NOT follow that a params-only descendant shadows nothing. On the
/// PARAMETER axis it shadows completely: ShaderProfile::overlay REPLACES
/// the whole parameter map rather than merging keys (pinned by
/// libs/phosphor-animation/tests/test_shaderprofiletree.cpp,
/// testResolveLeafFillsFromCategoryThenBaseline), so a descendant that
/// stores parameters freezes every value at that leaf and stops following
/// the ancestor's parameter edits. That is deliberately not surfaced here,
/// because this walk drives a warning whose one action is a destructive
/// clear: counting the parameter axis would put the ancestor back in the
/// business of offering to delete a tweak the user just made on the
/// descendant, which is the bug this exclusion exists to fix. The
/// descendant's own card is where that state is disclosed, via the shader
/// row's ownership caption, which AnimationProfileEditor derives from its
/// `shaderOwnsPack` and `shaderOwnsParamsOnly` inputs.
///
/// One consequence worth stating, because nothing surfaces it from the
/// ancestor: when the ancestor SWITCHES pack, a params-only descendant keeps
/// its stored map and resolves the new pack carrying the old pack's parameter
/// ids. Parameter ids are per-pack, so the new pack matches none of them and
/// falls back to its own defaults for every value. The descendant is not
/// broken and nothing is lost — clearing its parameters on its own card
/// removes the entry and restores inheritance — but the ancestor's "clear
/// shadowing children" affordance deliberately will not reach it, so that card
/// is the only place it can be resolved.
inline QStringList collectShaderOverrideDescendants(const PhosphorAnimationShaders::ShaderProfileTree& tree,
                                                    const QString& path)
{
    QStringList out;
    if (path.isEmpty())
        return out;
    const QString prefix = path + QLatin1Char('.');
    const QStringList paths = tree.overriddenPaths();
    for (const QString& p : paths) {
        if (!p.startsWith(prefix) || PhosphorAnimationShaders::shaderPathResolvesInIsolation(p))
            continue;
        if (!tree.directOverride(p).effectId.has_value())
            continue;
        out.append(p);
    }
    return out;
}

/// Descendants of @p path that store PARAMETERS but no pack of their own.
///
/// The complement of `collectShaderOverrideDescendants` above, over the same
/// prefix relation and with the same leaf-isolation exclusion. That one answers
/// "who shadows this parent's PACK", which is what the shadowing warning acts
/// on; this one answers "who carries parameter values while still following
/// this parent's pack", which is the population an ancestor pack SWITCH can
/// strand — their stored ids belong to the pack that was replaced.
///
/// Deliberately says nothing about whether those ids are still meaningful. That
/// needs the shader registry to know which ids a pack declares, which lives on
/// the controller, so the staleness test is applied by the caller.
inline QStringList collectParamsOnlyDescendants(const PhosphorAnimationShaders::ShaderProfileTree& tree,
                                                const QString& path)
{
    QStringList out;
    if (path.isEmpty())
        return out;
    const QString prefix = path + QLatin1Char('.');
    const QStringList paths = tree.overriddenPaths();
    for (const QString& p : paths) {
        if (!p.startsWith(prefix) || PhosphorAnimationShaders::shaderPathResolvesInIsolation(p))
            continue;
        const auto stored = tree.directOverride(p);
        // Owning a pack — including the engaged-empty "None" sentinel — puts a
        // path in the OTHER collector's population, not this one.
        if (stored.effectId.has_value())
            continue;
        if (!stored.parameters.has_value() || stored.parameters->isEmpty())
            continue;
        out.append(p);
    }
    return out;
}

/// Title-case a single camelCase segment: "placeIn" → "Place In", "show" →
/// "Show", "popIn" → "Pop In". Splits on lower→upper transitions; trivial
/// for single-word segments.
///
/// Its one caller is `AnimationsPageController::segmentLabel`
/// (animationspagecontroller_paths.cpp), which is itself the single source both
/// label surfaces go through: `eventSections` builds the cached event tree from
/// it and `eventLabel` answers per-path lookups with it, so the two format
/// identically by construction. Inline in this header to keep the format in one
/// place.
inline QString humanizeSegment(const QString& segment)
{
    if (segment.isEmpty())
        return segment;
    QString out;
    out.reserve(segment.size() + 4);
    out.append(segment.front().toUpper());
    for (int i = 1; i < segment.size(); ++i) {
        const QChar prev = segment.at(i - 1);
        const QChar cur = segment.at(i);
        if (cur.isUpper() && prev.isLower()) {
            out.append(QLatin1Char(' '));
        }
        out.append(cur);
    }
    return out;
}

// The per-event override FILES that predate schema v8 carried a top-level
// `name` field naming their path. An entry in the timing tree is named by its
// own key, so the field is stripped on write rather than added — this constant
// is what strips it. JSON keys are QLatin1String per the project's Qt6
// string-literal rule. `inline`
// (external linkage, one definition) so the sibling TUs that consume
// these helpers (animationspagecontroller{,_overrides,_shaders}.cpp)
// all share one definition without relying on unity-build TU merging.
inline constexpr QLatin1String JsonNameKey{"name"};

// ── The serialized `PhosphorAnimation::ProfileTree` shape ────────────────────
// Handled as a raw map rather than through the class. Parsing one needs a
// CurveRegistry, and a Profile stores its curve RESOLVED — so a parse against
// a registry missing the user's curve packs, followed by a re-serialize, drops
// the `curve` key and silently retimes the event. Reading or rewriting one
// path's fields never needs a curve at all, so this layer does not parse.
inline constexpr QLatin1String TreeOverridesKey{"overrides"};
inline constexpr QLatin1String TreePathKey{"path"};
inline constexpr QLatin1String TreeProfileKey{"profile"};
/// `ProfileTree::toJson` always emits this alongside `overrides`, and the v8
/// migration stamps an empty one, so a stored tree can carry it even though
/// nothing on this page ever writes one. Named here so the normalisation below
/// can tell an empty baseline (droppable) from real content (kept).
inline constexpr QLatin1String TreeBaselineKey{"baseline"};

/// The stored profile object for @p path, or an empty object when @p tree
/// carries no override there.
inline QJsonObject treeProfileForPath(const QVariantMap& tree, const QString& path)
{
    const QVariantList overrides = tree.value(TreeOverridesKey).toList();
    for (const QVariant& entry : overrides) {
        const QVariantMap map = entry.toMap();
        if (map.value(TreePathKey).toString() == path) {
            return QJsonObject::fromVariantMap(map.value(TreeProfileKey).toMap());
        }
    }
    return {};
}

/// Every path @p tree carries an entry for, in stored order.
inline QStringList treeOverriddenPaths(const QVariantMap& tree)
{
    QStringList out;
    const QVariantList overrides = tree.value(TreeOverridesKey).toList();
    out.reserve(overrides.size());
    for (const QVariant& entry : overrides) {
        const QString path = entry.toMap().value(TreePathKey).toString();
        if (!path.isEmpty() && !out.contains(path))
            out.append(path);
    }
    return out;
}

inline bool treeHasOverrideForPath(const QVariantMap& tree, const QString& path)
{
    const QVariantList overrides = tree.value(TreeOverridesKey).toList();
    for (const QVariant& entry : overrides) {
        if (entry.toMap().value(TreePathKey).toString() == path) {
            return true;
        }
    }
    return false;
}

/// @p tree with @p path's override replaced by @p profile, or removed when
/// @p profile is empty. A replaced entry keeps its position rather than moving
/// to the end, so rewriting one field does not reshuffle the stored key.
/// A tree carrying no overrides IS the schema default (an empty map).
///
/// Emitting `{"overrides": []}` for it instead leaves the stored key
/// permanently unequal to the baseline, because `Settings::isKeyModified` is a
/// raw QVariant compare. The page then reports unsaved changes forever with
/// nothing actually different, and — since the key now differs from the
/// defaults blob — it joins every settings-profile delta captured afterwards,
/// where activation replaces the recipient's whole timing tree.
inline QVariantMap normalizedMotionTree(QVariantMap tree)
{
    if (!tree.value(TreeOverridesKey).toList().isEmpty()) {
        return tree;
    }
    tree.remove(TreeOverridesKey);
    // A non-empty baseline is real content and stays. Nothing writes one
    // today, but silently erasing a future one would be worse than keeping it.
    if (tree.value(TreeBaselineKey).toMap().isEmpty()) {
        tree.remove(TreeBaselineKey);
    }
    return tree;
}

inline QVariantMap treeWithOverrideForPath(const QVariantMap& tree, const QString& path, const QJsonObject& profile)
{
    QVariantMap out = tree;
    QVariantList overrides = out.value(TreeOverridesKey).toList();
    for (int i = 0; i < overrides.size(); ++i) {
        if (overrides.at(i).toMap().value(TreePathKey).toString() != path) {
            continue;
        }
        if (profile.isEmpty()) {
            overrides.removeAt(i);
        } else {
            QVariantMap entry;
            entry.insert(TreePathKey, path);
            entry.insert(TreeProfileKey, profile.toVariantMap());
            overrides[i] = entry;
        }
        out.insert(TreeOverridesKey, overrides);
        return normalizedMotionTree(out);
    }
    if (profile.isEmpty()) {
        // Nothing to remove — but the tree may have arrived already carrying
        // the residue from a config written before this normalisation.
        return normalizedMotionTree(out);
    }
    QVariantMap entry;
    entry.insert(TreePathKey, path);
    entry.insert(TreeProfileKey, profile.toVariantMap());
    overrides.append(entry);
    out.insert(TreeOverridesKey, overrides);
    return out;
}

/// Convert a `Profile` value to its `toJson()` shape as a QVariantMap.
/// Sparse — only engaged fields appear, matching the wire format.
inline QVariantMap profileToVariantMap(const PhosphorAnimation::Profile& profile)
{
    return profile.toJson().toVariantMap();
}

/// Caps on a QVariantMap arriving from QML at a Q_INVOKABLE boundary.
///
/// Generous by design — well above any legitimate parameter set or preset
/// name — because the point is to bound what reaches disk, not to second-guess
/// a pack's schema. A pack declaring more than a few dozen parameters, or a
/// name longer than a sentence, is already outside what the UI can present.
constexpr int kMaxWrittenMapEntries = 256;
constexpr int kMaxWrittenMapKeyChars = 128;
constexpr int kMaxWrittenMapStringChars = 1024;

/// @p in with over-long keys and over-long string values dropped.
///
/// The effect id at these writers is already gated, and the merged writer
/// allowlists its field KEYS, but nothing bounded the VALUES riding with
/// either. Both are persisted close to verbatim — the shader parameter map is
/// copied through `ShaderProfile::fromJson` with no validation on the way back
/// in — so an over-long value written once stays on disk until some later write
/// happens to rewrite the object.
///
/// It is not merely untidy. Every event's override now shares ONE config key,
/// so an unbounded value is not confined to the path that wrote it: it inflates
/// the blob that every read of the tree copies, on a path that runs per card
/// rebind. There is no per-path skip to contain it and nothing prunes it, so it
/// stays until some later write to that same entry happens to replace it.
///
/// Drops rather than refuses, matching the merged writer's treatment of an
/// unknown field: the rest of the map is still what the user asked for.
inline QVariantMap boundedWrittenMap(const QVariantMap& in, QLatin1String context)
{
    QVariantMap out;
    for (auto it = in.constBegin(); it != in.constEnd(); ++it) {
        if (out.size() >= kMaxWrittenMapEntries) {
            qCWarning(lcConfig) << context << ": dropping entries past the" << kMaxWrittenMapEntries
                                << "entry cap; map carried" << in.size();
            break;
        }
        if (it.key().size() > kMaxWrittenMapKeyChars) {
            qCWarning(lcConfig) << context << ": dropping over-long key of" << it.key().size() << "characters";
            continue;
        }
        if (it.value().metaType().id() == QMetaType::QString
            && it.value().toString().size() > kMaxWrittenMapStringChars) {
            qCWarning(lcConfig) << context << ": dropping over-long value for key" << it.key() << "of"
                                << it.value().toString().size() << "characters";
            continue;
        }
        out.insert(it.key(), it.value());
    }
    return out;
}

/// Normalise a user-authored profile object the way `Profile::fromJson` would,
/// so the disk-first inheritance walk resolves to what the daemon will animate.
///
/// Needed because inheritance resolution reads override files straight off disk
/// rather than through the registry, and the registry path runs every file
/// through `Profile::fromJson`. Reading the raw JSON skips all of that, so a
/// hand-placed `{"duration": "fast"}` or `{"duration": -50}` in the user
/// profiles directory would reach QML verbatim, render as NaN or a negative
/// slider value, and then be propagated to every mirror path on the next edit.
/// `fillLibraryDefaults` cannot help on its own — it only fills keys that are
/// ABSENT, so a present-but-invalid value has to be resolved here.
///
/// Field-by-field equivalence with `Profile::fromJson` is the contract, and the
/// per-field comments below say where each one drops a key versus substitutes
/// the library default, because the two are not interchangeable under
/// `mergeMissingFields`. One deliberate divergence: `curve` is type-checked but
/// not RESOLVED (see the comment at its branch). Every numeric field is
/// range-checked before rounding, which now matches fromJson exactly — the
/// library's `minDistance` and `sequenceMode` branches used to round an
/// unbounded double, which was undefined behaviour, and have since been given
/// the same bound-first treatment.
inline QVariantMap sanitizedProfileMap(const QJsonObject& obj)
{
    using P = PhosphorAnimation::Profile;
    using PhosphorAnimation::SequenceMode;
    if (obj.isEmpty()) {
        return {};
    }

    // Built field by field from the JSON object rather than by pruning
    // `obj.toVariantMap()`, so the type rules are `QJsonValue`'s — the same
    // ones fromJson sees. Pruning a QVariantMap would not be equivalent:
    // `QVariant::toDouble` converts a JSON bool or a numeric string to a
    // number, where `QJsonValue` reports it as not-a-number. `{"duration":
    // "900"}` would then show 900 in the UI while the daemon inherited the
    // value instead.
    //
    // Dropping a key and substituting the library default are also NOT
    // interchangeable, because `mergeMissingFields` only fills keys that are
    // absent: a dropped key lets an ancestor's value through, a substituted one
    // blocks inheritance at this level. fromJson does each in specific cases,
    // so this mirrors which it does where rather than dropping uniformly.
    QVariantMap out;

    // Round only after bounding, so the float-to-int conversion is always in
    // range. `std::isfinite` alone is not enough — 1e300 is finite and
    // `qRound` on it is undefined behaviour.
    //
    // The caller's [lo, hi] is intersected with a band a FULL unit inside the
    // int range, because `qRound(d)` is `int(d + 0.5)` for non-negative d and
    // `int(d - 0.5)` otherwise: a caller passing the full int range as its
    // domain (sequenceMode does) would otherwise still hand `qRound` a value
    // whose conversion is out of range. Same shape of guard as the one in
    // `Profile::fromJson`, which needs only half a unit — this one is a whole
    // unit and so strictly tighter, not identical.
    const auto boundedRound = [](double v, double lo, double hi, std::optional<int>& into) {
        const double safeLo = std::max(lo, double(std::numeric_limits<int>::min()) + 1.0);
        const double safeHi = std::min(hi, double(std::numeric_limits<int>::max()) - 1.0);
        if (std::isfinite(v) && v >= safeLo && v <= safeHi) {
            into = qRound(v);
        }
    };

    if (obj.contains(QLatin1String(P::JsonFieldCurve))) {
        // Type-checked but NOT resolved. Resolving a spec needs a
        // `CurveRegistry`, and the process-wide accessor for one
        // (`PhosphorCurve::defaultRegistry`) lives in the QML module. The
        // settings binary does link that module, so reaching it is possible
        // here — it is avoided because the unit-test targets that compile this
        // header do NOT link it, and because a pure normalisation function
        // should not read process-global state. Validating against a
        // built-ins-only registry instead would silently drop legitimate
        // user-authored curves, which is worse than not validating. An unresolvable spec
        // reaching QML renders as an unrecognised curve, which is visible and
        // harmless. This is the ONE field where this function knowingly diverges
        // from fromJson, and the divergence is not free: fromJson drops a spec it
        // cannot resolve, letting the ancestor's curve through, whereas keeping
        // the key here BLOCKS `mergeMissingFields` at this level and every
        // descendant. So a typo'd spec in global.json shows a curve tree-wide
        // that the daemon will never play, and hides the one it will. Accepted
        // because the alternative — validating against a built-ins-only registry
        // — would drop legitimate user-authored curves, which is the same
        // failure for a much more common input. A non-string value is a different case
        // and IS rejected: it would reach QML as a map or an int where every
        // consumer expects a wire string, and fromJson rejects it too via
        // `toString()` yielding empty.
        const QJsonValue v = obj.value(QLatin1String(P::JsonFieldCurve));
        if (v.isString() && !v.toString().isEmpty()) {
            out.insert(QLatin1String(P::JsonFieldCurve), v.toString());
        }
    }

    // Type-checked exactly as `Profile::fromJson` type-checks: a non-number is
    // NOT coerced to the library default, because that default would pass every
    // range check and land ENGAGED, blocking inheritance where the daemon lets
    // it through. Returns NaN for a non-number so the range checks below reject
    // it on the same branch.
    const auto numeric = [&obj](const char* key) -> double {
        const QJsonValue v = obj.value(QLatin1String(key));
        return v.isDouble() ? v.toDouble() : std::numeric_limits<double>::quiet_NaN();
    };

    if (obj.contains(QLatin1String(P::JsonFieldDuration))) {
        // Rejected → left ABSENT, matching fromJson leaving `p.duration` unset
        // so `effectiveDuration()` substitutes the library default.
        const double raw = numeric(P::JsonFieldDuration);
        if (std::isfinite(raw) && raw > 0.0 && raw <= P::MaxDurationMs) {
            out.insert(QLatin1String(P::JsonFieldDuration), raw);
        }
    }

    if (obj.contains(QLatin1String(P::JsonFieldMinDistance))) {
        // fromJson leaves this unset when negative, so absent is right here too.
        std::optional<int> rounded;
        boundedRound(numeric(P::JsonFieldMinDistance), 0.0, double(P::MaxMinDistancePx), rounded);
        if (rounded.has_value()) {
            out.insert(QLatin1String(P::JsonFieldMinDistance), *rounded);
        }
    }

    if (obj.contains(QLatin1String(P::JsonFieldSequenceMode))) {
        // The one field fromJson SUBSTITUTES rather than leaves unset: an
        // unknown enumerator becomes DefaultSequenceMode, engaged. Mirrored, so
        // an ancestor's mode cannot leak through where the daemon would use the
        // default.
        std::optional<int> rounded;
        boundedRound(numeric(P::JsonFieldSequenceMode), double(std::numeric_limits<int>::min()),
                     double(std::numeric_limits<int>::max()), rounded);
        const bool known =
            rounded.has_value() && (*rounded == int(SequenceMode::AllAtOnce) || *rounded == int(SequenceMode::Cascade));
        out.insert(QLatin1String(P::JsonFieldSequenceMode), known ? *rounded : int(P::DefaultSequenceMode));
    }

    if (obj.contains(QLatin1String(P::JsonFieldStaggerInterval))) {
        std::optional<int> rounded;
        boundedRound(numeric(P::JsonFieldStaggerInterval), 0.0, double(P::MaxStaggerIntervalMs), rounded);
        if (rounded.has_value()) {
            out.insert(QLatin1String(P::JsonFieldStaggerInterval), *rounded);
        }
    }

    if (obj.contains(QLatin1String(P::JsonFieldPresetName))) {
        // `isString()`, for fromJson's reason: `toString()` on a non-string
        // yields an empty QString, and engaged-empty means "explicit empty
        // override" rather than "inherit", so a `"presetName": 42` would
        // otherwise block inheritance with a value nobody wrote.
        const QJsonValue v = obj.value(QLatin1String(P::JsonFieldPresetName));
        if (v.isString()) {
            out.insert(QLatin1String(P::JsonFieldPresetName), v.toString());
        }
    }

    return out;
}

/// Merge fields from @p source into @p target without overwriting keys
/// already present in @p target. Implements ProfileTree-style "deeper
/// path wins" inheritance when called from leaf to root.
inline void mergeMissingFields(QVariantMap& target, const QVariantMap& source)
{
    for (auto it = source.cbegin(); it != source.cend(); ++it) {
        if (!target.contains(it.key())) {
            target.insert(it.key(), it.value());
        }
    }
}

/// Fill any unset fields in @p profile with the `Profile::Default*`
/// library constants so the QML side always reads a populated map.
inline void fillLibraryDefaults(QVariantMap& profile)
{
    using P = PhosphorAnimation::Profile;
    if (!profile.contains(QLatin1String(P::JsonFieldDuration))) {
        profile.insert(QLatin1String(P::JsonFieldDuration), P::DefaultDuration);
    }
    if (!profile.contains(QLatin1String(P::JsonFieldMinDistance))) {
        profile.insert(QLatin1String(P::JsonFieldMinDistance), P::DefaultMinDistance);
    }
    if (!profile.contains(QLatin1String(P::JsonFieldSequenceMode))) {
        profile.insert(QLatin1String(P::JsonFieldSequenceMode), int(P::DefaultSequenceMode));
    }
    if (!profile.contains(QLatin1String(P::JsonFieldStaggerInterval))) {
        profile.insert(QLatin1String(P::JsonFieldStaggerInterval), P::DefaultStaggerInterval);
    }
    // `curve` left unset → fill with the canonical library default
    // (default-constructed `Easing` is OutCubic, matching
    // `Profile::withDefaults()` and `AnimatedValue::defaultFallbackCurve()`).
    // Without this, QML cards crashed with "Cannot read property of
    // undefined" when no parent supplied a curve.
    if (!profile.contains(QLatin1String(P::JsonFieldCurve))) {
        // Cache the canonical default curve string. Constructing a fresh
        // PhosphorAnimation::Easing() per call just to read its toString()
        // is wasteful — the function-local static is initialised once,
        // thread-safely under C++11.
        static const QString kDefaultCurve = PhosphorAnimation::Easing().toString();
        profile.insert(QLatin1String(P::JsonFieldCurve), kDefaultCurve);
    }
}

} // namespace animations_controller_detail
} // namespace PlasmaZones

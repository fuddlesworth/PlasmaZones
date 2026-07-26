// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Shared helpers between animationspagecontroller.cpp and
// animationspagecontroller_shaders.cpp. The two TUs split the same class across
// files, and both need to convert shader-effect / parameter / shader-profile
// values to QVariantMap for QML consumption. Inline definitions here ensure both
// TUs get their own copy without relying on unity-build TU merging for cross-TU
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
    // `previewPath` is resolved to an absolute path by the registry's
    // `parseEffect`, so QML can pass it directly to `Image.source` (with
    // a `file://` scheme prefix). Empty when the pack didn't ship a
    // preview — the page renders a placeholder for that case.
    m.insert(QLatin1String("previewPath"), effect.previewPath);
    QVariantList params;
    params.reserve(effect.parameters.size());
    for (const auto& p : effect.parameters) {
        params.append(parameterInfoToMap(p));
    }
    m.insert(QLatin1String("parameters"), params);
    return m;
}

inline QVariantMap shaderProfileToMap(const PhosphorAnimationShaders::ShaderProfile& profile)
{
    QVariantMap m;
    if (profile.effectId)
        m.insert(QLatin1String("effectId"), *profile.effectId);
    if (profile.parameters)
        m.insert(QLatin1String("parameters"), *profile.parameters);
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
/// Leaf-isolated paths (shaderPathResolvesInIsolation, today the
/// interactive-drag leaf window.movement.move) are EXCLUDED even though
/// they are prefix-descendants: their resolve() never walks the ancestor,
/// so an override there cannot shadow @p path. Counting one would show a
/// false "shadowing children" warning on the ancestor card, and the
/// paired clear action would silently wipe a setting the user made on the
/// Window Dragging page.
inline QStringList collectShaderOverrideDescendants(const PhosphorAnimationShaders::ShaderProfileTree& tree,
                                                    const QString& path)
{
    QStringList out;
    if (path.isEmpty())
        return out;
    const QString prefix = path + QLatin1Char('.');
    const QStringList paths = tree.overriddenPaths();
    for (const QString& p : paths) {
        if (p.startsWith(prefix) && !PhosphorAnimationShaders::shaderPathResolvesInIsolation(p))
            out.append(p);
    }
    return out;
}

/// Title-case a single camelCase segment: "snapIn" → "Snap In", "show" →
/// "Show", "popIn" → "Pop In". Splits on lower→upper transitions; trivial
/// for single-word segments. Shared by animationspagecontroller.cpp's
/// `eventSections` (cached event tree) and animationspagecontroller_paths.cpp's
/// `eventLabel` (per-path lookup) so the two surfaces format identically.
/// Inline in this header so the label format stays in one place —
/// diverging here would silently break the path-vs-tree label match
/// downstream consumers rely on.
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

// ProfileLoader's envelope helper reads the top-level `name` field to
// assign the registry path (and strips it from the returned root). We
// add it on write so the file is recognised. JSON keys are
// QLatin1String per the project's Qt6 string-literal rule. `inline`
// (external linkage, one definition) so the sibling TUs that consume
// these helpers (animationspagecontroller{,_overrides,_shaders}.cpp)
// all share one definition without relying on unity-build TU merging.
inline constexpr QLatin1String JsonNameKey{"name"};

/// Convert a `Profile` value to its `toJson()` shape as a QVariantMap.
/// Sparse — only engaged fields appear, matching the wire format.
inline QVariantMap profileToVariantMap(const PhosphorAnimation::Profile& profile)
{
    return profile.toJson().toVariantMap();
}

/// Ceiling on one profile file read. Derived from the shared cap so it
/// cannot drift from the snapshot, preset, and set-file readers.
constexpr qint64 kMaxProfileReadBytes = animfileutil::kMaxJsonFileBytes;

/// Read the JSON object at @p path. Returns an empty object on missing
/// file / parse error / non-object root. The `name` field is stripped so
/// the returned map matches the QML-facing Profile shape. Parse errors
/// are logged so silent corruption surfaces in journalctl.
inline QJsonObject readProfileJson(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists())
        return {};
    // A regular file under the cap, or nothing: this runs per card rebind on the
    // GUI thread, and the directory is a filesystem boundary a user can
    // hand-place anything at.
    if (!info.isFile() || info.size() > kMaxProfileReadBytes) {
        qCWarning(lcConfig) << "AnimationsPageController: skipping" << path
                            << "— not a regular file, or over the size cap";
        return {};
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcConfig) << "AnimationsPageController: cannot open profile" << path;
        return {};
    }
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(lcConfig) << "AnimationsPageController: failed to parse" << path << ":" << err.errorString();
        return {};
    }
    QJsonObject obj = doc.object();
    obj.remove(JsonNameKey);
    return obj;
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
/// `mergeMissingFields`. The single deliberate divergence is that `curve` is
/// type-checked but not RESOLVED; see the comment at its branch.
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
    // number, where `QJsonValue::toDouble(default)` hands back the default for
    // any non-double. `{"duration": "900"}` would then show 900 in the UI while
    // the daemon animated at the library default.
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
    const auto boundedRound = [](double v, double lo, double hi, std::optional<int>& into) {
        if (std::isfinite(v) && v >= lo && v <= hi) {
            into = qRound(v);
        }
    };

    if (obj.contains(QLatin1String(P::JsonFieldCurve))) {
        // Type-checked but NOT resolved. Resolving a curve spec needs a
        // `CurveRegistry`, and the process-wide accessor for one
        // (`PhosphorCurve::defaultRegistry`) lives in the QML module rather
        // than the core animation library, so this translation unit cannot
        // reach it; validating against a built-ins-only registry instead would
        // silently drop legitimate user-authored curves. An unresolvable spec
        // reaching QML renders as an unrecognised curve, which is visible and
        // harmless. A non-string one would reach QML as a map or an int where
        // every consumer expects a wire string, so that IS rejected here —
        // fromJson rejects it too, via `toString()` yielding empty.
        const QJsonValue v = obj.value(QLatin1String(P::JsonFieldCurve));
        if (v.isString() && !v.toString().isEmpty()) {
            out.insert(QLatin1String(P::JsonFieldCurve), v.toString());
        }
    }

    if (obj.contains(QLatin1String(P::JsonFieldDuration))) {
        // Rejected → left ABSENT, matching fromJson leaving `p.duration` unset
        // so `effectiveDuration()` substitutes the library default.
        const double raw = obj.value(QLatin1String(P::JsonFieldDuration)).toDouble(P::DefaultDuration);
        if (std::isfinite(raw) && raw > 0.0 && raw <= P::MaxDurationMs) {
            out.insert(QLatin1String(P::JsonFieldDuration), raw);
        }
    }

    if (obj.contains(QLatin1String(P::JsonFieldMinDistance))) {
        // fromJson leaves this unset when negative, so absent is right here too.
        std::optional<int> rounded;
        boundedRound(obj.value(QLatin1String(P::JsonFieldMinDistance)).toDouble(P::DefaultMinDistance), 0.0,
                     double(std::numeric_limits<int>::max()), rounded);
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
        boundedRound(obj.value(QLatin1String(P::JsonFieldSequenceMode)).toDouble(int(P::DefaultSequenceMode)),
                     double(std::numeric_limits<int>::min()), double(std::numeric_limits<int>::max()), rounded);
        const bool known =
            rounded.has_value() && (*rounded == int(SequenceMode::AllAtOnce) || *rounded == int(SequenceMode::Cascade));
        out.insert(QLatin1String(P::JsonFieldSequenceMode), known ? *rounded : int(P::DefaultSequenceMode));
    }

    if (obj.contains(QLatin1String(P::JsonFieldStaggerInterval))) {
        std::optional<int> rounded;
        boundedRound(obj.value(QLatin1String(P::JsonFieldStaggerInterval)).toDouble(P::DefaultStaggerInterval), 0.0,
                     double(P::MaxStaggerIntervalMs), rounded);
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

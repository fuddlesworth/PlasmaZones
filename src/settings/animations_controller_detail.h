// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Shared helpers between animationspagecontroller.cpp and
// animationspagecontroller_shaders.cpp. The two TUs split the same class
// across files to stay under the 1000-line guideline; both need to convert
// shader-effect / parameter / shader-profile values to QVariantMap for
// QML consumption. Inline definitions here ensure both TUs get their own
// copy without relying on unity-build TU merging for cross-TU linkage.

#include "../core/logging.h"
#include "animationfileutils.h"

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

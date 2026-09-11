// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file settingsschema_shadertrees.cpp
 * @brief Persistence sanitizers for the animation-shader and decoration trees.
 *
 * The zone-overlay twin lives in settingsschema_overlayshaders.cpp; all three
 * compose the same bounds from settingsschema_shaderbounds.h.
 *
 * ## What these add
 *
 * The four shader-assignment trees had arrived at three different answers to
 * the same question:
 *
 *   • The overlay tree bounds at the SCHEMA, so its bounds run on read as well
 *     as write.
 *   • The decoration tree and the motion tree bound in their typed SETTERS
 *     (`boundedIdList` / `boundedParameterMap` / `boundedProfileMap` in
 *     settings/profiletrees.cpp), which covers every writer that goes through
 *     the setter — the pages, a set import, a settings profile, the D-Bus
 *     setter — and nothing that does not.
 *   • The animation shader tree bounds SIZE nowhere at all. Its setter prunes
 *     unsupported paths, which is a different concern and leaves an unbounded
 *     string or an enormous parameter map free to reach the key.
 *
 * A setter-side bound cannot see a hand-edited `config.json`, because reading
 * one does not call the setter. A schema sanitizer runs on both directions, so
 * these close that side for both trees and close the animation tree's missing
 * size bounds outright.
 *
 * The caps here deliberately MATCH the setters' rather than tightening them. A
 * sanitizer that trimmed harder than the setter would rewrite a tree the setter
 * had just accepted, and the value would vanish on the next read instead of at
 * the write that produced it.
 *
 * ## What cannot be covered here
 *
 * The two motion keys, `Profile` and `MotionProfileTree`:
 * `PhosphorAnimation::Profile::fromJson` takes a `CurveRegistry` to re-resolve
 * its curve, so round-tripping them at this layer would need a registry the
 * schema must not own. Their setters keep that job.
 * `ShaderProfileTree` and `DecorationProfileTree` have registry-free
 * `fromJson`/`toJson`, which is the whole reason they can be covered and the
 * motion pair cannot.
 *
 * Neither sanitizer judges CONTENT. An unknown event or surface path is inert
 * at resolve time, and erasing one would throw away an assignment written by a
 * newer build the user is about to go back to — the same argument the overlay
 * twin makes for not judging a shaderId against the installed packs. Path
 * pruning against the supported namespace stays where it is, in the setter and
 * the getter, which is where a registry-free answer is actually available.
 */
#include "settingsschema_shaderbounds.h"
#include "settingsschema.h"

#include <PhosphorAnimation/ShaderProfile.h>
#include <PhosphorAnimation/ShaderProfileTree.h>
#include <PhosphorSurface/DecorationProfile.h>
#include <PhosphorSurface/DecorationProfileTree.h>

#include <QJsonObject>
#include <QMetaType>

namespace PlasmaZones {

namespace {

namespace PAS = PhosphorAnimationShaders;
namespace PSS = PhosphorSurfaceShaders;

/// Bound one animation-shader assignment.
///
/// Each field independently, like the overlay twin: dropping the surviving
/// parameters along with an over-long effect id would be a second, coupled rule
/// for no gain. Engagement is PRESERVED — an engaged-but-empty optional is a
/// deliberate statement ("explicitly no shader", "explicitly no preset") that
/// stops a child inheriting an ancestor's, so a bound that disengaged a field
/// would silently turn that statement back into "inherit".
PAS::ShaderProfile boundedShaderProfile(const PAS::ShaderProfile& profile)
{
    PAS::ShaderProfile out;
    if (profile.effectId && profile.effectId->size() <= kMaxShaderStringChars) {
        out.effectId = profile.effectId;
    }
    if (profile.presetId && profile.presetId->size() <= kMaxShaderStringChars) {
        out.presetId = profile.presetId;
    }
    if (profile.parameters) {
        out.parameters = boundedShaderParams(*profile.parameters);
    }
    return out;
}

/// Bound one decoration assignment.
///
/// `parameters` is `{ packId -> { paramId -> value } }`, so unlike the overlay
/// and animation twins a MAP value is legitimate at the top level and only the
/// inner level is scalar-only. Bounding the outer level with the scalar rule
/// would delete every pack's parameters wholesale.
PSS::DecorationProfile boundedDecorationProfile(const PSS::DecorationProfile& profile)
{
    PSS::DecorationProfile out;
    if (profile.chain) {
        out.chain = boundedIdList(*profile.chain, kMaxChainPacks);
    }
    if (profile.disabledPacks) {
        out.disabledPacks = boundedIdList(*profile.disabledPacks, kMaxChainPacks);
    }
    if (profile.presetIds) {
        out.presetIds = boundedIdMap(*profile.presetIds, kMaxChainPacks);
    }
    if (profile.parameters) {
        QVariantMap params;
        const QVariantMap in = *profile.parameters;
        for (auto it = in.cbegin(); it != in.cend(); ++it) {
            if (params.size() >= kMaxChainPacks) {
                break;
            }
            if (it.key().size() > kMaxShaderStringChars) {
                continue;
            }
            // A non-map value here is a hand-edit or a foreign writer: this
            // level is keyed by pack id and every value is that pack's
            // parameter map.
            if (it.value().typeId() != QMetaType::QVariantMap) {
                continue;
            }
            params.insert(it.key(), boundedShaderParams(it.value().toMap()));
        }
        out.parameters = params;
    }
    return out;
}

} // namespace

QVariant sanitizeShaderProfileTree(const QVariant& v)
{
    const PAS::ShaderProfileTree in = PAS::ShaderProfileTree::fromJson(QJsonObject::fromVariantMap(v.toMap()));
    PAS::ShaderProfileTree out;
    out.setBaseline(boundedShaderProfile(in.baseline()));
    int kept = 0;
    const QStringList paths = in.overriddenPaths();
    for (const QString& path : paths) {
        if (kept >= kMaxShaderOverrides) {
            break;
        }
        if (path.isEmpty() || path.size() > kMaxShaderStringChars) {
            continue;
        }
        out.setOverride(path, boundedShaderProfile(in.directOverride(path)));
        ++kept;
    }
    return QVariant(out.toJson().toVariantMap());
}

QVariant sanitizeDecorationProfileTree(const QVariant& v)
{
    const PSS::DecorationProfileTree in = PSS::DecorationProfileTree::fromJson(QJsonObject::fromVariantMap(v.toMap()));
    PSS::DecorationProfileTree out;
    out.setBaseline(boundedDecorationProfile(in.baseline()));
    int kept = 0;
    const QStringList paths = in.overriddenPaths();
    for (const QString& path : paths) {
        if (kept >= kMaxShaderOverrides) {
            break;
        }
        if (path.isEmpty() || path.size() > kMaxShaderStringChars) {
            continue;
        }
        out.setOverride(path, boundedDecorationProfile(in.directOverride(path)));
        ++kept;
    }
    return QVariant(out.toJson().toVariantMap());
}

} // namespace PlasmaZones

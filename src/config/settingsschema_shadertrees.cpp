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
 * The numeric caps here deliberately MATCH the setters' rather than tightening
 * them (64 entries, 1024 characters, on both sides). A sanitizer that trimmed
 * harder than the setter would rewrite a tree the setter had just accepted, and
 * the value would vanish on the next read instead of at the write that produced
 * it.
 *
 * The SHAPE rules are not a mirror, and the asymmetry is deliberate rather than
 * an oversight. On the decoration tree this sanitizer additionally drops a
 * duplicate chain entry and a non-map value at the pack-id level of
 * `parameters`, neither of which the setter judges. Both are values that can
 * only misbehave: the compositor folds the chain per entry, so a repeated pack
 * id costs a draw and a buffer slot for nothing, and a scalar where a pack's
 * parameter map belongs resolves to no parameters at all. Dropping them is
 * therefore not "trimming harder than the setter accepted" in the sense the
 * paragraph above warns about — nothing a user can see is being taken away.
 *
 * ## What cannot be covered here
 *
 * The two motion keys, `Profile` and `MotionProfileTree`, are NOT covered, and
 * the reason is not the one previously given here. That reason —
 * `PhosphorAnimation::Profile::fromJson` needing a `CurveRegistry` — does not
 * apply: nothing on the motion keys' path parses a `Profile` at all. The getter
 * hands back the raw `QVariantMap`, and the setter stores it verbatim precisely
 * so it never has to re-resolve a curve. Its own `boundedProfileMap` is a
 * known-field whitelist plus a string cap over that raw map, with no registry
 * involved, and could be registered here as-is.
 *
 * What is actually true is that the motion keys are bounded on the WRITE path
 * only, so a hand-edited `config.json` or an `importFromJson` blob reaches
 * `motionProfileTree()` unbounded — the same door this file closed for the other
 * two trees. Closing it is a follow-up, not an impossibility; the honest
 * statement is that nobody has done it yet.
 *
 * ## Content versus size
 *
 * Neither sanitizer judges content in order to REJECT an unknown key: an unknown
 * event path is inert at resolve time, and erasing one would throw away an
 * assignment written by a newer build the user is about to go back to.
 *
 * The decoration half is an exception worth stating, because it is not obvious
 * from this file. `sanitizeDecorationProfileTree` parses through
 * `DecorationProfileTree::fromJson`, which DOES drop an override whose surface
 * path this build does not support. Since a validator runs on every read as well
 * as every write, that drop now happens on each read of the key and on every
 * settings-profile export, not only when the user next edits decorations. A
 * decoration setting made by a newer build therefore does not survive a
 * downgrade. That is the long-standing behaviour of the decoration tree's parser
 * rather than something introduced here, and it matches the project's
 * no-ad-hoc-back-compat rule, but it is a real behaviour change in WHEN the drop
 * lands and it should not be discovered by accident.
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
/// Bound one animation assignment.
///
/// COPIES the input and bounds in place, rather than building a fresh profile and
/// moving the known fields across. That choice is load-bearing: with the
/// fresh-object form, the next field added to `ShaderProfile` is silently dropped
/// on every read AND every write unless whoever adds it remembers this function
/// and its decoration twin. This very file is the proof — `presetId` had to be
/// threaded through both bounders by hand when it was added, and only the overlay
/// type has a test pinning that a field survives the rebuild. Copying makes the
/// failure mode "new field is unbounded" instead of "new field disappears", which
/// is the one a reviewer notices.
PAS::ShaderProfile boundedShaderProfile(const PAS::ShaderProfile& profile)
{
    PAS::ShaderProfile out = profile;
    if (out.effectId && out.effectId->size() > kMaxShaderStringChars) {
        out.effectId.reset();
    }
    if (out.presetId && out.presetId->size() > kMaxShaderStringChars) {
        out.presetId.reset();
    }
    if (out.parameters) {
        out.parameters = boundedShaderParams(*out.parameters);
    }
    return out;
}

/// Bound one decoration assignment.
///
/// `parameters` is `{ packId -> { paramId -> value } }`, so unlike the overlay
/// and animation twins a MAP value is legitimate at the top level and only the
/// inner level is scalar-only. Bounding the outer level with the scalar rule
/// would delete every pack's parameters wholesale.
///
/// Copies and bounds in place, for the reason given on `boundedShaderProfile`.
PSS::DecorationProfile boundedDecorationProfile(const PSS::DecorationProfile& profile)
{
    PSS::DecorationProfile out = profile;
    if (out.chain) {
        // The only bound where duplicates matter: the compositor folds per
        // entry, so a repeated pack id costs a draw and a buffer slot each.
        out.chain = boundedIdList(*out.chain, kMaxChainPacks, IdListDuplicates::Drop);
    }
    if (out.disabledPacks) {
        out.disabledPacks = boundedIdList(*out.disabledPacks, kMaxChainPacks, IdListDuplicates::Drop);
    }
    if (out.presetIds) {
        out.presetIds = boundedIdMap(*out.presetIds, kMaxChainPacks);
    }
    if (out.parameters) {
        QVariantMap params;
        const QVariantMap in = *out.parameters;
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

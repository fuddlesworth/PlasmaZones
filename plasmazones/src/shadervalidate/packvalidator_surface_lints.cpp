// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The surface arm's METADATA LINT COLLECTOR, split out of packvalidator_surface.cpp when
// that file reached the 1150-line ceiling and a new lint had to go in.
//
// The cut is a concern boundary rather than a line count. This file decides what is WRONG
// with a pack's metadata.json and returns it as strings; it reads nothing but the parsed
// metadata object, the parsed effect and the pack directory, and it writes to no stream and
// counts no errors. packvalidator_surface.cpp keeps the orchestration: reading and parsing
// the file, the path-confinement exits that must refuse a pack before anything opens it,
// the report header, flushing these strings, the NOTES (which print to the stream
// rather than joining this list, so they belong with the printer), the preset lints, and
// the three stage bakes that share an include path set.
//
// The buffer-chain lints have since moved on to packvalidator_surface_buffer_lints.cpp, which
// this file appends. An earlier version of this note rejected that cut on the grounds that it
// would separate the buffer-pass LINTS from the buffer-pass BAKE — but the bake lives in
// packvalidator_surface.cpp, so the objection was about a three-way split and never applied to
// a seam inside the lint collector. See that file's header for the seam.

#include "packvalidators_surface_lints.h"

#include "packvalidatorcommon.h"

#include <PhosphorShaders/CustomParamsKey.h>
#include <PhosphorShaders/ShaderParamPreamble.h>
#include <PhosphorSurface/DecorationProfile.h>
#include <PhosphorSurface/SurfaceShaderContract.h>
#include <PhosphorSurface/SurfaceShaderEffect.h>
#include <PhosphorSurface/SurfaceShaderRegistry.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QString>
#include <QStringList>

#include <algorithm>

using PhosphorSurfaceShaders::SurfaceShaderEffect;
using PhosphorSurfaceShaders::SurfaceShaderRegistry;

namespace PlasmaZones::ShaderValidate {

QStringList surfaceMetadataLints(const QJsonObject& meta, const SurfaceShaderEffect& eff, const QString& packDir)
{
    static const QStringList kSurfaceParamTypes = {QStringLiteral("float"), QStringLiteral("int"),
                                                   QStringLiteral("bool"), QStringLiteral("color")};
    QStringList lints;
    // Same blind spot as `textures` below, and quieter, because a pack with no
    // parameters is legitimate: a `parameters` of the wrong shape loads as zero
    // parameters, every per-parameter lint iterates nothing, and the pack passes
    // with its entire control set discarded.
    const QJsonValue parametersValue = meta.value(QLatin1String("parameters"));
    if (!parametersValue.isUndefined() && !parametersValue.isNull() && !parametersValue.isArray()) {
        lints << QStringLiteral("`parameters` is not an array (every parameter is ignored at load)");
    }
    for (const SurfaceShaderEffect::ParameterInfo& p : eff.parameters) {
        if (!kSurfaceParamTypes.contains(p.type)) {
            lints << QStringLiteral("unknown param type '%1' for '%2' (surface params are float/int/bool/color)")
                         .arg(p.type, p.id);
        }
        if (!PhosphorShaders::isValidParamId(p.id)) {
            lints
                << QStringLiteral("invalid parameter id '%1' (not a GLSL identifier; skipped, no p_ define)").arg(p.id);
        }
    }
    // Slot budget, mirroring the animation arm. translateSurfaceParams drops
    // every scalar past kMaxParameterSlots and every colour past
    // kMaxCustomColors at load with a journal warning, and buildParamPreamble
    // emits no p_<id> for them, so a pack that READS such a parameter fails its
    // bake with a bare undeclared-identifier error the did-you-mean hint cannot
    // explain (the name IS declared), and one that does not read it ships green.
    {
        int scalarParams = 0;
        int colorParams = 0;
        for (const SurfaceShaderEffect::ParameterInfo& p : eff.parameters) {
            if (p.type == QLatin1String("color")) {
                ++colorParams;
            } else {
                // float / int / bool all take a scalar sub-slot.
                ++scalarParams;
            }
        }
        const int scalarBudget = PhosphorSurfaceShaders::SurfaceShaderContract::kMaxParameterSlots;
        const int colorBudget = PhosphorSurfaceShaders::SurfaceShaderContract::kMaxCustomColors;
        if (scalarParams > scalarBudget) {
            lints << QStringLiteral(
                         "too many scalar params: %1 declared, budget is %2 (the surplus get no p_<id> "
                         "and are dropped at load)")
                         .arg(QString::number(scalarParams), QString::number(scalarBudget));
        }
        if (colorParams > colorBudget) {
            lints << QStringLiteral(
                         "too many color params: %1 declared, budget is %2 (the surplus get no p_<id> "
                         "and are dropped at load)")
                         .arg(QString::number(colorParams), QString::number(colorBudget));
        }
    }
    // A declared default / min / max is never checked against the parameter's own
    // type, in fromJson or here, so `"type": "float", "default": "wide"` ships
    // green and renders 0.0 because the conversion fails silently. Same for a
    // default outside the min/max the pack itself declares, which the UI then
    // clamps to something the author never chose.
    // MIRRORS fromJson's DUPLICATE DROP, the way the radius-slot scan further down already does
    // and for the same reason: fromJson keeps the FIRST declaration of an id and discards every
    // later one, so a lint over the raw array can otherwise describe a declaration that never
    // loads. It did — an id declared cleanly and then re-declared with min above max drew both
    // range lines, and both were false of the pack as it loads.
    QSet<QString> seenRawParamIds;
    for (const QJsonValue& v : meta.value(QLatin1String("parameters")).toArray()) {
        const QJsonObject po = v.toObject();
        const QString pid = po.value(QLatin1String("id")).toString();
        const QString ptype = po.value(QLatin1String("type")).toString();
        if (pid.isEmpty()) {
            continue; // already linted above
        }
        // CLAIM THE ID BEFORE THE TYPE GATE. fromJson keeps the FIRST declaration of an id
        // whatever its type, so a later one is discarded even when the first names a type this
        // family does not have — and registering only type-valid ids would let that later one
        // through, which is the very thing this set exists to stop.
        const bool firstForId = !seenRawParamIds.contains(pid);
        seenRawParamIds.insert(pid);
        // `ptype.isEmpty()` is not tested separately: kSurfaceParamTypes cannot contain the empty
        // string, so the vocabulary check subsumes it, and the unknown-type arm above reports it.
        if (!firstForId || !kSurfaceParamTypes.contains(ptype)) {
            continue; // reported above, by the duplicate arm or the unknown-type arm
        }
        const QJsonValue def = po.value(QLatin1String("default"));
        if (ptype == QLatin1String("bool")) {
            if (!def.isUndefined() && !def.isBool()) {
                lints << QStringLiteral("parameter '%1' is bool but its default is not true or false").arg(pid);
            }
            continue;
        }
        // No `image` arm: the surface family has no such type, so one lands on the unknown-type
        // lint above and is skipped here rather than drawing a second claim about a contract
        // that does not exist. The animation family is where an image parameter belongs.
        if (ptype == QLatin1String("color")) {
            if (!def.isUndefined() && !def.isString()) {
                lints << QStringLiteral("parameter '%1' is %2 but its default is not a string").arg(pid, ptype);
            }
            continue;
        }
        // float / int from here.
        if (!def.isUndefined() && !def.isDouble()) {
            lints << QStringLiteral("parameter '%1' is %2 but its default is not a number").arg(pid, ptype);
            continue;
        }
        const QJsonValue lo = po.value(QLatin1String("min"));
        const QJsonValue hi = po.value(QLatin1String("max"));
        if (!lo.isUndefined() && !lo.isDouble()) {
            lints << QStringLiteral("parameter '%1' has a non-numeric min").arg(pid);
        }
        if (!hi.isUndefined() && !hi.isDouble()) {
            lints << QStringLiteral("parameter '%1' has a non-numeric max").arg(pid);
        }
        if (lo.isDouble() && hi.isDouble() && lo.toDouble() > hi.toDouble()) {
            lints << QStringLiteral("parameter '%1' has min %2 above max %3")
                         .arg(pid)
                         .arg(lo.toDouble())
                         .arg(hi.toDouble());
        }
        if (def.isDouble() && lo.isDouble() && hi.isDouble()
            && (def.toDouble() < lo.toDouble() || def.toDouble() > hi.toDouble())) {
            lints << QStringLiteral("parameter '%1' default %2 is outside its own declared range [%3, %4]")
                         .arg(pid)
                         .arg(def.toDouble())
                         .arg(lo.toDouble())
                         .arg(hi.toDouble());
        }
    }
    // Duplicate ids are linted over the RAW array rather than eff.parameters,
    // because fromJson drops the second declaration with only a qCWarning. A
    // pack that declares one id twice therefore lints clean against the parsed
    // struct and ships with one of the two silently gone. The overlay and
    // pointer arms already walk the raw array for this, each with its own seen-id
    // set, so this one was the exception (an earlier version credited that rule to
    // packvalidatorcommon, which does not state it).
    {
        const QJsonArray rawParams = meta.value(QLatin1String("parameters")).toArray();
        QSet<QString> seenParamIds;
        for (const QJsonValue& v : rawParams) {
            const QString pid = v.toObject().value(QLatin1String("id")).toString();
            if (pid.isEmpty()) {
                continue;
            }
            if (seenParamIds.contains(pid)) {
                lints << QStringLiteral("duplicate parameter id '%1' (only the first declaration survives load)")
                             .arg(pid);
            } else {
                seenParamIds.insert(pid);
            }
        }
    }
    // A `textures` that is not an array lints clean without this: toArray() answers empty for a
    // string, number or object, the loop never runs, and the pack passes with its list ignored.
    const QJsonValue texturesValue = meta.value(QLatin1String("textures"));
    if (!texturesValue.isUndefined() && !texturesValue.isNull() && !texturesValue.isArray()) {
        lints << QStringLiteral("`textures` is not an array (the whole list is ignored at load)");
    }
    const QJsonArray declaredTextures = texturesValue.toArray();
    // Counted in LOADABLE entries, not declarations. The loader appends only path-bearing entries,
    // so a pack with three real textures and two blank rows overflows nothing — and reporting
    // "5 declared, cap is 3" there sent the author to delete a texture that was loading fine.
    const auto loadable = std::count_if(declaredTextures.begin(), declaredTextures.end(), [](const QJsonValue& t) {
        return !t.toObject().value(QLatin1String("path")).toString().isEmpty();
    });
    if (loadable > PhosphorSurfaceShaders::SurfaceShaderContract::kMaxUserTextureSlots) {
        lints << QStringLiteral("too many textures: %1 with a path, cap is %2 (the loader keeps the first %2)")
                     .arg(static_cast<int>(loadable))
                     .arg(PhosphorSurfaceShaders::SurfaceShaderContract::kMaxUserTextureSlots);
    }
    qsizetype keptTextureSlots = 0;
    for (const QJsonValue& v : declaredTextures) {
        // A non-object entry (a bare path string, the natural mistake) reported
        // as "empty `path`", which describes an object that has the key and left
        // it blank. The author wrote no object at all, so they went looking for
        // a key that is not in their file.
        if (!v.isObject()) {
            lints << QStringLiteral(
                "texture entry is not an object (dropped at load, which also shifts every later "
                "texture down one sampler slot). An entry is `{\"path\": \"...\"}`, not a bare path");
            continue;
        }
        const QString texPath = v.toObject().value(QLatin1String("path")).toString();
        if (texPath.isEmpty()) {
            lints << QStringLiteral(
                "texture entry with empty `path` (dropped at load, which also shifts "
                "every later texture down one sampler slot)");
            continue;
        }
        // BOUNDED TO WHAT THE LOADER KEEPS, like every other per-entry arm in this file. fromJson
        // appends only path-bearing entries and breaks once the cap is full, WITHOUT reading the
        // surplus entry's path or wrap — so every message below would otherwise name a load
        // behaviour that does not happen for one. Counted in SLOTS TAKEN rather than by index,
        // because an empty or non-object entry is consumed without taking a slot, so
        // [empty, t0, t1, t2] genuinely loads all three.
        if (keptTextureSlots++ >= PhosphorSurfaceShaders::SurfaceShaderContract::kMaxUserTextureSlots) {
            continue;
        }
        // Same confinement and existence check the animation arm applies,
        // and for the same reason: the registry clears a rejected texture
        // path and the sampler falls back to transparent, so a typo ships
        // green and fails at first paint.
        const auto confined = confinedPackPath(packDir, texPath);
        if (!confined) {
            lints << QStringLiteral(
                         "texture path escapes the pack directory: %1 (rejected at load, sampler reads "
                         "transparent)")
                         .arg(texPath);
        } else if (!QFile::exists(*confined)) {
            lints << QStringLiteral("texture missing: %1 (sampler reads transparent at load)").arg(texPath);
        } else if (!QFileInfo(*confined).isFile()) {
            // exists() answers true for a DIRECTORY, so a path naming one passed both arms and
            // the whole pack reported OK. The runtime accepts it too (its only test is
            // confinement), so the failure lands at first paint with nothing having warned.
            lints << QStringLiteral("texture path is not a file: %1 (sampler reads transparent at load)").arg(texPath);
        }
        // Wrap vocabulary lint — read RAW metadata: SurfaceShaderEffect::fromJson
        // silently clears an invalid wrap to clamp, so a lint over the parsed
        // eff.textures could never surface an author's typo. Mirror fromJson's
        // {clamp,repeat,mirror} guard so a bad wrap fails the validator instead.
        const QJsonValue wrapValue = v.toObject().value(QLatin1String("wrap"));
        if (!wrapValue.isUndefined() && !wrapValue.isNull() && !wrapValue.isString()) {
            // A non-string wrap reached the user with no diagnostic ANYWHERE, the third key in this
            // file with that shape: fromJson stores toString() (empty for a number, bool, array or
            // object) and then guards its own warning on !isEmpty(), so the sampler silently clamps
            // and nothing says why. The same !isEmpty() gate exists in the animation and pointer
            // arms, so closing it completely is four-sided; this is the surface side.
            lints << QStringLiteral(
                "texture wrap is not a string, which is ignored at load (the sampler "
                "clamps)");
            continue;
        }
        const QString wrap = wrapValue.toString();
        if (!wrap.isEmpty() && !PhosphorSurfaceShaders::SurfaceShaderContract::isValidWrapToken(wrap)) {
            lints
                << QStringLiteral("texture wrap not in {clamp,repeat,mirror}: %1 (cleared to clamp at load)").arg(wrap);
        }
    }
    // The multipass lints below are gated on the separate "multipass" key, and so
    // is the RUNTIME: the registry clears every buffer pass of a pack that
    // declares bufferShaders without it. Such a pack would otherwise validate
    // here as a clean single-pass pack and then render with no chain at all,
    // which is the loudest possible difference between what the validator says
    // and what the user sees. The converse (multipass true, no bufferShaders)
    // already fails closed further down.
    // Boolean pack keys are read with toBool(default), which answers the DEFAULT
    // for anything that is not a bool rather than complaining. So
    // `"halfFloatBuffers": "false"` loads as TRUE, the exact opposite of what the
    // author wrote, and `"multipass": 1` leaves the pack single-pass. The JSON
    // schema catches this for the bundled packs only; a user pack never meets it.
    {
        static const QStringList kBoolKeys = {
            QStringLiteral("multipass"),        QStringLiteral("needsBackdrop"),
            QStringLiteral("animated"),         QStringLiteral("audio"),
            QStringLiteral("bufferFeedback"),   QStringLiteral("depthBuffer"),
            QStringLiteral("halfFloatBuffers"), QStringLiteral("interiorOpaque"),
            QStringLiteral("providesBorder"),   QStringLiteral("providesOpacityTint")};
        for (const QString& k : kBoolKeys) {
            const QJsonValue v = meta.value(k);
            if (!v.isUndefined() && !v.isNull() && !v.isBool()) {
                lints << QStringLiteral(
                             "\"%1\" must be true or false; any other value is ignored and the default "
                             "is used instead")
                             .arg(k);
            }
        }
    }
    // paddingParam names the parameter whose value becomes the pack's outer
    // padding request. paddingRequest answers 0 for a name that resolves to no
    // numeric parameter, so a typo does not fail anything: the pack simply asks
    // for no margin and clips at the frame edge, which looks like a shader bug.
    {
        // NON-STRING FIRST, the same shape lintSingleToken and the per-texture wrap arm use.
        // fromJson reads this with toString(), which answers EMPTY for a number, array or
        // object with no journal warning, and the block below is gated on !isEmpty() — so a
        // mistyped value got no diagnostic in the validator AND none at load, and the pack
        // shipped green with no outer padding.
        const QJsonValue rawPadding = meta.value(QLatin1String("paddingParam"));
        if (!rawPadding.isUndefined() && !rawPadding.isNull() && !rawPadding.isString()) {
            lints << QStringLiteral(
                "paddingParam is not a string, which is ignored at load, so the pack "
                "requests no padding and clips at the frame edge");
        }
        const QString paddingParam = rawPadding.toString();
        if (!paddingParam.isEmpty()) {
            bool resolves = false;
            for (const SurfaceShaderEffect::ParameterInfo& p : eff.parameters) {
                if (p.id == paddingParam && (p.type == QLatin1String("float") || p.type == QLatin1String("int"))) {
                    resolves = true;
                    // The host bounds the request into [0, kMaxDecorationOuterPaddingPx], so
                    // a max above that ceiling has a dead top end. All bundled packs comply.
                    bool okMax = false;
                    const double declaredMax = p.maxValue.toDouble(&okMax);
                    if (okMax
                        && declaredMax > static_cast<double>(PhosphorSurfaceShaders::kMaxDecorationOuterPaddingPx)) {
                        lints << QStringLiteral(
                                     "paddingParam '%1' declares a max of %2, above the host's "
                                     "%3 px ceiling, so the top of its range is unreachable")
                                     .arg(paddingParam)
                                     .arg(declaredMax)
                                     .arg(PhosphorSurfaceShaders::kMaxDecorationOuterPaddingPx);
                    }
                    break;
                }
            }
            if (!resolves) {
                lints << QStringLiteral(
                             "paddingParam '%1' names no declared float or int parameter, so the pack "
                             "requests no padding and clips at the frame edge")
                             .arg(paddingParam);
            }
        }
    }
    // preview is the pack's thumbnail. The registry clears one that escapes the
    // pack directory with a journal warning only, and accepts a name whose file
    // does not exist, so either mistake ships green and shows up as a pack with
    // no thumbnail. Same shape as the texture branch above.
    {
        // Non-string first, for the same reason as paddingParam above. The consequence names the
        // DECLARED thumbnail rather than claiming the pack has none: an empty previewPath sends the
        // registry down its else branch, which adopts a conventional preview.png beside the
        // metadata, so a pack shipping that file still gets a thumbnail. The two arms below cannot
        // reach that case, both running inside a non-empty previewPath test.
        const QJsonValue rawPreview = meta.value(QLatin1String("preview"));
        if (!rawPreview.isUndefined() && !rawPreview.isNull() && !rawPreview.isString()) {
            lints << QStringLiteral(
                "preview is not a string, which is ignored at load, so the declared thumbnail is not used");
        }
        const QString preview = rawPreview.toString();
        if (!preview.isEmpty()) {
            const auto confined = confinedPackPath(packDir, preview);
            if (!confined) {
                lints << QStringLiteral(
                             "preview path escapes the pack directory: %1 (cleared at load, so the pack "
                             "shows no thumbnail)")
                             .arg(preview);
            } else if (!QFile::exists(*confined)) {
                lints << QStringLiteral("preview missing: %1 (the pack shows no thumbnail)").arg(preview);
            } else if (!QFileInfo(*confined).isFile()) {
                // A directory, same as the texture arm above: exists() lets it through.
                lints << QStringLiteral("preview path is not a file: %1 (the pack shows no thumbnail)").arg(preview);
            }
        }
    }

    // THE BUFFER-CHAIN HALF, in packvalidator_surface_buffer_lints.cpp. Appended here rather
    // than called by the orchestrator so a report keeps its declaration order: the parameter,
    // texture and buffer lines interleave exactly as they did before the split.
    lints += surfaceBufferChainLints(meta, eff, packDir);
    if (!QFile::exists(eff.fragmentShaderPath)) {
        // Recomputed from eff rather than taken as a parameter: the caller derives the same
        // basename for its report header, and a second argument that must agree with a
        // local over there is a worse seam than one QFileInfo here.
        lints << QStringLiteral("fragment shader missing: %1").arg(QFileInfo(eff.fragmentShaderPath).fileName());
    }
    // An explicit per-pack `vertexShader` was resolved to absolute above; if the
    // author typo'd the path the vertex stage below silently skips it (the
    // exists() guard bows out with no diagnostic), so lint it here the same way
    // the fragment stage is linted. An empty vertexShaderPath is the normal
    // shared-surface.vert case and is not an error.
    if (!eff.vertexShaderPath.isEmpty() && !QFile::exists(eff.vertexShaderPath)) {
        lints << QStringLiteral("vertex shader missing: %1").arg(QFileInfo(eff.vertexShaderPath).fileName());
    }
    // A PACK-LOCAL surface.vert THAT THE METADATA DOES NOT DECLARE runs on one
    // host and not the other, which is the worst shape a divergence can take: the
    // pack looks fine and renders differently.
    //
    // The daemon's vertex lookup falls through an undeclared `vertexShader` to a
    // `surface.vert` sitting beside the fragment, so it picks the file up. The
    // compositor does not look there at all and uses its default vertex stage. So
    // an author who drops the file in without declaring it sees their stage in the
    // settings preview and not on a real window.
    //
    // Lints the UNDECLARED case only. Declaring the file is the supported way to
    // ship a per-pack vertex stage and is handled above.
    if (eff.vertexShaderPath.isEmpty()) {
        const QString siblingVert = QFileInfo(eff.fragmentShaderPath).absolutePath() + QStringLiteral("/surface.vert");
        if (QFile::exists(siblingVert)) {
            lints << QStringLiteral(
                "surface.vert sits beside the fragment but `vertexShader` does not name it. The daemon picks up an "
                "undeclared sibling and the compositor does not, so this stage runs in the settings preview and not "
                "on a real window. Declare it in metadata.json");
        }
    }

    return lints;
}

} // namespace PlasmaZones::ShaderValidate

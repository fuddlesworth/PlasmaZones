// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// FILE-SIZE EXCEPTION (sanctioned): this file is past the 1150-line ceiling, and its baseline
// entry was RAISED from 1199 to 1244 to fund two defect fixes. What it gained: the per-entry texture
// arms are now bounded to the slots the loader actually fills, and the parameter loop mirrors
// fromJson's duplicate-id drop. Both were reporting a load behaviour that does not happen,
// which is the one class this file exists to be correct about, and neither had a line-neutral
// form. The alternative was deleting the "why this message is true" commentary that several
// audit rounds built up — which is the length, and is also what keeps the arms from drifting
// back out of step with the loader. A genuine split by concern (metadata and parameter lints
// versus the multipass and buffer-chain block) is the right next move and is a refactor of its
// own, not something to do while funding a fix.

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
// The alternative cut — extracting the multipass block alone — was rejected: it would put
// the buffer-pass LINTS here and the buffer-pass BAKE there while both key off the same
// bufferShaderPaths.

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

    const QJsonArray rawBufferShaders = meta.value(QLatin1String("bufferShaders")).toArray();
    // The buffer keys are read only inside the multipass branch, so on a
    // single-pass pack they are inert. Declaring them reads as a pack that thinks
    // it is multipass, which is the same authoring mistake the gate below names
    // from the other side.
    if (!eff.isMultipass) {
        // PRESENCE, not type, for the same reason the singular keys below use it: this sweep's
        // subject is the DECLARATION, and a toArray() test skipped a mistyped key entirely, since
        // toArray() answers empty for a string, number, bool or object. That left "bufferWraps": 5
        // on a single-pass pack silent HERE and silent at load too — fromJson's loop never runs and
        // emits no journal line — which is the no-diagnostic-anywhere gap this family keeps
        // producing. An explicitly declared empty array now reports, which is the same call made
        // for the explicit empty string, and the message stays true of it.
        for (const QLatin1String key :
             {QLatin1String("bufferScales"), QLatin1String("bufferWraps"), QLatin1String("bufferFilters")}) {
            const QJsonValue arrayValue = meta.value(key);
            if (!arrayValue.isUndefined() && !arrayValue.isNull()) {
                lints
                    << QStringLiteral("%1 is declared on a single-pass pack, where it is never read").arg(QString(key));
            }
        }
        // The SCALAR and BOOL buffer keys are just as inert here, and the registry's single-pass
        // coherence block drops all of them. They were missing from the sweep above, which read
        // as deliberate but was not: the POINTER validator lints its own equivalent
        // ("bufferFeedback declared without `multipass: true` (ignored at load)"), so the
        // precedent in the tree is to report them rather than to treat an array as the only
        // shape worth reporting.
        //
        // "never read" rather than "dropped at load", because the three keys do NOT share a fate:
        // the coherence block resets bufferFeedback and depthBuffer to false but sets
        // halfFloatBuffers to TRUE, its declared default, so a declared true SURVIVES there and is
        // merely never consulted, there being no buffer targets. The registry's own comment says
        // the block resets to defaults rather than to zero. One wording true of all three beats
        // three messages.
        //
        // A bare toBool() is the whole test. QJsonValue::toBool() already answers its default for
        // a non-bool, so an isBool() guard in front of it changes no outcome and no test can kill
        // it — which is the kind of guard this audit has spent two rounds taking back out, so it is
        // not being kept here to look careful. A non-bool belongs to the kBoolKeys type arm above
        // and lands there either way.
        for (const QLatin1String key :
             {QLatin1String("bufferFeedback"), QLatin1String("depthBuffer"), QLatin1String("halfFloatBuffers")}) {
            if (meta.value(key).toBool()) {
                lints << QStringLiteral("%1 is declared true on a single-pass pack, where it is never read")
                             .arg(QString(key));
            }
        }
        const QJsonValue singleScaleValue = meta.value(QLatin1String("bufferScale"));
        if (!singleScaleValue.isUndefined() && !singleScaleValue.isNull()) {
            lints << QStringLiteral("bufferScale is declared on a single-pass pack, where it is never read");
        }
        // The SINGULAR string spellings, which the array sweep above does not cover and which the
        // coherence block resets like the rest. They were missed when this sweep was extended, and
        // the miss falsified a sentence in surface_multipass.glsl claiming the sweep reaches every
        // buffer-only key. Gated on PRESENCE, not on the value's type: this sweep's subject is the
        // DECLARATION, and a toString() test skipped a mistyped key entirely, since toString()
        // answers empty for a number or a bool. That left a single-pass pack with "bufferWrap": 5
        // silent here AND silent in the multipass non-string arm it never reaches, which is the same
        // no-diagnostic-anywhere gap that arm exists to close.
        for (const QLatin1String key : {QLatin1String("bufferWrap"), QLatin1String("bufferFilter")}) {
            const QJsonValue value = meta.value(key);
            if (!value.isUndefined() && !value.isNull()) {
                lints
                    << QStringLiteral("%1 is declared on a single-pass pack, where it is never read").arg(QString(key));
            }
        }
    }
    if (!rawBufferShaders.isEmpty() && !eff.isMultipass) {
        lints << QStringLiteral(
                     "bufferShaders declares %1 pass(es) but \"multipass\" is not true, so every one of "
                     "them is dropped at load and the pack renders single-pass")
                     .arg(static_cast<int>(rawBufferShaders.size()));
    }

    // Multipass buffer lints — read RAW metadata, not the parsed struct: fromJson
    // clamps bufferScale into [kMinBufferScale, 1.0] and drops missing buffers, so
    // a lint over the parsed values would hide author errors.
    if (eff.isMultipass) {
        const QJsonArray declaredBuffers = meta.value(QLatin1String("bufferShaders")).toArray();
        // The inverse of the gate above. With no usable bufferShaders every
        // buffer lint below and the whole buffer bake become no-ops while the
        // header still prints "multipass", and the registry quietly normalises
        // the pack back to single-pass with no warning of its own.
        if (declaredBuffers.isEmpty()) {
            lints << QStringLiteral(
                "\"multipass\" is true but bufferShaders is missing, empty or not an array, so "
                "the pack is normalised back to single-pass at load");
        }
        // A key handed something other than an array is ignored ENTIRELY at load,
        // because QJsonValue::toArray() answers an empty array for any non-array
        // value. So `"bufferScales": 0.5` or `"bufferWraps": "clamp"` ships green
        // with the whole list silently dropped.
        const auto lintIsArray = [&](QLatin1String key) {
            const QJsonValue v = meta.value(key);
            if (!v.isUndefined() && !v.isNull() && !v.isArray()) {
                lints << QStringLiteral("%1 must be an array; any other value is ignored entirely at load")
                             .arg(QString(key));
            }
        };
        lintIsArray(QLatin1String("bufferShaders"));
        lintIsArray(QLatin1String("bufferWraps"));
        lintIsArray(QLatin1String("bufferFilters"));
        lintIsArray(QLatin1String("bufferScales"));
        // The builtin Kawase pyramid is POSITIONAL, not a set: each pass is bound
        // to iChannel<j> by its INDEX, and the seven frags hardcode which channel
        // they read, so the chain composes in exactly one order. Reordered or
        // short, every individual token still resolves and every file still
        // compiles, so nothing else here would notice; the pack simply blurs
        // wrongly. Require the whole sequence as soon as any of it appears.
        static const QStringList kKawaseChain = {
            QStringLiteral("builtin:kawase-down-0"), QStringLiteral("builtin:kawase-down-1"),
            QStringLiteral("builtin:kawase-down-2"), QStringLiteral("builtin:kawase-down-3"),
            QStringLiteral("builtin:kawase-up-0"),   QStringLiteral("builtin:kawase-up-1"),
            QStringLiteral("builtin:kawase-up-2")};
        // The depth+bufferScales arm moved down beside the bufferScale arms, so it can carry
        // chainResolves (not declared until the buffer-resolve loop below).
        bool anyKawase = false;
        bool anyKawaseDown0 = false;
        bool anyGaussianH = false;
        bool anyGaussianV = false;
        for (const QJsonValue& v : declaredBuffers) {
            const QString tok = v.toString();
            anyKawase = anyKawase || kKawaseChain.contains(tok);
            anyKawaseDown0 = anyKawaseDown0 || tok == QLatin1String("builtin:kawase-down-0");
            anyGaussianH = anyGaussianH || tok == QLatin1String("builtin:gaussian-h");
            anyGaussianV = anyGaussianV || tok == QLatin1String("builtin:gaussian-v");
        }
        // TWO predicates, because the two builtin blur families read different things and
        // the gaussian pair was uncovered while both of the lints below said "Kawase".
        //
        // The BACKDROP is read by exactly TWO of the nine passes, and each has
        // backdropTexel() as its only source: kawase_down_0 through
        // surfaceKawaseDownBackdrop, and gaussian_h through surfaceGaussianBackdropH. The
        // other six Kawase passes sample iChannelN, and so does gaussian_v
        // (surfaceGaussianChannelV reads iChannel0 only). So this tests down-0 rather than
        // "any Kawase token": a pack declaring only an UP pass reads no backdrop, and telling
        // it otherwise named a pass that does not sample one. It draws the positional lint
        // below instead, which is the accurate complaint about that pack.
        //
        // The RADIUS SLOT is read by all nine, gaussian_v included: surfaceGaussianChannelV
        // takes customParams[0].x exactly as the horizontal half and the Kawase passes do.
        const bool readsBackdrop = anyKawaseDown0 || anyGaussianH;
        const bool readsRadiusSlot = anyKawase || anyGaussianH || anyGaussianV;

        if (anyKawase) {
            bool chainOk = declaredBuffers.size() >= kKawaseChain.size();
            for (int i = 0; chainOk && i < kKawaseChain.size(); ++i) {
                chainOk = declaredBuffers.at(i).toString() == kKawaseChain.at(i);
            }
            if (!chainOk) {
                lints << QStringLiteral(
                             "the builtin Kawase passes are positional and must appear as bufferShaders"
                             "[0..6] in the order %1")
                             .arg(kKawaseChain.join(QLatin1String(", ")));
            }
            // THE BASE SCALE. surfaceKawaseDownBackdrop cannot size its taps from
            // textureSize(), because it reads the backdrop capture whose size is
            // not the canvas's, so it derives them from a HARDCODED
            // kSurfaceKawaseBaseTexel of 4 canvas px per texel. That constant IS
            // the 0.25 base, so a first scale of anything else silently mis-spaces
            // the one pass that cannot detect it.
            const QJsonArray kawaseScales = meta.value(QLatin1String("bufferScales")).toArray();
            static const QList<double> kKawaseScales = {0.25, 0.125, 0.0625, 0.03125, 0.0625, 0.125, 0.25};
            if (kawaseScales.isEmpty()) {
                lints << QStringLiteral(
                             "the builtin Kawase chain needs per-pass bufferScales %1; with none declared every pass "
                             "renders at the pack-wide bufferScale and the pyramid is not a pyramid")
                             .arg(QStringLiteral("[0.25, 0.125, 0.0625, 0.03125, 0.0625, 0.125, 0.25]"));
            } else if (!qFuzzyCompare(kawaseScales.at(0).toDouble(), kKawaseScales.at(0))) {
                lints << QStringLiteral(
                             "bufferScales[0] is %1, but the first Kawase DOWN pass derives its tap spacing from a "
                             "hardcoded 4 canvas px per texel, which is the 0.25 base. Any other first scale "
                             "mis-spaces that pass and nothing at runtime can detect it")
                             .arg(kawaseScales.at(0).toDouble());
            } else if (chainOk && kawaseScales.size() >= kKawaseScales.size()) {
                // Only when the chain itself is the canonical seven: a pack that
                // failed chainOk above is already being told the bigger thing, and
                // comparing scales against a chain it does not have would be noise.
                for (qsizetype i = 0; i < kKawaseScales.size(); ++i) {
                    if (!qFuzzyCompare(kawaseScales.at(i).toDouble(), kKawaseScales.at(i))) {
                        lints << QStringLiteral(
                                     "bufferScales[%1] is %2 where the Kawase pyramid declares %3; the up passes read "
                                     "the level below by channel index, so an off-pyramid scale composes a level "
                                     "against the wrong resolution")
                                     .arg(i)
                                     .arg(kawaseScales.at(i).toDouble())
                                     .arg(kKawaseScales.at(i));
                        break;
                    }
                }
            }
        }
        // THE VERTICAL GAUSSIAN HALF NEEDS THE HORIZONTAL ONE AT PASS 0.
        // surfaceGaussianChannelV samples iChannel0 and nothing else, and iChannelN is buffer
        // pass N's output for EVERY later pass, not only for pass 1 — both hosts bind
        // channels 0..i-1 to the prior outputs and a 1x1 transparent fallback to channel i
        // and beyond (ShaderNodeRhi's multi-buffer SRB build, and the compositor's surface
        // fold). kawase_up_2 is the in-tree proof: it sits at index 6 and reads iChannel0.
        //
        // So only TWO shapes are provably broken, and an earlier version of this lint was
        // wider than that — it demanded index 1 exactly and rejected [h, X, v], which runs
        // the same three computations as the accepted [h, v, X], and [own_h.frag, v], which
        // composes correctly against the pack's own horizontal half.
        //
        //   (a) gaussian-v at index 0: nothing earlier in the chain has been written, so what
        //       it reads is the 1x1 transparent fallback and it writes a blank pane. (NOT a
        //       read of the FBO it is writing, which is what the fallback exists to prevent.)
        //       One exception, which does not change the verdict: on the daemon a pack with
        //       exactly ONE buffer pass and "bufferFeedback" true takes the single-buffer
        //       ping-pong path, where channel 0 is that pass's own previous frame. The
        //       compositor never reads the flag, so such a pack diverges between hosts and is
        //       still wrong — just wrong for a second reason.
        //   (b) gaussian-v declared alongside builtin:gaussian-h that is NOT at index 0: the
        //       author plainly meant the pair, and pass 0 is something else, so the vertical
        //       half cannot be reading the horizontal half's result.
        //
        // A pack that ships its own horizontal half is deliberately NOT linted, SO LONG AS it
        // does not also name the builtin token: the validator cannot identify an arbitrary frag
        // as a horizontal Gaussian, so [own_h.frag, v] is left alone. Declare both and clause
        // (b) fires at the builtin's index, which is the right answer for the shape it cannot
        // distinguish this from ([own_downsample.frag, h, v]) and harmless for this one, where
        // the builtin h is a wasted pass either way. Same token-only reach as the Kawase arm.
        if (anyGaussianV) {
            // By hand: QJsonArray has no indexOf, and a QJsonValue comparison would match a
            // non-string entry that happens to compare equal.
            int vIndex = -1;
            int hIndex = -1;
            for (int i = 0; i < declaredBuffers.size(); ++i) {
                const QString tok = declaredBuffers.at(i).toString();
                if (vIndex < 0 && tok == QLatin1String("builtin:gaussian-v")) {
                    vIndex = i;
                }
                if (hIndex < 0 && tok == QLatin1String("builtin:gaussian-h")) {
                    hIndex = i;
                }
            }
            if (vIndex == 0) {
                lints << QStringLiteral(
                    "builtin:gaussian-v is bufferShaders[0], and it samples iChannel0 — buffer pass 0's output, "
                    "which at pass 0 is nothing this chain has written: the 1x1 transparent fallback, or, on a "
                    "chain declaring a SINGLE buffer pass, that pass's own previous frame where the daemon "
                    "honours \"bufferFeedback\". The vertical half has "
                    "to follow the horizontal one, so declare builtin:gaussian-h as bufferShaders[0]");
            } else if (anyGaussianH && hIndex != 0) {
                lints << QStringLiteral(
                             "builtin:gaussian-v samples iChannel0, which is buffer pass 0's output, but "
                             "builtin:gaussian-h is declared at bufferShaders[%1] rather than [0], so the vertical "
                             "half blurs pass 0's result instead of the horizontal half's. Move "
                             "builtin:gaussian-h to bufferShaders[0]")
                             .arg(hIndex);
            }
        }
        // NEEDS BACKDROP. kawase_down_0 and gaussian_h each have backdropTexel() as their
        // ONLY source, so a chain that omits the flag captures nothing and composites a
        // fully transparent pane. Gated on readsBackdrop rather than on anyKawase: the
        // gaussian pair was uncovered for a round, and a pack declaring it validated OK
        // while rendering blank.
        //
        // The message NAMES the pass rather than saying "its first pass". That phrasing was
        // false for two reachable shapes: a gaussian pair written v-then-h samples the
        // backdrop in its SECOND pass, and a lone up-pack drew this lint while sampling no
        // backdrop at all. Both sent the author to inspect a pass that reads nothing.
        //
        // "is not true" rather than "is not declared", because the predicate is toBool()
        // and correctly matches the runtime's toBool(false) — so it also fires for an
        // explicit `false`, for null, and for a non-bool, none of which are undeclared.
        // A non-bool additionally draws the kBoolKeys type lint, which is the arm that
        // explains the shape.
        if (readsBackdrop && !meta.value(QLatin1String("needsBackdrop")).toBool()) {
            // BOTH names when both hold, not a ternary that picks one: a chain can declare the
            // gaussian pair and the Kawase pyramid together, and naming only the first sends
            // the author to a pass that is not the one they are looking at.
            QStringList backdropPasses;
            if (anyKawaseDown0) {
                backdropPasses << QStringLiteral("builtin:kawase-down-0");
            }
            if (anyGaussianH) {
                backdropPasses << QStringLiteral("builtin:gaussian-h");
            }
            lints << QStringLiteral(
                         "this chain's %1 %2 the backdrop through backdropTexel(), but "
                         "\"needsBackdrop\" is not true, so nothing is captured and every pass "
                         "composites a transparent pane")
                         .arg(backdropPasses.join(QLatin1String(" and ")),
                              backdropPasses.size() > 1 ? QStringLiteral("passes sample")
                                                        : QStringLiteral("pass samples"));
        }
        // THE RADIUS SLOT. Every pass in both builtin blur families reads the radius as
        // customParams[0].x — the Kawase passes, gaussian_h AND gaussian_v — and slots are
        // assigned by DECLARATION ORDER (buildParamPreamble), so the chain blurs by
        // whatever the pack's first scalar parameter happens to be. Reorder the parameters
        // array and the pack still compiles, still loads, and blurs by a corner radius.
        // surface_blur.glsl documents the slot convention and
        // test_surface_blur_chain_lints.cpp covers the arms. Do NOT re-quote that header,
        // which this comment used to: the two cited each other for rounds.
        if (readsRadiusSlot) {
            QString firstScalarId;
            // The DUPLICATE-ID drop, mirrored from the loader. SurfaceShaderEffect::fromJson
            // keeps the first entry for an id and discards every later one, and
            // translateSurfaceParams then assigns slots over the survivors — so a raw scan
            // could name a parameter the preamble never defines. It did: a pack declaring
            // the same id as a colour and then as a float had this arm name it as the first
            // scalar while --emit-preamble put it in customColors[0] and wrote no scalar
            // define at all. Such a pack also draws the duplicate-id lint, so it is rejected
            // either way; what this fixes is the two outputs of one binary disagreeing.
            QSet<QString> seenParamIds;
            for (const QJsonValue& pv : parametersValue.toArray()) {
                const QString pid = pv.toObject().value(QLatin1String("id")).toString();
                const QString ptype = pv.toObject().value(QLatin1String("type")).toString();
                if (!pid.isEmpty() && seenParamIds.contains(pid)) {
                    continue;
                }
                if (!pid.isEmpty()) {
                    seenParamIds.insert(pid);
                }
                // paramPreamble's OWN test, "not a color", not a float/int list. A BOOL
                // pools as a scalar too and takes a sub-slot in declaration order, so a
                // pack leading with roundBottomCorners blurs by that bool. Invalid ids
                // skipped as both runtimes do.
                if (ptype != QLatin1String("color") && PhosphorShaders::isValidParamId(pid)) {
                    firstScalarId = pid;
                    break;
                }
            }
            // The ID is the not-found sentinel, not the type: a missing or empty `type`
            // key sets the id and leaves the type empty, which made this arm claim "no
            // scalar" for a correct slot 0. isValidParamId("") is false.
            if (firstScalarId.isEmpty()) {
                lints << QStringLiteral(
                    "the builtin blur passes read the blur radius as customParams[0].x, but this pack declares "
                    "no scalar parameter, so the chain blurs by 0");
            } else if (firstScalarId != QLatin1String("blurRadius")) {
                // A name check, and the message says why rather than pretending the name
                // is load-bearing: the SLOT is what matters and the name is the only
                // thing decidable here.
                lints << QStringLiteral(
                             "the builtin blur passes read the blur radius as customParams[0].x, which is the "
                             "FIRST scalar parameter declared, and that is '%1' here. Every bundled chain pack "
                             "declares 'blurRadius' first; if '%1' is not the radius the chain blurs by the wrong "
                             "control")
                             .arg(firstScalarId);
            }
        }
        // Does the chain the pack declares actually RUN? The registry fail-closes the whole pack
        // to single-pass if ANY entry does not resolve, and its coherence block then resets
        // bufferFeedback to false — so on a fail-closed chain the daemon never sees the flag and the
        // feedback message below would describe a mechanism that cannot occur. Every arm that clears
        // this already emits its own louder lint, so suppressing the feedback line loses nothing.
        bool chainResolves = !declaredBuffers.isEmpty();
        // BOUNDED TO WHAT THE LOADER KEEPS. fromJson breaks at the pass budget, so a past-cap entry
        // is never appended, never reaches the registry's resolve loop (which iterates the CAPPED
        // list) and therefore cannot fail the chain closed. Reporting one here told the author two
        // untrue things at once: that the entry was "kept in place" and that it dropped the whole
        // pack to single-pass, when it was dropped as surplus and the chain runs at full length.
        // Worse, it cleared chainResolves and so SUPPRESSED a true bufferFeedback diagnostic for a
        // chain that does run. The over-cap lint below already reports the surplus by count, which
        // is the one thing an author can act on. This also retires the escaping-path arm's only
        // reachable case, which packvalidator_surface.cpp's own confinement comment already
        // identified as past-cap-only.
        const qsizetype loadedBufferCount =
            std::min<qsizetype>(declaredBuffers.size(), PhosphorSurfaceShaders::SurfaceShaderEffect::kMaxBufferPasses);
        for (qsizetype bufIndex = 0; bufIndex < loadedBufferCount; ++bufIndex) {
            const QJsonValue v = declaredBuffers.at(bufIndex);
            // A non-string entry reported as "empty", which says the author wrote
            // "" when they wrote an object or a number. It costs the same thing,
            // so the consequence below is repeated rather than softened.
            if (!v.isString()) {
                lints << QStringLiteral(
                    "bufferShaders entry is not a string (kept in place as empty, but it is rejected at "
                    "scan time and drops the WHOLE pack to single-pass)");
                chainResolves = false;
                continue;
            }
            const QString bufName = v.toString();
            if (bufName.isEmpty()) {
                // fromJson appends an empty entry IN PLACE rather than skipping
                // it, deliberately, so the positional alignment with bufferWraps
                // and bufferFilters holds (surfaceshadereffect.cpp says why: a
                // dropped empty broke that alignment on the very next load, since
                // toJson re-emits empties). What an empty entry costs is the whole
                // chain: it fails the registry's existence check at scan time and
                // fails the pack closed to single-pass.
                lints << QStringLiteral(
                    "empty bufferShaders entry (kept in place, but it is rejected at scan time and drops the "
                    "WHOLE pack to single-pass)");
                chainResolves = false;
                continue;
            }
            if (SurfaceShaderRegistry::isBuiltinBufferShader(bufName)) {
                const QString resolvedBuiltin =
                    SurfaceShaderRegistry::resolveBuiltinBufferShader(bufName, QDir(packDir).absolutePath());
                if (resolvedBuiltin.isEmpty()) {
                    // A correctly spelled `builtin:` PREFIX with a mis-cased SUFFIX lands here
                    // rather than in the spelling arm below, because isBuiltinBufferShader's
                    // startsWith is case-sensitive: a lower-case prefix makes it answer true and
                    // the failure moves to this exact-case table lookup. That is the likelier of
                    // the two author slips and it was getting a message that reads as "your
                    // install is missing a file". Decided by asking the RESOLVER whether the
                    // lower-cased token resolves, rather than by copying its token table here —
                    // the table is private to it, and a copy is one more thing to keep in step.
                    // trimmed() as well as toLower(), because a token whose prefix is already correct
                    // and lower case but which carries TRAILING whitespace reaches here too, and it
                    // was getting the missing-file reading while rendering as an apparently-correct
                    // token. A LEADING space never arrives here, since it breaks the exact prefix and
                    // the spelling arm below catches it instead.
                    const QString canonical = bufName.trimmed().toLower();
                    if (canonical != bufName
                        && !SurfaceShaderRegistry::resolveBuiltinBufferShader(canonical, QDir(packDir).absolutePath())
                                .isEmpty()) {
                        lints << QStringLiteral(
                                     "builtin buffer shader tokens are lower case with no surrounding "
                                     "whitespace: '%1' should be '%2'")
                                     .arg(bufName, canonical);
                    } else {
                        lints << QStringLiteral("unknown or unlocatable builtin buffer shader: %1").arg(bufName);
                    }
                    chainResolves = false;
                    continue;
                }
                // RESOLVED FROM OUTSIDE THIS TREE, which is the dev-passes /
                // CI-fails shape. The registry probes the pack's sibling shared/
                // first and then falls back to QStandardPaths, so a self-contained
                // tree missing a builtin file quietly resolves it from the
                // INSTALLED copy under /usr/share. On a developer machine with the
                // package on it the pack validates; in CI, or on any machine
                // without the install, the same tree fails.
                //
                // packSharedRoots is what draws the line, and it draws it the right
                // way round on its own: for a self-contained tree it is the sibling
                // shared/ and nothing else, so an outside resolution is not in the
                // list. For an INSTALLED pack it widens to the XDG chain, so
                // resolving from the system prefix is expected and silent.
                const QString resolvedDir = QFileInfo(resolvedBuiltin).canonicalPath();
                bool insideTree = false;
                for (const QString& root : packSharedRoots(packDir)) {
                    if (!root.isEmpty() && QFileInfo(root).canonicalFilePath() == resolvedDir) {
                        insideTree = true;
                        break;
                    }
                }
                if (!insideTree) {
                    lints << QStringLiteral(
                                 "%1 resolved to %2, which is outside this pack tree's shared roots. A tree that does "
                                 "not ship the file validates here only because a copy is installed, and fails "
                                 "anywhere without one")
                                 .arg(bufName, resolvedBuiltin);
                }
                continue;
            }
            // nullopt CANNOT arrive here, so it is folded into the missing test rather than given
            // its own arm: the caller refuses the whole pack on an escaping entry before any lint
            // runs, and this loop is bounded to the entries it already confined. The arm that used
            // to report an escape was reachable only past the pass budget, which the bound now
            // excludes, so it had become dead code whose only effect was to clear chainResolves for
            // a chain that runs. Kept as a disjunct rather than deleted, because dropping it would
            // leave an `else if` dereferencing a possibly-empty optional.
            const auto confined = confinedPackPath(packDir, bufName);
            if (!confined || !QFile::exists(*confined)) {
                chainResolves = false;
                // A MIS-CASED or space-prefixed `builtin:` prefix lands here, because the
                // registry's own prefix test is case-sensitive and exact. Saying "missing" is
                // true but sends the author looking for a file they never wrote, so name the
                // spelling instead. The test below is for the DIAGNOSTIC only; the resolver's
                // test stays exact, since that is what the runtime will do.
                if (bufName.trimmed().startsWith(QLatin1String("builtin:"), Qt::CaseInsensitive)) {
                    lints << QStringLiteral(
                                 "multipass buffer shader '%1' looks like a builtin token with the wrong spelling: "
                                 "the prefix is matched exactly, so write it as 'builtin:' — lower case, and with no "
                                 "surrounding whitespace. As declared it is treated as a file path, and there is no "
                                 "such file")
                                 .arg(bufName);
                } else {
                    lints << QStringLiteral("multipass buffer shader missing: %1").arg(bufName);
                }
            }
        }
        // The runtime caps buffer passes and drops the surplus with only a
        // journal warning, the same "runtime hid the author error" class the
        // sibling arms lint.
        if (declaredBuffers.size() > PhosphorSurfaceShaders::SurfaceShaderEffect::kMaxBufferPasses) {
            lints << QStringLiteral("too many buffer shaders: %1 declared, cap is %2 (surplus dropped at load)")
                         .arg(static_cast<int>(declaredBuffers.size()))
                         .arg(PhosphorSurfaceShaders::SurfaceShaderEffect::kMaxBufferPasses);
        }
        // bufferWraps / bufferFilters / bufferScales are positionally aligned to
        // bufferShaders, and a short array is padded with the single-value default
        // at load with no warning. Flag any mismatch, matching the animation arm,
        // rather than surplus alone, since a short array is the likelier slip.
        //
        // UNGATED, since a length mismatch is decidable either way, but the message says nothing
        // about LOAD behaviour any more. An earlier wording claimed a surplus entry is "never read"
        // and a missing one "falls back at load", neither of which happens on a fail-closed chain
        // (the coherence block clears these arrays and there are no passes), and it also claimed
        // declared length is kept, which is untrue past the 8-pass budget.
        const auto lintBufferArrayLen = [&](QLatin1String key) {
            const QJsonArray arr = meta.value(key).toArray();
            if (!arr.isEmpty() && arr.size() != declaredBuffers.size()) {
                lints << QStringLiteral(
                             "%1 has %2 entries for %3 buffer shaders (aligned positionally with bufferShaders; "
                             "a surplus entry has no pass and a missing one takes the single value)")
                             .arg(QString(key))
                             .arg(static_cast<int>(arr.size()))
                             .arg(static_cast<int>(declaredBuffers.size()));
            }
        };
        lintBufferArrayLen(QLatin1String("bufferWraps"));
        lintBufferArrayLen(QLatin1String("bufferFilters"));
        // bufferScales is aligned the same way, and each entry is clamped at
        // load like the single-value bufferScale (a non-number falls back to
        // it with only a journal warning).
        lintBufferArrayLen(QLatin1String("bufferScales"));
        {
            const QJsonArray scales = meta.value(QLatin1String("bufferScales")).toArray();
            // Past the pass budget fromJson DROPS the entry rather than clamping
            // it, so the per-entry messages below would state a consequence that
            // does not happen. Report the overflow once and lint only the entries
            // that survive.
            const qsizetype scaleCap = PhosphorSurfaceShaders::SurfaceShaderEffect::kMaxBufferPasses;
            if (scales.size() > scaleCap) {
                lints << QStringLiteral(
                             "bufferScales has %1 entries, past the %2-pass budget; the surplus is "
                             "dropped at load rather than clamped")
                             .arg(static_cast<int>(scales.size()))
                             .arg(static_cast<int>(scaleCap));
            }
            // BOTH per-entry arms carry chainResolves: each names a PASS, and a fail-closed chain
            // has bufferScales cleared and no passes, so neither has a subject.
            for (qsizetype i = 0; chainResolves && i < scales.size() && i < scaleCap; ++i) {
                const QJsonValue v = scales.at(i);
                if (!v.isDouble()) {
                    lints << QStringLiteral(
                                 "bufferScales entry %1 is not a number (that pass falls back to "
                                 "bufferScale at load)")
                                 .arg(i);
                } else if (v.toDouble() < PhosphorShaders::kMinBufferScale
                           || v.toDouble() > PhosphorShaders::kMaxBufferScale) {
                    lints << QStringLiteral("bufferScales entry %1 out of range [%2, %3]: %4 (clamped at load)")
                                 .arg(i)
                                 .arg(PhosphorShaders::kMinBufferScale)
                                 .arg(PhosphorShaders::kMaxBufferScale)
                                 .arg(v.toDouble());
                }
            }
        }
        // Vocabulary, on all four spellings. validatedWrap / validatedFilter
        // clear an unrecognised token to empty with a journal warning only.
        // BOUNDED TO WHAT THE LOADER KEEPS, like the buffer-shader loop and the bufferScales
        // loop: fromJson caps both arrays at kMaxBufferPasses and drops the surplus, so an
        // entry past the budget never reaches the struct and reporting on it claimed the
        // daemon honours a value that was thrown away. The over-length arm still names the
        // surplus by count, which is the part an author can act on.
        const auto lintSurfaceTokens = [&lints, &meta](QLatin1String key, bool wrap) {
            const QJsonArray arr = meta.value(key).toArray();
            const qsizetype kept =
                std::min<qsizetype>(arr.size(), PhosphorSurfaceShaders::SurfaceShaderEffect::kMaxBufferPasses);
            for (qsizetype idx = 0; idx < kept; ++idx) {
                const QJsonValue v = arr.at(idx);
                // A NON-STRING entry was a wrap/filter fault that reached
                // the user with no diagnostic anywhere, not even a journal line:
                // QJsonValue::toString() answers empty for a number, bool, null,
                // array or object, and the emptiness gate below then reads it as
                // "not specified" rather than as the mistake it is.
                if (!v.isString()) {
                    lints << QStringLiteral("%1 has a non-string entry, which is ignored at load").arg(QString(key));
                    continue;
                }
                const QString tok = v.toString();
                const bool ok = wrap ? PhosphorSurfaceShaders::SurfaceShaderContract::isValidWrapToken(tok)
                                     : PhosphorSurfaceShaders::SurfaceShaderContract::isValidFilterToken(tok);
                if (!tok.isEmpty() && !ok) {
                    lints << QStringLiteral("%1 value '%2' not in vocabulary (cleared at load)").arg(QString(key), tok);
                }
            }
        };
        lintSurfaceTokens(QLatin1String("bufferWraps"), true);
        lintSurfaceTokens(QLatin1String("bufferFilters"), false);
        // The SINGULAR spellings get the non-string arm their array twins have had for rounds. The
        // gap was identical and so was its cause: the loader reads them with toString(), which
        // answers empty for a number, bool, null, array or object, and validatedWrap's own guard
        // then skips its warning on an empty value — so a mistyped bufferWrap reached the user with
        // no diagnostic in the validator AND no journal line at load, on single-pass and multipass
        // alike. The array arm's comment called that "the one wrap/filter fault" with no
        // diagnostic anywhere; the same !isEmpty() shape turned up in the per-texture wrap too, so
        // no count is given here — the shape is what to look for.
        const auto lintSingleToken = [&lints, &meta](QLatin1String key, bool wrap) {
            const QJsonValue value = meta.value(key);
            if (!value.isUndefined() && !value.isNull() && !value.isString()) {
                lints << QStringLiteral("%1 is not a string, which is ignored at load").arg(QString(key));
                return;
            }
            const QString tok = value.toString();
            const bool ok = wrap ? PhosphorSurfaceShaders::SurfaceShaderContract::isValidWrapToken(tok)
                                 : PhosphorSurfaceShaders::SurfaceShaderContract::isValidFilterToken(tok);
            if (!tok.isEmpty() && !ok) {
                lints << QStringLiteral("%1 value '%2' not in vocabulary (cleared at load)").arg(QString(key), tok);
            }
        };
        lintSingleToken(QLatin1String("bufferWrap"), true);
        lintSingleToken(QLatin1String("bufferFilter"), false);
        // DAEMON-ONLY, and the schema accepting these gave no hint of it. All four
        // wrap/filter spellings are declared daemon-only on SurfaceShaderEffect,
        // and the compositor bears that out: it creates every buffer target
        // GL_LINEAR / GL_CLAMP_TO_EDGE and never reads the keys. A pack that asks
        // for "repeat" or "nearest" therefore renders one way in the settings
        // preview and another on a real window, which is precisely the class of
        // divergence this validator exists to surface before a pack ships.
        // THE SHAPE IS A PARAMETER, not something to sniff from the value. An earlier version
        // branched on value.isString() and fell back to toArray(), which is right for whichever
        // key it happens to be and WRONG for the other: a plural key given a string took the
        // string arm, a singular key given an array took the array arm, and both then reported
        // "which the DAEMON honours" for a value the loader reads as EMPTY and honours not at
        // all — printed directly beside the type lint that says it is ignored. That is the same
        // false-mechanism class the livePasses split was written to remove, in the one arm whose
        // entire purpose is a real preview-versus-window divergence. Reading only the declared
        // shape means a mistyped value falls to the arm that actually describes it.
        const auto lintDaemonOnlyToken = [&lints, &meta](QLatin1String key, QLatin1String honoured, bool plural,
                                                         bool wrap) {
            // IN VOCABULARY as well as different from the default, because validatedWrap and
            // validatedFilter CLEAR a rejected token, leaving the daemon on the same clamp/linear
            // the compositor uses. Without this an invalid token drew the divergence claim two
            // lines under the vocabulary lint saying the value was cleared. Same false-mechanism
            // class as the shape bug below, reached by VALUE instead of by shape.
            const auto reportable = [wrap, honoured](const QString& tok) {
                if (tok.isEmpty() || tok == honoured) {
                    return false;
                }
                return wrap ? PhosphorSurfaceShaders::SurfaceShaderContract::isValidWrapToken(tok)
                            : PhosphorSurfaceShaders::SurfaceShaderContract::isValidFilterToken(tok);
            };
            QStringList offending;
            const QJsonValue value = meta.value(key);
            if (plural) {
                // Capped for the same reason as the vocabulary arm above: an entry past the
                // pass budget is dropped by fromJson, so the daemon honours nothing there.
                const QJsonArray arr = value.toArray();
                const qsizetype kept =
                    std::min<qsizetype>(arr.size(), PhosphorSurfaceShaders::SurfaceShaderEffect::kMaxBufferPasses);
                for (qsizetype idx = 0; idx < kept; ++idx) {
                    const QString tok = arr.at(idx).toString();
                    if (reportable(tok)) {
                        offending << tok;
                    }
                }
            } else if (value.isString()) {
                if (reportable(value.toString())) {
                    offending << value.toString();
                }
            }
            if (!offending.isEmpty()) {
                offending.removeDuplicates();
                lints << QStringLiteral(
                             "%1 declares %2, which the DAEMON honours and the compositor ignores: it "
                             "creates every buffer target as '%3'. The pack will render differently in "
                             "the settings preview and on a window")
                             .arg(QString(key), offending.join(QLatin1String(", ")), QString(honoured));
            }
        };
        // GATED ON chainResolves, same gate and same argument as the feedback arm below: the
        // coherence block clears all four spellings on a fail-closed chain, so the daemon honours
        // none of them and the divergence cannot happen. The vocabulary and length arms above stay
        // UNGATED, since a typo is a typo whether or not the chain runs.
        //
        // WHAT IT DOES NOT COVER, corrected: a single-pass pack never reaches here (the enclosing
        // `if (eff.isMultipass)` excludes it), and for `multipass: true` with no bufferShaders the
        // single-pass sweep above does NOT pick these keys up, because eff.isMultipass is
        // fromJson's RAW bool and is TRUE there. That pack gets the normalisation lint instead.
        if (chainResolves) {
            lintDaemonOnlyToken(QLatin1String("bufferWrap"), QLatin1String("clamp"), false, true);
            lintDaemonOnlyToken(QLatin1String("bufferWraps"), QLatin1String("clamp"), true, true);
            lintDaemonOnlyToken(QLatin1String("bufferFilter"), QLatin1String("linear"), false, false);
            lintDaemonOnlyToken(QLatin1String("bufferFilters"), QLatin1String("linear"), true, false);
        }
        // The two daemon-only BOOLS. Only bufferFeedback is linted; the paragraph below says why
        // halfFloatBuffers is linted on an EXPLICIT true only. Same divergence class as the four
        // vocabulary arms above: the compositor's surface fold reads neither key. (Its POINTER
        // decoration path does honour bufferFeedback, which is why grepping the effect for the
        // name is misleading — the surface fold is the host that matters here.)
        //
        // THE MESSAGE SPLITS ON THE LIVE PASS COUNT, because the daemon only honours the flag
        // with exactly one buffer pass. Everything that implements feedback in ShaderNodeRhi
        // sits inside the single-buffer branch — the ping-pong clear, writeIndex, the
        // bufferRT/bufferSrb/writtenTexture selection, createBufferSrb's prevFrame, and the
        // m_srbB build, which is literally `if (!multiBufferMode && m_bufferFeedback ...)`. The
        // multi-buffer loop never mentions the flag. So above one pass it is inert on BOTH hosts
        // and there is nothing to diverge; the old single message claimed a settings-preview
        // difference the author could not have found, which is the one failure here that could
        // make someone change a working pack. This arm's contract used to come from the
        // neighbouring comment ("honoured by the DAEMON") rather than from the code that
        // implements it, which is the same mistake the gaussian positional lint was built on.
        // The count is the size the registry hands the node when the chain RESOLVES: the declared
        // size, capped at the pass budget. Empties are counted because the loader appends every
        // entry in place (deliberately, to keep bufferWraps and bufferFilters positionally aligned),
        // and the loop above is bounded to the same cap so nothing past it is considered at all.
        //
        // TWO EARLIER VERSIONS OF THIS PARAGRAPH ARGUED FROM THE WRONG MECHANISM, so what it does
        // NOT rest on is worth stating. It does not rest on multiBufferMode keying on the declared
        // size: setBufferShaderPaths caps AND strips trailing empties, and upstream of it the
        // registry clears the path list outright on any unresolvable entry, so an empty-bearing
        // chain reaches multiBufferMode as zero rather than as its declared length. And the
        // empties-in-the-count rule is not observable from the outside either, because any chain
        // carrying an empty entry clears chainResolves and suppresses this message entirely. The
        // rule is kept because it is the loader's, not because a fixture can tell the difference.
        //
        // The gaussian, backdrop and radius scans above deliberately do NOT take this cap: they can
        // only report on a dropped pass when the pack is already rejected for exceeding the budget,
        // so the extra diagnostic is redundant rather than wrong.
        const auto livePasses =
            std::min<qsizetype>(declaredBuffers.size(), PhosphorSurfaceShaders::SurfaceShaderEffect::kMaxBufferPasses);
        // Suppressed entirely on a fail-closed chain: the registry has already reset bufferFeedback
        // to false there, so neither message would be true. The louder lint that cleared
        // chainResolves is the one the author needs.
        if (chainResolves && meta.value(QLatin1String("bufferFeedback")).toBool()) {
            if (livePasses > 1) {
                lints << QStringLiteral(
                             "\"bufferFeedback\": true with %1 buffer passes is read by NEITHER host: the "
                             "daemon's feedback path is the single-buffer one, and the compositor's surface "
                             "fold ignores the key outright. Only a chain declaring exactly one buffer pass "
                             "gets feedback, and then on the daemon alone")
                             .arg(livePasses);
            } else {
                lints << QStringLiteral(
                    "\"bufferFeedback\": true is honoured by the DAEMON and ignored by the compositor's surface "
                    "fold, so a pass reads its own previous frame in the settings preview and the 1x1 "
                    "transparent fallback on a real window");
            }
        }
        // halfFloatBuffers, on an EXPLICIT true. One lookup, like the arm above, and gated on
        // chainResolves for the same reason: a fail-closed chain has no buffer targets at all, so
        // "creates RGBA16F buffer targets" would describe something that does not exist.
        const QJsonValue halfFloatValue = meta.value(QLatin1String("halfFloatBuffers"));
        if (chainResolves && halfFloatValue.isBool() && halfFloatValue.toBool()) {
            lints << QStringLiteral(
                "\"halfFloatBuffers\": true is honoured by the DAEMON, which creates RGBA16F buffer targets, "
                "and ignored by the compositor's surface fold, which creates every buffer target RGBA8. Any "
                "buffer value outside [0,1] is clipped on a real window");
        }
        // WHY THE halfFloatBuffers ARM ABOVE TESTS isBool() FIRST. The loader reads the key with
        // toBool(TRUE), so a multipass pack which says nothing gets RGBA16F on the daemon and
        // RGBA8 on the compositor, and all seven bundled chain packs write false explicitly to
        // avoid it. A lint on the LOADED value was written and withdrawn: because the DEFAULT is
        // the divergent value it fired on every idiomatic minimal pack rather than on an author
        // mistake, breaking all fourteen blur-chain slots, none of which is wrong. A lint whose
        // true positive is the normal case is reporting a bad default, not a bad pack.
        // Narrowing it to an EXPLICIT true is what makes it a lint about the pack again, and it
        // is the same argument that keeps bufferFeedback: the value that trips it is an opt-in
        // the author typed. Its positive set was measured before it was added, twice and
        // independently — zero of the seven bundled packs (all write false), zero of the fourteen
        // blur-chain slots (none declares the key), and nothing else in the tree declares it
        // true either. What it deliberately still does NOT catch is the pack that says nothing
        // and silently diverges; fixing THAT belongs at the default or in the compositor
        // honouring the key, both of which change rendering for existing third-party packs and
        // so are decisions rather than repairs.
        //
        // The single-value twin of the per-entry not-a-number lint above. toDouble
        // answers its DEFAULT for a string or a bool, so `"bufferScale": "0.5"`
        // silently loads as 1.0 and the range check below sees nothing wrong.
        //
        // GATED PER CASE, not per arm. TYPE and ABOVE-max are true on every path (all land on 1.0).
        // Only BELOW-min needs the gate: fromJson clamps to kMinBufferScale and the coherence block
        // then resets to 1.0, so "clamped at load" would name a value the pack never has. An earlier
        // version gated the whole range arm on that argument and silenced a true above-max line.
        const QJsonValue rawScaleValue = meta.value(QLatin1String("bufferScale"));
        if (!rawScaleValue.isUndefined() && !rawScaleValue.isNull() && !rawScaleValue.isDouble()) {
            lints << QStringLiteral("bufferScale is not a number, so it falls back to 1.0 at load");
        }
        const double rawScale = rawScaleValue.toDouble(1.0);
        if (rawScale > PhosphorShaders::kMaxBufferScale
            || (chainResolves && rawScale < PhosphorShaders::kMinBufferScale)) {
            lints << QStringLiteral("bufferScale out of range [%1, %2]: %3 (clamped at load)")
                         .arg(PhosphorShaders::kMinBufferScale)
                         .arg(PhosphorShaders::kMaxBufferScale)
                         .arg(rawScale);
        }
        // RELOCATED from the blur-chain block above to carry chainResolves: a fail-closed chain has
        // useDepthBuffer false, bufferScales cleared and no targets, so nothing is pinned. AND ON
        // REAL DIVERGENCE, mirroring the runtime's twin warning: each CLAMPED slot against the
        // clamped pack-wide scale, warning only if one differs. Without the clamp the arm FAILED A
        // VALID PACK — bufferScale 0.5 with bufferScales [0.5, 0.5] renders the same on both hosts.
        const QJsonArray depthScales = meta.value(QLatin1String("bufferScales")).toArray();
        const auto clampScale = [](double v) {
            return qBound(PhosphorShaders::kMinBufferScale, v, PhosphorShaders::kMaxBufferScale);
        };
        const double pinnedScale = clampScale(rawScale);
        // BOUNDED to the pass budget like every sibling arm: fromJson drops an entry past it, so it
        // reaches neither host and cannot diverge, and this was the last unbounded per-entry loop.
        // No isEmpty() gate below either — an empty array runs no iteration and answers false here.
        bool scalesDiverge = false;
        const qsizetype depthCap = PhosphorSurfaceShaders::SurfaceShaderEffect::kMaxBufferPasses;
        for (qsizetype i = 0; i < depthScales.size() && i < depthCap && !scalesDiverge; ++i) {
            const QJsonValue v = depthScales.at(i);
            scalesDiverge = v.isDouble() && !qFuzzyCompare(clampScale(v.toDouble()), pinnedScale);
        }
        if (chainResolves && scalesDiverge && meta.value(QLatin1String("depthBuffer")).toBool()) {
            lints << QStringLiteral(
                "bufferScales is declared alongside \"depthBuffer\": true, and the daemon pins every pass to "
                "bufferScale, so no per-pass entry takes effect there (the passes share one depth attachment, "
                "whose size the colour attachments must match). The compositor implements no depth buffer, so "
                "it honours every bufferScales entry at any chain length, including the one-pass case the "
                "daemon still pins");
        }
    }
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

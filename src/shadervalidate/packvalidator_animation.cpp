// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The animation/transition arm of plasmazones-shader-validate — see
// packvalidators.h. Reproduces the animation runtime's GLSL assembly
// (pTransition / pIn+pOut entry scaffold + generated p_<id> preamble + include
// expansion). Every animation pack runs in the Qt-RHI settings preview and
// can attach in KWin, so validate both uniform ABIs independently. QShaderBaker
// checks the preview's SPIR-V path. External glslang checks the compositor's
// classic-GL branch after injecting PLASMAZONES_KWIN.

#include "packvalidators.h"

#include "packvalidatorcommon.h"

#include <PhosphorAnimation/AnimationShaderContract.h>
#include <PhosphorAnimation/AnimationShaderEffect.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorRendering/ShaderCompiler.h>
#include <PhosphorShaders/CustomParamsKey.h>
#include <PhosphorShaders/ShaderEntryPoint.h>
#include <PhosphorShaders/ShaderParamPreamble.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <rhi/qshader.h>

using PhosphorAnimationShaders::AnimationShaderEffect;
using PhosphorAnimationShaders::AnimationShaderRegistry;
using PhosphorRendering::ShaderCompiler;

namespace PlasmaZones::ShaderValidate {

// COVERAGE BOUNDARY, so this is not read as more than it is: the source is
// handed to glslang with the pack's own `#version 450` intact, while KWin's
// generateCustomShader recompiles at the GL context's core version (140 on the
// 6.7 path this dialect exists for). So a construct legal at 450 and illegal
// at 140 still passes here and still fails live — which is the class the ARB
// extension block in kwinDefineBlock was added to fix. Rewriting the version
// line before the bake would close it, and would need a trial run against
// every bundled compositor-only pack first, since it can only make the gate
// stricter. What this gate does cover is everything a wrong version would not
// have caught anyway: undeclared identifiers, type errors, bad swizzles, a
// p_<id> the preamble never emitted.
//
// Assemble one stage of an animation pack exactly as the
// kwin-effect does (entry scaffold for the fragment, include expansion, the
// p_<id> preamble, then the shared KWin define block after #version) and
// compile it through glslangValidator. Returns 1 on failure, 0 on success.
//
// The splice order matters and mirrors the runtime: each spliceAfterVersion
// lands its block immediately below #version, so splicing the preamble first
// and the define block second leaves the define block ABOVE the preamble,
// which is what the compositor produces (the preamble, then
// injectKwinDefineAfterVersion).
static int bakeCompositorStage(QTextStream& out, const QString& packDir,
                               const PhosphorAnimationShaders::AnimationShaderEffect& eff, const QString& path,
                               const QString& label, const QString& stage, bool scaffold)
{
    // The absent-stage bail comes FIRST so the two arms agree on the same
    // input: a pack whose declared stage file is missing is already linted by
    // the caller, and reporting a missing tool for it would describe the
    // machine rather than the pack, on a machine without glslang only.
    if (!QFile::exists(path)) {
        return 0; // an absent stage is already linted by the caller
    }
    const QString tool = glslangValidatorPath();
    if (tool.isEmpty()) {
        // Hard failure rather than a skip. The point of this gate is that a
        // pack cannot reach a release uncompiled, and a quiet "no tool, no
        // coverage" degrade is how 35 packs went unchecked in the first place.
        out << "  " << label.leftJustified(15)
            << "ERROR\n    neither glslangValidator nor glslang found on PATH. One of them is required to "
               "compile animation packs for the compositor (install the glslang package)\n";
        return 1;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        out << "  " << label.leftJustified(15) << "ERROR\n    cannot read " << path << "\n";
        return 1;
    }
    const QString raw = QString::fromUtf8(f.readAll());
    const QString assembled = scaffold
        ? PhosphorShaders::assembleEntryPoint(raw, AnimationShaderRegistry::animationEntryPrologue(),
                                              AnimationShaderRegistry::animationEntryCandidates())
        : raw;
    const QStringList includePaths = packSharedRoots(packDir);
    QString err;
    QString src = ShaderCompiler::expandSource(assembled, QFileInfo(path).absolutePath(), includePaths, &err);
    if (src.isEmpty()) {
        out << "  " << label.leftJustified(15) << "ERROR\n    include expansion failed: " << err << "\n";
        return 1;
    }
    src = PhosphorShaders::spliceAfterVersion(src, AnimationShaderRegistry::paramPreamble(eff));
    src = PhosphorShaders::spliceAfterVersion(src, PhosphorShaders::kwinDefineBlock());
    return reportCompositorCompile(out, label, stage, src, tool);
}

// Validate one ANIMATION pack directory (data/animations/*). Reproduces the
// animation runtime's fragment assembly on the daemon (Qt-RHI) path — the entry
// scaffold (pTransition / pIn+pOut, or a pass-through main()), the generated
// p_<id> preamble, and include expansion — then bakes through headless glslang.
// Returns the number of errors found.
//
// QShaderBaker rejects KWin's default-block uniforms, so bakeCompositorStage
// independently checks that branch through an OpenGL-target compiler.
// test_animation_shader_kwin_bake remains the additional driver-level check.
int validateAnimationPack(const QString& packDir, QTextStream& out)
{
    const QString name = QFileInfo(packDir).fileName();

    QFile metaFile(QDir(packDir).filePath(QStringLiteral("metadata.json")));
    if (!metaFile.open(QIODevice::ReadOnly)) {
        out << name << "\n  metadata       ERROR\n    cannot read metadata.json\n  → 1 error\n\n";
        return 1;
    }
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(metaFile.readAll(), &perr);
    if (doc.isNull() || !doc.isObject()) {
        // A parsed-but-non-object root would otherwise report "invalid JSON:
        // no error occurred" — perr only describes parse failures.
        const QString reason = doc.isNull() ? QStringLiteral("invalid JSON: ") + perr.errorString()
                                            : QStringLiteral("metadata root is not a JSON object");
        out << name << "\n  metadata       ERROR\n    " << reason << "\n  → 1 error\n\n";
        return 1;
    }

    AnimationShaderEffect eff = AnimationShaderEffect::fromJson(doc.object());
    eff.sourceDir = QDir(packDir).absolutePath();
    // Mirror AnimationShaderRegistry::parseEffect: the fragment path comes from
    // the metadata `fragmentShader` field, resolved relative to the pack dir and
    // confined to it (the metadata is user-editable, so a `../…`/absolute path
    // must be rejected, not opened and fed to glslang — same guard as
    // validateSurfacePack). The runtime does NOT default to effect.frag — a pack
    // that omits the field is unreachable, so an empty path stays empty and is
    // caught by isValid() below.
    if (!eff.fragmentShaderPath.isEmpty()) {
        const auto confined = confinedPackPath(packDir, eff.fragmentShaderPath);
        if (!confined) {
            out << name
                << "\n  metadata       ERROR\n    fragmentShader path escapes the pack directory (path traversal "
                   "rejected)\n  → 1 error\n\n";
            return 1;
        }
        eff.fragmentShaderPath = *confined;
    }
    // The optional `vertexShader` path is user-editable metadata too, so it
    // gets the same traversal guard as the fragment before the stage bake
    // below ever opens it.
    if (!eff.vertexShaderPath.isEmpty()) {
        const auto confinedVert = confinedPackPath(packDir, eff.vertexShaderPath);
        if (!confinedVert) {
            out << name
                << "\n  metadata       ERROR\n    vertexShader path escapes the pack directory (path traversal "
                   "rejected)\n  → 1 error\n\n";
            return 1;
        }
        eff.vertexShaderPath = *confinedVert;
    }
    if (!eff.isValid()) {
        out << name << "\n  metadata       ERROR\n    missing required field (id / fragmentShader)\n  → 1 error\n\n";
        return 1;
    }
    const QString fragLabel = QFileInfo(eff.fragmentShaderPath).fileName();

    out << name << "  (" << eff.parameters.size() << " param" << (eff.parameters.size() == 1 ? "" : "s") << ", "
        << eff.textures.size() << " texture" << (eff.textures.size() == 1 ? "" : "s") << ")\n";

    int errors = 0;

    // ── metadata lints ──
    // Animation params are always auto-slot (declaration order per pool), so
    // there are no explicit-slot collisions to detect; lint the param types and
    // ids that gate whether a p_<id> define is emitted at all.
    static const QStringList kAnimParamTypes = {QStringLiteral("float"), QStringLiteral("int"), QStringLiteral("bool"),
                                                QStringLiteral("color")};
    QStringList lints;
    for (const AnimationShaderEffect::ParameterInfo& p : eff.parameters) {
        if (!kAnimParamTypes.contains(p.type)) {
            lints << QStringLiteral(
                         "unknown param type '%1' for '%2' (animation params are float/int/bool/color; "
                         "images are textures, not params)")
                         .arg(p.type, p.id);
        }
        if (!PhosphorShaders::isValidParamId(p.id)) {
            lints
                << QStringLiteral("invalid parameter id '%1' (not a GLSL identifier; skipped, no p_ define)").arg(p.id);
        }
    }
    // Lint appliesTo tokens from RAW metadata: fromJson drops unknown tokens,
    // so a misspelled class silently changes the pack's runtime eligibility.
    // Token handling mirrors fromJson exactly:
    // trimmed comparison against the ProfilePaths vocabulary constants,
    // empty / whitespace-only tokens skipped silently.
    {
        namespace PP = PhosphorAnimation::ProfilePaths;
        // The exported vocabulary, not a copy: the lint and fromJson's
        // acceptance test must never disagree about what a valid token is.
        static const QStringList kAnimAppliesToTokens = PP::allEventClassTokens();
        const QJsonValue appliesToValue = doc.object().value(QLatin1String("appliesTo"));
        // A present-but-non-array appliesTo (e.g. a bare string) is ignored
        // wholesale by fromJson's .toArray() and the pack silently becomes
        // universal — a plausible author typo worth a shape diagnostic.
        if (!appliesToValue.isUndefined() && !appliesToValue.isArray()) {
            lints << QStringLiteral("appliesTo must be an array of tokens (ignored at load; pack becomes universal)");
        }
        // An explicit empty array (`"appliesTo": []`) draws NO lint, and that
        // is deliberate rather than an oversight — do not "even it up" with
        // the two cases below. Those two are TYPOS: the author wrote a
        // constraint, it validated down to nothing, and the pack silently
        // became universal, which is the opposite of what they asked for. An
        // empty array is not a typo; it says "no constraint", which is
        // exactly what it gets. Linting it would flag a legal spelling of
        // the documented default, and every universal pack that spells it
        // out would start failing the CI gate. Pinned by
        // test_pack_validators::explicitEmptyAppliesToIsNotLinted.
        const QJsonArray declaredAppliesTo = appliesToValue.toArray();
        for (const QJsonValue& v : declaredAppliesTo) {
            const QString token = v.toString().trimmed();
            if (token.isEmpty()) {
                // fromJson drops these without even a journal warning, so a
                // stray "" entry is the one appliesTo typo that leaves no
                // runtime trace at all — and an array that validates down to
                // empty makes the pack universal.
                lints << QStringLiteral(
                    "empty appliesTo token (dropped at load with no runtime warning; an "
                    "appliesTo that validates down to empty makes the pack universal)");
                continue;
            }
            if (!kAnimAppliesToTokens.contains(token)) {
                lints << QStringLiteral(
                             "unknown appliesTo token '%1' (dropped at load; an appliesTo that "
                             "validates down to empty makes the pack universal; valid tokens are %2)")
                             .arg(token, kAnimAppliesToTokens.join(QLatin1Char('/')));
            }
        }
    }
    // Texture lints mirror parseEffect's parse-time journal warnings. fromJson
    // silently drops these from the in-memory struct, so read the RAW metadata
    // textures array (same as parseEffect) rather than eff.textures.
    const QJsonArray declaredTextures = doc.object().value(QLatin1String("textures")).toArray();
    if (declaredTextures.size() > PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots) {
        lints << QStringLiteral("too many textures: %1 declared, cap is %2 (surplus dropped at load)")
                     .arg(static_cast<int>(declaredTextures.size()))
                     .arg(PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots);
    }
    for (const QJsonValue& v : declaredTextures) {
        const QString texPath = v.toObject().value(QLatin1String("path")).toString();
        if (texPath.isEmpty()) {
            lints << QStringLiteral("texture entry with empty `path` (dropped at load)");
        } else {
            // Confined and existence-checked like every SHADER path this
            // validator reads. Without it a typo'd or escaping texture shipped
            // as a green pack: the registry's traversal guard clears the path
            // and the sampler falls back to transparent black, so the failure
            // surfaces at the first live playback rather than in CI.
            const auto confined = confinedPackPath(packDir, texPath);
            if (!confined) {
                lints << QStringLiteral(
                             "texture path escapes the pack directory: %1 (rejected at load, sampler reads "
                             "transparent)")
                             .arg(texPath);
            } else if (!QFile::exists(*confined)) {
                lints << QStringLiteral("texture missing: %1 (sampler reads transparent at load)").arg(texPath);
            }
        }
        // Wrap vocabulary lint — read RAW metadata: AnimationShaderEffect::fromJson
        // silently clears an invalid texture wrap to empty, so a lint over the
        // parsed struct could never surface an author's typo. Mirrors the
        // surface validator.
        const QString wrap = v.toObject().value(QLatin1String("wrap")).toString();
        if (!wrap.isEmpty() && !PhosphorAnimationShaders::AnimationShaderContract::isValidWrapToken(wrap)) {
            lints
                << QStringLiteral("texture wrap not in {clamp,repeat,mirror}: %1 (cleared to clamp at load)").arg(wrap);
        }
    }
    // Multipass buffer lints — read RAW metadata, not the parsed struct:
    // AnimationShaderEffect::fromJson clamps bufferScale, drops missing buffers,
    // and silently coerces invalid buffer wrap/filter tokens, so a lint over the
    // parsed values would hide the exact author errors this block exists to
    // surface. The animation validator had none of these, unlike its two
    // siblings, even though the animation parser silently rewrites all of them.
    if (eff.isMultipass) {
        const QJsonObject animRoot = doc.object();
        const QJsonArray declaredBuffers = animRoot.value(QLatin1String("bufferShaders")).toArray();
        for (const QJsonValue& v : declaredBuffers) {
            const QString bufName = v.toString();
            if (bufName.isEmpty()) {
                // Kept in place at load (the arrays below are positionally
                // aligned with this one), and an empty entry fails the
                // load-time existence check, which fail-closes multipass for
                // the whole pack. Silent without this lint.
                lints << QStringLiteral(
                    "empty bufferShaders entry (kept in place for alignment; fails the load-time existence "
                    "check, which fail-closes multipass for the whole pack)");
                continue;
            }
            const auto confined = confinedPackPath(packDir, bufName);
            if (!confined) {
                lints << QStringLiteral("multipass buffer shader path escapes the pack directory: %1").arg(bufName);
            } else if (!QFile::exists(*confined)) {
                lints << QStringLiteral("multipass buffer shader missing: %1").arg(bufName);
            }
        }
        if (declaredBuffers.size() > PhosphorAnimationShaders::AnimationShaderContract::kMaxBufferPasses) {
            lints << QStringLiteral("too many buffer shaders: %1 declared, cap is %2 (surplus dropped at load)")
                         .arg(static_cast<int>(declaredBuffers.size()))
                         .arg(PhosphorAnimationShaders::AnimationShaderContract::kMaxBufferPasses);
        }
        const QJsonValue scaleVal = animRoot.value(QLatin1String("bufferScale"));
        // A non-numeric value (e.g. the string "0.5", a plausible typo since
        // the wrap/filter fields ARE strings) coerces to the 1.0 default at
        // load with no diagnostic anywhere — surface the shape error here.
        if (!scaleVal.isUndefined() && !scaleVal.isDouble()) {
            lints << QStringLiteral("bufferScale is not a number (falls back to 1.0 at load)");
        }
        const double rawScale = scaleVal.toDouble(1.0);
        if (rawScale < PhosphorAnimationShaders::AnimationShaderEffect::kMinBufferScale
            || rawScale > PhosphorAnimationShaders::AnimationShaderEffect::kMaxBufferScale) {
            lints << QStringLiteral("bufferScale out of range [%1, %2]: %3 (clamped at load)")
                         .arg(PhosphorAnimationShaders::AnimationShaderEffect::kMinBufferScale)
                         .arg(PhosphorAnimationShaders::AnimationShaderEffect::kMaxBufferScale)
                         .arg(rawScale);
        }
        const auto lintTokens = [&lints](const QJsonArray& arr, const QString& field, bool wrap) {
            for (const QJsonValue& v : arr) {
                const QString tok = v.toString();
                const bool ok = wrap ? PhosphorAnimationShaders::AnimationShaderContract::isValidWrapToken(tok)
                                     : PhosphorAnimationShaders::AnimationShaderContract::isValidFilterToken(tok);
                if (!tok.isEmpty() && !ok) {
                    lints << QStringLiteral("%1 value '%2' not in vocabulary (coerced at load)").arg(field, tok);
                }
            }
        };
        const QJsonArray wrapsArr = animRoot.value(QLatin1String("bufferWraps")).toArray();
        const QJsonArray filtersArr = animRoot.value(QLatin1String("bufferFilters")).toArray();
        lintTokens(wrapsArr, QStringLiteral("bufferWraps"), true);
        lintTokens(filtersArr, QStringLiteral("bufferFilters"), false);
        // Both arrays are positionally aligned with bufferShaders and trimmed
        // to its length at load, so a longer one silently loses its tail and a
        // shorter one leaves the last buffers on the single-value default.
        // Same length lint the surface validator applies to its own aligned
        // arrays.
        const auto lintLength = [&lints, &declaredBuffers](const QJsonArray& arr, const QString& field) {
            if (!arr.isEmpty() && arr.size() != declaredBuffers.size()) {
                lints << QStringLiteral(
                             "%1 has %2 entries for %3 buffer shaders (aligned positionally; "
                             "surplus dropped and missing entries fall back at load)")
                             .arg(field)
                             .arg(static_cast<int>(arr.size()))
                             .arg(static_cast<int>(declaredBuffers.size()));
            }
        };
        lintLength(wrapsArr, QStringLiteral("bufferWraps"));
        lintLength(filtersArr, QStringLiteral("bufferFilters"));
        const QString singleWrap = animRoot.value(QLatin1String("bufferWrap")).toString();
        if (!singleWrap.isEmpty() && !PhosphorAnimationShaders::AnimationShaderContract::isValidWrapToken(singleWrap)) {
            lints << QStringLiteral("bufferWrap value '%1' not in vocabulary (coerced at load)").arg(singleWrap);
        }
        const QString singleFilter = animRoot.value(QLatin1String("bufferFilter")).toString();
        if (!singleFilter.isEmpty()
            && !PhosphorAnimationShaders::AnimationShaderContract::isValidFilterToken(singleFilter)) {
            lints << QStringLiteral("bufferFilter value '%1' not in vocabulary (coerced at load)").arg(singleFilter);
        }
    }
    if (!QFile::exists(eff.fragmentShaderPath)) {
        lints << QStringLiteral("fragment shader missing: %1").arg(fragLabel);
    }
    // A declared-but-absent vertexShader falls back to a shared/default vertex
    // stage at runtime (shared/animation.vert on the daemon; the built-in kwin
    // vertex source, with a journal warning, on the compositor), hiding the
    // author's typo. Same lint the surface validator applies.
    if (!eff.vertexShaderPath.isEmpty() && !QFile::exists(eff.vertexShaderPath)) {
        lints << QStringLiteral("vertex shader missing: %1").arg(QFileInfo(eff.vertexShaderPath).fileName());
    }
    // The two screen-level passes draw ONE fixed full-screen quad through
    // their own vertex stage, so a vertexShader or a geometryGrid declared by
    // a desktop / strip pack is loaded, kept, and then never used. Say so
    // rather than let the author wonder why their vertex stage does nothing.
    {
        namespace PP = PhosphorAnimation::ProfilePaths;
        const bool screenLevel =
            eff.appliesTo.contains(PP::EventClassDesktop) || eff.appliesTo.contains(PP::EventClassStrip);
        if (screenLevel && !eff.vertexShaderPath.isEmpty()) {
            lints << QStringLiteral(
                "vertexShader is ignored for desktop/strip packs (the pass draws its own "
                "full-screen quad)");
        }
        if (screenLevel && eff.geometryGridSubdivisions > 0) {
            lints << QStringLiteral(
                "geometryGrid is ignored for desktop/strip packs (the pass draws its own "
                "full-screen quad)");
        }
        // Neither screen-level pass binds a pack's declared textures: the
        // desktop pass binds its two scene captures, the strip pass its
        // capture and below-strip snapshot, and nothing else. Worse than
        // dead, on the preview branch a declared texture lands on the very
        // slots those captures alias (uTexture1 / uTexture2), so the pack
        // would sample its image in place of the scene.
        if (screenLevel && !eff.textures.isEmpty()) {
            lints << QStringLiteral(
                "textures are ignored for desktop/strip packs (the pass binds only its own "
                "scene captures, which alias uTexture1/uTexture2 on the preview branch)");
        }
    }
    // geometryGrid is qBound(0, raw, cap) at load, so a negative value
    // silently becomes 0 — indistinguishable from not declaring it, and the
    // vertex stage the author wanted never subdivides. A fractional or
    // non-numeric value is just as quiet: QJsonValue::toInt returns 0 for
    // both, so lint on the double reading. The over-cap side already warns
    // on the journal at load; this is the quiet side.
    {
        const QJsonValue gridVal = doc.object().value(QLatin1String("geometryGrid"));
        if (!gridVal.isUndefined()) {
            const double rawGrid = gridVal.toDouble();
            const bool wholeNumber = gridVal.isDouble() && rawGrid == static_cast<double>(gridVal.toInt(-1));
            if (rawGrid < 0.0) {
                lints << QStringLiteral("geometryGrid is negative (%1); it clamps to 0 at load, disabling the grid")
                             .arg(rawGrid);
            } else if (!wholeNumber) {
                lints << QStringLiteral(
                    "geometryGrid is not a whole number; it reads as 0 at load, disabling the grid");
            }
        }
    }

    if (lints.isEmpty()) {
        out << "  metadata       OK\n";
    } else {
        out << "  metadata       ERROR\n";
        for (const QString& l : lints) {
            out << "    " << l << "\n";
            ++errors;
        }
    }

    // Every class has a Qt-RHI preview, including compositor-only events.
    // Check both branches rather than allowing appliesTo to bypass either one.
    errors += bakeCompositorStage(out, packDir, eff, eff.fragmentShaderPath, fragLabel, QStringLiteral("frag"),
                                  /*scaffold=*/true);
    const QString previewFragLabel = fragLabel + QStringLiteral(" (Qt-RHI preview) ");
    if (QFile::exists(eff.fragmentShaderPath)) {
        QFile frag(eff.fragmentShaderPath);
        if (!frag.open(QIODevice::ReadOnly | QIODevice::Text)) {
            out << "  " << previewFragLabel.leftJustified(15) << "ERROR\n    cannot read " << eff.fragmentShaderPath
                << "\n";
            ++errors;
        } else {
            const QString raw = QString::fromUtf8(frag.readAll());
            // An entry-only pack (pTransition / pIn+pOut) gets a generated main();
            // a traditional main() pack passes through — exactly as both runtimes do.
            const QString assembled =
                PhosphorShaders::assembleEntryPoint(raw, AnimationShaderRegistry::animationEntryPrologue(),
                                                    AnimationShaderRegistry::animationEntryCandidates());
            // Animation runtime include paths are `shared`-only (see
            // surfaceanimator.cpp animIncludePaths, which appends only each
            // search path's `/shared` subdir), so the animation gate matches it.
            const QStringList includePaths = packSharedRoots(packDir);
            QString err;
            const QString expanded = ShaderCompiler::expandSource(
                assembled, QFileInfo(eff.fragmentShaderPath).absolutePath(), includePaths, &err);
            if (expanded.isEmpty()) {
                out << "  " << previewFragLabel.leftJustified(15) << "ERROR\n    include expansion failed: " << err
                    << "\n";
                ++errors;
            } else {
                const QString spliced =
                    PhosphorShaders::spliceAfterVersion(expanded, AnimationShaderRegistry::paramPreamble(eff));
                const ShaderCompiler::Result result = ShaderCompiler::compile(spliced.toUtf8(), QShader::FragmentStage);
                errors += reportCompile(out, previewFragLabel, result, declaredParamNames(eff.parameters));
            }
        }
    }

    // ── multipass buffer passes ──
    // The zone and surface validators have always baked their buffer passes;
    // the animation validator never did, so a broken buffer shader in an
    // animation pack passed this gate and failed only at the live daemon.
    //
    // Reproduce ShaderNodeRhi::bakeBufferShaders exactly: load, expand
    // includes, compile. NO entry scaffold (there is no pTransition to wrap)
    // and — verified against that function — NO p_<id> preamble either, so a
    // buffer pass that wants the pack's parameters must include
    // <animation_uniforms.glsl> itself and read customParams directly.
    // Splicing a preamble the runtime does not splice would be worse than no
    // coverage: the gate would pass sources that fail live and fail sources
    // that work.
    //
    // Qt previews configure multipass buffers for every event class through
    // applyEffectStaticConfig. The compositor executes only the final stage.
    if (eff.isMultipass) {
        const QStringList includePaths = packSharedRoots(packDir);
        for (const QString& declaredBuf : eff.bufferShaderPaths) {
            // fromJson leaves these RELATIVE (unlike the fragment path, which
            // the block at the top of this function resolves), so resolve
            // against the pack dir the way the runtime's parseEffect does.
            // Baking the raw string would silently find nothing and report a
            // clean pack — which is exactly how this whole stage was missing.
            if (declaredBuf.isEmpty()) {
                continue; // already linted above
            }
            const auto confinedBuf = confinedPackPath(packDir, declaredBuf);
            if (!confinedBuf || !QFile::exists(*confinedBuf)) {
                continue; // escaping and absent buffers are already linted above
            }
            const QString buf = *confinedBuf;
            const QString label = QFileInfo(buf).fileName();
            QFile bufFile(buf);
            if (!bufFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                out << "  " << label.leftJustified(15) << "ERROR\n    cannot read " << buf << "\n";
                ++errors;
                continue;
            }
            const QString rawBuf = QString::fromUtf8(bufFile.readAll());
            QString bufErr;
            const QString expandedBuf =
                ShaderCompiler::expandSource(rawBuf, QFileInfo(buf).absolutePath(), includePaths, &bufErr);
            if (expandedBuf.isEmpty()) {
                out << "  " << label.leftJustified(15) << "ERROR\n    include expansion failed: " << bufErr << "\n";
                ++errors;
                continue;
            }
            const ShaderCompiler::Result bufResult =
                ShaderCompiler::compile(expandedBuf.toUtf8(), QShader::FragmentStage);
            errors += reportCompile(out, label, bufResult, declaredParamNames(eff.parameters));
        }
    }

    // ── vertex stage ──
    // The daemon warm-bake compiles the pack's declared vertexShader, falling back to
    // shared/animation.vert (shader_warmup.cpp). Without baking it here a broken
    // animation vert — or a regression in the shared vert every pack inherits —
    // passes the shader_validate_bundled CI gate and only fails at runtime. The
    // sibling zone and surface validators have always baked their vert stage.
    if (!eff.vertexShaderPath.isEmpty()) {
        errors += bakeCompositorStage(out, packDir, eff, eff.vertexShaderPath,
                                      QFileInfo(eff.vertexShaderPath).fileName(), QStringLiteral("vert"),
                                      /*scaffold=*/false);
    }
    {
        const QStringList animIncludeDirs = packSharedRoots(packDir);
        QString vertPath = eff.vertexShaderPath;
        if (vertPath.isEmpty()) {
            // The default vertex stage lives beside the family's shared
            // helpers, so it has to be looked up across the same roots: an
            // installed pack finds it in the system prefix, not next to
            // itself.
            for (const QString& dir : animIncludeDirs) {
                const QString sharedVert = dir + QStringLiteral("/animation.vert");
                if (QFile::exists(sharedVert)) {
                    vertPath = sharedVert;
                    break;
                }
            }
        }
        if (!vertPath.isEmpty() && QFile::exists(vertPath)) {
            const QString vertLabel = QFileInfo(vertPath).fileName() + QStringLiteral(" (Qt-RHI preview) ");
            QFile vertFile(vertPath);
            if (!vertFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                out << "  " << vertLabel.leftJustified(15) << "ERROR\n    cannot read " << vertPath << "\n";
                ++errors;
            } else {
                const QString rawVert = QString::fromUtf8(vertFile.readAll());
                QString vertErr;
                const QString expandedVert = ShaderCompiler::expandSource(rawVert, QFileInfo(vertPath).absolutePath(),
                                                                          animIncludeDirs, &vertErr);
                if (expandedVert.isEmpty()) {
                    out << "  " << vertLabel.leftJustified(15) << "ERROR\n    include expansion failed: " << vertErr
                        << "\n";
                    ++errors;
                } else {
                    // The p_<id> preamble IS spliced here, mirroring the
                    // runtime: ShaderNodeRhi's vertex compile and the warm
                    // bake both splice m_paramPreamble into the vertex
                    // stage (added alongside the vertex-parameter work, for
                    // parity with the kwin path's one-preamble-both-stages
                    // splice), so vertex-driven packs can read p_<id> on
                    // both branches. Baking without it would reject sources
                    // the runtime accepts.
                    const QString splicedVert =
                        PhosphorShaders::spliceAfterVersion(expandedVert, AnimationShaderRegistry::paramPreamble(eff));
                    const ShaderCompiler::Result vertResult =
                        ShaderCompiler::compile(splicedVert.toUtf8(), QShader::VertexStage);
                    errors += reportCompile(out, vertLabel, vertResult, declaredParamNames(eff.parameters));
                }
            }
        }
    }

    if (errors == 0) {
        out << "  → OK\n\n";
    } else {
        out << "  → " << errors << (errors == 1 ? " error\n\n" : " errors\n\n");
    }
    return errors;
}

} // namespace PlasmaZones::ShaderValidate

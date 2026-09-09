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
#include <PhosphorShaders/ShaderEntryPoint.h>
#include <PhosphorShaders/ShaderIncludeResolver.h>
#include <PhosphorShaders/ShaderParamPreamble.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <rhi/qshader.h>

#include <cmath>

using PhosphorAnimationShaders::AnimationShaderEffect;
using PhosphorAnimationShaders::AnimationShaderRegistry;
using PhosphorRendering::ShaderCompiler;

namespace PlasmaZones::ShaderValidate {

namespace {

// One stage file read for both arms. Prints under @p label and returns false
// on a read failure or an empty file, so neither arm reports an empty file as
// a failed include expansion (the compositor refuses an empty stage before
// assembly with its own message, and so does this).
bool readStage(QTextStream& out, const QString& label, const QString& path, QString& raw)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        out << "  " << padLabel(label) << "ERROR\n    cannot read " << path << "\n";
        return false;
    }
    raw = QString::fromUtf8(f.readAll());
    if (raw.isEmpty()) {
        out << "  " << padLabel(label) << "ERROR\n    shader file is empty\n";
        return false;
    }
    return true;
}

// The compositor's HDR colour-management block declares `pzFinalizeColor` and
// routes the generated main()'s fragColor write through it. The real block
// includes KWin's `colormanagement.glsl` from a resource path glslang cannot
// see, so the bake splices an identity stand-in that declares the SAME symbol
// and macro: a pack that defines its own `pzFinalizeColor`, or redefines
// `PZ_FINALIZE_COLOR`, collides here the way it collides live.
QString finalizeColorStub()
{
    return QStringLiteral(
        "vec4 pzFinalizeColor(vec4 c) { return c; }\n"
        "#define PZ_FINALIZE_COLOR(c) pzFinalizeColor(c)\n");
}

// A shader body with its `//` line comments removed, for the textual lints
// that ask whether a pack CALLS something. GLSL has no string literals, so a
// line-wise strip is exact for line comments; a block comment is not handled,
// which only makes those lints more lenient, never stricter.
QString withoutLineComments(const QString& source)
{
    QString code;
    for (const QString& line : source.split(QLatin1Char('\n'))) {
        code += line.section(QLatin1String("//"), 0, 0) + QLatin1Char('\n');
    }
    return code;
}

} // namespace

// COVERAGE BOUNDARY, so this is not read as more than it is. Three things the
// live compositor does that this bake cannot reproduce:
//
// 1. The source is handed to glslang with the pack's own `#version 450`
//    intact, while KWin's generateCustomShader recompiles at the GL context's
//    core version (140 on the 6.7 path this dialect exists for). So a
//    construct legal at 450 and illegal at 140 still passes here and still
//    fails live — which is the class the ARB extension block in
//    kwinDefineBlock was added to fix. Rewriting the version line before the
//    bake would close it, and would need a trial run against every bundled
//    compositor-only pack first, since it can only make the gate stricter.
// 2. The per-window path splices KWin's colour-management block, which
//    `#include`s a KWin resource. The bake splices an identity stand-in
//    (finalizeColorStub) that declares the same symbols, so a symbol collision
//    is caught but the HDR transfer itself is not compiled.
// 3. The strip pass refuses a linked program that never samples `uStrip`
//    (optimised out at link). Whether a pack samples it is a link-time
//    property; the textual lint over `getStripColor` below catches the
//    common case, and a pack that calls it only from dead code still fails
//    live.
// 4. A main() pack with no `#version` line is linted below, and the bake
//    then still runs: spliceAfterVersion prepends its blocks to such a
//    source, where the compositor's injectKwinDefineAfterVersion synthesizes
//    `#version 450` and warns. The compile errors that follow the lint on
//    that pack describe the missing line, not a second defect.
//
// What this gate does cover is everything a wrong version would not have
// caught anyway: undeclared identifiers, type errors, bad swizzles, a p_<id>
// the preamble never emitted.
//
// Assemble one stage of an animation pack exactly as the kwin-effect does
// (entry scaffold for the fragment, include expansion through the SAME
// resolver the compositor uses, the p_<id> preamble, the colour-management
// stand-in on the per-window path, then the shared KWin define block after
// #version) and compile it through glslangValidator. Returns 1 on failure, 0
// on success.
//
// The include resolver matters: the compositor calls
// ShaderIncludeResolver::expandIncludes with the pack's shared roots only and
// searches the pack's own directory for `"..."` includes alone, whereas the
// Qt-RHI preview goes through ShaderCompiler::expandSource, which also
// searches the shader's directory for `<...>`. A pack-local `<x.glsl>` include
// therefore resolves on the preview and not on the compositor, and this arm
// has to see that.
//
// The splice order matters and mirrors the runtime: each spliceAfterVersion
// lands its block immediately below #version, so splicing the preamble first,
// the finalize block second and the define block last leaves the define block
// ABOVE the other two, which is what the compositor produces.
static int bakeCompositorStage(QTextStream& out, const QString& raw, const QStringList& includePaths,
                               const AnimationShaderEffect& eff, const QString& path, const QString& label,
                               const QString& stage, bool scaffold, bool finalizeColor)
{
    const QString tool = glslangValidatorPath();
    if (tool.isEmpty()) {
        // Hard failure rather than a skip. The point of this gate is that a
        // pack cannot reach a release uncompiled, and a quiet "no tool, no
        // coverage" degrade is how 35 packs went unchecked in the first place.
        // The explanation is printed once per run; every stage still counts
        // the error, so a tree of a hundred packs fails a hundred times but
        // says why once.
        static bool explained = false;
        out << "  " << padLabel(label) << "ERROR (compositor)\n";
        if (!explained) {
            explained = true;
            out << "    neither glslangValidator nor glslang found on PATH. One of them is required to "
                   "compile animation packs for the compositor (install the glslang package)\n";
        }
        return 1;
    }
    const QString assembled = scaffold
        ? PhosphorShaders::assembleEntryPoint(raw, AnimationShaderRegistry::animationEntryPrologue(),
                                              AnimationShaderRegistry::animationEntryCandidates())
        : raw;
    QString err;
    QString src = PhosphorShaders::ShaderIncludeResolver::expandIncludes(assembled, QFileInfo(path).absolutePath(),
                                                                         includePaths, &err);
    if (!err.isEmpty() || src.isEmpty()) {
        // Tagged like the compile outcome: the preview arm resolves includes
        // differently, so the report has to say which arm failed to expand.
        out << "  " << padLabel(label) << "ERROR (compositor)\n    include expansion failed: " << err << "\n";
        return 1;
    }
    src = PhosphorShaders::spliceAfterVersion(src, AnimationShaderRegistry::paramPreamble(eff));
    if (finalizeColor) {
        src = PhosphorShaders::spliceAfterVersion(src, finalizeColorStub());
    }
    src = PhosphorShaders::spliceAfterVersion(src, PhosphorShaders::kwinDefineBlock());
    return reportCompositorCompile(out, label, stage, src, tool);
}

// The Qt-RHI preview arm of one stage: the same scaffold, the preview's include
// resolver, the p_<id> preamble when @p preamble is non-empty, then
// QShaderBaker. @p declared feeds the did-you-mean hint and is empty for a
// stage that receives no preamble. Returns 1 on failure, 0 on success.
static int bakePreviewStage(QTextStream& out, const QString& raw, const QStringList& includePaths, const QString& path,
                            const QString& label, QShader::Stage stage, bool scaffold, const QString& preamble,
                            const QStringList& declared)
{
    const QString assembled = scaffold
        ? PhosphorShaders::assembleEntryPoint(raw, AnimationShaderRegistry::animationEntryPrologue(),
                                              AnimationShaderRegistry::animationEntryCandidates())
        : raw;
    QString err;
    const QString expanded =
        ShaderCompiler::expandSource(assembled, QFileInfo(path).absolutePath(), includePaths, &err);
    if (expanded.isEmpty()) {
        out << "  " << padLabel(label) << "ERROR\n    include expansion failed: " << err << "\n";
        return 1;
    }
    const QString spliced = PhosphorShaders::spliceAfterVersion(expanded, preamble);
    return reportCompile(out, label, ShaderCompiler::compile(spliced.toUtf8(), stage), declared);
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
    int scalarParams = 0;
    int colorParams = 0;
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
        // The same split translateAnimationParams makes: colour or scalar,
        // nothing else, so an unknown type still consumes a scalar lane.
        if (p.type == QLatin1String("color")) {
            ++colorParams;
        } else {
            ++scalarParams;
        }
    }
    // Slot budget. The registry drops every scalar past kMaxParameterSlots
    // and every colour past kMaxCustomColors at load with a journal warning,
    // and buildParamPreamble emits no p_<id> for them. A pack that reads such
    // a parameter fails its bake with a bare undeclared-identifier error the
    // did-you-mean hint cannot explain (the name IS declared); one that does
    // not read it ships green and warns on every leg. The texture and buffer
    // caps below have had this lint all along.
    if (scalarParams > PhosphorAnimationShaders::AnimationShaderContract::kMaxParameterSlots) {
        lints << QStringLiteral(
                     "too many scalar params: %1 declared, budget is %2 (surplus get no p_<id> and "
                     "are dropped at load)")
                     .arg(scalarParams)
                     .arg(PhosphorAnimationShaders::AnimationShaderContract::kMaxParameterSlots);
    }
    if (colorParams > PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors) {
        lints << QStringLiteral(
                     "too many color params: %1 declared, budget is %2 (surplus get no p_<id> and "
                     "are dropped at load)")
                     .arg(colorParams)
                     .arg(PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors);
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
    // The cap counts the entries the loader KEEPS, as parseEffect does: an
    // empty-path entry is dropped before it can occupy a slot and is linted on
    // its own below.
    int keptTextures = 0;
    for (const QJsonValue& v : declaredTextures) {
        if (!v.toObject().value(QLatin1String("path")).toString().isEmpty()) {
            ++keptTextures;
        }
    }
    if (keptTextures > PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots) {
        lints << QStringLiteral("too many textures: %1 declared, cap is %2 (surplus dropped at load)")
                     .arg(keptTextures)
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
    // The fragment and vertex sources are read ONCE here, for the textual
    // lints below and for both bake arms after them. An unreadable or empty
    // stage leaves the string empty; the bakes then call readStage to report
    // it under the stage label, so the lints stay quiet about it.
    QString fragRaw;
    QString vertRaw;
    if (!QFile::exists(eff.fragmentShaderPath)) {
        lints << QStringLiteral("fragment shader missing: %1").arg(fragLabel);
    } else if (QFile frag(eff.fragmentShaderPath); frag.open(QIODevice::ReadOnly | QIODevice::Text)) {
        fragRaw = QString::fromUtf8(frag.readAll());
    }
    if (!eff.vertexShaderPath.isEmpty() && QFile::exists(eff.vertexShaderPath)) {
        if (QFile vert(eff.vertexShaderPath); vert.open(QIODevice::ReadOnly | QIODevice::Text)) {
            vertRaw = QString::fromUtf8(vert.readAll());
        }
    }
    // A pack that ships its own main() is passed through both scaffolds
    // unchanged, so everything the generated main() would have done for it is
    // its own responsibility. The checks that follow are the runtime's own
    // refusals and degradations for such a pack, applied statically.
    const bool fragDefinesMain = !fragRaw.isEmpty() && PhosphorShaders::definesMain(fragRaw);
    // A `#version` line is the scaffold's job for an entry-only pack and the
    // author's for a main() pack. The compositor synthesizes `#version 450`
    // for a main() pack that lacks one and warns; the preview's baker and this
    // gate's glslang run would otherwise compile it at the default dialect
    // and report errors about `in`/`out`/`texture` the author's file does not
    // contain. Say what is missing instead.
    if (fragDefinesMain && PhosphorShaders::versionDirectiveEnd(fragRaw) < 0) {
        lints << QStringLiteral(
            "fragment shader defines main() but no #version directive (declare `#version 450`; the compositor "
            "synthesizes one with a warning, the preview does not)");
    }
    if (!vertRaw.isEmpty() && PhosphorShaders::versionDirectiveEnd(vertRaw) < 0) {
        lints << QStringLiteral("vertex shader has no #version directive (declare `#version 450`)");
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
    // The predicate is reused by the bakes: those two passes also splice no
    // colour-management block.
    const bool screenLevel = eff.appliesTo.contains(PhosphorAnimation::ProfilePaths::EventClassDesktop)
        || eff.appliesTo.contains(PhosphorAnimation::ProfilePaths::EventClassStrip);
    {
        namespace PP = PhosphorAnimation::ProfilePaths;
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
        // The strip pass abandons a pack that ships its own main() before it
        // compiles anything (striptransitionshader.cpp): the generated main's
        // PZ_FINALIZE_COLOR call is the re-composite of the pack's output over
        // the undisplaced below-strip content, and a hand-rolled main() skips
        // it, blacking out every gap between the columns. Both bakes pass
        // such a pack, so this is the only place it can be caught offline.
        const bool stripPack = eff.appliesTo.contains(PP::EventClassStrip);
        if (stripPack && fragDefinesMain) {
            lints << QStringLiteral(
                "strip packs must not define main() (the strip pass abandons the pack at load; write "
                "pTransition or pIn+pOut)");
        }
        // The strip pass also abandons a linked program that never samples
        // uStrip, and only getStripColor() reads it (the generated main's
        // re-composite samples uBelow). Whether the sampler survives the link
        // cannot be seen here, but a body that never mentions either name
        // cannot possibly keep it, so say so. A call kept only in dead code
        // still links it out live; that residual is the runtime's to report.
        if (stripPack && !fragRaw.isEmpty() && !fragDefinesMain) {
            const QString code = withoutLineComments(fragRaw);
            if (!code.contains(QLatin1String("getStripColor")) && !code.contains(QLatin1String("uStrip"))) {
                lints << QStringLiteral(
                    "strip packs must sample the strip through getStripColor() (a program that never reads "
                    "uStrip is abandoned by the strip pass at link)");
            }
        }
        // On the per-window path the compositor routes the generated main's
        // fragColor write through PZ_FINALIZE_COLOR, its HDR colour
        // management. A main() pack writes fragColor itself, and unless it
        // calls the macro (or bmw_compat's setOutputColor, which does) its
        // output skips that transfer and renders dim on an HDR output with
        // no diagnostic anywhere. The two screen-level passes keep the
        // identity macro on purpose, so they are exempt.
        if (!screenLevel && fragDefinesMain) {
            const QString code = withoutLineComments(fragRaw);
            if (!code.contains(QLatin1String("PZ_FINALIZE_COLOR")) && !code.contains(QLatin1String("setOutputColor"))) {
                lints << QStringLiteral(
                    "fragment shader defines main() but never routes fragColor through PZ_FINALIZE_COLOR "
                    "(renders without HDR colour management on the compositor; write an entry-only pack, or "
                    "assign `fragColor = PZ_FINALIZE_COLOR(color)`)");
            }
        }
    }
    // geometryGrid is qBound(0, raw, cap) at load, so a negative value
    // silently becomes 0 — indistinguishable from not declaring it, and the
    // vertex stage the author wanted never subdivides. A fractional or
    // non-numeric value is just as quiet: QJsonValue::toInt returns 0 for
    // both, so each gets its own message. The over-cap side already warns on
    // the journal at load; this is the quiet side.
    {
        const QJsonValue gridVal = doc.object().value(QLatin1String("geometryGrid"));
        if (!gridVal.isUndefined()) {
            if (!gridVal.isDouble()) {
                lints << QStringLiteral("geometryGrid is not a number (reads as 0 at load, disabling the grid)");
            } else {
                const double rawGrid = gridVal.toDouble();
                if (rawGrid < 0.0) {
                    lints << QStringLiteral("geometryGrid is negative (%1); it clamps to 0 at load, disabling the grid")
                                 .arg(rawGrid);
                } else if (rawGrid != std::floor(rawGrid)) {
                    lints << QStringLiteral(
                        "geometryGrid is not a whole number; it reads as 0 at load, disabling the grid");
                }
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

    // Every class has a Qt-RHI preview, including compositor-only events
    // (the settings preview stages every class through a ShaderEffect and
    // never gates on shaderEffectIsCompositorOnly). Check both branches
    // rather than allowing appliesTo to bypass either one. The roots are
    // resolved once per pack: every stage of it bakes against the same set.
    const QStringList includePaths = packSharedRoots(packDir);
    const QString preamble = AnimationShaderRegistry::paramPreamble(eff);
    const QStringList declared = declaredParamNames(eff.parameters);

    // ── fragment stage ──
    // Read once for both arms; an unreadable or empty fragment is one error
    // under the compositor label rather than two under both. An absent
    // fragment is already linted above.
    if (QFile::exists(eff.fragmentShaderPath)) {
        QString raw = fragRaw;
        if (raw.isEmpty() && !readStage(out, fragLabel, eff.fragmentShaderPath, raw)) {
            ++errors;
        } else {
            // The per-window path splices KWin's colour-management block; the
            // desktop and strip passes keep the header's identity macro.
            errors += bakeCompositorStage(out, raw, includePaths, eff, eff.fragmentShaderPath, fragLabel,
                                          QStringLiteral("frag"), /*scaffold=*/true,
                                          /*finalizeColor=*/!screenLevel);
            errors += bakePreviewStage(out, raw, includePaths, eff.fragmentShaderPath,
                                       fragLabel + QStringLiteral(" (Qt-RHI preview)"), QShader::FragmentStage,
                                       /*scaffold=*/true, preamble, declared);
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
    // that work. For the same reason the did-you-mean hint gets no declared
    // names here: an unscaffolded stage cannot see any p_<id>.
    //
    // Qt previews configure multipass buffers for every event class through
    // applyEffectStaticConfig. The compositor executes only the final stage
    // (shader_textures.cpp logs "buffer passes skipped"), so the report says
    // so: a multipass pack is a daemon-and-preview feature, and its final
    // stage runs alone wherever the compositor attaches it.
    if (eff.isMultipass) {
        out << "  note           multipass buffer passes run on the daemon and the preview only; the compositor "
               "runs the final stage alone\n";
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
            QString rawBuf;
            if (!readStage(out, label, buf, rawBuf)) {
                ++errors;
                continue;
            }
            errors += bakePreviewStage(out, rawBuf, includePaths, buf, label, QShader::FragmentStage,
                                       /*scaffold=*/false, QString(), QStringList());
        }
    }

    // ── vertex stage ──
    // The daemon warm-bake compiles the pack's declared vertexShader, falling back to
    // shared/animation.vert (shader_warmup.cpp). Without baking it here a broken
    // animation vert — or a regression in the shared vert every pack inherits —
    // passes the shader_validate_bundled CI gate and only fails at runtime. The
    // sibling zone and surface validators have always baked their vert stage.
    //
    // The p_<id> preamble IS spliced into the vertex stage on both arms,
    // mirroring the runtime: ShaderNodeRhi's vertex compile, the warm bake and
    // the compositor all splice it (added alongside the vertex-parameter work,
    // for parity with the kwin path's one-preamble-both-stages splice), so
    // vertex-driven packs can read p_<id> on both branches. Baking without it
    // would reject sources the runtime accepts.
    if (!eff.vertexShaderPath.isEmpty() && QFile::exists(eff.vertexShaderPath)) {
        // The declared vert: both arms. The compositor never splices the
        // colour-management block into a vertex stage.
        const QString vertLabel = QFileInfo(eff.vertexShaderPath).fileName();
        QString raw = vertRaw;
        if (raw.isEmpty() && !readStage(out, vertLabel, eff.vertexShaderPath, raw)) {
            ++errors;
        } else {
            errors += bakeCompositorStage(out, raw, includePaths, eff, eff.vertexShaderPath, vertLabel,
                                          QStringLiteral("vert"), /*scaffold=*/false, /*finalizeColor=*/false);
            errors += bakePreviewStage(out, raw, includePaths, eff.vertexShaderPath,
                                       vertLabel + QStringLiteral(" (Qt-RHI preview)"), QShader::VertexStage,
                                       /*scaffold=*/false, preamble, declared);
        }
    } else if (eff.vertexShaderPath.isEmpty()) {
        // No declared vert: the preview falls back to the family's shared
        // animation.vert, which lives beside the shared helpers and so has to
        // be looked up across the same roots (an installed pack finds it in
        // the system prefix, not next to itself). The compositor uses its own
        // built-in vertex source in that case, so only the preview arm bakes.
        QString vertPath;
        for (const QString& dir : includePaths) {
            const QString sharedVert = dir + QStringLiteral("/animation.vert");
            if (QFile::exists(sharedVert)) {
                vertPath = sharedVert;
                break;
            }
        }
        if (!vertPath.isEmpty()) {
            const QString vertLabel = QFileInfo(vertPath).fileName() + QStringLiteral(" (Qt-RHI preview)");
            QString raw;
            if (!readStage(out, vertLabel, vertPath, raw)) {
                ++errors;
            } else {
                errors += bakePreviewStage(out, raw, includePaths, vertPath, vertLabel, QShader::VertexStage,
                                           /*scaffold=*/false, preamble, declared);
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

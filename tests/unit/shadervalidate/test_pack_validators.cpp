// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The offline animation-pack validator's metadata lints. The bundled-pack CI
// gate (shader_validate_animations) only proves the shipped packs are clean —
// it cannot show that a BROKEN pack is actually caught, which is how an
// appliesTo token that the parser accepted and the lint rejected shipped
// undetected. These tests build deliberately-broken packs in a temp dir and
// assert the diagnostic.
//
// Mostly metadata lints, plus two bakes that the bundled-pack gate cannot
// exercise on its own. The multipass buffer pass has no other coverage at all
// (no bundled animation pack is multipass, so the CI gate walks straight past
// it). The COMPOSITOR-ONLY bake has the opposite problem: a third of the
// bundled packs take it, but a test over shipped packs can only show that
// clean source passes, never that broken source is caught, and that path used
// to be a silent skip.

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTextStream>

#include <PhosphorAnimation/ProfilePaths.h>

#include "shadervalidate/packvalidatorcommon.h"
#include "shadervalidate/packvalidators.h"

namespace {

struct PackResult
{
    int errors = 0;
    QString report;
};

/// The validator derives its include path from the pack's PARENT directory
/// (`<packs-root>/shared`), matching the animation runtime. A temp packs-root
/// has no such directory, so every fragment stage would fail include
/// expansion for reasons that have nothing to do with the pack under test.
/// Link the real bundled one in once per root.
///
/// Returns false when the source tree is not available, which is the caller's
/// cue to skip rather than fail.
bool linkSharedIncludes(const QTemporaryDir& tmp)
{
    const QString target = QStringLiteral(P_SOURCE_DIR "/data/animations/shared");
    if (!QDir(target).exists()) {
        return false;
    }
    const QString link = tmp.filePath(QStringLiteral("shared"));
    if (QFileInfo::exists(link)) {
        return true;
    }
    return QFile::link(target, link);
}

/// Write a pack directory from @p metadata (plus a trivial fragment shader
/// unless the caller declared its own) and run the animation validator over
/// it, capturing the report.
PackResult validate(const QTemporaryDir& tmp, const QString& name, const QJsonObject& metadata,
                    bool writeFragment = true)
{
    const QString dir = tmp.filePath(name);
    QDir().mkpath(dir);
    QFile meta(dir + QStringLiteral("/metadata.json"));
    if (!meta.open(QIODevice::WriteOnly)) {
        return {};
    }
    meta.write(QJsonDocument(metadata).toJson());
    meta.close();
    if (writeFragment) {
        QFile frag(dir + QStringLiteral("/effect.frag"));
        if (!frag.open(QIODevice::WriteOnly)) {
            return {};
        }
        frag.write("vec4 pTransition(vec2 uv, float t) { return vec4(0.0); }\n");
        frag.close();
    }

    PackResult result;
    QTextStream stream(&result.report);
    result.errors = PlasmaZones::ShaderValidate::validateAnimationPack(dir, stream);
    stream.flush();
    return result;
}

QJsonObject basePack(const QString& id)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("name"), QStringLiteral("Test Pack"));
    obj.insert(QStringLiteral("fragmentShader"), QStringLiteral("effect.frag"));
    return obj;
}

/// A bundled shared helper's CODE, with its `//` comments removed. The strip
/// helper's prose names iStripAxis a dozen times, so an assertion about what
/// the helper computes has to read past it or a commented-out body would
/// satisfy it.
///
/// Returns an empty string when the source tree is not available, the same
/// skip cue as linkSharedIncludes.
///
/// The strip is textual, not a lexer: a `//` inside a string or a block
/// comment would confuse it. GLSL has no string literals and the shared
/// helpers use line comments throughout, so the fixtures this reads never hit
/// that case. Left simple on purpose rather than hardened.
QString sharedHelperCode(const QString& fileName)
{
    QFile file(QStringLiteral(P_SOURCE_DIR "/data/animations/shared/") + fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    QString code;
    const QString text = QString::fromUtf8(file.readAll());
    for (const QString& line : text.split(QLatin1Char('\n'))) {
        code += line.section(QLatin1String("//"), 0, 0) + QLatin1Char('\n');
    }
    return code;
}

/// The body of the GLSL function named @p name in @p code, between its opening
/// brace and the matching close. Empty when the function is absent.
///
/// Matches the FIRST occurrence of the name, so a call site appearing before
/// the definition, or a longer name ending in @p name, would pick the wrong
/// body. The helpers this reads define each function before any use and share
/// no such name suffix. Left simple on purpose rather than hardened.
QString glslFunctionBody(const QString& code, const QString& name)
{
    const int signature = code.indexOf(name + QLatin1Char('('));
    if (signature < 0) {
        return QString();
    }
    const int open = code.indexOf(QLatin1Char('{'), signature);
    if (open < 0) {
        return QString();
    }
    int depth = 0;
    for (int i = open; i < code.size(); ++i) {
        if (code.at(i) == QLatin1Char('{')) {
            ++depth;
        } else if (code.at(i) == QLatin1Char('}')) {
            if (--depth == 0) {
                return code.mid(open + 1, i - open - 1);
            }
        }
    }
    return QString();
}

QJsonArray toArray(const QStringList& values)
{
    QJsonArray arr;
    for (const QString& v : values) {
        arr.append(v);
    }
    return arr;
}

} // namespace

class TestPackValidators : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// Every token the parser accepts must pass the lint. These two lists
    /// drifted apart once already: the parser learned "strip" while the lint
    /// still named four tokens, so a correct pack drew a spurious diagnostic.
    /// Both now read ProfilePaths::allEventClassTokens(), and this asserts it
    /// end to end rather than trusting that they still do.
    void everyAcceptedTokenPassesTheLint()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        if (!linkSharedIncludes(tmp))
            QSKIP("data/animations/shared not found — running outside source tree");

        const QStringList tokens = PhosphorAnimation::ProfilePaths::allEventClassTokens();
        QVERIFY(!tokens.isEmpty());

        QStringList rejected;
        for (const QString& token : tokens) {
            QJsonObject obj = basePack(QStringLiteral("tok-") + token);
            obj.insert(QStringLiteral("appliesTo"), toArray({token}));
            const PackResult r = validate(tmp, QStringLiteral("tok-") + token, obj);
            if (r.report.contains(QStringLiteral("unknown appliesTo token"))) {
                rejected << token;
            }
        }
        QVERIFY2(rejected.isEmpty(),
                 qPrintable(QStringLiteral("the lint rejects token(s) the parser accepts: ")
                            + rejected.join(QLatin1String(", "))));
    }

    /// The strip helpers must resolve through the bound axis uniform rather
    /// than through a hardcoded x.
    ///
    /// Nothing else can catch this. Strip packs are compositor-only, so the
    /// validator skips their stage compile entirely and the bundled-pack gate
    /// never reads a line of their GLSL — a stripAxisOffset reverted to
    /// `vec2(amount, 0.0)` compiles, links, bakes and ships, and each of the
    /// three packs that displace through it (jelly, chromatic, motion-blur;
    /// carousel builds its displacement from the axis directly) smears
    /// sideways on a vertical strip with no diagnostic anywhere. The helper
    /// is the single point they all go through, which is what makes a source
    /// assertion worth making here instead of per pack.
    void stripHelpersDisplaceAlongTheBoundAxis()
    {
        const QString code = sharedHelperCode(QStringLiteral("strip_transition.glsl"));
        if (code.isEmpty())
            QSKIP("data/animations/shared not found — running outside source tree");

        const QString offset = glslFunctionBody(code, QStringLiteral("stripAxisOffset"));
        QVERIFY2(!offset.isEmpty(), "stripAxisOffset is missing from shared/strip_transition.glsl");
        QVERIFY2(
            offset.contains(QLatin1String("iStripAxis")),
            qPrintable(QStringLiteral("stripAxisOffset does not multiply by the bound axis: ") + offset.simplified()));

        // The perpendicular is the axis SWAP, not a rotation: vec2(-y, x)
        // would hand a vertical strip (-1, 0) and mirror every across
        // coordinate against the horizontal case, which is a sign error no
        // horizontal-only test can see.
        const QString perp = glslFunctionBody(code, QStringLiteral("stripAxisPerp"));
        QVERIFY2(!perp.isEmpty(), "stripAxisPerp is missing from shared/strip_transition.glsl");
        // Accept both spellings of the same swap: the component form
        // vec2(iStripAxis.y, iStripAxis.x) and the equivalent .yx swizzle. A
        // reformat between them is semantically identical, and failing on it
        // would be a false alarm rather than a caught regression. What must
        // NOT appear either way is a negation, which is the rotation.
        const bool componentSwap =
            perp.contains(QLatin1String("iStripAxis.y")) && perp.contains(QLatin1String("iStripAxis.x"));
        const bool swizzleSwap = perp.contains(QLatin1String("iStripAxis.yx"));
        QVERIFY2(!perp.contains(QLatin1String("-iStripAxis")),
                 qPrintable(QStringLiteral("stripAxisPerp negates a component, which is the rotation the header "
                                           "warns against rather than the swap: ")
                            + perp.simplified()));
        QVERIFY2(componentSwap || swizzleSwap,
                 qPrintable(QStringLiteral("stripAxisPerp is not the plain component swap: ") + perp.simplified()));

        // stripEdgeFade is the third axis-coupled helper: its travel
        // coordinate must come from dot(uv, iStripAxis), not from a bare
        // uv.x — a revert fades the wrong edges on a vertical strip in every
        // pack that budgets its displacement against the fade (the same
        // silent, compositor-only failure mode as the offset above).
        const QString fade = glslFunctionBody(code, QStringLiteral("stripEdgeFade"));
        QVERIFY2(!fade.isEmpty(), "stripEdgeFade is missing from shared/strip_transition.glsl");
        QVERIFY2(fade.contains(QLatin1String("iStripAxis")),
                 qPrintable(QStringLiteral("stripEdgeFade does not derive its travel coordinate from the bound axis: ")
                            + fade.simplified()));
    }

    /// The complement: an unknown token IS linted, and the message names the
    /// live vocabulary rather than a stale hand-written list.
    void unknownTokenIsLintedAndMessageNamesTheVocabulary()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        if (!linkSharedIncludes(tmp))
            QSKIP("data/animations/shared not found — running outside source tree");

        QJsonObject obj = basePack(QStringLiteral("bad-token"));
        obj.insert(QStringLiteral("appliesTo"), toArray({QStringLiteral("teleport")}));
        const PackResult r = validate(tmp, QStringLiteral("bad-token"), obj);

        QVERIFY(r.errors > 0);
        QVERIFY(r.report.contains(QStringLiteral("unknown appliesTo token 'teleport'")));
        for (const QString& token : PhosphorAnimation::ProfilePaths::allEventClassTokens()) {
            QVERIFY2(r.report.contains(token),
                     qPrintable(QStringLiteral("diagnostic omits the valid token '") + token + QLatin1Char('\'')));
        }
    }

    /// An explicit `"appliesTo": []` is a legal spelling of the universal
    /// default and must stay lint-free, while the two degenerate spellings
    /// that LOOK like a constraint and silently become universal are linted.
    /// The difference is author intent, not effect: `[""]` and `["teleport"]`
    /// are typos, `[]` is a statement. This pins the asymmetry so nobody
    /// "evens it up" and starts failing every universal pack that spells its
    /// default out.
    void explicitEmptyAppliesToIsNotLinted()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        if (!linkSharedIncludes(tmp))
            QSKIP("data/animations/shared not found — running outside source tree");

        QJsonObject empty = basePack(QStringLiteral("empty-applies"));
        empty.insert(QStringLiteral("appliesTo"), QJsonArray());
        const PackResult r = validate(tmp, QStringLiteral("empty-applies"), empty);
        QVERIFY2(!r.report.contains(QStringLiteral("appliesTo")),
                 qPrintable(QStringLiteral("an explicit empty appliesTo must draw no diagnostic:\n") + r.report));
        QCOMPARE(r.errors, 0);

        // The degenerate cousins DO lint, which is what makes the silence
        // above a decision rather than a gap.
        QJsonObject blank = basePack(QStringLiteral("blank-token"));
        blank.insert(QStringLiteral("appliesTo"), toArray({QString()}));
        QVERIFY(validate(tmp, QStringLiteral("blank-token"), blank)
                    .report.contains(QStringLiteral("empty appliesTo token")));
    }

    /// A bare string instead of an array is ignored wholesale at load, which
    /// silently makes the pack universal.
    void nonArrayAppliesToIsLinted()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        if (!linkSharedIncludes(tmp))
            QSKIP("data/animations/shared not found — running outside source tree");

        QJsonObject obj = basePack(QStringLiteral("bad-shape"));
        obj.insert(QStringLiteral("appliesTo"), QStringLiteral("strip"));
        const PackResult r = validate(tmp, QStringLiteral("bad-shape"), obj);

        QVERIFY(r.errors > 0);
        QVERIFY(r.report.contains(QStringLiteral("appliesTo must be an array")));
    }

    /// The screen-level passes draw their own full-screen quad, so a vertex
    /// stage or a geometry grid declared by a desktop or strip pack is loaded
    /// and then never used.
    void screenLevelPacksAreToldTheirVertexStageIsIgnored()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        if (!linkSharedIncludes(tmp))
            QSKIP("data/animations/shared not found — running outside source tree");

        QJsonObject obj = basePack(QStringLiteral("strip-vert"));
        obj.insert(QStringLiteral("appliesTo"), toArray({QStringLiteral("strip")}));
        obj.insert(QStringLiteral("vertexShader"), QStringLiteral("effect.vert"));
        obj.insert(QStringLiteral("geometryGrid"), 8);

        const QString dir = tmp.filePath(QStringLiteral("strip-vert"));
        QDir().mkpath(dir);
        QFile vert(dir + QStringLiteral("/effect.vert"));
        QVERIFY(vert.open(QIODevice::WriteOnly));
        vert.write("void main() {}\n");
        vert.close();

        const PackResult r = validate(tmp, QStringLiteral("strip-vert"), obj);
        QVERIFY(r.report.contains(QStringLiteral("vertexShader is ignored for desktop/strip packs")));
        QVERIFY(r.report.contains(QStringLiteral("geometryGrid is ignored for desktop/strip packs")));

        // A single-surface pack keeps both without complaint.
        QJsonObject surface = basePack(QStringLiteral("surface-vert"));
        surface.insert(QStringLiteral("appliesTo"), toArray({QStringLiteral("appearance")}));
        surface.insert(QStringLiteral("geometryGrid"), 8);
        const PackResult ok = validate(tmp, QStringLiteral("surface-vert"), surface);
        QVERIFY(!ok.report.contains(QStringLiteral("geometryGrid is ignored")));
    }

    /// A multipass pack's BUFFER shaders are compiled, not just existence
    /// checked. No bundled animation pack is multipass today, so the
    /// bundled-pack gate cannot exercise this path at all — without this test
    /// the bake would be dead code that silently stops working.
    ///
    /// This one really does invoke glslang, unlike the metadata-only tests
    /// above.
    void multipassBufferShadersAreCompiled()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        if (!linkSharedIncludes(tmp))
            QSKIP("data/animations/shared not found — running outside source tree");

        // Returns bool for the caller to QVERIFY: a QVERIFY inside the lambda
        // only returns from the LAMBDA, so a failed open used to let the test
        // run on against a missing buffer file and fail somewhere misleading.
        const auto writeBuffer = [&tmp](const QString& pack, const QString& body) {
            const QString dir = tmp.filePath(pack);
            QDir().mkpath(dir);
            QFile buf(dir + QStringLiteral("/buffer0.frag"));
            if (!buf.open(QIODevice::WriteOnly)) {
                return false;
            }
            buf.write(body.toUtf8());
            buf.close();
            return true;
        };

        QJsonObject obj = basePack(QStringLiteral("mp-good"));
        obj.insert(QStringLiteral("multipass"), true);
        obj.insert(QStringLiteral("bufferShaders"), toArray({QStringLiteral("buffer0.frag")}));

        // A buffer pass ships its own main() and no entry scaffold.
        QVERIFY(writeBuffer(QStringLiteral("mp-good"),
                            QStringLiteral("#version 440\n"
                                           "layout(location = 0) out vec4 fragColor;\n"
                                           "void main() { fragColor = vec4(1.0); }\n")));
        const PackResult good = validate(tmp, QStringLiteral("mp-good"), obj);
        // Spacing-independent: the report pads the label with leftJustified(15),
        // so a literal with a hand-counted gap silently stops matching the
        // moment the label length or the pad width moves (the previous
        // four-space literal could never occur — "buffer0.frag" pads to three —
        // making this assertion vacuously green).
        QVERIFY2(!good.report.contains(QRegularExpression(QStringLiteral("buffer0\\.frag\\s+ERROR"))),
                 qPrintable(QStringLiteral("a valid buffer pass must bake clean:\n") + good.report));
        // Belt: the error count is spacing-proof and must be zero too.
        QCOMPARE(good.errors, 0);

        // The same pack with a syntax error in the buffer must be caught
        // HERE, not at the live daemon.
        QJsonObject bad = basePack(QStringLiteral("mp-bad"));
        bad.insert(QStringLiteral("multipass"), true);
        bad.insert(QStringLiteral("bufferShaders"), toArray({QStringLiteral("buffer0.frag")}));
        QVERIFY(writeBuffer(QStringLiteral("mp-bad"),
                            QStringLiteral("#version 440\n"
                                           "layout(location = 0) out vec4 fragColor;\n"
                                           "void main() { fragColor = notADeclaredThing; }\n")));
        const PackResult r = validate(tmp, QStringLiteral("mp-bad"), bad);
        QVERIFY2(r.errors > 0, qPrintable(QStringLiteral("a broken buffer pass must fail the gate:\n") + r.report));
        QVERIFY(r.report.contains(QStringLiteral("buffer0.frag")));
    }

    /// A buffer pass gets NO p_<id> preamble, because
    /// ShaderNodeRhi::bakeBufferShaders does not splice one — it loads,
    /// expands includes and compiles. The gate must reproduce that exactly:
    /// splicing a preamble the runtime withholds would pass sources that fail
    /// live, and withholding one the runtime splices would fail sources that
    /// work. Pin the direction so a future "helpful" splice is caught.
    void multipassBufferShadersGetNoParamPreamble()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        if (!linkSharedIncludes(tmp))
            QSKIP("data/animations/shared not found — running outside source tree");

        QJsonObject param;
        param.insert(QStringLiteral("id"), QStringLiteral("strength"));
        param.insert(QStringLiteral("type"), QStringLiteral("float"));
        param.insert(QStringLiteral("default"), 0.5);
        QJsonArray params;
        params.append(param);

        QJsonObject obj = basePack(QStringLiteral("mp-param"));
        obj.insert(QStringLiteral("multipass"), true);
        obj.insert(QStringLiteral("bufferShaders"), toArray({QStringLiteral("buffer0.frag")}));
        obj.insert(QStringLiteral("parameters"), params);

        const QString dir = tmp.filePath(QStringLiteral("mp-param"));
        QDir().mkpath(dir);
        QFile buf(dir + QStringLiteral("/buffer0.frag"));
        QVERIFY(buf.open(QIODevice::WriteOnly));
        buf.write(
            "#version 440\n"
            "layout(location = 0) out vec4 fragColor;\n"
            "void main() { fragColor = vec4(p_strength); }\n");
        buf.close();

        const PackResult r = validate(tmp, QStringLiteral("mp-param"), obj);
        QVERIFY2(r.errors > 0,
                 qPrintable(QStringLiteral("p_<id> in a buffer pass must NOT resolve — the runtime splices no "
                                           "preamble there, so the gate must not either:\n")
                            + r.report));
    }

    /// geometryGrid clamps to 0 at load, so a negative value disables the
    /// grid indistinguishably from never declaring it.
    /// The authoring model is DETECTED from the pack's sibling shared/ dir.
    ///
    /// This is the guard on a diagnostic that used to be actively misleading:
    /// the model was a flag with --overlay silently the default, so validating
    /// an animation pack without remembering --animation ran the ZONE checks
    /// over it and reported two errors ("rename it to zone.vert", "Include not
    /// found: common.glsl") that described nothing wrong with the pack.
    ///
    /// Each marker is asserted through a pack laid out the way the real trees
    /// are (a `shared/` dir beside the pack, not inside it), because the lookup
    /// is relative to the pack's PARENT — a detector that searched the pack dir
    /// itself would find nothing and silently fall back to overlay for
    /// everything, which is the exact bug this replaced.
    void authoringModelIsDetectedFromTheSharedMarker()
    {
        using PlasmaZones::ShaderValidate::detectPackModel;
        using PlasmaZones::ShaderValidate::PackModel;

        struct Case
        {
            const char* marker;
            PackModel expected;
        };
        const QList<Case> cases = {
            {"animation_uniforms.glsl", PackModel::Animation},
            {"surface_uniforms.glsl", PackModel::Surface},
            {"common.glsl", PackModel::Overlay},
        };

        for (const Case& c : cases) {
            QTemporaryDir tmp;
            QVERIFY(tmp.isValid());
            const QString sharedDir = tmp.filePath(QStringLiteral("shared"));
            QVERIFY(QDir().mkpath(sharedDir));
            QFile marker(sharedDir + QLatin1Char('/') + QLatin1String(c.marker));
            QVERIFY(marker.open(QIODevice::WriteOnly));
            marker.close();

            const QString pack = tmp.filePath(QStringLiteral("some-pack"));
            QVERIFY(QDir().mkpath(pack));

            const std::optional<PackModel> got = detectPackModel(pack);
            QVERIFY2(got.has_value(), c.marker);
            QVERIFY2(*got == c.expected, c.marker);
        }
    }

    /// A pack tree with no shared/ marker is reported as UNDETECTED rather than
    /// guessed at. The caller turns that into the overlay fallback plus a
    /// message telling the author to pass a flag, which is honest; silently
    /// picking a model would put back the wrong-validator diagnostics.
    void aPackWithNoSharedMarkerIsNotDetected()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString pack = tmp.filePath(QStringLiteral("orphan-pack"));
        QVERIFY(QDir().mkpath(pack));
        QVERIFY(!PlasmaZones::ShaderValidate::detectPackModel(pack).has_value());

        // A shared/ dir that exists but holds none of the three markers is the
        // same answer, not a crash or an accidental match on the first entry.
        const QString sharedDir = tmp.filePath(QStringLiteral("shared"));
        QVERIFY(QDir().mkpath(sharedDir));
        QFile stray(sharedDir + QStringLiteral("/unrelated.glsl"));
        QVERIFY(stray.open(QIODevice::WriteOnly));
        stray.close();
        QVERIFY(!PlasmaZones::ShaderValidate::detectPackModel(pack).has_value());
    }

    /// End to end over the REAL trees: every bundled pack must detect as the
    /// family it actually belongs to. The synthetic cases above pin the lookup
    /// rule, but only this catches a tree being reorganised (or a marker header
    /// renamed) out from under it, which would send a whole directory to the
    /// wrong validator.
    void everyBundledTreeDetectsAsItsOwnFamily()
    {
        using PlasmaZones::ShaderValidate::detectPackModel;
        using PlasmaZones::ShaderValidate::PackModel;

        struct Tree
        {
            const char* path;
            PackModel expected;
        };
        const QList<Tree> trees = {
            {P_SOURCE_DIR "/data/animations", PackModel::Animation},
            {P_SOURCE_DIR "/data/surface", PackModel::Surface},
            {P_SOURCE_DIR "/data/overlays", PackModel::Overlay},
        };

        for (const Tree& tree : trees) {
            QDir root(QLatin1String(tree.path));
            if (!root.exists()) {
                QSKIP("bundled pack tree not found — running outside source tree");
            }
            int checked = 0;
            const QStringList subdirs = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            for (const QString& sub : subdirs) {
                const QString packDir = root.filePath(sub);
                // Only real packs; `shared/` is a sibling helper dir, not a pack.
                if (!QFile::exists(packDir + QStringLiteral("/metadata.json"))) {
                    continue;
                }
                const std::optional<PackModel> got = detectPackModel(packDir);
                QVERIFY2(got.has_value(), qPrintable(packDir));
                QVERIFY2(*got == tree.expected, qPrintable(packDir));
                ++checked;
            }
            QVERIFY2(checked > 0, tree.path);
        }
    }

    /// A COMPOSITOR-ONLY pack's stages are compiled, not skipped. This path had
    /// no coverage of any kind: the validator printed
    /// "SKIP (compositor-only pack; kwin-path GLSL)" for 35 of the 94 bundled
    /// animation packs (every desktop-* transition and the whole geometry set),
    /// and the only thing that ever compiled them, the GPU bake test, QSKIPs
    /// without a desktop GL 4.5 context, which is exactly the headless CI case.
    /// So a broken compositor-only pack passed every gate and failed at the
    /// live compositor.
    ///
    /// Both directions are asserted. A test that only checked the clean pack
    /// would still pass if the bake were quietly turned back into a skip, which
    /// is the regression worth catching.
    void compositorOnlyPackStagesAreCompiled()
    {
        if (PlasmaZones::ShaderValidate::glslangValidatorPath().isEmpty()) {
            QSKIP("glslangValidator not on PATH");
        }
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        if (!linkSharedIncludes(tmp))
            QSKIP("data/animations/shared not found — running outside source tree");

        // "geometry" alone is compositor-only (no "appearance"), the same
        // shape the bundled morph packs declare.
        QJsonObject clean = basePack(QStringLiteral("kwin-clean"));
        clean.insert(QStringLiteral("appliesTo"), toArray({QStringLiteral("geometry")}));
        const PackResult ok = validate(tmp, QStringLiteral("kwin-clean"), clean);
        QCOMPARE(ok.errors, 0);
        // The stage was actually compiled rather than waved through.
        QVERIFY2(ok.report.contains(QStringLiteral("OK (compositor)")), qPrintable(ok.report));
        QVERIFY2(!ok.report.contains(QStringLiteral("SKIP")), qPrintable(ok.report));

        // The same pack with a GLSL error in the body must fail. Written
        // against the KWIN branch specifically: an undeclared identifier is
        // rejected by any dialect, so this fails for the one reason under test
        // (the stage got compiled) and not because of a dialect mismatch.
        QJsonObject broken = basePack(QStringLiteral("kwin-broken"));
        broken.insert(QStringLiteral("appliesTo"), toArray({QStringLiteral("geometry")}));
        const QString dir = tmp.filePath(QStringLiteral("kwin-broken"));
        QDir().mkpath(dir);
        QFile frag(dir + QStringLiteral("/effect.frag"));
        QVERIFY(frag.open(QIODevice::WriteOnly));
        frag.write("vec4 pTransition(vec2 uv, float t) { return vec4(notADeclaredThing); }\n");
        frag.close();
        const PackResult bad = validate(tmp, QStringLiteral("kwin-broken"), broken, /*writeFragment=*/false);
        QVERIFY2(bad.errors > 0, qPrintable(bad.report));
        QVERIFY2(bad.report.contains(QStringLiteral("ERROR (compositor)")), qPrintable(bad.report));
        QVERIFY2(bad.report.contains(QStringLiteral("notADeclaredThing")), qPrintable(bad.report));
    }

    /// A compositor-only pack's VERTEX stage is compiled too. It is the stage
    /// the geometry packs do their per-vertex work in and the one the daemon
    /// never touches, so it is both the likeliest to break and the least
    /// covered. Unlike the daemon vertex bake, the p_<id> preamble is spliced
    /// here, matching the compositor: a vertex-driven pack reading its params
    /// must compile, not fail on an undeclared identifier.
    void compositorOnlyVertexStageIsCompiledWithParams()
    {
        if (PlasmaZones::ShaderValidate::glslangValidatorPath().isEmpty()) {
            QSKIP("glslangValidator not on PATH");
        }
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        if (!linkSharedIncludes(tmp))
            QSKIP("data/animations/shared not found — running outside source tree");

        QJsonObject obj = basePack(QStringLiteral("kwin-vert"));
        obj.insert(QStringLiteral("appliesTo"), toArray({QStringLiteral("geometry")}));
        obj.insert(QStringLiteral("vertexShader"), QStringLiteral("effect.vert"));
        QJsonObject param;
        param.insert(QStringLiteral("id"), QStringLiteral("amount"));
        param.insert(QStringLiteral("type"), QStringLiteral("float"));
        param.insert(QStringLiteral("default"), 1.0);
        obj.insert(QStringLiteral("parameters"), QJsonArray{param});

        const QString dir = tmp.filePath(QStringLiteral("kwin-vert"));
        QDir().mkpath(dir);
        QFile vert(dir + QStringLiteral("/effect.vert"));
        QVERIFY(vert.open(QIODevice::WriteOnly));
        vert.write(
            "#version 450\n"
            "#include <animation_uniforms.glsl>\n"
            "layout(location = 0) in vec2 position;\n"
            "uniform mat4 modelViewProjectionMatrix;\n"
            "void main() {\n"
            "    gl_Position = modelViewProjectionMatrix * vec4(position * p_amount, 0.0, 1.0);\n"
            "}\n");
        vert.close();

        const PackResult r = validate(tmp, QStringLiteral("kwin-vert"), obj);
        QCOMPARE(r.errors, 0);
        QVERIFY2(r.report.contains(QStringLiteral("effect.vert")), qPrintable(r.report));
        QVERIFY2(r.report.contains(QStringLiteral("OK (compositor)")), qPrintable(r.report));
    }

    void negativeGeometryGridIsLinted()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        if (!linkSharedIncludes(tmp))
            QSKIP("data/animations/shared not found — running outside source tree");

        QJsonObject obj = basePack(QStringLiteral("neg-grid"));
        obj.insert(QStringLiteral("geometryGrid"), -4);
        const PackResult r = validate(tmp, QStringLiteral("neg-grid"), obj);

        QVERIFY(r.errors > 0);
        QVERIFY(r.report.contains(QStringLiteral("geometryGrid is negative")));
    }
};

QTEST_MAIN(TestPackValidators)
#include "test_pack_validators.moc"
